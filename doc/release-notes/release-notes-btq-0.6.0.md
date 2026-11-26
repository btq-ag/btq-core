BTQ Core v0.6.0 Release Notes
===============================

**Release date:** 2026-11-26
**Version:** `v0.6.0-testnet`

This is the Dilithium 5 version of BTQ Core. It is not compatible with the
old Dilithium 2 testnet (v0.5.0-testnet and earlier). Do not upgrade a
running Dilithium 2 node in place, and do not reuse its datadir or wallet.

How to Upgrade
==============

This is a new testnet, not a patch on the Dilithium 2 chain.

Shut down any v0.5.0-testnet node. Move the old `test` datadir aside
(`~/.btq/test`, or the `test` folder under `-datadir`). Start this build
with an empty datadir so it can build the new chain from genesis.

A Dilithium 2 wallet, address, or signature will not work here. Keys are
a different size.

The network magic (`0c120a08`), the default testnet port (`19333`), and the
seed hostnames are unchanged. An old node and a new node can still connect
to each other, and then they will reject each other's blocks. If those
seeds are still serving the Dilithium 2 chain, this node will connect and
stall. The old chain still needs the v0.5.0-testnet binary.

Consensus / network notice
==========================

The testnet genesis is new:

- Message: `BTQ Dilithium5 testnet genesis 20261124`
- Hash: `000000e99ba14ab7b5feb7f2f290931e22472a69714c7081a2c6fe82b0d48ee2`

v0.5.0-testnet used `BTQ testnet genesis block 20260526`. Those blocks are
a different chain.

This build only speaks Dilithium5 (ML-DSA-87):

- public key 2592 bytes
- secret key 4896 bytes
- signature 4627 bytes

Dilithium2 was 1312 / 2560 / 2420. There is no mixed mode.

LWMA starts at block 1. Testnet still allows min-difficulty blocks.
`nDilithiumP2MRHeight` is unscheduled on this testnet.

Notable changes since v0.5.0-testnet
=====================================

- Dilithium5 replaces Dilithium2 in the consensus build
- New testnet genesis, so the old testnet chain is not reused
- P2MR dust priced from a single-key Dilithium spend
- Sanitizer, clang thread-safety, and wallet dump/import fixes that landed
  with the Dilithium5 testnet branch
