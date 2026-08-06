# BTQ Core Pre-Mainnet Security Audit Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Systematically audit btq-core (a Bitcoin fork with CRYSTALS-Dilithium post-quantum signatures) for security vulnerabilities before mainnet launch.

**Architecture:** 10 audit domains ordered by criticality. Each task produces a findings report with severity ratings (CRITICAL / HIGH / MEDIUM / LOW / INFO). Findings that require code changes will be collected into a remediation plan at the end.

**Tech Stack:** C++17 codebase forked from Bitcoin Core. Key additions: Dilithium2 signatures (pqcrystals reference implementation), BIP360 P2MR (witness v2 Merkle-root vaults), LWMA-1 per-block difficulty adjustment, custom opcodes (OP_CHECKSIGDILITHIUM 0xbb, OP_CHECKSIGDILITHIUMVERIFY 0xbc).

**Research basis:** This plan is informed by:

*Bitcoin Core CVEs & Historical Attacks:*
- CVE-2010-5139 (value overflow), CVE-2018-17144 (inflation bug), CVE-2012-2459 (merkle duplicate), CVE-2024-52911 (use-after-free in script validation)
- 51% attacks on Bitcoin Gold ($18M, 2018), Ethereum Classic ($5.6M, 2020), Bitcoin SV (100-block reorg, 2021), Vertcoin ($100K, 2018)
- Bitcoin V2 P2P transport eclipse/downgrade attacks (arXiv:2605.19715)
- Time warp attacks (BIP54), fork replay attacks (BCH/BSV split)

*Dilithium/Post-Quantum Cryptographic Research:*
- Signature Correction Attack on Dilithium — Rowhammer-based key recovery, 1851/3072 bits recovered (arXiv:2203.00637)
- Single-Trace Side-Channel Attacks on CRYSTALS-Dilithium — 9% single-trace, 100% multi-trace key recovery (ePrint:2023/1931)
- Profiling Side-Channel Attacks on Dilithium: A Small Bit-Fiddling Leak Breaks It All (ResearchGate, 2024)
- Correction Fault Attacks on Randomized CRYSTALS-Dilithium (ePrint:2024/138)
- In-depth Correlation Power Analysis on Hardware Dilithium (Springer Cybersecurity, 2024)
- Machine-Checked Cardinality Bounds for Masked Barrett Reduction in PQC (arXiv:2604.24670)
- Structural Dependency Analysis for Masked NTT Hardware — ML-DSA FIPS 140-3 verification (arXiv:2604.15249)
- quantum-safe: Bridging the Post-Quantum Production Gap (arXiv:2605.17061)

*Post-Quantum Blockchain Research:*
- Securing Elliptic Curve Cryptocurrencies against Quantum Vulnerabilities — <1200 logical qubits to break ECDSA (arXiv:2603.28846)
- The Cost of Quantum Resistance: Hash-Based Commit-Reveal Alternative (arXiv:2605.06853)
- Performance Analysis of Quantum-Secure Digital Signatures in Blockchain — Dilithium/Falcon/Hawk benchmarks (arXiv:2601.17785)
- 51% Attack via Difficulty Increase with a Small Quantum Miner — O(√c) advantage via Grover's (arXiv:2403.08023)
- Towards Post-Quantum Bitcoin Blockchain using Dilithium Signature — DilithiumRK for HD wallets (IACR CiC, 2025)

*Bitcoin Fork Security Research:*
- Attack of the Clones: 80% of 510 C-language crypto forks have ≥1 unpatched vulnerability (arXiv:2201.08678)
- Estimating Patch Propagation Times Across Forks — mean 237.8 days to fix (arXiv:2205.07478)
- Quantum-Safe Code Auditing: LLM-Assisted Static Analysis for PQC Migration (arXiv:2604.00560)

*P2P Network & Consensus:*
- Eclipse Attacks on Bitcoin's Peer-to-Peer Network (USENIX Security 2015, ePrint:2015/263)
- Eclipse Attacks on Ethereum's Peer-to-Peer Network — post-Merge analysis (arXiv:2601.16560)
- Security Analysis of Distributed Ledgers via Agent-based Simulation (arXiv:2109.08358)
- Close Latency-Security Trade-off for Nakamoto Consensus (arXiv:2011.14051)
- Firmware Distribution as Attack Surface: ASIC Cryptocurrency Miners (arXiv:2605.03770)

