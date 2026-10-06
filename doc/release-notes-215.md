Wallet
------

- Descriptor wallets no longer treat a P2WPKH output as their own just
  because one of their descriptors holds the key. A descriptor now claims
  only the scripts it produces, as in Bitcoin Core. Coins received at the
  P2WPKH address of a key that the wallet holds only in another form, such
  as `pkh(K)`, `sh(wpkh(K))`, `tr(K)` or a multisig, no longer appear in
  `getbalance` or `listunspent`. They are not lost. To get them back, import
  `wpkh(K)` with `importdescriptors` and rescan. (#215)
