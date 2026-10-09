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
  only in the wallet database. Restoring from the seed or from an
  `importwallet` dump does not recreate P2MR addresses; `recoverdilithiumkeys`
  rebuilds the plain single-leaf address for an index, which is a different
  program from any hybrid or multi-leaf address using that key. Keep a
  `backupwallet` copy of any wallet holding P2MR funds.

- `signmessagewithdilithium` now refuses addresses whose tree contains a
  hybrid leaf. Spending such an address needs the schnorr key too, so a
  Dilithium-only message signature would prove less than the address implies.
