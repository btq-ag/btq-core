# Changed files: where the review surface actually is

A map of BTQ Core's divergence from its upstream baseline, written so that a
reviewer can find the code worth reading without walking two thousand files to
get there.

**Baseline:** Bitcoin Core 26.0, commit `44d8b13c8` (2023-12-04)
**Measured at:** `5ce8e10bd` (2026-07-30)
**Raw diff:** 2,085 files changed, +61,242 / −18,486

That headline number is close to useless as a scoping figure, and taking it at
face value is the main way to waste a review. Most of it is a rename.

## The short version

| | files | + | − |
| --- | ---: | ---: | ---: |
| BTQ-authored, security-relevant | **178** | **9,526** | **1,041** |
| Vendored Dilithium reference implementation | 82 | 11,435 | 0 |
| Everything else | 1,825 | 40,281 | 17,445 |

The first row is the review target. It is **8.5% of the changed files and 16% of
the added lines.** The third row is GUI, documentation, tests, build plumbing,
vendored libraries, and the `Bitcoin`→`BTQ` rename that touches most files in
`src/` without changing behaviour.

## How this was derived

Files were classified by path against the diff, then the classification was spot
checked by reading diffs. This is a heuristic, not a proof, and it is offered as
a starting point rather than a boundary — **it is not a claim that nothing
outside the first row matters.** Anything surprising found elsewhere is a finding
about this document as much as about the code.

The one systematic weakness to know about: the rename touches nearly every file
in `src/`, so a real change sitting in an otherwise-renamed file can hide in the
noise. `src/init.cpp` is the archetype — 40-odd lines of
`BITCOIN_PID_FILENAME`→`BTQ_PID_FILENAME` and `bitcoin-config.h`→`btq-config.h`,
plus a single `#include <rpc/dilithium.h>` that is the only line that changes what
the binary does. Diffs in the "everything else" bucket were sampled with that in
mind, but sampling is what it is.

## By category

| Category | files | + | − |
| --- | ---: | ---: | ---: |
| **A.** Dilithium primitive (vendored reference) | 82 | 11,435 | 0 |
| **B.** Dilithium integration | 10 | 1,892 | 0 |
| **C.** Consensus core (`chainparams`, `validation`, `pow`, `consensus/`) | 18 | 524 | 300 |
| **D.** Script / interpreter | 22 | 1,418 | 220 |
| **E.** Policy / standardness | 14 | 108 | 60 |
| **F.** Wallet | 60 | 3,974 | 269 |
| **G.** RPC | 28 | 270 | 115 |
| **H.** P2P / transport | 19 | 89 | 66 |
| Tests | 702 | 13,316 | 2,918 |
| Documentation | 198 | 15,022 | 3,650 |
| GUI (Qt) | 287 | 8,014 | 6,643 |
| Build / CI | 129 | 1,711 | 1,506 |
| Vendored (secp256k1, univalue, leveldb, minisketch) | 24 | 50 | 50 |
| Remaining `src/` — predominantly the rename | 445 | 1,954 | 2,409 |
| Other | 47 | 1,465 | 280 |

Category **A** is the upstream CRYSTALS-Dilithium reference implementation,
vendored rather than written here. It is worth reviewing on different terms from
the rest: the question is whether it faithfully matches the published reference
and whether the build selects the parameters it claims, not whether the algorithm
is sound.

Note that **A contains no deletions at all** — nothing upstream was modified, it
was added wholesale. That is the cheapest thing in this document to verify and
worth verifying first.

## Ranked reading list

BTQ-authored, security-relevant files by total churn. Roughly the order a review
should approach them.

| File | + | − |
| --- | ---: | ---: |
| `src/wallet/p2mr.cpp` | 916 | 0 |
| `src/wallet/scriptpubkeyman.cpp` | 806 | 17 |
| `src/crypto/dilithium_key.h` | 463 | 0 |
| `src/crypto/dilithium_key.cpp` | 424 | 0 |
| `src/script/interpreter.cpp` | 392 | 24 |
| `src/wallet/rpc/p2mr.cpp` | 378 | 0 |
| `src/wallet/rpc/dilithium.cpp` | 371 | 0 |
| `src/wallet/key_types.h` | 340 | 0 |
| `src/wallet/walletdb.cpp` | 283 | 2 |
| `src/kernel/chainparams.cpp` | 262 | 208 |
| `src/script/sign.cpp` | 241 | 14 |
| `src/script/signingprovider.cpp` | 227 | 1 |
| `src/wallet/dilithium_wallet_manager.cpp` | 214 | 0 |
| `src/wallet/p2mr.h` | 201 | 0 |
| `src/key_io.cpp` | 167 | 7 |
| `src/wallet/rpc/backup.cpp` | 149 | 21 |
| `src/wallet/wallet.cpp` | 142 | 19 |
| `src/wallet/rpc/addresses.cpp` | 142 | 16 |
| `src/wallet/interfaces.cpp` | 139 | 1 |
| `src/script/dilithium_signing_provider.cpp` | 111 | 0 |
| `src/rpc/util.cpp` | 108 | 7 |
| `src/addresstype.cpp` | 106 | 1 |

Churn is a proxy for where new code lives, not for where risk lives. Two files
low on this list carry more consensus weight per line than most of the top:

- **`src/pow.cpp`** — the LWMA implementation. Small, and it decides difficulty
  on every block.
- **`src/policy/policy.cpp`** — 108 added lines across the whole policy
  directory, several of which change what the mempool will accept.

`src/kernel/chainparams.cpp` is the one file where the deletions matter as much
as the additions: it is where activation heights diverge, and where BTQ turns on
at height 1 what upstream turns on at a fork height.

Two shell scripts at the repository root (`test_dilithium_wallet.sh`,
`test_dilithium_sendmany.sh`, +721 between them) match the Dilithium filter but
are not production code. They are known-defective and tracked in
[#107](https://github.com/btq-ag/btq-core/issues/107) — they delete `~/.btq/regtest`
without opt-in. Do not run them against a datadir you care about.

## What is deliberately not on the list

- **GUI (Qt)** — 287 files, the second-largest block after tests.
- **Documentation** — 198 files.
- **Tests** — 702 files. Relevant to *whether* a rule is covered — and coverage
  is genuinely uneven, so treat a passing suite as weak evidence — but not a
  source of consensus behaviour.
- **The rename** — `Bitcoin Core`→`BTQ Core`, `bitcoind`→`btqd`,
  `bitcoin-config.h`→`btq-config.h` and similar, across most of `src/`.

## Companion documents

- `doc/audit-briefing.md` — the divergence table with the consensus interactions
  that fall out of it. Read that first; this document only says *where* the code
  is, not *what is surprising about it*. **Not yet on `master`** — it is in
  [#101](https://github.com/btq-ag/btq-core/pull/101), pending corrections.
- `doc-btq/INTERNAL_SECURITY_AUDIT_PLAN.md` — internal audit planning.

## Reproducing this

```sh
git diff --numstat 44d8b13c8 5ce8e10bd
```

Every figure here comes from that command. If the review pin moves, the numbers
move with it, and this document should be regenerated rather than adjusted.
