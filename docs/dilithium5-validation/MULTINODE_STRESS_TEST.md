# Multi-node soak

Five nodes, then a sixth started late. The harness in `harness/measure.py` ran this twice, once per binary. Duration was 120 seconds of requested stress, plus the benchmark blocks before it.

A later Dilithium5 run used `harness/finish_sims.py soak 1800`. Five nodes, 1,801 seconds, 5,965 spends, 1,193 samples, every sample converged, height 2,911. Origin RSS ended at 409 MB. The other four stayed between 59 MB and 76 MB. After the run, one peer was stopped, the origin mined 3 blocks, and the peer was started again. All five returned tip `7579f14c7cb3305ecb908f3fb3f88bc69e3badb6096458a1709a18ef66921d5d`. That is 30 minutes, not 24 hours. JSON: `dilithium5-soak.json`.

## Topology

| Node | Role |
|---|---|
| origin | Listening, miner, both wallets |
| public | Listening, `-connect` to origin |
| private | `-listen=0`, `-connect` to origin |
| blocksonly | `-listen=0 -blocksonly=1`, `-connect` to origin |
| extra | Listening, `-connect` to origin |
| late | Started after the measured block, `-listen=0`, then included in the stress loop |

Regtest, localhost, ports 19744-19749 and RPC 18744-18749.

## Dilithium5

Stress loop: fund 5 P2MR outputs, spend them, mine one block, wait until every live node reports that height. 121.2 seconds, 720 spends, 144 samples. `all_samples_converged` is true. No sample recorded a dead process.

Origin RSS high-water went from 103 MB at the first sample to 127 MB at the last. The other nodes stayed near their first reading (public 73 MB, private 71 MB, blocksonly 63 MB, extra 71 MB, late 79 MB). That is not a full mempool, and it is not a long run.

The private node was stopped, the origin mined 3 more blocks, and the private node was started again. It reached the origin tip. Origin, public, private, and blocksonly then returned the same best block hash `4e38283b6d7ef291f37a8aa82a7b17807b07c3008788d264aa99142e02c7a42e`.

No validation failure, crash, or split showed up in this window. Height moved from 125 to 411 during the stress samples, so the nodes were producing blocks the whole time.

## Dilithium2

Same harness. 120.2 seconds, 835 spends, 167 samples, all converged. After the same restart, origin, private, and blocksonly shared one tip.

## What this does not show

A day of uptime. A partition that is not "stop the process". Mempool eviction under a 300 MB fill. Disk filling up. RPC latency under load was not timed, only that RPC answered `getblockcount` on each sample.
