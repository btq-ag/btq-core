Wallet
------

- New `getnewhybridp2mraddress` RPC creates an opt-in P2MR address whose
  single leaf requires both a Dilithium signature and a BIP340 (schnorr)
  signature to spend. The wallet mints a fresh seed-derived Dilithium key;
  the x-only key is supplied by the caller and may belong to this wallet or
  to an external cosigner.

- A wallet that holds a key used in a P2MR leaf can now cosign a PSBT input
  for a tree it does not track. The PSBT must carry the leaf scripts and
  control blocks (the tracking wallet's `walletprocesspsbt` fills them), and
  the cosigner contributes whatever signatures its keys allow, in either
  signing order.

- Backup caveat: P2MR trees, including the hybrid leaf's key pairing, exist
  only in the wallet database. Restoring from the seed does not recreate
  them. `recoverdilithiumkeys` rebuilds the plain single-leaf address for an
  index, which is a different program from any hybrid or multi-leaf address
  using that key. A legacy `dumpwallet` file writes each tree as a `p2mr`
  record and `importwallet` restores that address. Descriptor wallets cannot
  use `dumpwallet`; use `backupwallet`, or `listp2mr` and `importp2mr`.

- `signmessagewithdilithium` refuses a hybrid-only tree, because a Dilithium
  message signature would prove less than that address implies. It still
  signs when some other leaf can be spent with the Dilithium key alone
  (a single-key leaf, a 1-of-1 threshold or checkmultisig, or a pubkey-hash
  leaf) and that key is the tree's only Dilithium key.
