# Mempool and storage

Measured on the runs in `RAW_RESULTS/`. The harness's own `chainstate_bytes` field came out at 317 and is wrong. The figures below are from `du` after the processes stopped.

## Mempool

`getmempoolinfo` `usage` is the memory the mempool tracks. `bytes` is virtual size, weight divided by 16, not the serialized transaction.

Forty unconfirmed spends:

| | Dilithium2 | Dilithium5 |
|---|---:|---:|
| `usage` | 196,496 | 336,016 |
| per transaction | 4,912 | 8,400 |
| `bytes` (vsize) | 13,160 | 21,880 |
| vsize per transaction | 329 | 547 |

Serialized size from the mined 200-input blocks, coinbase roughly removed: Dilithium2 about 3,841 bytes, Dilithium5 about 7,328 bytes. Mempool `usage` is higher than that serialized size, 4,912 versus 3,841 (1.28x) and 8,400 versus 7,328 (1.15x).

Default `-maxmempool` is 300,000,000. Dividing by the measured usage per transaction:

- Dilithium5: 35,700 transactions
- Dilithium2: 61,100 transactions

The old 40,700 figure divided 300 MB by a 7,372-byte serialized spend and assumed usage equals that size. Usage is larger, so the count is smaller.

The same node was then filled. At 34,500 transactions, `usage` was 290,213,456 and the minimum fee was still the relay floor. Another 1,174 transactions brought it to 35,674 transactions, `usage` 299,999,920, `bytes` 19,513,678. `mempoolminfee` rose to 0.00183815 BTC/kvB. The next spends were rejected with `mempool min fee not met, 100000 < 100547`. The pool did not drop older transactions to make room for a new one at the same fee. RSS at 34,500 transactions was 1.46 GB. JSON: `dilithium5-mempool-fill.json`, `dilithium5-mempool-evict.json`.

At one full Dilithium5 block per minute (848 spends), 35,700 transactions is about 42 minutes of transactions. That uses the extrapolated full block, not a measured full mempool.

## Block files

| | Dilithium5, height 414 | Dilithium2, height 460 |
|---|---:|---:|
| `blk00000.dat` | 17 MB | 17 MB |
| `rev00000.dat` | 1.0 MB | 1.0 MB |
| chainstate | 140 KB right after the run, 272 KB after a later restart | 324 KB |

The two blk files matching is an accident of the mixed workload. Most blocks in the stress loop hold 5 spends, and the heights differ. The comparison that holds the transaction count fixed is the 200-input block:

| | Bytes | Weight |
|---|---:|---:|
| Dilithium2, 201 txs | 768,649 | 1,053,844 |
| Dilithium5, 201 txs | 1,466,049 | 1,751,244 |
| Ratio | 1.91x | 1.66x |

Undo was about 1.0 MB against 17 MB of blk data in both runs, about 6% extra, for this spend-heavy regtest. That ratio is not a law for a chain of full blocks.

Chainstate stayed under a megabyte because the test spends what it creates. It says nothing about a UTXO set that accumulates for a year.

## Projections, separate from the measurements

A saturated Dilithium5 wallet block is 6.25 MB in the size model (848 transactions at the measured 7,328 to 7,372 byte spend, plus a coinbase). That model was not confirmed by mining an 848-transaction block.

| | Block bytes per day at 1,440 blocks | Per year | Undo at the measured 6% |
|---|---:|---:|---:|
| Dilithium5 full wallet blocks | 9.0 GB | 3.29 TB | about 0.20 TB |
| Dilithium2 full wallet blocks | 7.5 GB | 2.75 TB | about 0.16 TB |

Chainstate and a txindex are not in those numbers. Txindex was off.
