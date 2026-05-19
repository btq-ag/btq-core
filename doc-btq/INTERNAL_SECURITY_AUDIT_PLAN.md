# BTQ Core Internal Security and Correctness Audit Plan

**Classification:** Internal (Bitcoin Quantum engineering)  
**Document type:** Pre-audit scope and methodology (Plan of Work)  
**Version:** 1.0  
**Status:** Living document  
**Primary repository:** BTQ-Core (`btq-core`)

---

## 0. Document control

| Field | Value |
|-------|-------|
| **Owner** | Bitcoin Quantum security and protocol engineering |
| **Review cadence** | At each release candidate; after major PQ milestone; after upstream Bitcoin security backports |
| **Scope binding** | Exact Git tag or commit SHA, `depends` lock state, subtree SHAs (`secp256k1`, `leveldb`, `crypto/dilithium`) |
| **Related docs** | [TESTING_GUIDE.md](testing/TESTING_GUIDE.md), [SECURITY.md](SECURITY.md), [README.md](README.md), root `SECURITY.md` |
| **Out of scope for this document** | Actual audit findings, formal certification, legal or securities opinions |

### 0.1 Disclaimer

This document defines objectives, methodologies, checkpoints, risk areas, testing expectations, and severity taxonomy. It does **not** assert that BTQ Core is secure or correct. It does **not** replace adversarial execution, independent cryptographic review, economic modeling, or operational security assessments unless those workstreams are explicitly scheduled and completed.

### 0.2 Revision history

| Version | Date | Author | Summary |
|---------|------|--------|---------|
| 1.0 | 2026-05-18 | Internal audit planning | Initial comprehensive audit plan |

---

## 1. Executive summary

BTQ Core is a Bitcoin-derived full node, wallet, and network stack modified for post-quantum (PQ) cryptography, enlarged consensus and policy limits, and distinct chain identity. Relative to upstream Bitcoin Core, the fork compounds risk in four dimensions that must be audited together:

1. **Cryptographic perimeter:** Dilithium integration (with Falcon and SPHINCS+ represented in consensus-facing structures), new script opcodes (`OP_CHECKSIGDILITHIUM`, multisig variants, `OP_DILITHIUM_PUBKEY`), and phased activation via `Consensus::SignatureAlgorithm`.
2. **Resource scaling:** Current consensus headers define **8 MB** serialized block size and **8 MW** block weight (`src/consensus/consensus.h`), with policy soft caps near **7.6 MW** (`src/policy/policy.h`). PQ signatures and pubkeys are orders of magnitude larger than ECDSA, amplifying DoS, memory, disk, and propagation surfaces.
3. **Phased rollout:** Roadmap phases (parameter changes, PPK infrastructure, future consensus activation) create **version skew** risk between miners, mempools, wallets, indexers, and alternate implementations.
4. **Inherited Bitcoin attack surface:** Decades of subtle consensus, mempool, and wallet behavior remain; **upstream merge drift** can reintroduce fixed CVE classes or silently diverge fixes.

This plan is structured like external blockchain audit engagements (scope, methodology, threat model, work packages, deliverables) but is executed **internally** by the Bitcoin Quantum team with full repository access and long-term maintenance responsibility.

---

## 2. Audit objectives

### 2.1 Primary objectives

| ID | Objective | Measurable indicator |
|----|-------------|----------------------|
| **O1** | Consensus fidelity | No divergent valid-chain acceptance between correctly configured full nodes under defined network conditions |
| **O2** | Authorization soundness | No spend of encumbered outputs without satisfying enforced script and PQ verification rules |
| **O3** | Liveness under abuse | Bounded CPU, RAM, disk I/O, and bandwidth under adversarial blocks, txs, and P2P traffic at declared limits |
| **O4** | Key lifecycle security | Wallet and signing paths resist defined local and network-adjacent threats for both legacy and Dilithium keys |
| **O5** | Supply-chain integrity | Reproducible or attestable release artifacts; pinned dependencies; no unexplained binary drift |

### 2.2 Secondary objectives

| ID | Objective |
|----|-------------|
| **O6** | Operational clarity | Runbooks for activation days, incident response, and forensic data collection |
| **O7** | Ecosystem alignment | Mining pools, explorers, and SDKs agree on serialization, address formats, and activation height semantics |
| **O8** | Claims accuracy | Public documentation matches implemented parameters (sizes, algorithms, activation) |

### 2.3 Explicit non-objectives

Unless a separate engagement is chartered:

- Regulatory compliance or token securities analysis  
- Third-party exchange custody, bridge protocols, or hardware wallet firmware  
- Quantum hardware lab validation or formal proof of NIST security reductions  
- Marketing review (handled separately from technical claims validation)

