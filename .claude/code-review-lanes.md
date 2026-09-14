# BTQ-Core project review lanes

BTQ-Core is a **Bitcoin Core 26.0 fork** with post-quantum signatures (Dilithium),
BIP360 P2MR outputs, and LWMA-1 difficulty. Most incoming changes are either upstream
Core backports or audit fixes. The lanes below target the failure modes that a
reviewer who assumes "this is Bitcoin Core" will miss.

## Constants that differ from upstream Bitcoin Core

Check any diff that touches, derives from, or reasons about these. Upstream code and
upstream review intuition both assume the Core column.

| Constant | Bitcoin Core 26 | BTQ-Core | Where |
|---|---|---|---|
| `nPowTargetSpacing` | 600 s | **60 s** | `src/kernel/chainparams.cpp` |
| `nSubsidyHalvingInterval` | 210,000 | **2,100,000** | `src/kernel/chainparams.cpp` |
| `nMinerConfirmationWindow` | 2016 | **20,160** | `src/kernel/chainparams.cpp` |
| `WITNESS_SCALE_FACTOR` | 4 | **16** | `src/consensus/consensus.h` |
| `MAX_BLOCK_WEIGHT` | 4,000,000 | **8,000,000** | `src/consensus/consensus.h` |
| `MAX_BLOCK_SIGOPS_COST` | 80,000 | 80,000 (unchanged) | `src/consensus/consensus.h` |
| `MAX_STANDARD_TX_WEIGHT` | 400,000 | 400,000 (unchanged) | `src/policy/policy.h` |
| `MAX_PACKAGE_WEIGHT` | 404,000 | **40,000,000** | `src/policy/packages.h` |
| `DEFAULT_MAX_MEMPOOL_SIZE_MB` | 300 | **2000** | `src/kernel/mempool_options.h` |
| `DILITHIUM_SIGOP_COST` | — | **50** (new) | `src/script/script.h` |

Two consequences that are easy to get backwards:

- Because `WITNESS_SCALE_FACTOR` is 16 but `MAX_STANDARD_TX_WEIGHT` stayed at 400,000,
  a standard transaction is capped at **25,000 vbytes**, not Core's 100,000. Likewise a
  block holds **500,000 vbytes**, not 1,000,000 — BTQ blocks carry more *weight* but
  fewer *vbytes* than Core's. Any backported code that converts between weight and
  vsize, or that hardcodes `4`, is suspect.
- A Dilithium signature is large. Sizing heuristics tuned on ECDSA input sizes
  (~72-byte sigs) will be wrong by more than an order of magnitude.

---

## Lane: Consensus divergence

The single question: **after this change, can a patched node and an unpatched node
disagree about whether a block or transaction is valid?**

- Does the diff change what is *accepted* (consensus) or merely what is *relayed and
  mined* (policy)? Say which, explicitly, for every changed default. A change filed as
  policy that alters block validity is the highest-severity finding available here.
- Anything under `src/consensus/`, `src/validation.cpp`, `src/script/interpreter.cpp`,
  or `src/kernel/chainparams.cpp` is consensus until proven otherwise.
- Script verification flags: a flag enforced on blocks is consensus even if it is
  registered as "standard, not mandatory". Check both the block path and the mempool
  path, and report any flag whose classification contradicts where it is enforced.
- Buried deployments and activation heights (`nLWMAHeight`, P2MR gates): does the change
  alter behaviour *before* the activation height on any chain — mainnet, testnet,
  signet, regtest? Each has different heights; check all four.
- The network is live on testnet. A consensus change without a coordinated activation
  height splits it. If the diff needs a gate and does not have one, say so.

## Lane: Backport fidelity

For any change described as a backport, port, or "match Core N":

- Fetch or reason about what upstream actually does, then state whether this diff
  **matches upstream semantics or deliberately diverges**. Silent divergence is the
  finding — an intentional divergence that is documented in the diff is fine.
- Did the port carry over upstream constants that assume Core's parameters (600-second
  blocks, `WITNESS_SCALE_FACTOR` of 4, 4 MW blocks, 210,000-block halving)? Re-derive
  every numeric literal the port introduces against the BTQ table above and show the
  arithmetic.
- Upstream code often reasons in "number of blocks" as a proxy for elapsed time. At 60
  seconds per block that proxy is off by 10x. Any horizon, timeout, expiry, window,
  confirmation target, or rolling average expressed in blocks needs its unit checked.
- Did the port drop an upstream guard, assert, or `static_assert` that BTQ still needs?
- Partial backports: if upstream's change depends on an earlier upstream commit that
  BTQ never took, does the ported code still hold together?

## Lane: Dilithium and P2MR sizing

- Weight, vsize, sigop cost, and fee estimates computed for Dilithium inputs and P2MR
  outputs: is the arithmetic right under `WITNESS_SCALE_FACTOR = 16`?
- Does the change let a single transaction, package, or block exceed an existing
  resource limit once inputs are Dilithium-sized rather than ECDSA-sized? Work a
  concrete example with real Dilithium sizes rather than asserting it is fine.
- Coin selection, change calculation, and fee bumping: an algorithm that iterates over
  candidate input sets can behave very differently when each input is large. Check for
  quadratic blowup and for size estimates that assume a small signature.
- P2MR (BIP360) wallet metadata — Merkle leaves, scripts, and key material — must
  survive dump/import, backup/restore, and rescan without loss. A round-trip that
  silently drops metadata makes funds unspendable; treat it as critical, not medium.
- Dust thresholds and minimum relay fees price outputs by spend cost. Does the change
  price a P2MR spend as if it were P2WPKH?

## Lane: Resource limits and DoS

Bitcoin Core's bounds were chosen for its own parameter set. BTQ raised several.

- Any new or changed cap: is the unit right (count vs bytes vs weight vs vbytes), and
  is it still bounded when every object is Dilithium-sized? A count cap over large
  objects bounds nothing.
- Memory: can a peer make the node hold unbounded data by staying under a count limit
  while exceeding a sane byte limit — orphanage, extra-txn pools, per-peer buffers,
  announcement sets?
- Eviction: does the policy prefer evicting cheap or attacker-supplied entries, and can
  a peer force eviction of honest entries?
- Time: does the change use peer-adjusted time anywhere a consensus decision, mining
  template, or header check is made? Peer-adjusted time is attacker-influenced.
- Per-peer accounting that is actually per-connection can be multiplied by opening more
  connections. Check which one this is.

---

## Repo facts worth knowing while reviewing

- **CI proves very little.** In `.github/workflows/ci.yml` the macOS and Win64 jobs are
  disabled with `if: false`. The only job that runs on a PR is
  `Build btqd and run address tests` — a Linux build plus unit tests. **No functional
  tests run in CI.** Do not treat a green check as evidence that behaviour is correct,
  and weight missing-test findings accordingly.
- The `test each commit` job currently fails on every multi-commit PR for reasons
  unrelated to the PR (a rebase conflict against a historical merge commit). Its red
  status is not a finding.
- `test/functional/` tests exist but are run by hand. A change to behaviour that has no
  functional test is a real gap even though nothing will go red.
