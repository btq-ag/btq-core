# Dilithium2 vs Dilithium5 simulation findings

Branch `dilithium-5-tests` in `/home/barney/btq/BTQ-Core-dilithium5`, on top of `origin/master` at `41513c04d`. Dilithium2 is an unmodified build of that same commit in `/home/barney/btq/BTQ-Core-dilithium2`. The Dilithium5 tree links ML-DSA-87 only. A Dilithium2 key or signature fails the size check. This is not a drop-in for a live Dilithium2 chain. A fresh chain can run the build.

Host: WSL2, AMD Ryzen 9 5950X, 12 GB RAM. Both binaries built with g++ 11.4.0 `-g -O2`, `--without-gui --with-incompatible-bdb --disable-bench --disable-fuzz --disable-fuzz-binary`. Node runs used `-par=4`. Raw JSON is in `RAW_RESULTS/`.

Consensus limits were not changed. `MAX_BLOCK_WEIGHT` is 8,000,000. The miner clamps `-blockmaxweight` to `DEFAULT_BLOCK_MAX_WEIGHT`, 7,600,000, in `src/node/miner.cpp`. `MAX_BLOCK_SIGOPS_COST` is 80,000. `DILITHIUM_SIGOP_COST` is 50. Block interval is 60 seconds.

## Sizes

FIPS 204 ML-DSA-87, not the round-3 Dilithium5 sizes.

| | Dilithium2 | Dilithium5 | D5 / D2 |
|---|---:|---:|---:|
| Public key | 1,312 | 2,592 | 1.98x |
| Secret key | 2,560 | 4,896 | 1.91x |
| Signature | 2,420 | 4,627 | 1.91x |
| Signature plus sighash byte | 2,421 | 4,628 | 1.91x |
| Wallet blob (sk \|\| pk) | 3,872 | 7,488 | 1.93x |
| NIST category | 2 | 5 | |
| K x L | 16 | 56 | 3.50x |

## Crypto, not the node

Single-thread gcc `-O3` reference code on this machine, 20 warm iterations. This is not `ConnectBlock`.

| | Dilithium2 | Dilithium5 | D5 / D2 |
|---|---:|---:|---:|
| Keygen | 0.06 ms | 0.17 ms | 2.8x |
| Sign | 0.31 ms | 0.58 ms | 1.9x |
| Verify | 0.07 ms | 0.17 ms | 2.4x |

## One transaction

Unit-test 1-input 1-output spend, and the wallet's 1-input 2-output spend (pay plus change). The 2-output Dilithium2 row is derived from the Dilithium5 measurement by shrinking the witness. It was not serialized on the Dilithium2 build.

| | Dilithium2 | Dilithium5 | D5 / D2 |
|---|---:|---:|---:|
| 1-in 1-out bytes | 3,809 | 7,296 | 1.92x |
| 1-in 1-out weight | 4,724 | 8,211 | 1.74x |
| 1-in 2-out bytes | 3,885 | 7,372 | 1.90x |
| 1-in 2-out weight | 5,940 | 9,427 | 1.59x |
| 20-of-20 bytes | 74,933 derived | 144,673 | 1.93x |
| 20-of-20 weight | 75,848 derived | 145,588 | 1.92x |

How many of those fit. Weight cap is 8,000,000. Sigop cap is 80,000, at 50 per Dilithium check. The miner will stop at 7,600,000.

| Spend | What binds | Dilithium2 fit | Dilithium5 fit |
|---|---|---:|---:|
| 1-in 1-out, 8 MW | D2 sigops, D5 weight | 1,600 | 974 |
| 1-in 2-out, 8 MW | weight | 1,346 | 848 |
| 1-in 2-out, 7.6 MW | weight | 1,279 | 805 |
| 20-of-20, 8 MW | D2 sigops, D5 weight | 80 | 54 |

A normal Dilithium5 spend hits weight before sigops. A script that is only `OP_CHECKSIGDILITHIUM` hits sigops first: 1,600 opcodes are accepted as far as the sigop check, 1,601 returns `bad-blk-sigops` on both P2MR and P2WSH. Leave the cost at 50.

## Cold ConnectBlock