---

## 3. System under review

### 3.1 In-scope components

| Layer | Paths and artifacts (representative) |
|-------|----------------------------------------|
| Consensus | `src/consensus/`, `src/validation.cpp`, `src/kernel/chainparams.cpp` |
| Script and PQ bridge | `src/script/`, `src/crypto/dilithium/`, interpreter and `btqconsensus` |
| Policy and mempool | `src/policy/`, `src/txmempool.*`, `src/node/transaction.cpp` |
| P2P and net processing | `src/net*`, `src/validation.cpp` block/tx ingress |
| Wallet | `src/wallet/`, Dilithium keystore and encryption paths |
| RPC and REST | `src/rpc/`, `src/rest.cpp` |
| Build and release | `contrib/guix/`, `depends/`, CI configs, `contrib/verify-*` |
| Tests | `src/test/`, `src/test/fuzz/`, `test/functional/`, `test/lint/` |

### 3.2 Out of scope (default)

- Website, mobile apps, and closed-source pool software (coordinate via ecosystem register)  
- Historical Bitcoin mainnet state (except where replay or cross-chain confusion applies)  
- Social engineering of contributors (covered only at high level in supply-chain section)

### 3.3 Phased roadmap context (audit must track)

Per `doc-btq/README.md`:

| Phase | Target | Audit focus |
|-------|--------|-------------|
| Phase 1 (v0.1.0) | Consensus parameter and capacity changes | Limits, weight, sigops, propagation |
| Phase 2 (v1.1.0) | Dilithium in PPK infrastructure without consensus activation | Wallet, RPC, serialization, no premature mainnet enforcement |
| Phase 3 (future) | Consensus activation of PQ signatures | Activation height, `signature_algorithm`, fork taxonomy, migration |

The auditor must record **which phase the scoped commit implements** and downgrade or upgrade risk ratings if code on `master` advances phase boundaries without documentation updates.

---

## 4. Threat model

### 4.1 Adversary classes

| Class | Capabilities | Typical goals |
|-------|--------------|---------------|
| **A1 Remote network** | P2P and optionally RPC; asymmetric bandwidth; packet fragmentation | Eclipse, mempool DoS, slow block propagation, censorship |
| **A2 Mempool advertiser** | Publishes txs; no mining power | CPU/RAM exhaustion, pinning, eviction games |
| **A3 Rogue miner** | Block production (mainnet hash or signet key) | Invalid block acceptance on minority software, selfish mining amplified by large blocks |
| **A4 Malicious sync peer** | Serves headers/blocks during IBD | Disk fill, validation bypass if ordering wrong |
| **A5 Local attacker** | Same-host user, malware | Wallet key extraction, RPC cookie theft |
| **A6 Supply-chain** | Compromise CI, release host, or maintainer machine | Backdoored binaries, weakened `#ifdef` test flags |
| **A7 Quantum-capable (future)** | Breaks discrete log on exposed classical keys | Harvest-now-decrypt-later against pre-migration UTXOs |

### 4.2 Trust boundaries

Produce and maintain diagrams for:

1. **Untrusted bytes** (P2P, RPC `sendrawtransaction`, block files) entering deserialization.  
2. **Consensus verifier** (script + PQ + amount + locktime context).  
3. **Policy layer** (standardness, feerate, package limits) vs **consensus** (must never be confused in security arguments).  
4. **Wallet keystore** (encrypted at rest, decrypted in memory for signing).  
5. **Operator config** (trusted if host is trusted; dangerous if remote-controlled).

For each boundary crossing, document **preconditions**, **postconditions**, and **failure blast radius**.

### 4.3 Cryptographic time horizon

Explicitly separate:

- **Classical threats** against secp256k1 ECDSA/Schnorr paths still in use.  
- **PQ threats** against Dilithium (and future Falcon/SPHINCS+) paths.  
- **Migration threats** where users believe funds are PQ-protected but spends still use classical templates, or vice versa.

---

## 5. Methodology and phases

| Phase | Name | Activities | Exit artifact |
|-------|------|------------|---------------|
| **P0** | Kickoff and inventory | Architecture diagrams, diff vs Bitcoin, parameter sheet, file manifest with owners | Scope sign-off |
| **P1** | Static and differential review | Trace critical paths; invariant tables; ambiguity list | Annotated hotspot register |
| **P2** | Dynamic testing and fuzzing | Unit, functional, fuzz, sanitizer, benchmark per Section 10 | Test and corpus report |
| **P3** | Cryptographic review | KAT alignment, API misuse, RNG, side channels on signing | Crypto memorandum |
| **P4** | Economic and mempool | Pinning, eviction, package relay under PQ sizes | Scenario notebook |
| **P5** | Build and reproducibility | Guix/verify scripts, dependency pins | Attestation record |
| **P6** | Red-team rehearsal | Chained exploits, rollback table-top | IR playbook updates |
| **P7** | Remediation verification | Re-run targeted suites; closure matrix | Signed audit closure |

