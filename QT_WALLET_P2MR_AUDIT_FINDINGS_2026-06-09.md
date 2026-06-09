# QT Wallet / P2MR Audit Findings - 2026-06-09

Branch: `fix/p2mr-metadata-idempotent`
PR: https://github.com/btq-ag/btq-core/pull/57
Reviewed HEAD: `bed39bb`
Primary specification reference: https://bips.xyz/360

## Scope

This pass focused on the QT wallet P2MR flow and the implementation paths it
depends on:

- QT vault UI and controller: `src/qt/p2mrvaultdialog.cpp`,
  `src/qt/p2mrwizards.cpp`, `src/qt/p2mrcontroller.cpp`
- Wallet P2MR metadata, balance, construction, signing, and dry-run helpers:
  `src/wallet/p2mr.cpp`, `src/wallet/wallet.cpp`, `src/wallet/interfaces.cpp`
- P2MR script verification and signing support:
  `src/script/interpreter.cpp`, `src/script/sign.cpp`,
  `src/script/signingprovider.cpp`
- P2MR RPC surface and address/script recognition:
  `src/wallet/rpc/p2mr.cpp`, `src/addresstype.cpp`, `src/key_io.cpp`,
  `src/script/solver.cpp`

The P2MR consensus-style verifier was checked against the current BIP360 shape:
native witness version 2, 32-byte witness program, no key path spend, a
`1 + 32*m` control block, required control-byte low bit `1`, direct merkle-root
comparison, optional annex handling, and tapscript execution for leaf version
`0xc0`.

## Test Evidence

Focused tests run locally against `bed39bb`:

```text
src/test/test_btq --run_test=p2mr_tests/*
src/test/test_btq --run_test=ismine_tests/*
python3 test/functional/test_runner.py --portseed=40957 -t /tmp/btq_func_p2mr_audit feature_p2mr_rpc.py -j 1
python3 test/functional/test_runner.py --portseed=40958 -t /tmp/btq_func_p2mr_full_audit feature_p2mr.py -j 1
python3 test/functional/test_runner.py --portseed=40959 -t /tmp/btq_func_bip360_wallet_audit wallet_bip360_send_paths.py -j 1
```

All passed. These tests validate the current happy paths and several malformed
P2MR witnesses, but they do not cover the unresolved wallet/QT issues below.

## Remediation Status

The findings below were remediated on this branch after the audit pass:

- P2MR `IsMine()` now distinguishes tracked/watch-only metadata from
  wallet-spendable metadata by checking whether at least one standard leaf is
  satisfiable by wallet key material.
- The P2MR wallet signing provider now exports wallet-owned x-only/Schnorr keys
  for `<xonly> OP_CHECKSIG` and `multi_a` leaves, including descriptor wallets
  that derive private child keys from ranged descriptors.
- The QT OP_TRUE template is gated to regtest and cannot be funded from the
  GUI; production defaults to custom JSON instead of an anyone-spendable
  template.
- The QT custom JSON parser now requires exact integer fields, string scripts,
  and rejects leaf versions with the control-block parity bit set.
- P2MR spend construction now aggregates matching confirmed UTXOs and reports
  the effective fee after dust-change handling through RPC and QT preview data.
- The Receive tab now opens P2MR vault creation directly with the label carried
  over, and blocks amount/message fields that cannot be represented as P2MR
  receive request metadata.

Post-fix focused verification:

```text
src/test/test_btq --run_test=p2mr_tests/*
src/test/test_btq --run_test=ismine_tests/*
python3 test/functional/test_runner.py --portseed=41057 -t /tmp/btq_func_p2mr_rpc_fix_serial feature_p2mr_rpc.py -j 1
python3 test/functional/test_runner.py --portseed=41058 -t /tmp/btq_func_p2mr_full_fix_serial feature_p2mr.py -j 1
python3 test/functional/test_runner.py --portseed=41059 -t /tmp/btq_func_bip360_wallet_fix_serial wallet_bip360_send_paths.py -j 1
```

QT object compilation was attempted for `qt/libbtqqt_a-p2mrwizards.o` and
`qt/libbtqqt_a-receivecoinsdialog.o`, but this local build is not configured
with Qt include paths and failed before compiling project code on missing
`QObject`/`QApplication` headers.

## Findings

| ID | Severity | Status | Summary |
|---|---|---|---|
| QT-P2MR-001 | High | Fixed | Wallet marks any valid tracked P2MR metadata as spendable without proving the wallet can satisfy a leaf. |
| QT-P2MR-002 | High | Fixed | QT/RPC wallet P2MR signing provider does not populate normal Schnorr/x-only keys for P2MR leaves. |
| QT-P2MR-003 | High | Fixed | Production QT creation flow defaults to an OP_TRUE P2MR vault template that is intentionally anyone-spendable. |
| QT-P2MR-004 | Medium | Fixed | QT custom JSON parser silently coerces malformed numeric fields to zero. |
| QT-P2MR-005 | Medium | Fixed | P2MR spend builder uses a first-match single-UTXO fixed-fee model and can burn dust change without surfacing the effective fee. |
| QT-P2MR-006 | Low | Fixed | P2MR receive-tab routing discards the normal receive label/amount/message context. |

