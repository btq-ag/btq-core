# External audit briefing

Orientation for an external review of BTQ Core. This document exists because the
tree is a Bitcoin Core fork, and almost everything surprising in it traces back
to a small set of deliberate divergences from upstream. Reading those first will
save more time than reading code first.

Nothing here is a substitute for the source. Every value in the divergence table
is followed by the file it lives in; treat this document as an index, and verify
against the tree.

## The audit commit

`5ce8e10bdc9d0b1e8782ec35d9d5b239df7f8ac3` on `master`, the merge of PR #96.

It was chosen because it is the first commit at which the test suites can be
used as a signal at all. Everything measured below was measured there.

## Where to start

```sh
./autogen.sh
./configure --without-gui --disable-bench
make -j"$(nproc)"
make check
```

The unit binary is `src/test/test_btq`, not `test_bitcoin`; the fork renamed it.
It can be run directly, and `make check` additionally drives the `btq-tx` and
`btq-util` cases in `test/util/data`. Both should report no errors.

That has only recently been true, and the reasons it was not are worth knowing
before reading git history, because all of them were mechanical rather than
substantive:

- A single aborting test case left `gArgs` registered, which tripped a
  duplicate-registration assert in every fixture that followed and turned one
  failure into roughly 590.
- `make check` passed Boost a newline-joined list of suite names where it wanted
  a comma-separated one. The one file declaring two suites therefore matched
  nothing and failed, and because the target stopped there, the rest of
  `wallet/test` and the whole `btq-tx` case table had never run under CI.
- That case table named `./bitcoin-tx` and `./bitcoin-util` as the programs to
  run. This fork builds neither, so all 95 cases failed to start.

Underneath those sat a long tail of test data inherited from Bitcoin that no
longer described this chain: addresses under the wrong base58 prefixes,
transactions paying out a 50 BTC subsidy on a chain that pays 5, packet vectors
computed under a different network magic, and weight arithmetic computed with a
different scale factor.

The practical consequence for a reviewer is that a green suite here is recent.
Treat any test that looks like it has always been passing with suspicion, and
prefer breaking a rule deliberately to confirm the test would catch it.

## Divergences from Bitcoin Core

Thirty consensus, policy and encoding values differ from upstream. Several
entries below are marked unchanged on purpose: some of the sharper effects come
from a constant that moved sitting next to one that did not.

### Weight and size

| Constant | Bitcoin | BTQ | Defined in |
| --- | --- | --- | --- |
| `WITNESS_SCALE_FACTOR` | 4 | 16 | `consensus/consensus.h` |
| `MAX_BLOCK_WEIGHT` | 4,000,000 | 8,000,000 | `consensus/consensus.h` |
| `MAX_BLOCK_SERIALIZED_SIZE` | 4,000,000 | 8,000,000 | `consensus/consensus.h` |
| `MAX_STANDARD_TX_WEIGHT` | 400,000 | 400,000 (unchanged) | `policy/policy.h` |
| `DEFAULT_BLOCK_MAX_WEIGHT` | 3,996,000 | 7,600,000 | `policy/policy.h` |

### Script

| Constant | Bitcoin | BTQ | Defined in |
| --- | --- | --- | --- |
| `MAX_SCRIPT_ELEMENT_SIZE` | 520 | 15,000 | `script/script.h` |
| `MAX_SCRIPT_SIZE` | 10,000 | 100,000 | `script/script.h` |
| `MAX_OPCODE` | `OP_NOP10` (0xb9) | `OP_DILITHIUM_PUBKEY` (0xbf) | `script/script.h` |
| `MAX_OPS_PER_SCRIPT` | 201 | 201 (unchanged) | `script/script.h` |
| `MAX_STACK_SIZE` | 1,000 | 1,000 (unchanged) | `script/script.h` |

Dilithium reclaims BIP342 OP_SUCCESS bytes `0xbb`–`0xbf` (`IsOpSuccess` in `src/script/script.cpp`). Upstream `script_assets_test` cases are skipped when a GetOp walk of the success or failure executed scripts finds those opcodes, or when the comment starts with `tapscript/inputmaxlimit` / `tapscript/pushmaxlimit` (Bitcoin’s 520-byte element limit; BTQ allows 15000). This is intentional and there are no unexplained corpus divergences. The JSON is fetched from `bitcoin-core/qa-assets` at the commit in `contrib/qa-assets.pin`, not `main`. `btq-ag/qa-assets` is TBD. Weekly pin bumps are `.github/workflows/qa-assets-bump.yml`.

### Mempool policy