**Gate rule:** No release candidate promotion while any **open Critical** finding lacks a verified fix or formally accepted risk record signed by security lead and protocol lead.

---

## 6. Risk areas (exhaustive register)

Each area below requires a **workpaper**: threat hypotheses, code references, test cases executed, residual risk, and owner.

### 6.1 Consensus parameters and activation

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| C-01 | `SignatureAlgorithm::NONE` vs enforced PQ mode mismatch | `src/consensus/params.h`, chainparams per network |
| C-02 | Premature PQ enforcement splits network | Height/time activation, version bits, buried deployments |
| C-03 | Cross-network replay (main/test/signet/regtest) | Magic bytes, HRP, genesis, address versions |
| C-04 | `script_flag_exceptions` map errors | Incorrect exceptions → consensus split or frozen upgrades |
| C-05 | LWMA or retarget changes (`DEPLOYMENT_LWMA`) | Time-warp, oscillation, difficulty griefing |

### 6.2 Block and transaction validation

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| V-01 | Merkle tree malleability (CVE-2012-2459 class) | `BlockMerkleRoot`, duplicate tx rejection |
| V-02 | Witness commitment and weight accounting | PQ witness size vs discount rules |
| V-03 | Sigops cost under Dilithium opcodes | `feature_dilithium_sigops.py`, `MAX_BLOCK_SIGOPS_COST` |
| V-04 | Sighash and message binding for PQ | Preimage serialization order across implementations |
| V-05 | Locktime/sequence/CSV/CLTV interaction with new templates | `tx_verify.cpp`, functional matrices |

### 6.3 Post-quantum cryptography

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| PQ-01 | Parameter set mismatch (e.g. Dilithium2 vs ML-DSA naming) | Literals in `src/crypto/dilithium/` |
| PQ-02 | Vendor subtree drift from reference implementation | Diff vs PQClean/NIST KATs |
| PQ-03 | Verification shortcut or batch verify without equivalence proof | Any optimized paths |
| PQ-04 | Signing side-channel leakage | Wallet signing, online RPC sign paths |
| PQ-05 | RNG failure in keygen or deterministic signing | `GetStrongRandBytes`, retry loops |
| PQ-06 | Hybrid `CPubKey` / `CKeyID` namespace collision | `src/wallet/scriptpubkeyman.cpp` Dilithium bridging |
| PQ-07 | Downgrade to legacy-only verification | Flags, `-acceptdilithium`, peer misconfiguration |

### 6.4 Script system

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| S-01 | `OP_CHECKSIGDILITHIUM` / VERIFY / multisig counting DoS | Repeated VERIFY loops (see functional tests) |
| S-02 | `OP_DILITHIUM_PUBKEY` push size vs `MAX_SCRIPT_ELEMENT_SIZE` | Consensus vs policy limits |
| S-03 | Taproot/Segwit interplay with PQ witness versions | `script/interpreter.cpp`, flags in `policy.h` |
| S-04 | Standardness vs consensus gap | Non-standard mempool txs mined in blocks |
| S-05 | `btqconsensus` API surface for external bindings | `src/script/btqconsensus.h` |

### 6.5 Serialization and limits

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| D-01 | `MAX_BLOCK_SERIALIZED_SIZE` / `MAX_BLOCK_WEIGHT` (8M) exceeded via edge encoding | Deserialization fuzz |
| D-02 | `MAX_STANDARD_TX_WEIGHT` (400k weight units) bypass | Policy checks in `policy.cpp` |
| D-03 | Package weight `MAX_PACKAGE_WEIGHT` (40M) vs ancestor limits | `policy/packages.*` |
| D-04 | P2P `MAX_MSG_*` vs block size | `net.h`, compact block paths |
| D-05 | Integer overflow in fee or weight arithmetic | Sanitizer builds |

### 6.6 Mempool, mining, and economics

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| M-01 | Pinning via large PQ packages | Package relay, RBF rules |
| M-02 | `prioritisetransaction` policy divergence | Miner vs public mempool |
| M-03 | Block template starvation (few giant txs fill block) | `BlockAssembler`, `DEFAULT_BLOCK_MAX_WEIGHT` |
| M-04 | Feerate estimation under PQ traffic mix | Wallet and RPC estimates |
| M-05 | Orphan handling memory with large txs | Orphan pool limits |