Wall time after the script queue, `-par=4`. The first `generatetoaddress` is a script-cache hit (about 0.05 ms for 25 inputs) and is not in this table. These rows are a restart, then `invalidateblock` / `reconsiderblock`.

| Inputs | Dilithium2 | Dilithium5 | D5 / D2 |
|---:|---:|---:|---:|
| 25, run 1 | 0.66 ms | 1.63 ms | 2.47x |
| 25, run 2 | 0.63 ms | 1.55 ms | 2.46x |
| 25, run 3 | 0.65 ms | 1.57 ms | 2.42x |
| 50 | 1.27 ms | 2.86 ms | 2.25x |
| 100 | 2.25 ms | 5.74 ms | 2.55x |
| 200 | 4.55 ms | 13.77 ms | 3.03x |

Mean of the 25-input runs: 0.0259 ms/input for Dilithium2, 0.0633 ms/input for Dilithium5, 2.45x. The 200-input sample is one point at 3.03x.

Fixed-count blocks from that same harness, 200 spends plus a coinbase:

| | Bytes | Weight | D5 / D2 |
|---|---:|---:|---:|
| Dilithium2 | 768,649 | 1,053,844 | |
| Dilithium5 | 1,466,049 | 1,751,244 | 1.91x bytes, 1.66x weight |

## Miner-full blocks

These were mined, then submitted to a second node that had the parent chain and had never seen the spends. `-blockmaxweight=8000000` did not raise the cap. The clamp at 7,600,000 is what filled them.

| | Spends | Bytes | Weight | Cold verify |
|---|---:|---:|---:|---:|
| Dilithium2 | 1,446 | 5,555,784 | 7,597,884 | 31.60 ms |
| Dilithium5 | 869 | 6,369,152 | 7,597,667 | 48.78 ms |
| D5 / D2 | 0.60x | 1.15x | 1.00x | 1.54x |

Per input on these blocks: 0.022 ms Dilithium2, 0.056 ms Dilithium5, 2.6x. The block is only 1.54x because fewer Dilithium5 transactions fit. At one block per minute, 49 ms is about 0.08% of the interval on this CPU.

Reconnecting the same block on the node that mined it was about 2 ms. That is the cache.

## Relay

`getpeerinfo` byte counters. No packet capture, so TCP/IP headers are not in these numbers.

Five nodes: origin, two more listeners, one private node, one blocksonly node. The three full-relay peers each fetched every transaction. The blocksonly peer fetched none.

| | Dilithium2 (20 txs, 5 s pause) | Dilithium5 (30 txs) | D5 / D2 |
|---|---:|---:|---:|
| Tx bytes per peer | 3,863 | 7,350 | 1.90x |
| Origin tx bytes sent | 231,780 | 661,500 | |
| Blocksonly full block | 77,110 | 220,140 | |
| Compact block, peers that had the txs | 400 each | 1,380 total across 3 | |

The first Dilithium2 capture used a 2 second pause and missed the inventory trickle. Those bytes showed up later as `blocktxn` (345,942, about 3,844 bytes each). The 5 second pause is the one to use.

One hundred protocol peers, all requesting every inventory, 10 Dilithium5 transactions: origin sent 7,353,000 tx bytes, 7,353 per transaction per peer. All 100 fetched all 10.

Twelve real nodes, each connected to the origin and to the previous node: origin still sent 7,350 bytes per transaction per peer. The extra link did not stop the getdata.

Compact-block misses, Dilithium5, 100 spends in the block:

| Missing | Kept in mempool | blocktxn | cmpctblock | Full block |
|---:|---:|---:|---:|---|
| 0 (had the mempool) | all | none | ~400-460 each | none |
| 1 | 99 | 7,383 | 880 | none |
| 10 | 90 | 73,344 | 880 | none |
| 100 (blocksonly or late IBD) | 0 | none | none | the whole block |

A 1% miss and a 10% miss reconstructed. They did not fall back to the full block. 73,344 / 10 is 7,334 bytes per missing transaction.

## Bandwidth if every peer fetches

Scaled from the measured per-transaction sizes and the measured miner-full blocks. 1,440 blocks/day. This is not a 100-peer capture of a full day. The 15% inbound-fetch model (about 20 copies, about 193 GB/day upload for a public Dilithium5 node) was not observed. Every topology we built fetched from the origin at 100%.

