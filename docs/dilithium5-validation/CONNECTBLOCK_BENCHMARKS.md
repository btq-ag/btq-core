# ConnectBlock benchmarks

Both builds were made on the same machine, the same day, with the same flags.

- CPU: AMD Ryzen 9 5950X, 32 threads visible. `MemTotal` 12,247,008 kB. Swap 4,194,304 kB.
- Compiler: g++ `-g -O2` (`CXXFLAGS` in both `config.log` files). gcc 11.4.0.
- Configure: `--without-gui --with-incompatible-bdb --disable-bench --disable-fuzz --disable-fuzz-binary`.
- Runtime: `-par=4 -debug=bench -fallbackfee=0.0001`.
- Dilithium5 `btqd` sha256 `052c52aad52dcccf062d46ca1a248cf5cdbad6201db4121d77543f1a54bb48a4`.
- Dilithium2 `btqd` sha256 `b0c8d05ec6e95e7de7e81fda2153ab4f96162431fce32accb2124e50e38b2672`, built from `origin/master` at `41513c04d` in `/home/barney/btq/BTQ-Core-dilithium2`.

The harness is `harness/measure.py`. It funds independent P2MR outputs, signs one spend per output with `signrawtransactionwithwallet`, mines, and reads the bench log.

## The warm number is not verification

The first `generatetoaddress` of a 25-input Dilithium5 block logged `Verify ... txins: 0.05ms`. Those transactions had just been accepted to the mempool, and `ConnectBlock` consults the script cache. A restart clears it. `invalidateblock` of an early block, then `reconsiderblock`, connects the chain again with an empty cache. The times below are that second pass. The log line is the one after `CCheckQueueControl::Wait`, so it is wall time, including the four script threads.

## Cold verify wall time

| Inputs | Dilithium2 | Dilithium5 | D5 / D2 |
| ---: | ---: | ---: | ---: |
| 25, run 1 | 0.66 ms | 1.63 ms | 2.47 |
| 25, run 2 | 0.63 ms | 1.55 ms | 2.46 |
| 25, run 3 | 0.65 ms | 1.57 ms | 2.42 |
| 50 | 1.27 ms | 2.86 ms | 2.25 |
| 100 | 2.25 ms | 5.74 ms | 2.55 |
| 200 | 4.55 ms | 13.77 ms | 3.03 |

The three 25-input runs sit in a tight band. The 50 and 100 point the same way, a bit over 2.2x. The single 200-input Dilithium5 sample is the outlier at 3.0x. Treat 2.4x per input as the repeated result, and 3.0x as one larger point that was slower.

Per input, using the mean of the 25-input runs: Dilithium2 0.0259 ms, Dilithium5 0.0633 ms. A single-thread `-O3` crypto loop on this same box earlier in the day was about 0.07 ms for mode 2 and 0.17 ms for mode 5. The ConnectBlock wall time is lower because four script threads share the block. It is not four times lower, so the queue and the sighash work are not free.

## Full blocks, measured

`BlockAssembler` clamps `-blockmaxweight` to `DEFAULT_BLOCK_MAX_WEIGHT` (7,600,000) in `src/node/miner.cpp`. Passing `-blockmaxweight=8000000` is logged and then ignored. These are the fullest blocks this binary will mine.

The verify time is a `submitblock` onto a node that had the parent chain and had never seen the spends. A `reconsiderblock` on the node that mined them is a cache hit (about 2 ms) and is not the number below.

| | Spends | Bytes | Weight | Cold verify |
|---|---:|---:|---:|---:|
| Dilithium5 | 869 | 6,369,152 | 7,597,667 | 48.78 ms |
| Dilithium2 | 1,446 | 5,555,784 | 7,597,884 | 31.60 ms |
| Ratio | | 1.15x | | 1.54x |

Per input that is 0.056 ms for Dilithium5 and 0.022 ms for Dilithium2, about 2.6x, in line with the 25-input runs. The block ratio is lower because fewer Dilithium5 transactions fit. JSON: `dilithium5-full-block-cold.json`, `dilithium2-full-block-cold.json`.

## Full blocks, the earlier extrapolation

The 25-input rate, before these blocks existed:

| | Transactions at the 8 MW cap | Time at the 25-input rate | Time at the 200-input rate |
|---|---:|---:|---:|
| Dilithium2, 1-in 2-out | 1,346 | 35 ms | 31 ms |
| Dilithium5, 1-in 2-out | 848 | 54 ms | 58 ms |
| Ratio |  | 1.54x | 1.91x |

The older 1.5x claim was a count of raw verifies: 1,600 mode-2 signatures versus 1,014 mode-5 signatures, times the single-thread crypto cost. The measured per-input ratio is higher than that crypto ratio's 2.4x only in the 200-input sample. Folded into a full block, the 25-input rate lands near the old 1.5x. That agreement is accidental. The crypto loop did not run inside `ConnectBlock`, and the 1,014 figure counted inputs by weight rather than whole transactions (the whole one-input transaction fits 974 times).

At one block per minute, 54 ms is about 0.1% of the interval. On this CPU, validation of a full wallet block is not the limit. A slower host, or `-par=1`, moves the absolute number. The ratio is the part to carry to another machine.

## Disconnect

Invalidating the 200-input Dilithium5 block on the warm node logged `Disconnect block: 0.32ms`. The cold reconsider of the whole tail (299 blocks, mixed sizes) took 0.174 s wall and was not split per block in the CPU counters.

## What was not timed

A block of 20-of-20 leaves. The multisig functional test mines individual 2-of-3 and 3-of-3 spends. It does not fill a block with them. `bench_btq` was not built. Configure had `--disable-bench`.