### 6.7 P2P network

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| N-01 | Compact block failure mode on PQ-heavy blocks | `cmpctblock`, fallback bandwidth |
| N-02 | Eclipse via anchor/feeler logic | Addrman, asmap if enabled |
| N-03 | Headers-first IBD accepting invalid PQ blocks | Validation ordering |
| N-04 | BIP324 encrypted transport (if enabled) interaction | No regression in MAC handling |

### 6.8 Wallet and key management

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| W-01 | `EncryptDilithiumSecret` salt and KDF strength | `scriptpubkeyman.cpp` |
| W-02 | Plaintext `mapDilithiumKeys` on unencrypted wallet | File permissions, crash dumps |
| W-03 | Address type confusion (`dilithium-legacy`, `dilithium-bech32`) | RPC and GUI labels |
| W-04 | Backup/restore of Dilithium metadata | `walletdb.cpp` serialization |
| W-05 | PSBT partial fields for PQ (when implemented) | Fail-closed parsing |

### 6.9 RPC, REST, and operator interfaces

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| R-01 | Unauthorized spend via exposed RPC | Cookie, whitelist, ZMQ |
| R-02 | Large `hex` payloads causing OOM | Argument size limits |
| R-03 | Algorithm stub RPCs enabling unsafe test modes on mainnet | PR #2 class endpoints |

### 6.10 Build, CI, and supply chain

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| B-01 | Non-reproducible release binaries | Guix, `verify-binaries` |
| B-02 | Sanitizer-disabled CI merge | Required jobs for crypto PRs |
| B-03 | Subtree updates without KAT re-run | `src/crypto/dilithium/` |
| B-04 | Pre-commit or hook bypass culture | CONTRIBUTING enforcement |

### 6.11 Ecosystem and operational

| Risk ID | Description | Review focus |
|---------|-------------|--------------|
| E-01 | Pool template software ≠ released `btqd` | Version matrix per block |
| E-02 | Explorer decodes PQ scripts incorrectly | User sends to unspendable scripts |
| E-03 | Checkpoint/assumevalid misuse | Fast sync trust assumptions |
| E-04 | Incident communication gaps on consensus split | `doc-btq/communication/` |

---

## 7. Severity taxonomy and finding definitions

Findings from execution of this plan use the severities below. They are **aligned with** `doc-btq/SECURITY.md` CVSS bands but add **blockchain-specific impact criteria** required for consensus software.

### 7.1 Critical

**Definition:** A vulnerability or defect that, with practical exploitability under the stated threat model, enables at least one of:

1. **Consensus divergence:** Two honest nodes running the same released version and configuration accept different valid chains for the same network, or one accepts a block/tx the protocol specification forbids.  
2. **Unauthorized spend:** Movement of funds without satisfying the intended script/PQ conditions (including "anyone-can-spend", signature forgery, or sighash confusion).  
3. **Remote code execution** on default or commonly recommended configurations without user interaction beyond normal P2P/RPC exposure.  
4. **Unbounded private key extraction** from wallet material at rest or in memory under network-only attacker (not merely local malware; local-only key theft is often High unless mass-deployed RPC misconfig makes it remote).

**CVSS mapping (guidance):** 9.0 to 10.0 when scored generically; consensus splits may warrant Critical even when CVSS is debated.

**Response SLA (internal):** Immediate embargo; emergency patch branch; halt RC promotion; notify ecosystem partners under NDA.

**Examples (illustrative, not asserted present):**

- Dilithium verify accepts malformed signatures on mainnet spends  
- Off-by-one in sighash omits output index for PQ path  
- `signature_algorithm` ignored so legacy nodes accept invalid PQ blocks  

**Closure requires:** Fix merged; **new** regression tests; independent re-review; signed verification checklist (Section 9.3).

### 7.2 High

**Definition:** Serious security or liveness impact that does not automatically imply chain split or universal unauthorized spend, but can cause:

1. **Network-wide DoS** on default full nodes (crash, hang, OOM) from a single remote message or block under policy limits.  
2. **Wallet fund loss** for users following documented workflows (e.g. corrupted backup, destructive migration).  
3. **Transaction malleability** affecting protocols that rely on txid stability post-confirmation.  
4. **Local privilege escalation** to wallet keys from service account running `btqd`.  
5. **Practical eclipse** of targeted nodes enabling double-spend against SPV or weakly confirmed services.

**CVSS mapping (guidance):** 7.0 to 8.9.

**Response SLA:** Target fix within 30 days; coordinated release; may require mandatory upgrade advisory.

**Examples:**

- Single malformed P2P message triggers OOM at default `dbcache`  
- Dilithium multisig counts sigops incorrectly allowing block rejection storm  
- Encrypted wallet migration loses Dilithium keys silently  

**Closure requires:** Fix and tests; performance or DoS regression test where applicable.

