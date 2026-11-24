# Dilithium5 validation

This branch is not something to put under an existing Dilithium2 chain. It links ML-DSA-87 only. A Dilithium2 public key, signature, or wallet blob fails the size check before verify. That is the intended shape of `dilithium-5-tests`, and it is a hard break with coins that already exist.

On a fresh chain the costs we measured are real and, on this machine, not an emergency. A miner-full Dilithium5 block, 869 spends and 7,597,667 WU, took 48.78 ms to verify on a node that had never seen those transactions. The Dilithium2 block at the same miner cap, 1,446 spends and 7,597,884 WU, took 31.6 ms. That is 1.54x. The 25-input samples are still about 2.4x per input. The chain targets one block a minute. 49 ms is a small fraction of that interval on this CPU.

A five-node regtest did not match the earlier 114-peer model, and neither did a later run with 100 peers. Every peer that was offered the inventory fetched the whole transaction, 7,353 bytes each. A 12-node mesh did the same. Compact blocks stayed small when the peer already had the transactions. A peer missing 1 of 100 pulled 7,383 bytes of `blocktxn`. A peer missing 10 of 100 pulled 73,344. Neither downloaded the whole block.

## Recommendation

Do not ship this as a replacement for a running Dilithium2 network. A new chain, or a chain that has never created Dilithium2 outputs, can run the build. Keep `DILITHIUM_SIGOP_COST` at 50. Changing it is a consensus change, and the measurements do not justify one.

## What is still open

The later soak ran 30 minutes, 5,965 spends, 1,193 samples, every sample on one tip, and a restart that caught up. It was not 24 hours. A block of 20-of-20 spends was not timed. Chainstate growth for a long-lived UTXO set was not measured. `btq-k8s` was not started: Docker's WSL integration is off and `kind` is not installed. The Dockerfile now installs `libdb5.3++`, which this `btqd` needs. The script-check cache still makes a normal `generatetoaddress` look instant. The cold numbers above are the ones that count.

Details are in the other files in this directory. Raw JSON is in `RAW_RESULTS/`.
