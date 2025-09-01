# BTQ Core version 0.1.0 Release Notes
=====================================

BTQ Core version 0.1.0 is now available from:

<https://github.com/btq-ag/BTQ-Core/>

This release introduces major consensus changes in preparation for post-quantum (PQ) signatures, enables reliable regtest mining support, and finalizes genesis block configurations across networks.

Please report bugs using the issue tracker at GitHub:

<https://github.com/btq-ag/BTQ-Core/issues>


# Notable changes
=================

### Consensus

- Increase `MAX_BLOCK_SERIALIZED_SIZE` to 64 MiB (was 4 MiB)
- Redefine weight to equal raw serialized byte size (no witness discount)
- Increase `MAX_SCRIPT_ELEMENT_SIZE` to 10,000 bytes
- Increase `MAX_SCRIPT_SIZE` to 100,000 bytes
- Normalize `GetTransactionWeight`, `GetBlockWeight`, etc. to use serialized byte size
- Remove BIP141 weight calculation behavior (`vsize == weight == bytes`)

### Policy & Mempool

- `DEFAULT_BLOCK_MAX_WEIGHT`: 32 MiB (soft cap)
- `MAX_STANDARD_TX_WEIGHT`: 1 MiB
- Standard stack/script element limits increased to 64 KiB for P2WSH, P2WSH stack items, Taproot
- Ancestor/descendant size limits increased to 5,000 KB (count limits unchanged)
- `MAX_PACKAGE_WEIGHT`: 20 MiB
- `DEFAULT_MAX_MEMPOOL_SIZE_MB`: 2,000 MB (was 300 MB)

### P2P

- `MAX_PROTOCOL_MESSAGE_LENGTH` increased to 70 MiB
- Enables full propagation of PQ-era blocks and messages

### RPC

- vsize, size, and weight fields now reflect exact serialized byte size
- Removed legacy assumptions of vsize < size due to witness discount

### Build

- `SignatureAlgorithm` enum added to consensus params (DILITHIUM, FALCON, SPHINCS)
- Default signature algorithm set to `NONE` across all chains

### Regtest / Testing

- Fix segfault in `CCheckpointData::GetHeight()` when checkpoints are empty
- Add support for regtest mining:
  - `generateblock`, `generatetoaddress`, `getblockchaininfo`
- Chain identity correctly returns: `main`, `test`, `signet`, `regtest`
- New functional test: `btq_regtest_mining.py`
- New functional test: `btq_chain_identity.py`

### Genesis Blocks

- Mainnet and Testnet genesis blocks updated with mined values:
  - Finalized `hashGenesisBlock`, `hashMerkleRoot`, `nNonce`, `nBits`
- Added helper: `MineGenesisBlock(CBlock&)`
- POW limits increased for early network bootstrapping

---

# Credits
=========

Thanks to everyone who directly contributed to this release:

- Oskii, BarneyChambers
- Contributors to previous infrastructure and refactoring  
- Everyone assisting with test infrastructure and functional test migration

Special thanks to those continuing post-quantum R&D, contributing to reliable test environments, and helping prepare BTQ Core for a quantum-resistant future.