### 7.3 Medium

**Definition:** Limited-impact issues including:

1. **Information disclosure** (non-key material) aiding further attack (peer IPs, wallet labels, RPC metadata).  
2. **Policy violations** not consensus-breaking (non-standard txs unexpectedly relayed).  
3. **Performance degradation** beyond SLO thresholds (Section 9.2) but not node death.  
4. **Incorrect RPC help or misleading logs** causing operator misconfiguration.  
5. **Denial of service requiring local access** or unlikely resource levels.

**CVSS mapping (guidance):** 4.0 to 6.9.

**Response SLA:** Next scheduled release unless chained to High.

### 7.4 Low

**Definition:** Minor security-adjacent defects with **negligible direct fund or consensus impact**, including:

1. Defensive hardening opportunities with no known exploit path.  
2. Bugs in non-default, documented-experimental features.  
3. Low-severity leaks (verbose debug in non-production builds).  
4. Inconsistencies between docs and code on non-security parameters (ports, cosmetic flags).

**CVSS mapping (guidance):** 0.1 to 3.9.

**Response SLA:** Backlog; public issue after fix optional.

### 7.5 Informational

**Definition:** Observations that **do not constitute vulnerabilities** but improve security posture, maintainability, or auditability:

1. Code quality or clarity in critical paths without exploit narrative.  
2. Missing defense-in-depth (extra bounds check) where primary check exists.  
3. Recommendations for monitoring, logging, or test coverage gaps without proven flaw.  
4. Best-practice deviations from Bitcoin Core or NIST guidance without demonstrated impact.  
5. Documentation drift (whitepaper vs `consensus.h` limits) tracked for O8.

**Handling:** Track in audit report appendix; no embargo; prioritize by effort/risk reduction ratio.

### 7.6 Severity decision matrix

| Impact / Exploitability | High exploitability | Medium | Low |
|-------------------------|---------------------|--------|-----|
| Consensus break or theft | **Critical** | **Critical** | **High** |
| Network DoS default node | **High** | **Medium** | **Low** |
| Wallet loss (documented use) | **High** | **Medium** | **Low** |
| Info leak only | **Medium** | **Low** | **Info** |
| Docs/test gap only | **Info** | **Info** | **Info** |

When in doubt, **rate up** until disproven by test evidence.

### 7.7 Finding record template

Each finding must include:

```text
ID:          BTQ-AUDIT-YYYY-NNN
Title:       Short imperative description
Severity:    Critical | High | Medium | Low | Informational
Component:   e.g. validation / dilithium / wallet
CWE:         if applicable
Preconditions:
Impact:
Proof:       steps, commit, test name, or PoC branch
Recommendation:
Verification: exact command(s) proving fix
Status:      Open | Fixed | Accepted Risk | Won't Fix
```

---

## 8. Success criteria

Audit cycles complete successfully only when **all** mandatory criteria below are met. Partial completion is recorded as **"Audit suspended"** with explicit blockers.

### 8.1 Mandatory completion criteria

| ID | Criterion | Evidence |
|----|-----------|----------|
| SC-1 | Scope commit frozen and recorded | Tag in audit cover sheet |
| SC-2 | Zero open **Critical** findings | Closure matrix |
| SC-3 | All **High** findings fixed or Accepted Risk with signed AR form | AR register |
| SC-4 | Consensus-critical paths have listed test coverage ≥ targets in Section 10.6 | Coverage report + manual map |
| SC-5 | PQ KAT vectors pass on all release platforms in CI matrix | CI logs archived |
| SC-6 | Fuzz smoke ≥ 24h per listed target on release branch (or corpus replay equivalent) | Fuzz run log |
| SC-7 | No sanitizer failures on ASan/UBSan job for release commit | CI artifact |
| SC-8 | Performance SLOs met (Section 8.2) | Benchmark output |
| SC-9 | Ecosystem version matrix signed (pools, explorers) or documented gaps | E-01 workpaper |
| SC-10 | Public docs updated for any parameter changed during audit fixes | Doc PR links |

### 8.2 Performance and resource SLOs (regtest/signet synthetic)

Establish baseline on reference hardware (document machine spec in workpaper). **Regression > 20%** blocks release unless Accepted Risk.

| Metric | Target (initial; tune per release) |
|--------|-------------------------------------|
| `ConnectBlock` 1 MB-equivalent PQ-heavy block | < 2× baseline ECDSA-only block time |
| Mempool accept 10k standard PQ txs | Stable RSS within 110% configured `maxmempool` |
| IBD from checkpoint to tip (testnet) | No worse than prior release + 10% |
| Dilithium verify bench (single core) | Documented ops/sec floor; no >15% drop vs prior tag |

