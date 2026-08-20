BTQ Core v0.5.0 Release Notes
===============================

**Release date:** 2026-08-20
**Version:** `v0.5.0-testnet`

How to Upgrade
==============

Shut down the running node or wallet, wait for a clean exit, then replace
`btqd`, `btq-cli`, and `btq-qt` (or the `.exe` equivalents on Windows) with
the binaries from this release.

This is a version-only rerelease of the code that was briefly tagged
`v0.4.4-testnet`. That 0.4.4 tag is withdrawn. There are no functional
changes versus that build.

Consensus / network notice
==========================

The LWMA averaging window change to 144 blocks (from `v0.4.3-testnet`, #143)
is a consensus change. A consensus change must not live on a 0.4.x patch
number, so this tree is published as **0.5.0**.

Upgrade nodes together. Dilithium P2MR-only remains policy-ahead only on the
live testnet (`nDilithiumP2MRHeight` unscheduled).

If you are upgrading from v0.4.2-testnet or earlier, also read
[release-notes-btq-0.4.3.md](release-notes-btq-0.4.3.md).

Notable changes since v0.4.3-testnet
====================================

Identical to the withdrawn `v0.4.4-testnet` notes:

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
