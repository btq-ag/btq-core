BTQ Core v0.4.1 Release Notes
===============================

**Release date:** 2026-07-02  
**Version:** `v0.4.1-testnet`

This is a single-fix bugfix release based on `v0.4.0-testnet`.

How to Upgrade
==============

Shut down the running node or wallet, wait for a clean exit, then replace
`btqd`, `btq-cli`, and `btq-qt` (or the `.exe` equivalents on Windows) with
the binaries from this release.

Consensus notice
=================

This release changes script validation behaviour for Dilithium multisig and is
therefore consensus-relevant. All nodes on the network must upgrade in lockstep;
running a mix of v0.4.0 and v0.4.1 nodes can lead to a chain split.

Notable changes since v0.4.0-testnet
====================================

### Fix OP_CHECKMULTISIGDILITHIUM key/signature matching loop
- Corrected the key/signature matching loop in `OP_CHECKMULTISIGDILITHIUM` so
  that m-of-n Dilithium multisig scripts are validated against all provided
  keys, not only a leading prefix of them.
- Previously, non-prefix m-of-n Dilithium multisig subsets (signatures matching
  keys other than the first m) were incorrectly rejected, making those outputs
  effectively unspendable.
- This is a relaxation of the previous, overly strict behaviour and is
  consensus-relevant: it can cause previously-invalid transactions to become
  valid, so all nodes must upgrade together.

Included Artifacts
==================

- `linux-x86_64.zip` — `btqd`, `btq-cli`, `btq-qt`
- `windows-x86_64.zip` — `btqd.exe`, `btq-cli.exe`, `btq-qt.exe`