### 8.3 Accepted Risk criteria

Accepted Risk (AR) is permitted only when:

1. Severity is **High** or below (never Critical).  
2. Exploit path is documented and deemed impractical with stated mitigations.  
3. Protocol lead and security lead sign AR with expiry date and re-review trigger.  
4. Monitoring or operational workaround is deployed where applicable.

### 8.4 Sign-off roles

| Role | Responsibility |
|------|----------------|
| Audit lead | Methodology, finding quality, closure matrix |
| Protocol lead | Consensus classification and fork communications |
| Crypto lead | PQ memorandum approval |
| QA lead | Test evidence completeness (Section 10) |
| Release manager | Build reproducibility attestation |

---

## 9. Verification and closure procedures

### 9.1 Fix verification levels

| Level | Description | Required for |
|-------|-------------|--------------|
| **V1** | Unit test demonstrates fix | All Medium+ |
| **V2** | Functional test on regtest/signet | High, Critical |
| **V3** | Differential test vs second implementation or KAT | Critical crypto |
| **V4** | Fuzz corpus entry added | Parser/decoder fixes |
| **V5** | Independent reviewer not original author | Critical, High |

### 9.2 Regression policy

Any change touching `validation.cpp`, `interpreter.cpp`, `dilithium`, `chainparams`, or `policy.h` triggers **full** `make check` and **BTQ functional subset** (Section 10.3) minimum.

### 9.3 Critical closure checklist

- [ ] Root cause documented  
- [ ] Fix minimal and reviewed by two protocol engineers  
- [ ] New tests fail on pre-fix commit  
- [ ] Fuzz target updated if input-dependent  
- [ ] Release notes security section drafted  
- [ ] Ecosystem notification if externally exploitable  

---

## 10. Testing practices (audit execution standard)

This section defines **how auditors prove** hypotheses. It extends `doc-btq/testing/TESTING_GUIDE.md` with audit-specific rigor.

### 10.1 Testing pyramid for BTQ audits

```
                    ┌─────────────────────┐
                    │  Red-team / chaos   │  (P6, optional mainnet shadow)
                    ├─────────────────────┤
                    │  Functional E2E     │  test/functional/, multi-node
                    ├─────────────────────┤
                    │  Integration / bench│  bench_btq, sync tests
                    ├─────────────────────┤
                    │  Unit + property    │  src/test/
                    ├─────────────────────┤
                    │  Fuzz + sanitizer   │  src/test/fuzz/, CI ASan
                    └─────────────────────┘
```

Higher layers are fewer but mandatory for consensus and wallet findings.

### 10.2 Unit testing (C++ / Boost.Test)

**Location:** `src/test/`  
**Command:** `make check` or targeted `src/test/test_btq --run_test=...`

| Audit work package | Required suites (minimum) |
|--------------------|---------------------------|
| PQ crypto | `crypto_tests`, any `dilithium_*` tests |
| Script/PQ ops | `script_tests`, `script_p2sh_tests` |
| Consensus | `transaction_tests`, `validation_*` if present |
| Policy | `policyestimator_tests`, package-related tests |
| Wallet | `wallet_tests`, `wallet_crypto_tests` |

**Practices auditors enforce:**

- Every new consensus branch needs **both** accept and reject vectors.  
- Boundary tests at `MAX_BLOCK_WEIGHT`, `MAX_STANDARD_TX_WEIGHT`, `MAX_SCRIPT_ELEMENT_SIZE`.  
- No tests depending on wall-clock timing or external network.  
- Tests must pass on `-DCMAKE_BUILD_TYPE=Debug` with sanitizers when investigating memory bugs.

### 10.3 Functional testing (Python framework)

**Location:** `test/functional/`  
**Command:** `test/functional/test_runner.py` (optionally `-j N`, `--extended`)

**BTQ-specific mandatory subset for PQ-related releases:**

| Test | Purpose |
|------|---------|
| `feature_dilithium_sigops.py` | Sigop counting, DoS loops for repeated `OP_CHECKSIGDILITHIUMVERIFY` |
| `btq_chain_identity.py` | Chain params, magic, ports |
| `btq_regtest_mining.py` | Regtest mining and rewards |
| Wallet and RPC tests touching new address types | When wallet PRs in scope |

**Auditor-authored scenarios (implement if missing):**

1. **Activation boundary:** block N-1 legacy-only, block N PQ-enforced (when Phase 3 active).  
2. **Oversize transaction:** just below and above `MAX_STANDARD_TX_WEIGHT`.  
3. **Package relay:** ancestor package at `MAX_PACKAGE_COUNT` with PQ-sized children.  
4. **Reorg depth 10:** PQ txs in orphaned branch return to mempool correctly.  
5. **Multi-node partition:** 2+2 split with conflicting PQ policy flags (must not silently converge wrong).

