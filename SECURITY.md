# Security Policy

## Supported Versions

BTQ Core has not launched mainnet. Releases are tagged `vX.Y.Z-testnet`, and
only the most recent release line receives security fixes.

## Reporting a Vulnerability

**Do not report security vulnerabilities through public GitHub issues.**

Two private channels, in order of preference:

1. **GitHub private vulnerability reporting** — the "Report a vulnerability"
   button under this repository's Security tab. Preferred: the report stays
   private, the thread is authenticated, and the advisory and CVE are issued
   from the same place.
2. **Email bitcoinq@btq.com** — for security reports only, not support.

`doc-btq/SECURITY.md` documents the severity categories, response timelines
and coordinated-disclosure process.

## Bugs that also affect upstream

BTQ Core is a fork of Bitcoin Core 26.0. It also bundles upstream libraries
in `src/secp256k1`, `src/leveldb`, `src/crc32c`, `src/minisketch` and
`src/crypto/ctaes`. If a bug is in code that BTQ did not change, the upstream
project owns the fix. Report it to upstream first:

- Bitcoin Core: security@bitcoincore.org
- libsecp256k1: secp256k1-security@bitcoincore.org
- Other libraries: the project's own security policy

If upstream agrees, please also tell us through a private channel above, so
we can ship the fix when upstream releases it. If you are not sure whether
BTQ changed the code, report it to us and we will check.

Bugs in BTQ-specific code — Dilithium signatures, P2MR, and BTQ consensus
and network parameters — come to us only.

## Encryption

BTQ Core does not publish PGP keys. Use GitHub private reporting if you need
the exchange to stay confidential — only you, the maintainers and GitHub can
read it, and it requires no key management. Email to the address above is
unencrypted.
