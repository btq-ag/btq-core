BTQ Core v0.4.4 Release Notes
===============================

**Release date:** 2026-08-18
**Version:** `v0.4.4-testnet`

How to Upgrade
==============

Shut down the running node or wallet, wait for a clean exit, then replace
`btqd`, `btq-cli`, and `btq-qt` (or the `.exe` equivalents on Windows) with
the binaries from this release.

`v0.4.3-testnet` was tagged but binaries were not published. If you are
upgrading from v0.4.2-testnet or earlier, also read
[release-notes-btq-0.4.3.md](release-notes-btq-0.4.3.md). That tag includes
the LWMA window change to 144 blocks, which is a consensus change.

Consensus / network notice
==========================

This release does not change consensus rules. Dilithium P2MR-only remains
policy-ahead only on the live testnet (`nDilithiumP2MRHeight` unscheduled).

Notable changes since v0.4.3-testnet
====================================

### Dilithium PSBT
- A PSBT can now carry Dilithium P2MR leaf scripts, control blocks, and
  partial signatures, so a 2-of-3 spend does not need a shared `.btqms`
  file (#163)
- Decode is fail-closed: a forged Dilithium signature, an uncommitted leaf,
  a merkle-root mismatch, or a non-`SIGHASH_ALL` sighash is rejected when
  the PSBT is read
- A leaf with no control block is rejected, and merge unions control
  blocks so an empty entry cannot discard a real one (#169)
- New RPCs `createdilithiummultisig` and `getdilithiumpubkey`, plus
  `contrib/btq/dilithium-psbt.sh`

### Documentation
- Live-chain proof one-pager for Dilithium PSBT (#160)
- PSBT Appendix A size units corrected to KB (#159)
- Stale Bitcoin-inherited release notes and unowned download links removed
  (#158)

### Wallet
- Remove the unused DilithiumWalletManager (#164)

Included Artifacts
==================

- `linux-x86_64.zip` — `btqd`, `btq-cli`, `btq-qt`
- `windows-x86_64.zip` — `btqd.exe`, `btq-cli.exe`, `btq-qt.exe`