**Functional test quality bar:**

- Use `assert_equal` with descriptive messages.  
- `set_test_params` documents `chain = 'regtest'`.  
- `--nocleanup` only for local debug, not CI.  
- Skip tests via framework `skip_test` with explicit reason, never silent pass.

### 10.4 Fuzz testing (libFuzzer)

**Location:** `src/test/fuzz/`  
**Build:** `./configure --enable-fuzz --with-sanitizers=fuzzer` (see TESTING_GUIDE)

**Mandatory fuzz targets for PQ audit cycles:**

| Target area | Goal |
|-------------|------|
| Transaction deserialization | No OOM/UB on hostile witness |
| Script interpreter | Crash-free on random scripts incl. Dilithium opcodes |
| Address/key parsing | Reject invalid Dilithium key bytes |
| P2P message parsers | Bounded allocation |
| RPC argument parsing | If fuzz harness exists |

**Operational requirements:**

- Seed corpus checked into repo or audit artifact store.  
- Min 24h continuous fuzz per target on release branch **or** replay of last 30-day corpus with zero new crashes.  
- Every crash: minimized repro, severity rated, regression test or fuzz entry added.

### 10.5 Sanitizer and dynamic analysis

| Tool | When required |
|------|----------------|
| ASan + UBSan | All crypto and deserialization PRs; release candidate |
| TSan | Threading changes in validation or net threads |
| Valgrind (memcheck) | Wallet encryption changes (spot check) |
| GDB scripted backtrace | Any crash reproduced once |

CI matrix per TESTING_GUIDE must be green on scoped commit.

### 10.6 Coverage requirements

| Scope | Line coverage target | Branch / error paths |
|-------|---------------------|----------------------|
| New code in audit window | ≥ 95% | All documented error returns tested |
| `validation.cpp` / interpreter PQ paths | 100% of touched lines | Required for Phase 3 |
| Wallet Dilithium storage | ≥ 90% | Encrypt/decrypt/upgrade paths |
| Overall project | ≥ 80% maintained | No drop > 2% without AR |

Generate: `./configure --enable-lcov && make check && make cov`

### 10.7 Benchmark and load testing

**Micro:** `src/bench/bench_btq -filter=...`  
Include Dilithium sign/verify benchmarks in every crypto release comparison.

**Macro scenarios:**

| Scenario | Method |
|----------|--------|
| Large block propagation | Signet or regtest generate max-weight blocks; measure `getblock` latency |
| Mempool flood | Submit 10k txs via RPC; monitor RSS and `getmempoolinfo` |
| Long run | 72h testnet node under `-test=1` load generator |

Record CPU model, RAM, disk type with results.

### 10.8 Cross-implementation and KAT testing

| Source | Use |
|--------|-----|
| NIST PQC KATs | Dilithium (ML-DSA) verify vectors |
| Reference implementation | Byte compare on fixed message/sig pairs |
| `test/functional/data/` | Shared hex fixtures between CI and partners |

**Cross-implementation test:** Export signed PQ tx hex from BTQ; verify with partner stub or `btqconsensus` binding; document any mismatch as **Critical** until resolved.

### 10.9 Lint, static analysis, and CI gates

| Check | Command / location |
|-------|-------------------|
| Lint all | `test/lint/lint-all.sh` |
| Clang-tidy (if enabled) | `contrib/devtools/btq-tidy` |
| GitHub Actions / CI | All jobs green on release SHA |

Security-sensitive PRs require **additional** sanitizer job and security officer review per TESTING_GUIDE.

### 10.10 Test evidence archival

For each audit cycle, archive in internal storage (not necessarily git):

- Exact `test_runner.py` command line and seed  
- `make check` log  
- Fuzz crash corpora and fixes  
- `lcov` HTML or summary  
- Benchmark tables  
- Platform matrix (OS, compiler)  

Retention: **7 years** or per org policy.

### 10.11 PR and release test gates (enforcement)

Before merge to release branch:

| Change type | Minimum tests |
|-------------|---------------|
| Any code | Existing suites pass; lint clean |
| RPC | New functional test + help text test |
| Consensus | Unit + functional activation/reorg tests |
| Crypto | KAT + fuzz + constant-time review notes |
| Policy only | Mempool functional scenarios |

Release candidate additionally requires Section 8 success criteria.

---

## 11. Differential review vs Bitcoin Core

Maintain a living **diff register**:

1. Files changed vs upstream tag (document baseline Bitcoin version).  
2. Classification: consensus / policy / network / wallet / build / rename-only.  
3. For each consensus change: intentional fork vs accidental drift.  

