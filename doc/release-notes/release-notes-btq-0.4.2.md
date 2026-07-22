BTQ Core v0.4.2 Release Notes
===============================

**Release date:** 2026-07-22  
**Version:** `v0.4.2-testnet`

Audit/hardening snapshot based on `v0.4.1-testnet` and the umbrella merge of
open audit PRs (#64–#75 / #78), plus the Dilithium multisig matching-loop fix
from `v0.4.1-testnet`.

How to Upgrade
==============

Shut down the running node or wallet, wait for a clean exit, then replace
`btqd`, `btq-cli`, and `btq-qt` (or the `.exe` equivalents on Windows) with
the binaries from this release.

Consensus / network notice
==========================

Several changes in this release are consensus- or relay-policy-relevant.
Upgrade nodes together. On the live testnet, Dilithium P2MR-only remains
policy-ahead only (`nDilithiumP2MRHeight` unscheduled); do not mix this release
with older nodes without understanding relay and wallet behaviour changes.

Notable changes since v0.4.1-testnet
====================================

### Audit umbrella (#78)
- Dilithium opcodes restricted to P2MR tapscript (height-gated; testnet
  policy-ahead)
- `MAX_PROTOCOL_MESSAGE_LENGTH` reduced to block size + 1 MB
- `NODE_BTQ_DILITHIUM` service bit (bit 25)
- Dilithium seed memory cleanse and constant-time key equality
- Structural `IsFullyValid()` for Dilithium public keys
- `script_flag_exceptions` cannot clear Dilithium/P2MR/P2MR_ONLY
- C compilation hardening flags
- Mainnet LWMA activation from block 1 (testnet unchanged)
- Legacy (BDB) wallet Dilithium SigningProvider wiring
- Wallet cutover to P2MR receive/spend paths for Dilithium

### Included from v0.4.1-testnet
- Fix `OP_CHECKMULTISIGDILITHIUM` key/signature matching loop so non-prefix
  m-of-n Dilithium multisig subsets validate correctly

Included Artifacts
==================

- `linux-x86_64.zip` — `btqd`, `btq-cli`, `btq-qt`
- `windows-x86_64.zip` — `btqd.exe`, `btq-cli.exe`, `btq-qt.exe`
