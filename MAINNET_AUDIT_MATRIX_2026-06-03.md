# BTQ Mainnet Component Audit Matrix

Date: 2026-06-03
Branch: `fix/p2mr-metadata-idempotent`
Status: Stage 0 audit tranche complete, mainnet readiness not achieved.

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

## Component Matrix

| ID | Component | State model / FSM | Existing coverage observed | Required gaps to close | Required level |
|---|---|---|---|---|---|
| C01 | Chainparams and network identity | Network selection, genesis, message magic, ports, HRPs, base58 prefixes, chainwork and assumevalid policy | `chainparams_genesis_tests`, `pow_tests`, `btq_chain_identity.py` smoke coverage | Exact assertions for all networks, distinct genesis/header corpus, nonzero mainnet launch policy or explicit accepted risk | V2/V5 |
| C02 | PoW legacy retarget | Height and timespan transitions before LWMA | Generic `pow_tests` | Boundary vectors, malformed `nBits`, alternate-timespan clamps, regression against Bitcoin-derived paths | V3 |
| C03 | LWMA retarget | Activation height, 45-block window, solvetime clamp, `PermittedDifficultyTransition` | `pow_lwma_tests` is shallow | Unit vectors, functional activation with `-testactivationheight=lwma@N`, wrong-block rejection, reorg over activation | V3/V4 |
| C04 | Consensus activation flags | Height-driven script flags for Dilithium and always-on P2MR | Dilithium/P2MR unit and functional smoke | Pre/post Dilithium-height block tests, P2MR mandatory/standard split, libconsensus exposure parity | V3 |
| C05 | Block validation and ConnectBlock | Header, tx, witness, script, sigops, undo, chainstate mutation | Generic validation/unit tests | Blocks with valid and invalid Dilithium/P2MR spends, max-weight blocks, undo/reorg with PQ witnesses, prune/reindex | V4 |
| C06 | Transaction checks and UTXO | Noncontextual tx rules, coin view state, duplicate spends, witness serialization | `transaction_tests`, `txvalidation_tests`, `coins_tests` | Large witness serialization, PQ spend undo, P2MR tx mutation tests, weight/vsize cross-checks | V3 |
| C07 | Script interpreter base/witness | `EvalScript`, witness program dispatch, NULLFAIL, encoding, stack/resource limits | `script_tests`, `script_standard_tests`, `dilithium_basic_tests` | Full FSM table for BASE/WITNESS_V0/TAPSCRIPT/P2MR, malformed Dilithium pubkeys, large stack items | V3 |
| C08 | P2MR consensus path | Witness v2 root, control block, leaf execution, omitted nodes, no key path | `feature_p2mr.py`, wallet P2MR unit tests | Deterministic C++ control-path FSM, malformed control sizes, annex, parity, unknown leaf versions, cross-domain mutations | V3 |
| C09 | Real P2MR Dilithium script spends | Dilithium signature creation/checking inside P2MR leaf | Current tests prove opcode reachability and wallet P2MR send path, not a real valid Dilithium leaf spend | Valid spend mined, bad sig/pubkey/script/control/amount mutations fail, same script rejected under Taproot | V3/V4 |
| C10 | Dilithium C wrapper | Keygen, seeded keygen, sign, verify, fail-closed unsupported helpers | `dilithium_key_tests`, new `sk_to_pk` regression | NIST/known-answer vectors, malformed/null input policy, deterministic seed vectors | V3 |
| C11 | `CDilithiumKey` and `CDilithiumPubKey` | Invalid, generated, loaded, serialized, mismatched `sk||pk`, signing | `dilithium_key_tests`, `dilithium_wallet_tests` | Reject or quarantine random full-size blobs, mismatched `sk||pk`, `IsValid` vs `IsFullyValid` consistency | V3 |
| C12 | Dilithium HD/ext keys | Seed, derive hardened, encode/decode, depth/fingerprint state | `dilithium_wallet_tests`, `scriptpubkeyman_tests` | Max-depth, invalid master metadata, decode mutation, reload and restored-signing tests | V3 |
| C13 | Classical ECDSA/Schnorr compatibility | Legacy, segwit, taproot, existing Bitcoin signing FSMs | Upstream-derived unit and functional tests | Prove BTQ changes did not weaken DER/STRICTENC, Taproot, PSBT, or descriptor signing | V2/V3 |
| C14 | Sighash and domain separation | BASE/WITNESS_V0/TAPSCRIPT/P2MR hashing and signature family split | `sighash_tests`, Dilithium sighash-byte test, mixed-mode tests | P2MR Dilithium mutation matrix for leaf script, control path, prevout, amount, annex, codeseparator | V3 |
| C15 | Sigops and resource accounting | Legacy/witness/P2MR sigop counts, Dilithium cost, validation weight | `sigopcount_tests`, `feature_dilithium_sigops.py`, constants lint | Interpreter debit equals script counting, P2MR witness-v2 accounting, max-cost block and mempool tests | V4 |
| C16 | Policy and standardness | Standard flags, dust, max standard tx weight, scriptsig/witness limits | Generic policy tests, `dilithium_network_policy_tests` | P2MR+Dilithium mempool acceptance/rejection matrix, dust economics for PQ spends, package policy | V3 |
| C17 | Mempool accept/RBF/packages | Accept states, ancestor/descendant, package validation, replacement | `mempool_tests`, `rbf_tests`, `txpackage_tests` | PQ/P2MR package tests, large witness packages, RBF fee delta and witness malleability behavior | V3/V4 |
| C18 | Mining and GBT | Template assembly, block limits, rules array, inclusion policy | `miner_tests`, `miniminer_tests`, `btq_regtest_mining.py` | Valid P2MR Dilithium tx in template and mined block, LWMA GBT rule boundary, near-8MW stress | V4 |
| C19 | P2P headers/block relay | Handshake, network magic, headers sync, DoS header tree, invalid blocks | Generic P2P tests | Replace stale Bitcoin header corpus, BTQ genesis/minchainwork tests, invalid LWMA/P2MR block relay tests | V3/V4 |
| C20 | Compact blocks and block filters | Short IDs, prefilled txs, filter/index state | Generic `blockencodings_tests`, `blockfilter_index_tests` | Blocks containing large PQ witnesses and P2MR outputs through compact relay and filters | V2/V3 |
| C21 | RPC raw transaction/script | Decode, create, sign, submit, validate address, descriptor info | Generic RPC tests plus P2MR/Dilithium RPC smoke | P2MR address fields, raw Dilithium signing, invalid scripts, named-arg shape regressions | V2 |
| C22 | Wallet legacy ScriptPubKeyMan | Keypool, legacy key maps, Dilithium side maps, imports | Wallet unit tests and some functional Dilithium sends | Import/rescan, encrypted reload, old-record migration, disabled unsupported address types | V3 |
| C23 | Wallet descriptor ScriptPubKeyMan | Descriptor key maps plus Dilithium side keys | Descriptor encryption/migration unit tests, functional sends | Reload persistence, listdescriptors/importdescriptors behavior, watch-only and solvability semantics | V3 |
| C24 | Wallet DB and encryption | Plain/encrypted records, IV derivation, load/unload/reload | `walletdb_tests`, `wallet_crypto_tests`, Dilithium wallet tests | Descriptor and legacy encrypted Dilithium restart-unlock-sign, corrupt record handling, migration tests | V3 |
| C25 | P2MR wallet metadata and RPC | Create/list/get/fund/spend/sign metadata FSM | `p2mr_tests`, `feature_p2mr_rpc.py`, restored BIP360 send paths | Unload/reload metadata, multi-leaf signed leaves, RPC invalid tree matrix, duplicate metadata cleanup | V2/V3 |
| C26 | Dilithium wallet RPCs | Address, import/export, sign/verify, raw signing | `wallet_dilithium_send.py`, HD restore, registration tests | `importdilithiumkey` rescan, cross-wallet verify behavior, malformed base64/address, rawsign mixed inputs | V2/V3 |
| C27 | Address, key_io, output types | Encode/decode destinations, prefixes, script generation | `key_io_tests`, `dilithium_address_script_tests` | Network mismatch import tests, disabled Dilithium bech32 behavior, all output type RPC round-trips | V2 |
| C28 | Descriptors, miniscript, PSBT | Parse, infer, expand, satisfactions, updater/signer | Generic descriptor/miniscript/PSBT tests and Dilithium descriptor smoke | Real Dilithium descriptor parse/import/sign or explicit unsupported negatives, P2MR descriptor policy | V3 |
| C29 | Coin selection, fees, fee bump | Input weight estimation, selection state, fee bump limits | `coinselector_tests`, `feebumper_tests` | Dilithium/P2MR UTXO fee bumping, RBF and max-weight estimation for PQ witnesses | V2/V3 |
| C30 | Indexes and scanners | txindex, blockfilter, coinstats, wallet scan | Generic index tests | PQ/P2MR outputs through index build, reorg, restart, prune/reindex | V3 |
| C31 | Functional test framework | Python constants, block/tx builders, wallet helpers, P2P harness | Constants lint and many generic tests | Reusable valid P2MR Dilithium spend helper, BTQ header corpus, full runner hygiene in CI | V3 |
| C32 | Fuzzing | Script, tx, policy, deserialization, P2P fuzz targets | Existing upstream fuzz targets | Dedicated Dilithium wrapper/pubkey, P2MR witness, signing-provider, wallet metadata fuzz targets | V3 |
| C33 | Bench/performance | Validation, crypto, mempool, block assembly cost | Generic benches | Dilithium verify throughput, all-PQ block validation, P2MR control path cost, target SLOs | V4 |
| C34 | Build, CI, release hygiene | Autotools, CI matrix, lint, secrets, reproducibility | Existing lint/CI files, new registration lint | Full Linux/macOS/Windows matrix, ASan/UBSan/TSan, Guix/reproducible evidence, secret scanning | V5 |
| C35 | Init/config/args | Args parsing, activation overrides, wallet/node init | `argsman_tests`, init tests | `-testactivationheight=lwma@N` help/config coverage, invalid activation args, chain-specific defaults | V2 |
| C36 | Ancillary networking | Tor/I2P, DNS seeds, peer eviction, addrman | Generic upstream tests | BTQ chain identity in peer tests, launch seed policy, P2P magic and stale peer corpus checks | V2 |
| C37 | UI/Qt/external signer | Wallet UI, coin control, external signer integration | Qt tests if enabled, external signer generic tests | PQ/P2MR address display, fee estimates, unsupported signing paths clearly rejected | V2 |

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

- Real valid P2MR Dilithium spend through mempool, mining, block validation,
  and mutation failure tests.
- LWMA activation and difficulty transition functional coverage.
- Revalidation of prior chainparams findings: mainnet chainwork/assumevalid,
  genesis/network identity, signet/testnet/regtest separation.
- Dilithium KATs and malformed key/pubkey/load tests.
- Wallet encrypted/reload/import/rescan coverage for Dilithium and P2MR.
- P2P stale header corpus replacement and invalid-block tests.
- Dedicated P2MR/Dilithium fuzz targets and resource/performance stress.

This document should be updated after each stage with exact commands, outputs,
and commit hashes for every fix.
