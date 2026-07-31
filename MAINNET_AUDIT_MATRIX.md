# BTQ Mainnet Component Audit Matrix

Last updated: 2026-07-31
Measured at: `5ce8e10bd` (master)
Status: Stage 1 component audit tranche in progress, mainnet readiness not achieved.

Renamed from `MAINNET_AUDIT_MATRIX_2026-06-03.md`. The date in the filename
implied a snapshot, but the document closes by asking to be updated after each
stage, so it is a living record and git carries the history.

## Changes since the 2026-06-04 revision

Recorded here rather than edited silently into the rows below, because what
moved is as interesting as where things stand.

**Closed.**

- The `script_tests` validation boundary is gone. The unit suite is **133 suites,
  733 cases, 0 failures** at `5ce8e10bd`, and `build-and-address-tests` is green
  on master. The stale-vector buckets named in the June revision were repaired in
  PR #91 (bip324 vectors regenerated rather than skipped) and PR #96 (key_io,
  descriptor, key, miner, psbt_wallet). The `bloom_tests` abort that turned one
  failure into roughly 590 was fixed in #87.

**Opened, and larger than the June revision assumed.**

- **C31.** The functional runner could not run in parallel at all — `p2p_port()`
  and `rpc_port()` ignored `PortSeed`, so every concurrent job bound identical
  ports. `--jobs` defaults to 4, so the *default invocation* produced roughly 261
  spurious failures. This is very likely why the functional suite had never been
  baselined: anyone who ran it saw near-total failure and reasonably concluded the
  tree was broken. Tracked in #105, fix open in #106.
- With that fixed, the functional suite was baselined for the first time:
  **162 pass, 126 fail, 16 skip**. The dominant cause is arithmetic, not
  behaviour — BTQ pays 5 BTQ per block where inherited tests hardcode Bitcoin's
  50. Tracked in #104.
- **C01 / C36.** Both rows already ask for launch seed policy. Nobody had checked
  whether the configured host resolves. **It does not.** Mainnet carries a single
  DNS seed, `seed1.btq.com`, with no A record, and `chainparams_seed_main[]` is a
  self-described invalid placeholder — so a default-configured mainnet node cannot
  find a peer. `btq.com` is a domain the project no longer controls. Tracked in
  #114, with related contact and supply-chain exposure in #115 and #118.
- **C03.** `nLWMAHeight` is 1 on mainnet but **300000 on testnet, signet and
  regtest**. LWMA has therefore never run on a live chain, and since functional
  chains are ~200 blocks, no functional test exercises it either. Coverage is the
  12 synthetic unit cases in `pow_lwma_tests`. Separately, `LWMA_WINDOW = 45`
  carries no justifying comment anywhere in the tree; at 60-second spacing that is
  45 minutes of history where the same constant gives a Bitcoin-spaced chain 7.5
  hours. Tracked in #110.
- **C10 / C11.** The KAT gap is confirmed rather than merely suspected:
  `src/crypto/dilithium/SHA256SUMS` lists `tvecs2`, `tvecs3` and `tvecs5`, and
  none of the three exist in the tree. Tracked in #103.

**Lesson for how this matrix is used.**

C01 and C36 named the seed gap in June. The gap was real and the entry was
correct. What had not happened in the seven weeks since was anyone going and
checking. This document is good at naming what needs evidence and silent on
whether evidence was gathered — every row reads the same whether it was examined
yesterday or never. A "last checked" column would fix that, and is worth adding
before it is handed to an external reviewer.

This is the working matrix for the staged pre-mainnet audit. It is intentionally
not a sign-off document. A component is not considered mainnet-ready until its
state model is identified, its existing tests are mapped, its gaps are closed,
and the required validation level is met.

Input documents and limitations:

- `AUDIT_REPORT_2026-05-19.md` was readable locally and was used as the
  baseline findings register and gate model.
- `Audit Scope of Work BTQ.pdf` exists locally, but this environment did not
  have `pdfinfo`, `pdftotext`, `pypdf`, `PyPDF2`, `fitz`, `pdfminer`, `mutool`,
  or `qpdf`. Only PDF structure metadata could be read. Before final sign-off,
  the PDF must be extracted or provided as text and this matrix must be
  reconciled against it.
- Subagent sidecar audits were used for crypto/script, wallet/RPC, and
  consensus/network/mining coverage mapping. Their findings are folded into the
  matrix below.

