# Remaining work

The simulation runs that were open are done, with three exceptions below.

Done since the first write-up:

- Miner-full blocks, cold `submitblock`. Dilithium5 869 spends, 48.78 ms. Dilithium2 1,446 spends, 31.6 ms. The miner clamps the block at 7,600,000 WU.
- Dilithium2 transaction relay with a 5 second pause. 3,863 bytes per transaction per full-relay peer.
- Compact-block misses at 1 of 100 and 10 of 100. `blocktxn` only, no full block.
- Mempool filled to 299,999,920 bytes of `usage`, 35,674 transactions. Further spends rejected, `mempool min fee not met`.
- 100 peers, every one fetched every transaction. A 12-node mesh did the same.
- 30 minute soak, 1,193 samples, all on one tip, restart caught up.

## Still not done

1. A 24 hour soak. The long run here is 30 minutes. The command is `python3 docs/dilithium5-validation/harness/finish_sims.py soak 86400`. It mines continuously. Do not start it on a disk that is already near full.

2. `btq-k8s` was not started. This WSL distro has no Docker daemon (`docker` tells you to turn on Docker Desktop WSL integration) and `kind` is not installed. `docker/node/Dockerfile` now installs `libdb5.3++`. The controller still calls `getnewaddress` and `sendtoaddress`, so a cluster that does come up will not be spending Dilithium outputs until that changes. The image still needs this branch's `btqd` and `btq-cli` copied into `docker/node/` and stripped.

3. A block of 20-of-20 spends through cold `ConnectBlock`. The full blocks above are one-input wallet spends.

4. Packet capture. The counters are `getpeerinfo` payload bytes. TCP/IP overhead was not measured.

5. Txindex disk, and a chainstate that grows because outputs are kept. These regtest runs spend what they create.

6. `bench_btq` against ECDSA. Configure used `--disable-bench`.
