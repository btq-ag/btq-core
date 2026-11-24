# Raw results

All of these were produced on 9 Nov 2026 by `harness/measure.py` unless noted.

| File | What it is |
|---|---|
| `dilithium5-measure.json` | Dilithium5 blocks, warm bench lines, mempool, peer counters, 121 s stress, restart tips |
| `dilithium2-measure.json` | The same harness on the Dilithium2 binary |
| `dilithium5-cold-connect.json` | Cold `invalidateblock` / `reconsiderblock` verify times, Dilithium5 |
| `dilithium2-cold-connect.json` | The same, Dilithium2 |

Warm `connect_total_ms` values near 0.1 ms in the measure files are script-cache hits. Use the cold files for verification cost.

`chainstate_bytes` inside the measure files is 317 and does not match `du`. The storage write-up uses `du`.