*Industry Reports & Guides:*
- SlowMist Cryptocurrency Security Audit Guide (GitHub)
- Google PQC migration deadline: 2029 (CoinDesk, March 2026)
- Project Eleven: 6.9M BTC with exposed public keys (CoinDesk, May 2026)
- 2025 total crypto losses: $3.4B; Bybit hack: $1.5B (Lazarus Group)
- Bitcoin network Sybil attack: 250,000+ fake nodes (April 2026)

---

## Task 1: Replay Protection Audit (CRITICAL)

**Why this is #1:** Every Bitcoin fork that launched without replay protection caused user fund losses. BTQ transactions must be invalid on Bitcoin mainnet and vice versa. Initial code inspection shows NO `SIGHASH_FORKID` or equivalent mechanism.

**Files:**
- Audit: `src/script/interpreter.h:29-36` (SIGHASH types — currently identical to Bitcoin)
- Audit: `src/script/interpreter.cpp` (SignatureHash function — how transaction signing digest is computed)
- Audit: `src/kernel/chainparams.cpp:112-115` (network magic bytes `0xf1b2a3d4` — differs from Bitcoin's `0xf9beb4d9`)
- Audit: `src/consensus/tx_check.cpp` (CheckTransaction — no fork-specific validation)
- Audit: `src/primitives/transaction.h` (transaction format — does it differ from Bitcoin?)

- [ ] **Step 1: Verify SIGHASH types**
  Check `src/script/interpreter.h` for any BTQ-specific sighash flag (like BCH's `SIGHASH_FORKID = 0x40`). Currently observed: SIGHASH_ALL=1, SIGHASH_NONE=2, SIGHASH_SINGLE=3, SIGHASH_ANYONECANPAY=0x80 — identical to Bitcoin. If no fork-specific flag exists, this is a **CRITICAL** finding.

- [ ] **Step 2: Analyze SignatureHash computation**
  Read `SignatureHash()` and `SignatureHashSchnorr()` in `src/script/interpreter.cpp`. Check whether any chain-specific data (chain ID, fork ID, unique prefix) is mixed into the hash. If the sighash digest is computed identically to Bitcoin Core, a valid legacy (ECDSA) BTQ transaction can be replayed on Bitcoin and vice versa.

- [ ] **Step 3: Check natural replay protection from Dilithium**
  Dilithium-signed transactions use witness v2 (P2MR) or custom opcodes. Bitcoin nodes will reject these as non-standard. Verify that:
  - Dilithium transactions are invalid on Bitcoin (expected: yes, due to unknown witness version / unknown opcode)
  - Legacy ECDSA transactions are cross-chain replayable (expected: yes, this is the vulnerability)

- [ ] **Step 4: Assess network magic separation**
  BTQ uses `pchMessageStart = {0xf1, 0xb2, 0xa3, 0xd4}` vs Bitcoin's `{0xf9, 0xbe, 0xb4, 0xd9}`. This prevents P2P cross-talk but does NOT prevent transaction replay (transactions are submitted via RPC, not raw P2P magic).

- [ ] **Step 5: Check address format differentiation**
  Verify that BTQ base58 prefixes and bech32 HRP differ from Bitcoin. If they're the same, users can accidentally send to wrong-chain addresses.

- [ ] **Step 6: Document finding**
  Write up the replay protection assessment with severity rating. If no transaction-level replay protection exists for legacy ECDSA transactions, rate as **CRITICAL** and recommend implementing `SIGHASH_FORKID` or equivalent before mainnet.

---

## Task 2: Dilithium Cryptographic Implementation Audit (CRITICAL)

**Why this is #2:** The entire security proposition of BTQ rests on correct Dilithium integration. Academic research shows the reference implementation is vulnerable to side-channel attacks (single-trace key recovery achieves ~9% success, 100% with multiple traces). The wrapper code has a potentially incorrect `sk_to_pk` implementation.

**Files:**
- Audit: `src/crypto/dilithium_wrapper.c` (C FFI to pqcrystals reference implementation)
- Audit: `src/crypto/dilithium_wrapper.h` (API constants and declarations)
- Audit: `src/crypto/dilithium_key.h` (CDilithiumKey class — secure memory, key operations)
- Audit: `src/crypto/dilithium_key.cpp` (Sign, MakeNewKey, GenerateFromEntropy implementations)
- Audit: `src/crypto/dilithium_pubkey.cpp` (Verify, IsFullyValid implementations)
- Audit: `src/crypto/dilithium_hd_key.h/.cpp` (HD wallet derivation for Dilithium)
- Audit: `src/crypto/dilithium/ref/` (vendored pqcrystals reference code)

### 2a: Reference Implementation Correctness

- [ ] **Step 1: Identify the Dilithium version**
  Check `src/crypto/dilithium/ref/api.h` for the version. Determine whether this is:
  - NIST FIPS 204 (ML-DSA) — the standard (August 2024)
  - Round 3 Dilithium — pre-standard, has subtle differences
  - An older draft
  If pre-FIPS-204, signatures may be incompatible with other ML-DSA implementations. Rate as **HIGH**.

- [ ] **Step 2: Verify parameter set**
  Confirm that the constants in `dilithium_wrapper.h` match the chosen Dilithium mode:
  - Dilithium2/ML-DSA-44: pk=1312, sk=2560, sig=2420 ✓ (matches header)
  - Cross-check with `pqcrystals_dilithium2_ref_PUBLICKEYBYTES` etc. in the vendored code

- [ ] **Step 3: Audit `btq_dilithium_sk_to_pk` (SUSPICIOUS)**
  The current implementation at `dilithium_wrapper.c:47-55` does:
  ```c
  memcpy(pk, sk, pqcrystals_dilithium2_ref_PUBLICKEYBYTES);
  ```
  This assumes the public key is the first 1312 bytes of the secret key. In the standard Dilithium2 format, sk = `(rho || K || tr || s1 || s2 || t0)` and pk = `(rho || t1)`. The public key is NOT simply the first bytes of the secret key unless the implementation packs them that way. Verify against the actual `ref/` code's key format. If incorrect, this is a **CRITICAL** bug — `GetPubKey()` would return wrong data.

- [ ] **Step 4: Check signing mode (hedged vs deterministic)**
  The FIPS 204 standard defaults to hedged mode (mixing fresh randomness with deterministic nonce derivation). Check whether `pqcrystals_dilithium2_ref_signature()` uses hedged mode. If purely deterministic, fault injection attacks can forge signatures with a single fault (65.2% of execution time is vulnerable). Rate as **HIGH** if deterministic-only.

- [ ] **Step 5: Audit entropy source**
  Trace how `pqcrystals_dilithium2_ref_keypair()` obtains randomness. The reference implementation typically calls `randombytes()` — verify this is hooked to a cryptographically secure source (e.g., Bitcoin Core's `GetStrongRandBytes()`), not a weak PRNG.

### 2b: Key Management Security

- [ ] **Step 6: Verify secure memory usage**
  `CDilithiumKey` uses `secure_unique_ptr<KeyType>` — good, this ensures secure allocation and zeroing. But check:
  - `GetPrivKey()` returns `CPrivKey` (`std::vector<unsigned char>`) which is NOT securely allocated — key material escapes secure memory. Rate as **MEDIUM**.
  - `Set()` uses `memcpy` to copy into secure memory — verify no intermediate copies on stack.

- [ ] **Step 7: Check for timing leaks in key comparison**
  `CDilithiumKey::operator==` uses `memcmp()` which is NOT constant-time. If used in any security-critical path (e.g., key verification), this leaks information. Rate as **MEDIUM**.

- [ ] **Step 8: Audit HD wallet derivation**
  Read `src/crypto/dilithium_hd_key.cpp` — the `Derive()` and `SetSeed()` methods. Dilithium does not natively support BIP32-style derivation. Check how child key derivation is implemented:
  - Is the derivation scheme published/reviewed? (cf. DilithiumRK paper from IACR)
  - Does child key derivation preserve unforgeability?
  - Can knowledge of a child key + chain code reveal the parent key?

- [ ] **Step 9: Verify `GenerateFromEntropy` safety**
  Check `CDilithiumKey::GenerateFromEntropy()` — how is 32 bytes of entropy expanded into a full Dilithium2 keypair? Standard Dilithium uses 32 bytes of seed. Verify no entropy reduction or bias.

### 2c: Signature Verification

- [ ] **Step 10: Audit `CDilithiumPubKey::IsFullyValid()`**
  Read `src/crypto/dilithium_pubkey.cpp`. What validation is performed beyond "not all zeros"? Malformed public keys could cause crashes or undefined behavior in the verification function.

- [ ] **Step 11: Check signature malleability**
  Dilithium signatures have a canonical encoding. Verify that the verification function rejects non-canonical signatures. If multiple valid encodings exist for the same signature, this enables transaction malleability. Check whether signature length is strictly validated (must be exactly 2420 bytes, not "at most").

- [ ] **Step 12: Test with known test vectors**
  Verify the implementation against NIST ML-DSA test vectors (ACVP vectors or KAT files). Run `src/test/dilithium_basic_tests.cpp` and assess coverage.

---

## Task 3: Consensus-Critical Code Diff Audit (CRITICAL)

**Why this is #3:** CVE-2018-17144 (inflation bug) was introduced by removing a "redundant" check. Every line that differs from Bitcoin Core in consensus-critical paths must be reviewed for similar regressions.

**Files:**
- Audit: `src/consensus/tx_check.cpp` (CheckTransaction — duplicate input check present at line 35)
- Audit: `src/consensus/tx_verify.cpp` (CheckTxInputs — coin value validation)
- Audit: `src/validation.cpp` (ConnectBlock, CheckBlock, ContextualCheckBlock)
- Audit: `src/script/interpreter.cpp` (EvalScript — Dilithium opcode handlers)
- Audit: `src/consensus/amount.h` (MAX_MONEY, MoneyRange)
- Audit: `src/consensus/merkle.cpp` (ComputeMerkleRoot — mutation flag)

- [ ] **Step 1: Verify CVE-2018-17144 patch is intact**
  Confirm that `CheckTransaction()` in `src/consensus/tx_check.cpp:35-43` still checks for duplicate inputs. The comment "Check for duplicate inputs (see CVE-2018-17144)" is present — verify the actual check logic matches Bitcoin Core's fix.

- [ ] **Step 2: Verify CVE-2010-5139 patch is intact**
  Check `CheckTransaction()` for value overflow protection. Confirm that output values are individually checked against MAX_MONEY AND that the sum is checked with overflow detection.

- [ ] **Step 3: Verify CVE-2012-2459 patch is intact**
  Check `ComputeMerkleRoot()` in `src/consensus/merkle.cpp` for the `mutation` flag that detects duplicate transactions. Verify `CheckBlock()` rejects blocks where `mutated == true`.

- [ ] **Step 4: Audit Dilithium opcode execution in EvalScript**
  Read the handlers for `OP_CHECKSIGDILITHIUM` (0xbb) and `OP_CHECKSIGDILITHIUMVERIFY` (0xbc) in `src/script/interpreter.cpp`. Check for:
  - Stack underflow (are stack size checks performed before popping?)
  - Correct signature verification (is the hash computed correctly?)
  - Proper script cleanup (does VERIFY variant correctly handle the boolean result?)
  - Sigop counting (does Dilithium signature verification count toward sigop limits?)

- [ ] **Step 5: Audit P2MR witness validation**
  Read the witness v2 (P2MR) validation path in `src/script/interpreter.cpp`. Check:
  - Merkle proof verification (control block parsing, sibling hash validation)
  - Boundary conditions (empty tree, single-leaf tree, max 128 nodes)
  - Witness program length validation
  - Annex handling (if applicable)

- [ ] **Step 6: Verify block subsidy calculation**
  Check `GetBlockSubsidy()` or equivalent. BTQ uses 5 BTQ per block (not Bitcoin's halving schedule). Verify no integer overflow in subsidy × halvings, and that the total supply is bounded.

- [ ] **Step 7: Check ConnectBlock for Dilithium-specific validation**
  Read `ConnectBlock()` in `src/validation.cpp`. Verify that Dilithium signature validation is mandatory for Dilithium-type transactions (not skippable via flags). Check that the `SCRIPT_VERIFY_DILITHIUM` flag is always set for consensus validation.

- [ ] **Step 8: Diff the full consensus path against Bitcoin Core**
  Generate a diff of all files in `src/consensus/` against the upstream Bitcoin Core version this was forked from. Review every change for potential security regressions.

---

## Task 4: Difficulty Adjustment Security (HIGH)

**Why this is #4:** BTQ uses LWMA-1 (per-block adjustment) after block 300,000, falling back to Bitcoin's 2016-block adjustment before that. Time warp attacks have been exploited on Bitcoin Gold and Verge. A new chain with low hashrate is especially vulnerable.

**Files:**
- Audit: `src/pow.cpp` (GetNextWorkRequired, LwmaGetNextWorkRequired, CalculateNextWorkRequired)
- Audit: `src/consensus/params.h:134-135` (nLWMAHeight, LWMA_WINDOW=45)
- Audit: `src/validation.cpp` (timestamp validation rules)

- [ ] **Step 1: Audit LWMA-1 implementation**
  Read `LwmaGetNextWorkRequired()` in `src/pow.cpp:62-103`. Verify:
  - Window size N=45 is appropriate (standard LWMA-1 recommendation)
  - Solvetime clamping: currently `[-6T, 6T]` — is this sufficient to prevent manipulation?
  - The weighted solvetime formula: `sumTarget * weightedSolvetimes` — check for integer overflow
  - The early division `target / (k * N)` — verify precision loss is acceptable
  - Edge case: `weightedSolvetimes < 1` is clamped to 1 — can an attacker force this?

- [ ] **Step 2: Check time warp vulnerability in legacy mode**
  The pre-LWMA code (`GetNextWorkRequired()`) uses Bitcoin's 2016-block adjustment. Verify:
  - Does the off-by-one bug exist? (Bitcoin uses `nHeight - (interval-1)` which means it uses 2015 blocks, not 2016)
  - Is there a minimum timestamp requirement for the first block of each period? (BIP54 fix)
  - Can timestamps be manipulated to reduce difficulty to minimum?

- [ ] **Step 3: Verify timestamp validation**
  Check `ContextualCheckBlockHeader()` in `src/validation.cpp` for:
  - Median Time Past (MTP) rule: block timestamp must be > median of previous 11 blocks
  - Future time limit: block timestamp must be < current time + 2 hours
  - Any BTQ-specific timestamp rules

- [ ] **Step 4: Assess LWMA-1 manipulation resistance at low hashrate**
  At mainnet launch, hashrate will be very low. Can an attacker with majority hashrate:
  - Rapidly lower difficulty by mining blocks with timestamps far in the future?
  - Oscillate difficulty by alternating between fast and slow blocks?
  - Exploit the transition at block 300,000 (switching from legacy to LWMA)?

- [ ] **Step 5: Check `PermittedDifficultyTransition`**
  The function at `pow.cpp:133-138` returns `true` for all blocks after LWMA activation (`height >= nLWMAHeight`). This means ANY difficulty change is permitted post-LWMA. Verify this is intentional and doesn't allow unreasonable difficulty drops.

---

## Task 5: Transaction Validation and Malleability (HIGH)

**Why this is #5:** Transaction malleability enabled the Mt. Gox collapse. Dilithium transactions introduce new witness structures that need malleability protection.

**Files:**
- Audit: `src/primitives/transaction.h/.cpp` (CTransaction — Dilithium input/witness fields)
- Audit: `src/script/sign.h/.cpp` (P2MRSpendData, transaction signing)
- Audit: `src/policy/policy.h/.cpp` (standardness rules for Dilithium transactions)
- Audit: `src/consensus/tx_check.cpp` (CheckTransaction — size/weight limits)

- [ ] **Step 1: Verify txid/wtxid separation for Dilithium**
  SegWit separates witness data from txid computation. Verify that Dilithium signatures (in witness v2) are:
  - Excluded from txid computation (preventing malleability)
  - Included in wtxid computation (preventing witness malleability)

- [ ] **Step 2: Check Dilithium signature encoding canonicality**
  Dilithium2 signatures are exactly 2420 bytes. Verify that:
  - The consensus code rejects signatures that are not exactly 2420 bytes
  - There is no padding or encoding flexibility that allows multiple valid representations
  - The `SIGNATURE_SIZE` constant is enforced, not just advisory

- [ ] **Step 3: Audit P2MR witness structure**
  Read `P2MRSpendData` in `src/script/sign.h`. Check:
  - Can the Merkle proof be reordered or modified without invalidating the transaction?
  - Is the control block format strictly validated?
  - Are there any fields that are not covered by the signature?

- [ ] **Step 4: Review transaction weight calculation**
  Dilithium signatures are ~34x larger than ECDSA. Check:
  - `GetTransactionWeight()` correctly accounts for Dilithium witness data
  - Block weight limits are not exceeded by a single Dilithium transaction
  - Fee estimation correctly prices Dilithium transactions

- [ ] **Step 5: Check mixed-mode transaction validation**
  BTQ supports transactions with both ECDSA and Dilithium inputs. Verify:
  - Mixed transactions are validated correctly (both signature types checked)
  - An attacker cannot substitute a Dilithium input for an ECDSA input or vice versa
  - Sigop counting is correct for mixed transactions

---

## Task 6: P2P Network Security (HIGH)

**Why this is #6:** Eclipse attacks have been demonstrated on Bitcoin and attempted at scale (250,000 fake nodes in April 2026). A new chain with few nodes is especially vulnerable.

**Files:**
- Audit: `src/net.h/.cpp` (peer connections, eviction, connection limits)
- Audit: `src/net_processing.h/.cpp` (message handling, DoS scoring)
- Audit: `src/addrman.h/.cpp` (address manager — peer discovery, bucketing)
- Audit: `src/banman.h/.cpp` (banning logic)
- Audit: `src/chainparamsseeds.h` (hardcoded seed nodes)
- Audit: `src/kernel/chainparams.cpp` (DNS seeds, network configuration)

- [ ] **Step 1: Review peer diversity enforcement**
  Check `addrman.cpp` for:
  - Bucketing by /16 subnet (prevents eclipse from one network range)
  - New vs tried table separation
  - Eviction logic — does it favor diverse connections?

- [ ] **Step 2: Check connection limits and types**
  Verify in `src/net.cpp`:
  - How many outbound connections are maintained? (Bitcoin Core: 8 full, 2 block-relay-only)
  - Are outbound slots protected from being filled by attacker-initiated connections?
  - Is there anchor connection support?

- [ ] **Step 3: Audit message size limits for Dilithium**
  Dilithium transactions are ~34x larger. Verify:
  - `MAX_PROTOCOL_MESSAGE_LENGTH` accommodates Dilithium transactions
  - Individual message handlers validate size before allocating memory
  - Block messages with many Dilithium transactions don't exceed limits

- [ ] **Step 4: Review DNS seed and hardcoded seed configuration**
  Check `src/kernel/chainparams.cpp` and `src/chainparamsseeds.h`:
  - Are DNS seeds configured for mainnet? (research indicates: placeholder)
  - Are hardcoded seed nodes present?
  - If no seeds exist, new nodes cannot bootstrap — **CRITICAL** for mainnet launch

- [ ] **Step 5: Check DoS protection**
  Review `src/net_processing.cpp` for:
  - Misbehavior scoring and banning thresholds
  - Rate limiting for addr messages (cf. CVE-2024-52919)
  - Protection against oversized inventory messages

- [ ] **Step 6: Verify P2P message version compatibility**
  Check whether BTQ nodes can accidentally peer with Bitcoin nodes (despite different magic bytes). Verify the version handshake includes chain-specific validation.

---

## Task 7: Memory Safety Audit (HIGH)

**Why this is #7:** CVE-2024-52911 (use-after-free in script validation) affected Bitcoin Core through version 28.x. Fork codebases are often months behind on patches.

**Files:**
- Audit: `src/script/interpreter.cpp` (parallel validation threading)
- Audit: `src/crypto/dilithium_wrapper.c` (C code — no RAII, manual memory)
- Audit: `src/crypto/dilithium_key.cpp` (boundary between C++ RAII and C FFI)
- Audit: `src/crypto/dilithium/ref/` (vendored C code — reference implementation)
- Audit: `src/validation.cpp` (block validation, cache management)

- [ ] **Step 1: Verify CVE-2024-52911 patch status**
  Check if the script validation use-after-free fix (merged to Bitcoin Core December 2024) is present in this fork. Search for the relevant commit or code pattern in `src/script/interpreter.cpp` and `src/validation.cpp`.

- [ ] **Step 2: Static analysis of new C++ code**
  Run static analysis (clang-tidy, cppcheck, or equivalent) on all files in `src/crypto/dilithium_*.cpp` and `src/wallet/dilithium_*.cpp`. Flag:
  - Buffer overflows (especially in `Set()` and `Load()` functions)
  - Use-after-free or dangling references
  - Integer overflow in size calculations
  - Uninitialized memory reads

- [ ] **Step 3: Audit C FFI boundary**
  The `dilithium_wrapper.c` is pure C called from C++. Check:
  - All buffer sizes are validated before passing to C functions
  - Return values from `btq_dilithium_keypair/sign/verify` are checked
  - No stack-allocated buffers are too small for Dilithium's large keys/signatures

- [ ] **Step 4: Review vendored Dilithium reference code**
  Check `src/crypto/dilithium/ref/` for:
  - Known vulnerabilities in the specific version
  - Any local modifications (diff against upstream pqcrystals)
  - Memory safety of the NTT and polynomial operations

- [ ] **Step 5: Check compiler hardening flags**
  Review the build system (`configure.ac`, `Makefile.am`) for:
  - Stack canaries (`-fstack-protector-strong`)
  - ASLR (`-fPIE` + `-pie`)
  - RELRO (`-Wl,-z,relro,-z,now`)
  - Fortify source (`-D_FORTIFY_SOURCE=2`)
  - These should match or exceed Bitcoin Core's hardening

- [ ] **Step 6: Audit secure memory cleanup**
  Verify all cryptographic key material is zeroed after use:
  - `CDilithiumKey` uses `secure_unique_ptr` ✓
  - But `GetPrivKey()` returns non-secure `CPrivKey` (std::vector) — key escapes secure memory
  - Check all code paths that handle raw key bytes for proper cleanup

---

## Task 8: RPC Security Audit (MEDIUM)

**Why this is #8:** Bitcoin Core RPC has been exploited for remote code execution (dumpwallet → arbitrary file write → cron RCE). BTQ adds new Dilithium-specific RPCs.

**Files:**
- Audit: `src/rpc/dilithium.h` and related implementation
- Audit: `src/wallet/rpc/` (wallet RPC commands)
- Audit: `src/httprpc.h/.cpp` (HTTP transport, authentication)
- Audit: `src/rpc/server.h/.cpp` (RPC dispatch, access control)

- [ ] **Step 1: Inventory all new RPC commands**
  List all BTQ-specific RPCs:
  - `getnewdilithiumaddress`, `importdilithiumkey`, `dumpdilithiumkey`, `signtransactionwithdilithium`
  - `getnewp2mraddress`, `sendtop2mr`, `createp2mrspend`, `signp2mrtransaction`, `testp2mrtransaction`, `listp2mr`, `getp2mrinfo`
  Check each for input validation, authorization requirements, and error handling.

- [ ] **Step 2: Audit `dumpdilithiumkey` for file-write escalation**
  Similar to Bitcoin's `dumpwallet`, does `dumpdilithiumkey` write to an arbitrary path? If so, it inherits the known RCE path. Check if output is restricted to specific directories.

- [ ] **Step 3: Audit `importdilithiumkey` for injection**
  Check that key import validates the key material before storing. Malformed keys should be rejected, not stored and cause crashes later during signing.

- [ ] **Step 4: Check RPC authentication**
  Verify `src/httprpc.cpp` uses `rpcauth` (salted HMAC) and not plaintext passwords. Check that RPC is bound to localhost only by default.

- [ ] **Step 5: Review all RPC handlers for logging injection**
  Check that user-supplied strings in RPC parameters are sanitized before being written to debug.log (cf. Bitcoin Core pre-0.17.1 log injection).

---

## Task 9: Wallet and Key Management Audit (MEDIUM)

**Why this is #9:** Wallet security directly impacts user funds. The HD wallet derivation for Dilithium is non-standard and needs careful review.

**Files:**
- Audit: `src/wallet/dilithium_scriptpubkeyman.h/.cpp` (Dilithium key manager)
- Audit: `src/wallet/dilithium_wallet_manager.h/.cpp` (high-level wallet API)
- Audit: `src/crypto/dilithium_hd_key.h/.cpp` (HD derivation)
- Audit: `src/wallet/spend.h/.cpp` (transaction creation with Dilithium)
- Audit: `src/script/dilithium_signing_provider.h/.cpp` (key retrieval for signing)

- [ ] **Step 1: Audit HD wallet derivation scheme**
  Dilithium does not support BIP32 natively. Read `CDilithiumExtKey::Derive()` in `dilithium_hd_key.cpp`:
  - How is child key derivation implemented?
  - Is there a risk of related-key attacks?
  - Can a child private key + chain code reveal the master key?
  - Is the derivation path compatible with standard wallet recovery?

- [ ] **Step 2: Check key storage encryption**
  Verify that Dilithium private keys stored in the wallet database (BDB or SQLite) are encrypted with the wallet passphrase, same as ECDSA keys.

- [ ] **Step 3: Audit backup and recovery**
  Check that wallet backup (`backupwallet`) includes Dilithium keys. Verify that seed phrase recovery regenerates Dilithium addresses correctly.

- [ ] **Step 4: Review coin selection with mixed address types**
  Check `src/wallet/spend.cpp` for correct behavior when selecting UTXOs from both ECDSA and Dilithium addresses. Verify fee estimation accounts for the different signature sizes.

- [ ] **Step 5: Check address generation for collisions**
  Dilithium public keys (1312 bytes) are hashed to 20-byte addresses (via Hash160). Verify the hashing is correct and that the larger key space doesn't introduce unexpected collision properties.

---

## Task 10: Supply Chain and Dependency Audit (MEDIUM)

**Why this is #10:** 80% of C-language cryptocurrency forks have unpatched upstream vulnerabilities, with a mean fix time of 237.8 days.

**Files:**
- Audit: `src/crypto/dilithium/` (vendored Dilithium library)
- Audit: `depends/` (build dependency recipes)
- Audit: `configure.ac` (build configuration, linked libraries)
- Audit: `.github/workflows/` (CI/CD pipeline)

- [ ] **Step 1: Identify upstream Bitcoin Core version**
  Determine which Bitcoin Core version this fork is based on. Check git history for the last merge from upstream. Calculate how many security patches are missing.

- [ ] **Step 2: Verify Dilithium library provenance**
  Check `src/crypto/dilithium/ref/`:
  - Which version of pqcrystals-dilithium is vendored?
  - Has it been modified locally? (diff against upstream)
  - Is it the NIST-standardized version (FIPS 204) or a pre-standard draft?

- [ ] **Step 3: Audit all linked dependencies**
  Inventory and check versions of: Boost, OpenSSL/LibreSSL, libevent, BerkeleyDB, SQLite, LevelDB, secp256k1, miniupnpc, ZMQ. Flag any with known CVEs.

- [ ] **Step 4: Check build reproducibility**
  Bitcoin Core uses Guix for deterministic builds. Does BTQ have:
  - Reproducible build scripts?
  - Hash verification for dependencies?
  - A process to verify binary releases match source?

- [ ] **Step 5: Review CI/CD security**
  Check `.github/workflows/` for:
  - Are dependencies pinned by hash (not just version)?
  - Are there any `curl | sh` patterns?
  - Is the CI pipeline protected from PR-based injection?

---

## Task 11: 51% Attack Resistance Assessment (HIGH — Strategic)

**Why this is #11:** Not a code audit task but an existential threat assessment. Every Bitcoin fork (BTG, ETC, BSV) has been 51% attacked. BTQ shares SHA-256d with Bitcoin, meaning Bitcoin miners can trivially attack BTQ.

- [ ] **Step 1: Calculate attack cost**
  Estimate the cost to rent sufficient SHA-256 hashpower (e.g., from NiceHash) to 51% attack BTQ at launch. Compare BTQ's expected hashrate to Bitcoin's ~600 EH/s. If the attack costs less than the value at risk (exchange deposits), the chain will be attacked.

- [ ] **Step 2: Assess PoW algorithm**
  BTQ uses SHA-256d (same as Bitcoin). This means:
  - All Bitcoin ASICs work on BTQ
  - Hashpower is trivially rentable
  - A fraction of a percent of Bitcoin's hashrate overwhelms BTQ
  Consider whether a different PoW algorithm (e.g., RandomX, Ethash, Verthash) would provide better security.

- [ ] **Step 3: Evaluate merge mining feasibility**
  Merge mining with Bitcoin (like Namecoin) would inherit Bitcoin's security. Assess whether this is architecturally feasible for BTQ.

- [ ] **Step 4: Review checkpoint implementation**
  Does BTQ implement checkpoints (hardcoded block hashes that prevent deep reorgs)? If so, check their placement and update frequency. If not, consider adding them for the bootstrapping period.

- [ ] **Step 5: Document exchange guidance**
  Recommend minimum confirmation requirements for exchanges listing BTQ, based on the attack cost analysis. Consider Ethereum Classic's approach of requiring 40,000+ confirmations after their 51% attacks.

---

## Audit Output Format

Each task should produce a findings document with this structure:

```
### Finding: [Title]
**Severity:** CRITICAL / HIGH / MEDIUM / LOW / INFO
**Location:** file:line
**Description:** What the vulnerability is
**Impact:** What an attacker could do
**Evidence:** Code snippets, test results
**Recommendation:** Specific fix
**References:** CVEs, papers, prior incidents
```

After all tasks are complete, compile findings into a prioritized remediation plan sorted by severity, with estimated effort for each fix.

---

## Summary of Pre-Identified Concerns

These issues were identified during research and preliminary code inspection. They should be confirmed or refuted during the audit:

| # | Concern | Severity | Task |
|---|---------|----------|------|
| 1 | No SIGHASH_FORKID — legacy ECDSA transactions may be replayable on Bitcoin | CRITICAL | 1 |
| 2 | `btq_dilithium_sk_to_pk` may incorrectly extract public key via memcpy | CRITICAL | 2 |
| 3 | Reference Dilithium implementation is not side-channel resistant | HIGH | 2 |
| 4 | Unknown whether signing uses hedged or deterministic mode | HIGH | 2 |
| 5 | `CDilithiumKey::operator==` uses non-constant-time memcmp | MEDIUM | 2 |
| 6 | `GetPrivKey()` copies key material to non-secure std::vector | MEDIUM | 2 |
| 7 | Mainnet DNS seeds and hardcoded seeds may be placeholder | CRITICAL | 6 |
| 8 | CVE-2024-52911 patch status unknown | HIGH | 7 |
| 9 | SHA-256d PoW shared with Bitcoin — trivial 51% attack | HIGH | 11 |
| 10 | `PermittedDifficultyTransition` allows any change post-LWMA | MEDIUM | 4 |
