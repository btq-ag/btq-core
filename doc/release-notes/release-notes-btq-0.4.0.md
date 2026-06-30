BTQ Core v0.4.0 Release Notes
===============================

**Release date:** 2026-06-25  
**Version:** `v0.4.0-testnet`

How to Upgrade
==============

Shut down the running node or wallet, wait for a clean exit, then replace
`btqd`, `btq-cli`, and `btq-qt` (or the `.exe` equivalents on Windows) with
the binaries from this release.

Notable changes since v0.3.2-testnet
====================================

### P2MR Qt wallet integration
- BIP-360 P2MR vault manager integrated into the Qt wallet UI
- P2MR metadata handling made idempotent; audit hardening for Qt wallet flows

### Dilithium wallet and consensus
- Deterministic Dilithium HD wallet derivation (#53)
- Taproot activation and second-pass audit remediation
- Dilithium activation, sigops, and wallet send/consensus validation tests

### Network and parameters
- Testnet genesis correction and related audit hardening
- LWMA difficulty retarget remains active from the v0.3.1 line (height 300,000)

### Build and CI
- Lean CI build gate with address/wallet smoke tests
- Qt wallet defaults to testnet when no chain is specified

Included Artifacts
==================

- `linux-x86_64.zip` — `btqd`, `btq-cli`, `btq-qt`
- `windows-x86_64.zip` — `btqd.exe`, `btq-cli.exe`, `btq-qt.exe`
