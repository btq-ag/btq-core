# Independent Verification: BTQ-Core Audit Remediation (2026-05-19)

This document is a **copy-paste agent prompt** for an unbiased reviewer to determine whether audit findings `BTQ-AUDIT-2026-001` through `-027` (and related issue #53 HD wallet work) are actually fixed.

**Primary audit source:** [`AUDIT_REPORT_2026-05-19.md`](AUDIT_REPORT_2026-05-19.md)  
**Short predecessor:** [`INTERNAL_AUDIT_FINDINGS_2026-05-19.md`](INTERNAL_AUDIT_FINDINGS_2026-05-19.md)

---

## Agent role

You are an **unbiased security/correctness reviewer**. You did not write the fixes. Your job is to determine, with **evidence**, whether each audit finding is actually fixed — not whether a PR description claims it is.

**Be skeptical by default.** Passing a few unit tests is not enough for High/Critical findings. The audit plan requires:

| Severity | Minimum verification |
|----------|----------------------|
| Critical | V3 (differential/KAT) + independent review |
| High | V2 (functional) + independent review |
| Medium | V1 (unit) minimum |
| Low | V1 |

Report **FIXED**, **PARTIAL**, **NOT FIXED**, or **REGRESSION** for each finding, with `file:line` evidence and test output.

---

## Repository and scope

| Item | Value |
|------|-------|
| Repository | `btq-ag/btq-core` |
| Original scoped commit | `9db36ba958cabee6487d8c008470a9c7385c917c` |
| Audit date | 2026-05-19 |

### Remediation PRs (review together)

| PR | Branch | Scope |
|----|--------|-------|
| [#54](https://github.com/btq-ag/btq-core/pull/54) | `fix/dilithium-hd-wallet-issue-53` | Crypto/HD primitives + wallet wiring (issue #53) |
| [#56](https://github.com/btq-ag/btq-core/pull/56) | `fix/audit-remediation-2026-05` | Claims to address findings 001–027 |

**Start from:** `fix/audit-remediation-2026-05` (includes #54 commits). Compare against `master`.

**Do not trust PR descriptions.** Verify in code and by running tests.

---

## Prior review context (hypotheses — verify independently)

A prior reviewer concluded:

- **PR #54 crypto layer:** largely correct after wallet wiring commit `f13a6fa`
- **PR #56:** fixes many structural bugs but is **under-tested**
- **Overall confidence ~40–65%** that all claimed fixes survive rigorous verification
- **Highest gap:** encrypted wallet restart/sign (001/002/009) has no functional test

Your job is to confirm or refute this independently.

---

## Findings checklist

For each `BTQ-AUDIT-2026-NNN`, produce a row:

```
| ID | Severity | Verdict | Evidence (file:line or test output) | Gaps |
```

---

### Critical

#### BTQ-AUDIT-2026-007 — Dilithium keygen discards entropy / HD non-deterministic

**Was broken:**
- `MakeNewKey()` / `GenerateFromEntropy()` routed through `randombytes()` that overwrote caller entropy
- Wallet used `MakeNewKey()` with fake HD metadata (timestamp-based `unique_seed` never passed to keygen)

**Claimed fix (PR #54):**
- `crypto_sign_keypair_from_seed` in Dilithium ref
- `CDilithiumExtKey` 73-byte seed model
- `DeriveNewDilithiumChildKey` wired in `scriptpubkeyman.cpp`

**Verify:**
1. Read `src/crypto/dilithium/ref/sign.c`, `src/crypto/dilithium_key.cpp`, `src/wallet/scriptpubkeyman.cpp` — no path from HD seed → `MakeNewKey()` with timestamp
2. Run unit tests (see [Build and test protocol](#build-and-test-protocol))
3. Run `python3 test/functional/wallet_dilithium_hd_restore.py --legacy-wallet` (needs BDB; note if skipped)

**Exit criteria:** Same mnemonic → same Dilithium address on fresh wallet (functional test green).

---

#### BTQ-AUDIT-2026-008 — CDilithiumExtKey encode/decode/SetSeed broken

**Verify:**
- `DILITHIUM_EXTKEY_SIZE = 73`
- `Derive()` I_L/I_R matches BIP32 (bytes 0–31 → seed, 32–63 → chaincode)
- Encode/decode round-trip tests pass

**Run:** `dilithium_wallet_tests` (especially `dilithium_extkey_bip32_il_ir_split`, `dilithium_extkey_encode_decode_roundtrip`).

---

#### BTQ-AUDIT-2026-009 — Encrypted Dilithium IV mismatch (descriptor + legacy)

**Was broken:**
- Encrypt used `uint256(keyID)`
- Decrypt used `vchPubKey.GetHash()` with dummy `CPubKey()`

**Claimed fix (PR #56):**
- `DecryptDilithiumKey(..., CKeyID& keyid)` in `crypter.cpp`
- Updated call sites in `scriptpubkeyman.cpp`

**Verify:**
1. Grep encrypt/decrypt paths — single IV scheme end-to-end
2. Unit test `dilithium_key_encryption` uses key ID IV
3. **Must add/run:** encrypt wallet → write DB → reload → unlock → `signmessagewithdilithium` (likely **missing** — flag if absent)

**Exit criteria:** V2 functional encrypted-wallet restart test passes.

---

#### BTQ-AUDIT-2026-010 — Mainnet powLimit = 2^255−1

**Claimed fix:** Mainnet/testnet `powLimit = 0x00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff`.

**Verify:** `src/kernel/chainparams.cpp` `CMainParams` / `CTestNetParams`; signet/regtest still use `7fff…`.

**Gap check:** Was value validated against `difficulty-sim` or launch hashrate? If not → PARTIAL.

---

#### BTQ-AUDIT-2026-011 — Testnet and regtest share genesis hash

**Claimed fix:** Testnet remined with timestamp `"BTQ testnet genesis block 20260526"`, hash `0xb82c2238f94b4c89c1d29af05ca1004ab8eaf35e313790b634aa663cb734b7f0`.

**Verify:** `chainparams_genesis_tests/unique_genesis_hashes` passes; testnet ≠ regtest byte-for-byte.

---

### High

#### BTQ-AUDIT-2026-001 — Dilithium key persistence/load broken

**Was broken:**
- `LoadDilithiumKey` stub returned true without storing keys
- No `DILITHIUM_CRYPTED_KEY` load in `walletdb.cpp`

**Claimed fix (PR #56):**
- Legacy stores in `mapDilithiumKeys`
- `LoadCryptedDilithiumKey` in walletdb + both spk managers

**Verify:** Trace load path for plaintext + encrypted, legacy + descriptor. Restart functional test.

**Exit criteria:** Create address → stop node → start → sign with same key.

---

#### BTQ-AUDIT-2026-002 — Encrypted IV mismatch (legacy)

Subsumed by **009**; verify together.

---

#### BTQ-AUDIT-2026-012 — P2WPKH dispatches ECDSA vs Dilithium by 100-byte heuristic

**Claimed fix:** `interpreter.cpp` witness v0 keyhash requires `CPubKey::COMPRESSED_SIZE` (33 bytes); no `is_dilithium` branch.

**Verify:**
- Grep for `is_dilithium`, `> 100` in witness path
- Confirm **intentional breakage** of spending Dilithium via P2WPKH v0/20-byte (Dilithium must use `dilithium_bech32_hrp`)

**Regression check:** Do existing Dilithium bech32/legacy address tests still pass?

---

#### BTQ-AUDIT-2026-013 — CheckSignatureEncoding bypass for sigs > 500 bytes

**Verify:**
- No early `return true` for large sigs in ECDSA path
- Dilithium path uses explicit `BTQ_DILITHIUM_SIGNATURE_SIZE` check in `EvalChecksigDilithium`

---

#### BTQ-AUDIT-2026-014 — Python WITNESS_SCALE_FACTOR = 4 vs C++ 16

**Verify:**
- `test/functional/test_framework/messages.py`, `blocktools.py`, `feature_dilithium_sigops.py` all use `16`
- Matches `src/consensus/consensus.h`

**Run:** Weight-related functional tests if time permits.

---

#### BTQ-AUDIT-2026-015 — LWMA at H=300k has no tests

**Claimed fix:** `src/test/pow_lwma_tests.cpp` (minimal).

**Verify:** Audit asked for activation boundary, ±6T clamp, overflow, functional test, fuzz, `difficulty-sim` parity. Likely **PARTIAL** — quantify what's missing.

---

#### BTQ-AUDIT-2026-016 — MAX_FUTURE_BLOCK_TIME enables difficulty manipulation

**Claimed fix:** `src/chain.h` — `MAX_FUTURE_BLOCK_TIME = 15 * 60`, `MAX_BLOCK_TIME_GAP = 15 * 60`.

**Verify:** Value vs `nPowTargetSpacing` (60s). No adversarial simulation → note as PARTIAL if unproven.

---

#### BTQ-AUDIT-2026-017 — Dilithium opcodes not gated by SCRIPT_VERIFY_DILITHIUM

**Claimed fix:**
- Opcode cases check `flags & SCRIPT_VERIFY_DILITHIUM`
- Removed from `MANDATORY_SCRIPT_VERIFY_FLAGS`
- `GetBlockScriptFlags` sets flag when `nHeight >= nDilithiumHeight`

**Verify:** `nDilithiumHeight = 1` everywhere → **behavior unchanged at runtime**. Check whether a pre-activation test exists. Soft-fork lever may exist on paper only.

---

#### BTQ-AUDIT-2026-018 — Secret files in working tree

**Verify:**
- `id_ed25519_production*`, `private.pem` absent from repo and working tree
- `.gitignore` covers `*.pem`, `id_ed25519*`

**Warn:** Credential rotation still required if keys were ever deployed.

---

### Medium

#### BTQ-AUDIT-2026-019 — Dilithium sigops weighted 1:1 with ECDSA

**Claimed fix:** `DILITHIUM_SIGOP_COST = 50` in `GetSigOpCount`.

**Verify:** Also check `WitnessSigOps()` — may still return `1` for v0 keyhash (secondary undercount).

---

#### BTQ-AUDIT-2026-020 — CDilithiumPubKey::IsValid only rejects all-zero

**Claimed fix:** `IsFullyValid()` checks non-zero ρ prefix.

**Verify:** This is **not** full lattice validation. Test malformed pubkey — does it still enter expensive verify?

---

#### BTQ-AUDIT-2026-021 — MakeNewKey silent on RNG failure

Likely **NOT FIXED** (still returns `void`). Confirm in `dilithium_key.cpp`.

---

#### BTQ-AUDIT-2026-022 — Non-deterministic Dilithium signatures

**Claimed fix:** `DILITHIUM_RANDOMIZED_SIGNING` commented out in `src/crypto/dilithium/ref/config.h`.

**Verify:** Sign same message twice → identical bytes.

---

#### BTQ-AUDIT-2026-023 — HKDF context string off-by-one

Likely obsolete post-PR #54; confirm `GenerateFromEntropy` no longer uses broken HKDF.

---

#### BTQ-AUDIT-2026-024 — Stale script_tests.json MAX_OPCODE

Likely **NOT FIXED**. Check `src/test/data/script_tests.json`.

---

#### BTQ-AUDIT-2026-025 — MAX_SCRIPT_ELEMENT_SIZE 15KB DoS surface

Likely **PARTIAL** (docs only). Check for separate `MAX_DILITHIUM_PUSH_SIZE` constant.

---

#### BTQ-AUDIT-2026-006 — Descriptor wallet cross-type send regression

**Was broken:** `sendtoaddress` p2sh-segwit → legacy: `Missing solving data (-6)`.

**Verify:** Run `wallet_all_types_simulation.py --descriptors` if present; reproduce original failure case.

---

### Low

#### BTQ-AUDIT-2026-003 — Phase docs vs consensus

Check `doc-btq/README.md` vs `GetBlockScriptFlags` / `nDilithiumHeight`.

#### BTQ-AUDIT-2026-004 — Signet placeholder challenge

Default must be valid hex; empty parse must abort. See `SigNetParams` in `chainparams.cpp`.

#### BTQ-AUDIT-2026-005 — Docs block capacity

README should say 8 MB weight, not 64 MiB.

#### BTQ-AUDIT-2026-026 — Signet WIF prefix overlap

Signet `SECRET_KEY = 196`; testnet/regtest still 239. Functional import-mismatch test likely missing.

#### BTQ-AUDIT-2026-027 — nMinimumChainWork / defaultAssumeValid zero

Expected pre-launch; mark **ACCEPTED/DEFERRED** if documented, not FIXED.

---

## HD wallet / issue #53 (cross-cutting)

Separate from numbered findings but critical:

1. `DeriveNewDilithiumChildKey` must use `CDilithiumExtKey::SetSeed` + hardened `Derive`, **not** `MakeNewKey()` or timestamp
2. `getnewdilithiumaddress` / `GetNewDestination(DILITHIUM_*)` must route through HD path
3. Hardened-only — non-hardened/xpub derivation must remain refused (lattice limitation, not a bug)

**Verdict:** Is issue #53 legitimately closable after PR #54 + #56?

---

## Build and test protocol

```bash
cd BTQ-Core   # or: git clone git@github.com:btq-ag/btq-core.git && cd btq-core
git fetch origin
git checkout fix/audit-remediation-2026-05
git log --oneline master..HEAD

# Configure (match CI where possible)
./autogen.sh
./configure --without-gui --with-incompatible-bdb

# Build
make -j$(nproc) src/test/test_btq src/btqd

# Unit tests — audit-related
src/test/test_btq --run_test=dilithium_wallet_tests
src/test/test_btq --run_test=scriptpubkeyman_tests
src/test/test_btq --run_test=chainparams_genesis_tests
src/test/test_btq --run_test=pow_lwma_tests
src/test/test_btq --run_test=dilithium_basic_tests,dilithium_network_policy_tests

# Full unit suite (if time)
make check

# Functional (BDB required for legacy wallet tests)
python3 test/functional/wallet_dilithium_hd_restore.py --legacy-wallet
# python3 test/functional/wallet_all_types_simulation.py --descriptors  # if file exists
```

Record pass/fail/skip for each. **Skips count as unverified.**

---

## Static analysis commands

```bash
# Wallet IV consistency
rg -n "DecryptDilithiumKey|EncryptDilithiumSecret|uint256\(key" src/wallet/

# No fake HD / timestamp derivation
rg -n "unique_seed|GetTime\(\)" src/wallet/scriptpubkeyman.cpp

# No P2WPKH size heuristic
rg -n "is_dilithium|> 100|> 500" src/script/interpreter.cpp

# WITNESS_SCALE_FACTOR drift
rg -n "WITNESS_SCALE_FACTOR" test/ src/consensus/consensus.h

# Genesis uniqueness
rg -n "hashGenesisBlock" src/kernel/chainparams.cpp
```

---

## Required deliverables

### 1. Executive summary (5–10 sentences)

- Overall verdict: **Ready to merge / merge with follow-ups / do not merge**
- Count: FIXED / PARTIAL / NOT FIXED / REGRESSION
- Top 3 remaining risks

### 2. Findings table (all 27 + HD/#53)

| ID | Severity | Verdict | Evidence | Required to reach FIXED |
|----|----------|---------|----------|-------------------------|

### 3. Test evidence log

Exact commands run, exit codes, skip reasons.

### 4. Regressions or new bugs

Examples:
- P2WPKH change breaking P2DWPKH spend path
- Testnet genesis remine breaking existing testnet nodes

### 5. Go/No-Go vs audit plan §8.1 SC-2

Can any Critical/High finding be marked closed under the audit's verification levels?

### 6. Recommended follow-up PRs (prioritized, max 5)

---

## Review rules

1. **Read the code** — don't infer from commit messages alone
2. **Run tests** — if you can't run something, say so and downgrade confidence
3. **Distinguish** "code looks fixed" vs "proven fixed end-to-end"
4. **Flag overclaims** — PR #56 title says 001–027; call out anything not actually fixed
5. **Note lattice limits** — hardened-only HD and no xpub watch-only are intentional, not integration gaps
6. **Compare to master**, not just read the branch in isolation
7. Be concise in tables, thorough in evidence columns

---

## Key files reference

| Area | Files |
|------|-------|
| Dilithium crypto/HD | `src/crypto/dilithium_key.cpp`, `dilithium/ref/sign.c`, `dilithium/ref/config.h` |
| Wallet derivation | `src/wallet/scriptpubkeyman.cpp`, `scriptpubkeyman.h` |
| Wallet encrypt/load | `src/wallet/crypter.cpp`, `walletdb.cpp` |
| Script/consensus | `src/script/interpreter.cpp`, `validation.cpp`, `policy/policy.h`, `script/script.cpp` |
| Chainparams | `src/kernel/chainparams.cpp`, `consensus/params.h` |
| Timestamps | `src/chain.h` |
| Test framework | `test/functional/test_framework/messages.py`, `blocktools.py` |
| Audit report | `AUDIT_REPORT_2026-05-19.md` |

---

## Copy-paste prompt (single block for agents)

```
You are an independent security reviewer for btq-ag/btq-core.

Read and follow every instruction in:
  BTQ-Core/AUDIT_REMEDIATION_VERIFICATION.md

Checkout branch: fix/audit-remediation-2026-05
Compare against: master
Also consider PR #54 (HD wallet) commits included in that branch.

Do not trust PR descriptions. For each BTQ-AUDIT-2026-001 through -027 plus issue #53:
  - Read the relevant code
  - Run the tests listed in the doc
  - Report FIXED / PARTIAL / NOT FIXED / REGRESSION with file:line evidence

Produce all six deliverables from the doc. Be skeptical. Skipped tests = unverified.
Begin now.
```

---

*Document version: 2026-05-26. Update this file when remediation PRs or verification requirements change.*