## QT-P2MR-001 - P2MR Metadata Is Treated As Spendable Without Key Availability

Severity: High

Affected code:

- `src/wallet/wallet.cpp:1595`
- `src/wallet/p2mr.cpp:321`
- `src/wallet/p2mr.cpp:353`
- `src/wallet/p2mr.cpp:439`

`CWallet::IsMine()` upgrades a script from `ISMINE_NO` to `ISMINE_SPENDABLE`
whenever `IsTrackedP2MRScript()` returns true. `IsTrackedP2MRScript()` only
checks that a stored metadata entry has a valid P2MR tree matching the
scriptPubKey. It does not check whether the wallet can actually satisfy any
leaf in that tree.

Impact:

- A user can create or restore P2MR metadata for a tree containing external
  keys, missing Dilithium keys, unsupported Schnorr leaves, or otherwise
  unsatisfied scripts, and the wallet will still report matching UTXOs as
  spendable.
- Wallet balances and transaction classification can be wrong because general
  wallet accounting relies on `IsMine()`.
- The QT vault table and `CreateP2MRSpend()` can select funds that later fail
  signing, producing a late UX failure instead of representing the output as
  watch-only, solvable-only, or unspendable.

Recommended remediation:

- Split P2MR tracking from P2MR spendability. A valid tree should make the
  output tracked/solvable, not automatically `ISMINE_SPENDABLE`.
- Add a wallet-level satisfiability check that proves at least one standard
  leaf can be satisfied with currently available wallet material before
  returning `ISMINE_SPENDABLE`.
- Return watch-only/solvable semantics for valid metadata without private
  spend material.
- Add unit coverage where a P2MR tree uses a valid key not in the wallet and
  assert it is not spendable and is not counted as spendable balance.

## QT-P2MR-002 - Wallet P2MR Signing Provider Omits Schnorr/X-Only Keys

Severity: High

Affected code:

- `src/wallet/p2mr.cpp:284`
- `src/wallet/p2mr.cpp:299`
- `src/script/sign.cpp:495`
- `src/wallet/test/p2mr_tests.cpp:295`
- `src/wallet/test/p2mr_tests.cpp:542`

The low-level P2MR signer can sign a normal x-only Schnorr leaf when the caller
manually supplies both the P2MR tree and the ECDSA/Schnorr key material in a
`FlatSigningProvider`. The wallet/QT/RPC P2MR provider construction path,
however, only extracts Dilithium keys from recognized leaf scripts and adds
those Dilithium keys to the temporary provider.

There is no equivalent export of wallet-owned normal keys or x-only pubkeys for
P2MR leaves such as:

```text
<xonly pubkey> OP_CHECKSIG
```

Impact:

- BIP360-compatible P2MR outputs using ordinary tapscript Schnorr checks may be
  valid and signable at the script layer but not signable through the QT/RPC
  wallet P2MR flow.
- This breaks the most direct Taproot-to-P2MR migration pattern described by
  BIP360: moving from a key-path Taproot spend to a simple script-path
  `OP_CHECKSIG` leaf.
- Existing tests do not catch this. `produce_signature_preserves_p2mr_witness_stack`
  manually populates the provider with `provider.keys`, while
  `build_signing_provider_exports_descriptor_dilithium_p2mr_leaf` only covers
  Dilithium.

Recommended remediation:

- Make `BuildP2MRSigningProvider()` merge a hiding/filtered view of the wallet's
  existing signing providers with the P2MR tree provider, or explicitly collect
  x-only keys referenced by supported P2MR leaf templates.
- Add descriptor and legacy wallet tests that create a wallet-owned
  `<xonly> OP_CHECKSIG` P2MR leaf, fund it, sign it through
  `SignP2MRTransaction()`, and verify/broadcast it on regtest.

## QT-P2MR-003 - Production QT Flow Defaults To Anyone-Spendable OP_TRUE Vaults

Severity: High

Affected code:

- `src/qt/p2mrcontroller.cpp:28`
- `src/qt/p2mrwizards.cpp:149`
- `src/qt/p2mrwizards.cpp:158`
- `src/qt/p2mrwizards.cpp:236`

The QT P2MR new-vault dialog presents `OP_TRUE leaf (testing only)` as the first
and default template. The dialog warning correctly says this produces a vault
that anyone who knows the scriptPubKey can spend, but the production UI still
lets the user create and fund that vault in the same flow.

Impact:

- A user can accidentally fund a known-template P2MR root that is intentionally
  anyone-spendable.
- The warning is not a sufficient safety boundary for a main wallet UI because
  the default action remains the unsafe template.
- For a single OP_TRUE leaf, the script/control data are trivial and can be
  precomputed from the known template root.

Recommended remediation:

- Remove the OP_TRUE template from production builds, or gate it behind regtest
  or an explicit developer/advanced flag.