| Constant | Bitcoin | BTQ | Defined in |
| --- | --- | --- | --- |
| `MAX_STANDARD_P2WSH_SCRIPT_SIZE` | 3,600 | 65,536 | `policy/policy.h` |
| `MAX_STANDARD_SCRIPTSIG_SIZE` | 1,650 | 15,000 | `policy/policy.h` |
| `MAX_STANDARD_P2WSH_STACK_ITEM_SIZE` | 80 | 15,000 | `policy/policy.h` |
| `MAX_PACKAGE_WEIGHT` | 404,000 | 40,000,000 | `policy/packages.h` |
| `DEFAULT_ANCESTOR_SIZE_LIMIT_KVB` | 101 | 2,000 | `policy/policy.h` |
| `DEFAULT_DESCENDANT_SIZE_LIMIT_KVB` | 101 | 2,000 | `policy/policy.h` |

### Chain

| Constant | Bitcoin | BTQ | Defined in |
| --- | --- | --- | --- |
| Block subsidy | 50 BTC | 5 BTQ | `validation.cpp` |
| `nPowTargetSpacing` | 600s | 60s | `kernel/chainparams.cpp` |
| `nSubsidyHalvingInterval` | 210,000 | 2,100,000 | `kernel/chainparams.cpp` |
| Difficulty algorithm | 2016-block retarget | LWMA, window 144, every block | `pow.cpp`, `consensus/params.h` |
| `CSVHeight` / `SegwitHeight` | 419,328 / 481,824 | 1 / 1 | `kernel/chainparams.cpp` |
| `nDilithiumP2MRHeight` | n/a | 1 on mainnet, signet, regtest; unscheduled on testnet | `kernel/chainparams.cpp` |
| `MAX_MONEY` | 21,000,000 | 21,000,000 (unchanged) | `consensus/amount.h` |

### Encoding and cryptography

| Constant | Bitcoin | BTQ | Defined in |
| --- | --- | --- | --- |
| `MESSAGE_MAGIC` | `Bitcoin Signed Message:\n` | `BTQ Signed Message:\n` | `util/message.cpp` |
| Network magic (mainnet) | `f9beb4d9` | `f1b2a3d4` | `kernel/chainparams.cpp` |
| base58 `PUBKEY_ADDRESS` | 0 | 75 | `kernel/chainparams.cpp` |
| base58 `SCRIPT_ADDRESS` | 5 | 135 | `kernel/chainparams.cpp` |
| base58 `SECRET_KEY` | 128 | 235 | `kernel/chainparams.cpp` |
| bech32 HRP (mainnet) | `bc` | `qbtc`, plus `dbtc` for Dilithium | `kernel/chainparams.cpp` |
| BIP324 HKDF salt | `bitcoin_v2_shared_secret` | `btq_v2_shared_secret` | `bip324.cpp` |

## Interactions worth checking first

The constants above were changed independently, and nothing in the tree records
how they combine. Each of the following falls out of the table rather than from
any single line of code.

### Non-witness capacity moved the opposite way from witness capacity

The scale factor is the divisor for non-witness data, so raising it to 16 while
doubling `MAX_BLOCK_WEIGHT` doubles witness capacity to 8,000,000 bytes per
block and halves non-witness capacity to 500,000. `MAX_STANDARD_TX_WEIGHT` was
left at 400,000, so a standard transaction gets 25,000 bytes of base data
against upstream's 100,000.

For a chain carrying roughly 3.3 kB signatures in the witness that is a coherent
trade. The consequence to confirm is deliberate is the other side of it: a
legacy non-witness Dilithium spend puts its signature in the scriptSig at 16
weight units per byte, four times the upstream penalty, which caps such a
transaction at about seven inputs.

### The mempool's size limits are now larger than a block

A block holds 8,000,000 weight units, which at a scale factor of 16 is 500,000
vbytes. Against that, `DEFAULT_ANCESTOR_SIZE_LIMIT_KVB` and
`DEFAULT_DESCENDANT_SIZE_LIMIT_KVB` are 2,000 kvB, four times a block, and
`MAX_PACKAGE_WEIGHT` is 40,000,000, five times `MAX_BLOCK_WEIGHT`. Upstream
holds all three near a tenth of block capacity, which is what makes an accepted
package always minable in one block. That ordering is inverted here.

The 40,000,000 looks derived rather than chosen: `policy/packages.h` asserts
`MAX_PACKAGE_WEIGHT >= DEFAULT_ANCESTOR_SIZE_LIMIT_KVB * WITNESS_SCALE_FACTOR *
1000`, so the 2,000 kvB ancestor limit forced it. The ancestor limit is
therefore the value to ask about.

