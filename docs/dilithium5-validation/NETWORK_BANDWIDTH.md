# Network bandwidth

The earlier dashboard is a model: 8 outbound, up to 114 inbound, a guessed 15% of inbound peers fetching each transaction. This run was five processes on localhost. It does not test that guess. `getpeerinfo` `bytessent_per_msg` and `bytesrecv_per_msg` are the counters. No packet capture was taken. `tcpdump` was not run. The node counters are payload accounting, which is the right comparison to the model, and they miss TCP/IP header overhead.

## Topology

One origin, listening. Two more listening nodes connected to it. One private node, `-listen=0`. One `-blocksonly` node, `-listen=0`. All `-connect` to the origin. Regtest. Same harness as the benchmarks.

## Dilithium5, 30 spends, tx phase caught

The origin's counter moved before the block was mined:

| Message | Bytes the origin sent | Bytes one full-relay peer received |
|---|---:|---:|
| tx | 661,500 | 220,500 |
| inv | 3,306 | 1,102 |
| getdata (origin received) | 3,306 | |

661,500 / 3 peers / 30 txs = 7,350 bytes per transaction per peer. Three peers asked. The blocksonly peer's tx counter did not move.

After the block:

| Message | Origin sent |
|---|---:|
| cmpctblock | 1,380 |
| block | 220,140 |
| headers | 103 |

1,380 bytes of compact block is a few hundred bytes per peer that already had the transactions. 220,140 bytes is one full block, the peer that did not. On this topology that peer is the blocksonly node. Zero missing transactions among the peers that had the mempool, and a full block for the peer that did not. The 1% and 10% miss rates were not constructed.

A node started after the block existed (`late` in the JSON) downloaded the chain as `block` messages, 3,684,144 bytes, and no `cmpctblock`. Initial sync of a node with an empty mempool is full blocks.

## Dilithium2, the 2 second pause lost

The same harness slept 2 seconds between sending and mining. On Dilithium2 the origin's tx counter did not move in that window. The block phase then shows `blocktxn` 345,942 bytes, `block` 115,530, and `cmpctblock` 1,380. The transactions were delivered inside block reconstruction, because the inv trickle had not finished. 345,942 / 3 / 30 = 3,844 bytes, which matches a Dilithium2 spend. A follow-up with a 5 second pause was started and killed before it printed. Do not treat the empty Dilithium2 tx-phase counter as evidence that Dilithium2 does not relay.

Wire size ratio from these two captures: 7,350 / 3,844 = 1.91x per payment. The saturated-block model ratio of about 1.20x was not re-measured, because neither side mined a weight-full block.

## Where the model is wrong for this network, and where it was not tested

The model assumed 20.6 upload copies on a default public node. This network uploaded 3 copies, which is every full-relay peer and not the blocksonly peer. That is the "every peer pulls" case for a 4-peer full-relay set, and it happened without any special flag. On a tiny regtest, partial fetching did not happen. The 15% figure is still an assumption about a large public network.

Handshake, ping, and feefilter were tens of bytes per peer next to a 7 KB transaction. They do not matter at this size.

## Daily extrapolation, labelled as such

Thirty Dilithium5 transactions cost the origin 661,500 bytes of tx payload plus 3,306 of inv. A day with 1,440 blocks of 848 such transactions, and the same 3 peers each fetching every one, is 848 / 30 * 1,440 * 661,500 = 26.9 GB/day of tx upload. That is this topology, not a 114-peer node. The model's 193 GB/day public figure remains a model.

## What the later runs added

Dilithium2, same four peers, 5 second pause, 20 spends. The tx phase was caught this time. Origin sent 231,780 tx bytes, 3,863 per transaction per full-relay peer, and 2,226 bytes of inv. The three full-relay peers each took a 400-byte compact block. The blocksonly peer took no transactions and then one 77,110-byte block. JSON: `dilithium2-relay-5s.json`.

One hundred `P2PInterface` peers connected to one listening Dilithium5 node and requested every inventory. All 100 completed the handshake. Ten transactions produced 7,353,000 tx bytes sent, 7,353 per transaction per peer, and 38,500 bytes of getdata received. Every peer fetched every transaction. JSON: `dilithium5-peer-ceiling.json`.

Twelve real nodes, each connected to the origin and to the previous node, did not change that. Ten transactions, origin sent 882,000 tx bytes, 7,350 per transaction per peer. The neighbor link did not stop the getdata to the origin. JSON: `dilithium5-mesh.json`.

Compact-block misses, 100 spends in the block, the peer given a subset and then connected:

| Missing | Kept in mempool | `blocktxn` | `cmpctblock` | Full `block` |
|---:|---:|---:|---:|---|
| 10 | 90 | 73,344 | 880 | none |
| 1 | 99 | 7,383 | 880 | none |

73,344 / 10 is 7,334 bytes for each missing transaction, close to the 7,350-byte relay size. A 1% miss and a 10% miss both reconstructed. They did not fall back to the full block. JSON: `dilithium5-cmpct-miss.json`, `dilithium5-cmpct-miss1.json`.

If a public node really does have 100 peers that each fetch every transaction, the upload is about 100 / 3 times the 26.9 GB/day figure, around 900 GB/day. The 15% model is the case these runs did not produce.