## Validation Levels

| Level | Meaning | Minimum evidence |
|---|---|---|
| V0 | Inventory only | Files/functions identified, no trust claim |
| V1 | Unit/FSM | Deterministic unit tests for state transitions and edge cases |
| V2 | Functional | Regtest node tests including restart where relevant |
| V3 | Differential/KAT/fuzz | KATs, independent model, fuzz target, or differential oracle |
| V4 | Stress/reorg/perf | Large blocks, reindex/prune/reorg, resource/performance bounds |
| V5 | Independent review | Separate reviewer signs off on code and evidence |

Critical and High findings from the prior audit require V3/V5 or V2/V5
respectively before being closed. Passing a smoke test is not enough.

## Stage 0 Evidence Completed In This Tranche

| Area | Change | Evidence |
|---|---|---|
| Unit-test registration | Registered `dilithium_mixed_mode_tests.cpp` in `src/Makefile.test.include`. | Source-vs-binary probe: missing make registrations 0, missing explicit suites 0. |
| Registration lint | Added lint checks for unregistered C++ unit tests and missing functional runner scripts. | `python3 test/lint/lint-tests.py` passed. |
| Functional runner integrity | Restored `wallet_bip360_send_paths.py` and made runner invoke it with descriptors. | Functional runner entries 296, missing functional scripts 0. |
| Mixed signing | Replaced placeholder mixed ECDSA/Dilithium tests with real signing and script execution tests. | `dilithium_mixed_mode_tests/*` and broader Dilithium unit tranche passed. |
| P2MR wallet ownership | Wallet-created P2MR metadata now participates in `IsMine`, with cache invalidation and corrupt-metadata rejection. | `p2mr_tests/*`, `ismine_tests/*`, `wallet_tests/*`, `wallet_bip360_send_paths.py --descriptors` passed. |
| Partial signature merge | `SignatureData::MergeSignatureData` now preserves Dilithium, Taproot, missing-key, and preimage fields. | `dilithium_basic_tests/*`, `script_tests/script_combineSigs`, `transaction_tests/test_witness` passed. |
| Dilithium raw `sk_to_pk` helper | Helper now fails closed instead of returning invalid public-key bytes from a raw secret key. | `dilithium_key_tests/dilithium_raw_secret_key_to_public_key_fails_closed` passed. |
| BTQ constants lint | Confirmed consensus/test-framework constants are currently synchronized. | `python3 test/lint/lint-btq-consensus-constants.py` passed. |

Stage 0 does not close the mainnet audit. It only removes several blockers that
would have invalidated later evidence.

## Stage 1 Evidence Completed In This Tranche

| Area | Change | Evidence |
|---|---|---|
| P2MR Dilithium script-path signing | Added P2MR Dilithium sighash support, OP_2/P2MR BIP341-style precompute detection, script-path Dilithium wallet signing provider export, and RPC signing coverage. | `p2mr_tests/*`, `feature_p2mr_rpc.py --descriptors`, and `wallet_bip360_send_paths.py --descriptors` passed. |
| P2MR Dilithium mutation coverage | Added real P2MR Dilithium pubkey and pubkeyhash leaf spends, wrong-amount, mutated-signature, mutated-script, and descriptor-wallet provider tests. | `p2mr_tests` passed. |
| Dilithium key material invariants | `CDilithiumKey::Load()` and `Set()` now reject `sk||pk` blobs whose stored pubkey cannot verify a signature from the secret key. | `dilithium_key_tests/dilithium_load_and_set_reject_mismatched_stored_pubkey` and full Dilithium focused tranche passed. |
| Dilithium HD metadata invariants | Extended key decode now rejects non-hardened child metadata, matching hardened-only Dilithium derivation semantics. | `dilithium_wallet_tests/dilithium_extkey_decode_rejects_non_hardened_child_metadata` and `dilithium_extpubkey_decode_rejects_non_hardened_child_metadata` passed. |
| LWMA retarget and activation | LWMA now honors `fPowNoRetargeting`, avoids low-target zero underflow, documents `lwma` in `-testactivationheight`, and advertises `!lwma` in GBT for the next active block. | `pow_lwma_tests`, `pow_tests`, `argsman_tests/testactivationheight_help_lists_lwma`, and `feature_lwma_activation.py` passed. |
| Dilithium NULLFAIL | `OP_CHECKSIGDILITHIUM` now rejects failed non-empty signatures under `SCRIPT_VERIFY_NULLFAIL`, including P2MR script-path execution. | `dilithium_basic_tests/dilithium_checksig_nullfail_rejects_nonempty_invalid_signature` and `p2mr_tests/p2mr_dilithium_checksig_nullfail_rejects_nonempty_invalid_signature` passed. |

