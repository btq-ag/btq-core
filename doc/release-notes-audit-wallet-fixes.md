Wallet
------

- `verifymessagewithdilithium` and the deprecated `verifydilithiumsignature`
  take an optional `pubkey` argument: the signer's Dilithium public key in hex
  (see `getdilithiumpubkey`). With it no wallet needs to be loaded, so anyone
  running a node with wallet support can check a proof of ownership.

- A Dilithium message signature now proves only addresses that its key alone
  controls: the key's single-key P2MR receive address, or its Dilithium key
  hash on a network that still allows payments to those (none on mainnet).
  `signmessagewithdilithium` and both verify RPCs return an error for any
  other address, including a P2MR tree with spend paths besides the key, which
  they used to accept when the wallet tracked the tree.

- `createp2mrspend` pays change to a new P2MR address of the wallet instead of
  a witness-v0 address, or to the new optional `change_address` argument,
  which must be a P2MR address. Deriving the change address needs the wallet
  unlocked if it is encrypted, and a wallet without private keys must pass
  `change_address`. The result reports `change_address`.

- P2MR change addresses are not pre-derived, so a backup taken before a
  `createp2mrspend` with change does not know the change address. Back up the
  wallet after such spends, as for other P2MR change.

- A wallet no longer loads if a plaintext Dilithium key record has a checksum
  that does not match, or is filed under any record key other than its own key
  id. Records written by BTQ releases pass these checks.

- P2MR tree metadata is no longer returned as payment requests. The receive
  tab decoded it as one, which added garbage rows. For some small trees
  without a Dilithium key, such as ones added with `importp2mr`, the decode
  failed, so `btq-qt` aborted at startup with that wallet and could not open
  it from a running GUI. Those wallets open again.