- Default the GUI to a wallet-owned key-controlled template, preferably
  Dilithium/PQ if that is the intended BTQ P2MR path.
- Disable `Fund now` for OP_TRUE templates even if OP_TRUE remains available for
  test workflows.
- Require a typed confirmation for any custom tree that contains no wallet-owned
  required key material.

## QT-P2MR-004 - QT Custom JSON Tree Parser Silently Coerces Types

Severity: Medium

Affected code:

- `src/qt/p2mrwizards.cpp:51`
- `src/qt/p2mrwizards.cpp:82`
- `src/qt/p2mrwizards.cpp:83`
- `src/wallet/p2mr.cpp:181`

The RPC/core parser uses typed UniValue getters and rejects malformed numeric
fields. The QT parser uses `QJsonValue::toInt()` directly for `depth` and
`leaf_version`. In Qt, `toInt()` returns a default value when the JSON value is
not a number, so malformed input can silently become `0`.

Impact:

- A pasted custom tree can create a different P2MR root than intended without a
  clear validation error.
- A malformed `leaf_version` can become `0`, producing an upgradable/future leaf
  rather than tapscript `0xc0`; under standard flags this will not behave like a
  normal spendable tapscript leaf.
- The GUI and RPC disagree on what malformed input is accepted.

Recommended remediation:

- Require `depth` and `leaf_version` to be JSON numbers and exact integers.
- Reject leaf versions with the control parity bit set, matching
  `ParseP2MRTreeChecked()`.
- Prefer converting the QT JSON array into the shared wallet parser, or add QT
  unit tests that mirror the RPC parser's rejection cases.

## QT-P2MR-005 - Spend Builder Has Single-UTXO Fixed-Fee Semantics

Severity: Medium

Affected code:

- `src/wallet/p2mr.cpp:439`
- `src/wallet/p2mr.cpp:462`
- `src/wallet/p2mr.cpp:478`
- `src/wallet/p2mr.cpp:485`
- `src/qt/p2mrwizards.cpp:301`

`CreateP2MRSpend()` selects the first confirmed unspent output matching the
vault script, spends exactly one input, applies a caller-provided fixed fee, and
silently rolls any change at or below `DEFAULT_P2MR_DUST_THRESHOLD` into the
effective fee.

Impact:

- Users cannot spend from multiple P2MR UTXOs in one QT flow even if aggregate
  balance is sufficient.
- Fee rate is not estimated from transaction weight, which matters for large
  P2MR/Dilithium witnesses.
- The preview says "No change (dust threshold)" but does not surface the actual
  effective fee paid after dust is added.

Recommended remediation:

- Use normal wallet coin selection and fee-rate estimation for P2MR spends, or
  clearly label this as a manual expert fixed-fee tool.
- Show effective fee and effective feerate in the QT preview.
- Add tests for multiple P2MR UTXOs, dust-change behavior, and fee-rate
  calculation with Dilithium-sized witnesses.

## QT-P2MR-006 - Receive Tab Context Is Lost When Routing To P2MR Vaults

Severity: Low

Affected code:

- `src/qt/receivecoinsdialog.cpp:155`
- `src/qt/p2mrvaultdialog.cpp:199`
- `src/qt/p2mrwizards.cpp:235`

When a user selects P2MR in the normal Receive tab, the code opens the P2MR vault
manager and drops the receive form's label, amount, and message. The vault
creation dialog starts from its own blank label and optional funding amount.

Impact:

- P2MR receive requests are not equivalent to normal receive requests.
- Users can believe they are creating a labeled/amounted receive request, but
  the metadata is not carried into the vault creation flow.

Recommended remediation:

- Pass the receive label into the P2MR creation dialog.
- Do not expose amount/message fields for P2MR in the normal receive form unless
  they are carried into a real P2MR receive request record.

## Positive Findings

- The interpreter-side P2MR verifier enforces no key path, control-block size,
  control-byte low bit `1`, direct root equality, annex handling, and tapscript
  execution for `0xc0` leaves.
- `ProduceSignature()` now preserves P2MR witness stacks and verifies the
  assembled result before marking P2MR signing complete.
- `signp2mrtransaction` no longer reports completion for invalid fallback
  witnesses in the tested OP_DROP/OP_TRUE negative case.
- P2MR metadata persistence invalidates the `IsMine` cache for the destination,
  avoiding stale `ISMINE_NO` after metadata creation.

## Highest-Priority Closure Tests

Add these tests before treating the QT wallet P2MR path as mainnet-ready:

1. P2MR tree with a valid external Schnorr key: wallet should not report
   `ISMINE_SPENDABLE` and should not count it in spendable balance.
2. Wallet-owned Schnorr P2MR leaf through QT/RPC provider construction:
   create, fund, sign with `SignP2MRTransaction()`, verify, and broadcast.
3. QT custom JSON malformed numeric fields: strings, booleans, nulls, floats,
   and `leaf_version = 193` must be rejected.
4. OP_TRUE QT template must be unavailable in production mode or must block
   funding without explicit developer override.
5. Multi-UTXO P2MR spend and dust-change preview tests.
