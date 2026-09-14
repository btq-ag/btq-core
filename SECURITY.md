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

## Bundled upstream code

`src/secp256k1`, `src/leveldb`, `src/crc32c`, `src/minisketch` and
`src/crypto/ctaes` are copies of upstream projects. If you find a
vulnerability in one of them, report it to us through either channel above,
and we will coordinate with upstream. You may also report it to the upstream
project directly. `src/secp256k1/SECURITY.md` gives the libsecp256k1 contact.

## Encryption

BTQ Core does not publish PGP keys. Use GitHub private reporting if you need
the exchange to stay confidential — only you, the maintainers and GitHub can
read it, and it requires no key management. Email to the address above is
unencrypted.