| | Dilithium2 | Dilithium5 | D5 / D2 |
|---|---:|---:|---:|
| Miner-full block | 5.56 MB | 6.37 MB | 1.15x |
| Block download, 1,440 blocks/day | 8.00 GB | 9.17 GB | 1.15x |
| Same, per year | 2.92 TB | 3.35 TB | 1.15x |
| Tx upload, 100 peers, one full block of spends each minute | 804 GB/day | 920 GB/day | 1.14x |
| Tx upload, 100 peers, 848 payments/block (D5's 8 MW count) | 472 GB/day | 898 GB/day | 1.90x |

Same payment count tracks the 1.90x transaction size. A full block of each scheme tracks the 1.15x block size, because Dilithium2 fits more payments.

## Mempool

`usage` is what the mempool accounts. `bytes` is weight/16, not serialized size. Forty unconfirmed spends:

| | Dilithium2 | Dilithium5 | D5 / D2 |
|---|---:|---:|---:|
| usage | 196,496 | 336,016 | 1.71x |
| usage per tx | 4,912 | 8,400 | 1.71x |
| vsize bytes | 13,160 | 21,880 | 1.66x |
| vsize per tx | 329 | 547 | 1.66x |
| Fits in 300 MB, from that sample | 61,100 | 35,700 | 0.58x |

Dilithium5 was then filled for real. 35,674 transactions, `usage` 299,999,920, `bytes` 19,513,678. `mempoolminfee` moved from 0.00001 to 0.00183815 BTC/kvB. The next spends were rejected: `mempool min fee not met, 100000 < 100547`. The pool did not evict an older transaction to take a new one at the same fee. RSS at 34,500 transactions was 1.46 GB.

At 869 Dilithium5 spends per block, 35,674 transactions is about 41 minutes of unconfirmed traffic. Dilithium2 was not filled to 300 MB.

## Storage

The harness `chainstate_bytes` field (317) is wrong. `du` after the short stress runs:

| | Dilithium2, height 460 | Dilithium5, height 414 |
|---|---:|---:|
| blk00000.dat | 17 MB | 17 MB |
| rev00000.dat | 1.0 MB | 1.0 MB |
| chainstate | 324 KB | 140 KB, 272 KB after a later restart |

Matching blk sizes are an accident of mixed 5-spend blocks at different heights. The 200-input blocks above are the fixed-count comparison (1.91x bytes). Undo was about 6% of blk on this spend-heavy regtest. Chainstate stayed tiny because the tests spend what they create. Txindex was off.

## Soak

Five nodes, spend and mine, localhost.

| | Dilithium2 | Dilithium5 | Dilithium5, longer |
|---|---:|---:|---:|
| Duration | 120 s | 121 s | 1,801 s |
| Spends | 835 | 720 | 5,965 |
| Samples | 167 | 144 | 1,193 |
| All on one tip | yes | yes | yes |
| Restart caught up | yes | yes | yes |
| Origin RSS at the end | not singled out | 127 MB | 409 MB |

The other nodes in the 30 minute run stayed between 59 MB and 76 MB. No crash and no split. This is not 24 hours.

## What the runs did not cover

- A 24 hour soak.
- A 20-of-20 block through cold `ConnectBlock`. The multisig functional test mined 2-of-3 and 3-of-3 spends one at a time.
- `tcpdump`. Counters are payload bytes.
- Txindex, and a chainstate that keeps outputs.
- `bench_btq` versus ECDSA. Configure disabled the bench binary.
- `btq-k8s`. Docker's WSL integration is off and `kind` is not installed. The node Dockerfile now installs `libdb5.3++`. The controller still sends with `getnewaddress` and `sendtoaddress`.

## Call

Do not put this build on a chain that already has Dilithium2 outputs. On a fresh chain, verification of a miner-full block was 49 ms here, block bytes grow about 1.15x, and the same number of payments costs about 1.90x on the wire. Keep `DILITHIUM_SIGOP_COST` at 50. The bandwidth model that assumes only 15% of peers fetch each transaction was not what these networks did. Every peer we attached fetched every transaction.