**High-risk diff categories:**

- `validation.cpp`, `tx_check.cpp`, `consensus.h`, `policy.h`  
- `script/interpreter.cpp`, `script_error.cpp`  
- `chainparams.cpp`, `versionbits`  
- New opcodes and `SCRIPT_VERIFY_DILITHIUM`  

On each upstream security release, run **cherry-pick triage** within 72 hours and re-run Section 10.3 subset.

---

## 12. Work packages and RACI

| WP ID | Title | Responsible | Accountable | Consulted | Informed |
|-------|-------|-------------|-------------|-----------|----------|
| WP-01 | Consensus and activation | Protocol eng | Protocol lead | Audit lead | Ecosystem |
| WP-02 | PQ cryptography | Crypto eng | Crypto lead | External PQ advisor | Protocol |
| WP-03 | Script and btqconsensus | Script maintainer | Protocol lead | Wallet | Integrators |
| WP-04 | Mempool/mining economics | Node team | Protocol lead | Mining partners | Audit |
| WP-05 | Wallet and PSBT | Wallet team | Wallet lead | Security | Support |
| WP-06 | P2P and DoS | Net team | Protocol lead | Infra | Audit |
| WP-07 | Build and supply chain | Release eng | Release mgr | Security | All |
| WP-08 | Test and QA evidence | QA lead | Audit lead | All WP owners | Leadership |
| WP-09 | Ecosystem matrix | DevRel | Protocol lead | Pools, explorers | Community |

---

## 13. Deliverables (post-execution report structure)

When this plan is executed, produce:

1. **Executive summary** (1 to 2 pages): systemic risks, severity counts, go/no-go recommendation.  
2. **Scope and methodology**: commit SHA, exclusions, threat model reference (Section 4).  
3. **Architecture and trust boundaries**: diagrams (Section 4.2).  
4. **Findings register**: all items per Section 7.7.  
5. **Test evidence appendix**: Section 10 artifacts.  
6. **Accepted Risk register**: Section 8.3.  
7. **Remediation roadmap**: prioritized backlog with owners.  
8. **Public disclosure recommendations**: aligned with `doc-btq/SECURITY.md` timelines.

---

## 14. Continuous assurance (post-audit)

| Trigger | Action |
|---------|--------|
| Upstream Bitcoin security advisory | Triage within 72h; patch or document N/A |
| NIST PQ standard update | Parameter review; KAT re-run |
| New mining pool software version | E-01 matrix update |
| Phase 3 activation scheduled | Full re-audit per this plan |
| Major dependency bump (`depends`) | Reproducible build re-verify |

**Canary:** Maintain signet or internal testnet with PQ features **always active** to detect drift before mainnet activation.

---

## 15. Appendices

### Appendix A: Key source anchors (audit starting points)

| Topic | Location |
|-------|----------|
| Signature algorithm enum | `src/consensus/params.h` |
| Block limits | `src/consensus/consensus.h` |
| Policy soft cap / standard tx weight | `src/policy/policy.h` |
| Package limits | `src/policy/packages.h` |
| Dilithium subtree | `src/crypto/dilithium/` |
| Wallet Dilithium | `src/wallet/scriptpubkeyman.cpp` |
| Dilithium sigop functional test | `test/functional/feature_dilithium_sigops.py` |
| Testing guide | `doc-btq/testing/TESTING_GUIDE.md` |

### Appendix B: Glossary

| Term | Definition |
|------|------------|
| **Consensus** | Rules all full nodes must apply; violation causes fork or rejection |
| **Policy** | Local relay/mining preferences; stricter than consensus is allowed |
| **PQ** | Post-quantum cryptography (Dilithium, Falcon, SPHINCS+ in BTQ context) |
| **PPK** | Post-quantum key infrastructure (Phase 2) |
| **KAT** | Known Answer Test vectors from standards bodies |
| **AR** | Accepted Risk, formally waived finding |
| **SLO** | Service level objective for performance tests |
| **RC** | Release candidate |

### Appendix C: Reference commands (quick audit kit)

```bash
# Full unit suite
make check

# Functional (BTQ subset example)
test/functional/test_runner.py feature_dilithium_sigops.py btq_chain_identity.py btq_regtest_mining.py

# Lint
test/lint/lint-all.sh

# Fuzz example (after configure --enable-fuzz)
timeout 86400 src/test/fuzz/fuzz transaction < corpus_dir

# Coverage
./configure --enable-lcov && make check && make cov

# Bench
src/bench/bench_btq -filter=dilithium
```

---

## 16. Document approval

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Audit lead | | | |
| Protocol lead | | | |
| Security lead | | | |
| QA lead | | | |

---

*End of Internal Security Audit Plan*
