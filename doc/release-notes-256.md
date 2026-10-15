Taproot wallet fee estimates
---------------------------

Taproot inputs with script trees now use a bound covering the known script
paths, including their scripts and control blocks. When every descriptor
manager for an output has an available key-path private key, the wallet uses
the smaller key-path size and prefers key-path signing.

Encrypted wallets retain the larger script-path estimate while locked, so
funding the same transaction while locked can require a higher fee than when
unlocked. If a PSBT will be signed outside the wallet through a script path,
provide each input's `weight` to `walletcreatefundedpsbt`, or use
`fundrawtransaction`'s `input_weights` option. The unlocked wallet's key-path
estimate does not cover an external signer's larger witness.