Separately, `MAX_PACKAGE_COUNT` is still 25, and twenty-five transactions at the
standard ceiling of 400,000 weight units total 10,000,000 — a quarter of the
package weight limit. In ordinary operation the count limit always binds first
and the weight limit is close to dead code.

### Per-input weight changes what a wallet can spend

A P2WPKH input costs 768 weight units here against upstream's 272, because an
input's base bytes are multiplied by the scale factor. A standard transaction
holds roughly 519 inputs rather than about 1,470. This constrains consolidation
and changes which coin selection solutions exist at all; it is measured in
`wallet/test/coinselector_tests.cpp` rather than assumed.

### Every soft fork is active from height 1

`CSVHeight` and `SegwitHeight` are 1, as are `nDilithiumHeight` and, on every
chain but the live testnet, `nDilithiumP2MRHeight`. Upstream code and tests
frequently assume these rules are dormant at low heights. Any inherited logic
resting on that assumption is exercised here from the first block, which is a
useful place to look for behaviour that was never really tested upstream.

## Where the review is most valuable

Ranked by how much of the code is new rather than inherited, on the reasoning
that inherited Bitcoin Core code has had far more scrutiny than anything written
for this fork.

1. **Dilithium signing and verification** — entirely new. Domain separation
   between message signing and transaction signing has already required one fix.
   See `crypto/dilithium_key.cpp`, `script/sign.cpp`, `wallet/rpc/dilithium.cpp`.
2. **P2MR output type** — a new consensus-visible output type, active from
   height 1 on all chains but the live testnet. Wallet support sits outside the
   descriptor system, which is the source of several rough edges.
   See `wallet/p2mr.cpp`, `script/interpreter.cpp`, `addresstype.cpp`.
3. **LWMA difficulty** — replaces the 2016-block retarget entirely and runs
   every block against 60-second spacing. See `pow.cpp`.
4. **Weight and fee accounting** — the scale factor change touches fee
   estimation, coin selection, mempool limits and block assembly at once.
   See `wallet/spend.cpp`, `wallet/coinselection.cpp`, `policy/policy.cpp`.
5. **BIP324 transport** — forked key derivation on inherited cryptography, so
   the derivation and rekeying paths matter more than the ciphers.
   See `bip324.cpp`, `crypto/chacha20poly1305.cpp`.

## Known open items

- **#97** — the wallet issues `dilithium-legacy` destinations on chains where
  they are neither valid nor spendable. `IsValidDestination` rejects the
  destination while `GetNewDestination` still returns one, and
  `CalculateMaximumSignedInputSize` cannot size an input for it, so coin
  selection marks any such output unsolvable.
- **#94** — the resource limits above, and the question of which were chosen
  deliberately rather than scaled to make something else fit.
- **#95** — transport policy, covering the deliberate BIP324 incompatibility.

## What is green, and what is not

| Suite | State at the audit commit | How it was measured |
| --- | --- | --- |
| `src/test/test_btq` | 733 cases, no errors | Four consecutive full runs |
| `test/util/data` | 95 of 95 cases | `test/util/test_runner.py`, via `make check` |
| `src/qt/test/test_btq-qt` | 21 cases, no failures | Built with `--with-gui=qt5`, run headless |
| `test/functional` | 177 of 304 pass, 127 fail | One full sequential run, 25 minutes |

The functional suite is the known gap, and the 127 failures have **not** been
triaged. Do not read them as 127 defects, and do not read them as 127 stale
expectations either; nobody has looked.

What is known is that the suite fails for at least some of the same reasons the
unit suite did. `rpc_blockchain.py` asserts a UTXO total of 8725 BTC, which is
Bitcoin's 50-per-block subsidy over the test chain, against the 1000 that BTQ's
subsidy produces. That particular one is a stale expectation rather than a
defect. Until the suite is migrated it cannot distinguish between the two
categories, and neither can a reviewer reading its output.

Run it with a single job. Under parallel jobs almost everything fails within a
second on node startup contention, producing a failure list that has nothing to
do with the code — the run above took 25 minutes at `--jobs=1`.

Two further notes on the tests:

- A small number of unit cases assert present behaviour rather than intended
  behaviour, so that a known defect stays visible in the suite instead of only
  on the issue tracker. The `dilithium-legacy` case in
  `wallet/test/spend_tests.cpp` is the notable one; it is labelled as such in
  the source and will fail, by design, once the defect is fixed.
- Of the CI workflows, only `build-and-address-tests` guards `master`. The
  `test each commit` job runs solely on pull requests carrying more than one
  commit, and the macOS and Win64 jobs are disabled outright. A green tick on
  `master` is a narrower claim than it looks.
