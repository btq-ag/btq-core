BTQ Core v0.4.3 Release Notes
===============================

**Release date:** 2026-08-18
**Version:** `v0.4.3-testnet`

How to Upgrade
==============

Shut down the running node or wallet, wait for a clean exit, then replace
`btqd`, `btq-cli`, and `btq-qt` (or the `.exe` equivalents on Windows) with
the binaries from this release.

Consensus / network notice
==========================

This release raises the LWMA averaging window from 45 to 144 blocks (#143).
That is a consensus change. Upgrade nodes together.

On the live testnet, Dilithium P2MR-only remains policy-ahead only
(`nDilithiumP2MRHeight` unscheduled). Legacy Dilithium outputs on that chain
are still consensus-valid.

Notable changes since v0.4.2-testnet
====================================

### Security backports
- CVE-2024-52911: use-after-free in the script interpreter (#135)
- CVE-2025-46597: cap `-maxmempool` on 32-bit systems (#136)
- CVE-2024-52919: widen the addrman entry id past 32 bits (#137)
- CVE-2025-54604 / CVE-2025-54605: rate-limit unconditional logging (#138)
- CVE-2025-46598: detect witness stripping without re-running scripts (#139, #149)
- Stop punishing peers for invalid transactions; check scripts once (#149)

### Consensus and network
- LWMA averaging window raised to 144 blocks (#124, #143)
- Testnet P2MR-only activation made height-settable; default remains unscheduled (#128)
- Placeholder fixed seeds removed; bootstrap failure is explicit (#134)
- `-blocksonly=1` starts under BTQ's mempool floor (#157)

### Dilithium and wallet
- Domain-separate Dilithium message signing from transaction signing (#126)
- Dilithium known-answer tests and provenance pin (#129)
- Remove the dead Dilithium AVX2 tree (#130)
- Keep Dilithium sends quantum-safe; stop issuing dead addresses (#125)
- Stop aborting on Dilithium types from the legacy keypool (#140)
- Do not fail a whole wallet when a Dilithium key loads before its manager (#153)
- `verifymessagewithdilithium` added; `verifydilithiumsignature` argument order fixed (#84)
- Client conversion table entries for Dilithium and P2MR RPCs (#156)
- P2MR PSBT signing fix (#83)
- Legacy Dilithium `validateaddress` on testnet (#88)

### Documentation
- Protocol notes mapping each Dilithium consensus rule to the code, and the
  pre-P2MR script templates still present on public testnet (#167)
- Dilithium PSBT design specification (#99)
- Security contact settled on btq.tech (#115, #148)
- Audit briefing, changed-file map, and audit-matrix refresh (#101, #113, #119)

### Build and tests
- Untrack compiled binaries and a stray 100 MB archive (#122, #133)
- Functional test address and subsidy fixtures (#141, #142)
- Isolate Dilithium helper scripts from the user datadir (#123)
- Dilithium verification benchmarks (#100)
- Bench and sigop-count test fixes under BTQ consensus rules (#151, #155)

Included Artifacts
==================

- `linux-x86_64.zip` — `btqd`, `btq-cli`, `btq-qt`
- `windows-x86_64.zip` — `btqd.exe`, `btq-cli.exe`, `btq-qt.exe`