Validation boundary (as at 2026-06-04, **now closed** — see Changes above):
`script_tests` failed with 73 stale-vector/harness failures. The buckets were
uncompressed P2WPKH expectations, Dilithium opcode reservation expectations, BTQ
15,000-byte push / 100,000-byte script limit drift from Bitcoin vectors, and
missing newer script-error names in `script_tests.cpp`. All were repaired in
PR #91 and PR #96; the unit suite is 133 suites / 733 cases / 0 failures at
`5ce8e10bd`.

Current validation boundary: the **functional** suite, not the unit suite.
126 of 304 tests fail at `5ce8e10bd` (#104), and the runner cannot execute in
parallel until #106 lands (#105). No broad functional-coverage claim should be
made until both are resolved — and note that a green unit suite is a much
narrower statement than it appears, since only `build-and-address-tests` gates
master while the macOS and Win64 jobs are disabled outright.

## Component Matrix

**On the "Last verified" column.** Added 2026-08-01, because without it every row
read identically whether it had been checked the day before or never at all. That
is how the seed problem survived seven weeks: C01 and C36 both named "launch seed
policy" as an open gap in June, correctly, and nobody noticed that no one had gone
and checked whether the configured hostname resolves. It does not.

The column records when a human last confirmed the row against the source, not
when the row was written or last edited. As of 2026-08-01:

| | rows |
| --- | ---: |
| Verified during the 2026-07-30 – 08-01 audit-prep sweep | 13 |
| Verified in the June Stage 0/1 tranche, not since | 5 |
| **Never independently verified** | **19** |

Nineteen is the number that matters. Those rows state required evidence levels
for components nobody has examined, and they look exactly like the rows that have
been examined. Treat a `never` row as an inventory entry (V0), regardless of what
its Required level column says.

Dates are deliberately conservative: a row is only marked verified where the check
covered the component's actual state model, not merely where a test touching it
happened to pass.


| ID | Component | State model / FSM | Existing coverage observed | Required gaps to close | Required level | Last verified |
|---|---|---|---|---|---|---|
| C01 | Chainparams and network identity | Network selection, genesis, message magic, ports, HRPs, base58 prefixes, chainwork and assumevalid policy | `chainparams_genesis_tests`, `pow_tests`, `btq_chain_identity.py` smoke coverage | Exact assertions for all networks, distinct genesis/header corpus, nonzero mainnet launch policy or explicit accepted risk | V2/V5 | 2026-08-01 |
| C02 | PoW legacy retarget | Height and timespan transitions before LWMA | Generic `pow_tests` | Boundary vectors, malformed `nBits`, alternate-timespan clamps, regression against Bitcoin-derived paths | V3 | **never** |
| C03 | LWMA retarget | Activation height, 45-block window, solvetime clamp, `PermittedDifficultyTransition` | Stage 1 adds low-target preservation, no-retarget, help, and GBT activation coverage | Wrong-block rejection, reorg over activation, headers-sync boundary vectors, independent LWMA oracle | V3/V4 | 2026-07-31 |
| C04 | Consensus activation flags | Height-driven script flags for Dilithium and always-on P2MR | Dilithium/P2MR unit and functional smoke | Pre/post Dilithium-height block tests, P2MR mandatory/standard split, libconsensus exposure parity | V3 | **never** |
| C05 | Block validation and ConnectBlock | Header, tx, witness, script, sigops, undo, chainstate mutation | Generic validation/unit tests | Blocks with valid and invalid Dilithium/P2MR spends, max-weight blocks, undo/reorg with PQ witnesses, prune/reindex | V4 | **never** |
| C06 | Transaction checks and UTXO | Noncontextual tx rules, coin view state, duplicate spends, witness serialization | `transaction_tests`, `txvalidation_tests`, `coins_tests` | Large witness serialization, PQ spend undo, P2MR tx mutation tests, weight/vsize cross-checks | V3 | **never** |
| C07 | Script interpreter base/witness | `EvalScript`, witness program dispatch, NULLFAIL, encoding, stack/resource limits | Stage 1 adds Dilithium single-sig NULLFAIL coverage; broad `script_tests` still fails stale BTQ vector/harness buckets | Repair stale `script_tests`, full FSM table for BASE/WITNESS_V0/TAPSCRIPT/P2MR, malformed Dilithium pubkeys, large stack items | V3 | 2026-07-31 |
| C08 | P2MR consensus path | Witness v2 root, control block, leaf execution, omitted nodes, no key path | `feature_p2mr.py`, wallet P2MR unit tests | Deterministic C++ control-path FSM, malformed control sizes, annex, parity, unknown leaf versions, cross-domain mutations | V3 | 2026-07-31 |
| C09 | Real P2MR Dilithium script spends | Dilithium signature creation/checking inside P2MR leaf | Stage 1 adds real C++ P2MR Dilithium pubkey/pubkeyhash signing and RPC mined spend coverage | Broaden mined mutation matrix, invalid control paths, annex/codeseparator binding, same script rejected under Taproot | V3/V4 | 2026-06-04 |
| C10 | Dilithium C wrapper | Keygen, seeded keygen, sign, verify, fail-closed unsupported helpers | `dilithium_key_tests`, new `sk_to_pk` regression | NIST/known-answer vectors, malformed/null input policy, deterministic seed vectors | V3 | 2026-07-31 |
| C11 | `CDilithiumKey` and `CDilithiumPubKey` | Invalid, generated, loaded, serialized, mismatched `sk||pk`, signing | Stage 1 rejects mismatched `sk||pk`; focused Dilithium key/wallet suites pass | Random full-size blob quarantine, `IsValid` vs `IsFullyValid` consistency, KAT/differential verification | V3 | 2026-07-31 |
| C12 | Dilithium HD/ext keys | Seed, derive hardened, encode/decode, depth/fingerprint state | Stage 1 rejects non-hardened child metadata during extkey/extpubkey decode | Max-depth, invalid master metadata, broader decode mutation, reload and restored-signing tests | V3 | 2026-06-04 |
| C13 | Classical ECDSA/Schnorr compatibility | Legacy, segwit, taproot, existing Bitcoin signing FSMs | Upstream-derived unit and functional tests | Prove BTQ changes did not weaken DER/STRICTENC, Taproot, PSBT, or descriptor signing | V2/V3 | **never** |
| C14 | Sighash and domain separation | BASE/WITNESS_V0/TAPSCRIPT/P2MR hashing and signature family split | `sighash_tests`, Dilithium sighash-byte test, mixed-mode tests | P2MR Dilithium mutation matrix for leaf script, control path, prevout, amount, annex, codeseparator | V3 | **never** |
| C15 | Sigops and resource accounting | Legacy/witness/P2MR sigop counts, Dilithium cost, validation weight | `sigopcount_tests`, `feature_dilithium_sigops.py`, constants lint | Interpreter debit equals script counting, P2MR witness-v2 accounting, max-cost block and mempool tests | V4 | 2026-07-30 |
| C16 | Policy and standardness | Standard flags, dust, max standard tx weight, scriptsig/witness limits | Generic policy tests, `dilithium_network_policy_tests` | P2MR+Dilithium mempool acceptance/rejection matrix, dust economics for PQ spends, package policy | V3 | 2026-07-30 |
| C17 | Mempool accept/RBF/packages | Accept states, ancestor/descendant, package validation, replacement | `mempool_tests`, `rbf_tests`, `txpackage_tests` | PQ/P2MR package tests, large witness packages, RBF fee delta and witness malleability behavior | V3/V4 | **never** |
| C18 | Mining and GBT | Template assembly, block limits, rules array, inclusion policy | Stage 1 adds LWMA GBT rule boundary; existing mining smoke remains | Valid P2MR Dilithium tx in template and mined block, near-8MW stress, package inclusion policy | V4 | 2026-06-04 |
| C19 | P2P headers/block relay | Handshake, network magic, headers sync, DoS header tree, invalid blocks | Generic P2P tests | Replace stale Bitcoin header corpus, BTQ genesis/minchainwork tests, invalid LWMA/P2MR block relay tests | V3/V4 | **never** |
| C20 | Compact blocks and block filters | Short IDs, prefilled txs, filter/index state | Generic `blockencodings_tests`, `blockfilter_index_tests` | Blocks containing large PQ witnesses and P2MR outputs through compact relay and filters | V2/V3 | **never** |
| C21 | RPC raw transaction/script | Decode, create, sign, submit, validate address, descriptor info | Generic RPC tests plus P2MR/Dilithium RPC smoke | P2MR address fields, raw Dilithium signing, invalid scripts, named-arg shape regressions | V2 | 2026-07-31 |
| C22 | Wallet legacy ScriptPubKeyMan | Keypool, legacy key maps, Dilithium side maps, imports | Wallet unit tests and some functional Dilithium sends | Import/rescan, encrypted reload, old-record migration, disabled unsupported address types | V3 | **never** |
| C23 | Wallet descriptor ScriptPubKeyMan | Descriptor key maps plus Dilithium side keys | Descriptor encryption/migration unit tests, functional sends | Reload persistence, listdescriptors/importdescriptors behavior, watch-only and solvability semantics | V3 | **never** |
| C24 | Wallet DB and encryption | Plain/encrypted records, IV derivation, load/unload/reload | `walletdb_tests`, `wallet_crypto_tests`, Dilithium wallet tests | Descriptor and legacy encrypted Dilithium restart-unlock-sign, corrupt record handling, migration tests | V3 | **never** |
| C25 | P2MR wallet metadata and RPC | Create/list/get/fund/spend/sign metadata FSM | `p2mr_tests`, `feature_p2mr_rpc.py`, restored BIP360 send paths | Unload/reload metadata, multi-leaf signed leaves, RPC invalid tree matrix, duplicate metadata cleanup | V2/V3 | 2026-06-04 |
| C26 | Dilithium wallet RPCs | Address, import/export, sign/verify, raw signing | `wallet_dilithium_send.py`, HD restore, registration tests | `importdilithiumkey` rescan, cross-wallet verify behavior, malformed base64/address, rawsign mixed inputs | V2/V3 | **never** |
| C27 | Address, key_io, output types | Encode/decode destinations, prefixes, script generation | `key_io_tests`, `dilithium_address_script_tests` | Network mismatch import tests, disabled Dilithium bech32 behavior, all output type RPC round-trips | V2 | 2026-07-31 |
| C28 | Descriptors, miniscript, PSBT | Parse, infer, expand, satisfactions, updater/signer | Generic descriptor/miniscript/PSBT tests and Dilithium descriptor smoke | Real Dilithium descriptor parse/import/sign or explicit unsupported negatives, P2MR descriptor policy | V3 | **never** |
| C29 | Coin selection, fees, fee bump | Input weight estimation, selection state, fee bump limits | `coinselector_tests`, `feebumper_tests` | Dilithium/P2MR UTXO fee bumping, RBF and max-weight estimation for PQ witnesses | V2/V3 | **never** |
| C30 | Indexes and scanners | txindex, blockfilter, coinstats, wallet scan | Generic index tests | PQ/P2MR outputs through index build, reorg, restart, prune/reindex | V3 | **never** |
| C31 | Functional test framework | Python constants, block/tx builders, wallet helpers, P2P harness | Constants lint and many generic tests | Reusable valid P2MR Dilithium spend helper, BTQ header corpus, full runner hygiene in CI | V3 | 2026-07-30 |
| C32 | Fuzzing | Script, tx, policy, deserialization, P2P fuzz targets | Existing upstream fuzz targets | Dedicated Dilithium wrapper/pubkey, P2MR witness, signing-provider, wallet metadata fuzz targets | V3 | **never** |
| C33 | Bench/performance | Validation, crypto, mempool, block assembly cost | Generic benches | Dilithium verify throughput, all-PQ block validation, P2MR control path cost, target SLOs | V4 | **never** |
| C34 | Build, CI, release hygiene | Autotools, CI matrix, lint, secrets, reproducibility | Existing lint/CI files, new registration lint | Full Linux/macOS/Windows matrix, ASan/UBSan/TSan, Guix/reproducible evidence, secret scanning | V5 | 2026-08-01 |
| C35 | Init/config/args | Args parsing, activation overrides, wallet/node init | Stage 1 adds `-testactivationheight=lwma@N` help and functional parse/use coverage | Invalid LWMA activation args, chain-specific defaults, restart/reindex activation override tests | V2 | 2026-06-04 |
| C36 | Ancillary networking | Tor/I2P, DNS seeds, peer eviction, addrman | Generic upstream tests | BTQ chain identity in peer tests, launch seed policy, P2P magic and stale peer corpus checks | V2 | 2026-08-01 |
| C37 | UI/Qt/external signer | Wallet UI, coin control, external signer integration | Qt tests if enabled, external signer generic tests | PQ/P2MR address display, fee estimates, unsupported signing paths clearly rejected | V2 | **never** |

## Staged Execution Plan

1. Stage 0 - Audit harness and first defects.
   - Status: this tranche.
   - Goal: make later evidence trustworthy by registering tests, restoring
     missing functionals, and fixing immediate audit-discovered defects.

2. Stage 1 - Revalidate prior Critical/High findings.
   - Re-run each finding from `AUDIT_REPORT_2026-05-19.md` against current
     branch.
   - Produce a finding ledger: closed with evidence, still open, superseded,
     or invalid with proof.
   - Do not close any Critical/High without the validation level above.

3. Stage 2 - Consensus FSMs.
   - Chainparams, PoW, LWMA, script flags, block validation, UTXO undo,
     P2MR consensus, Dilithium consensus opcodes.
   - Required outputs: deterministic unit vectors, functional activation tests,
     invalid-block tests, reorg/reindex/prune tests.

4. Stage 3 - PQ cryptography and signing FSMs.
   - Dilithium wrapper KATs, key load/serialize invariants, HD/extkey state,
     signature domain separation, malformed pubkey handling.
   - Required outputs: KAT/differential evidence, fuzz targets, mutation tests.

5. Stage 4 - Wallet FSMs.
   - Legacy and descriptor wallets, encrypted key storage, imports/rescan,
     P2MR metadata reload, Dilithium RPCs, PSBT/descriptors, fee bumping.
   - Required outputs: restart-cycle functionals and negative RPC matrices.

6. Stage 5 - Mempool, mining, P2P, and resource stress.
   - Package/RBF, template inclusion, block relay, compact blocks, large PQ
     witnesses, near-limit blocks.
   - Required outputs: V4 stress evidence and performance envelopes.

7. Stage 6 - CI, fuzz, release gate.
   - Full runner, sanitizers, fuzz smoke and longer fuzz campaigns,
     reproducible-build evidence, secret scanning, independent review.

## Current No-Go Items

The branch is still no-go for mainnet until at least these are resolved with
evidence:

- **Mainnet peer discovery does not work.** The sole DNS seed `seed1.btq.com`
  has no A record and `chainparams_seed_main[]` is a placeholder marked invalid
  in its own comment, so a default-configured node cannot bootstrap. `btq.com` is
  additionally a domain the project no longer controls, which makes this a trust
  anchor held by a third party rather than only a launch bug. Signet has the same
  configuration. **#114** — treat as launch-blocking.
- P2MR Dilithium mined-spend coverage exists, but the full mined mutation
  matrix, invalid-control matrix, and reorg/reindex coverage remain open.
- Revalidation of prior chainparams findings: mainnet chainwork/assumevalid,
  genesis/network identity, signet/testnet/regtest separation.
- Dilithium KATs and malformed key/pubkey/load tests. Confirmed absent: the three
  files named in `SHA256SUMS` are not in the tree. **#103**
- **LWMA has never executed on any live chain, and no functional test covers it**
  — activation is height 300000 on testnet, signet and regtest, and 1 on mainnet
  alone. Whether `LWMA_WINDOW = 45` is deliberate for 60-second spacing is also
  unrecorded. **#110**
- **126 of 304 functional tests fail** (#104), and the runner cannot run in
  parallel until #106 lands (#105).
- Wallet encrypted/reload/import/rescan coverage for Dilithium and P2MR.
- P2P stale header corpus replacement and invalid-block tests.
- Dedicated P2MR/Dilithium fuzz targets and resource/performance stress.
- Three known-unspendable or unreachable code paths that read as live capability:
  unused `DILITHIUM5_*` constants against a hardcoded Dilithium2 build,
  `SignatureAlgorithm::FALCON`/`SPHINCS` written on every network and read
  nowhere (#116), and `TxoutType::DILITHIUM_SCRIPTHASH`, which `Solver()` can
  never produce (#112). None are defects; all will cost a reviewer time.

Closed since the June revision: broad `script_tests` is now green, along with
the rest of the unit suite.

This document should be updated after each stage with exact commands, outputs,
and commit hashes for every fix.
