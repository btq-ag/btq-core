Wallet
------

- Descriptor wallets now derive Dilithium keys from a dedicated counter
  (persisted as a `dilithiumdescindex` record) instead of sharing the
  descriptor's `next_index` with ECDSA address derivation (Quarks F2.5).
  Previously, every ECDSA address generated between two Dilithium addresses
  left a hole in the Dilithium sequence, making the keys unrecoverable by
  scanning.

- New RPC `recoverdilithiumkeys start stop ( internal )` derives the keys at
  the given indexes of a sequence and adds any that are missing, together
  with their single-leaf P2MR destinations. To restore a wallet whose
  Dilithium keys were lost or derived under the old shared counter, run it
  over both sequences (`internal=false` and `internal=true`) across the
  index range the wallet may have used, then `rescanblockchain`. Note that
  encrypting a wallet rotates its descriptors to a new seed, which also
  starts a fresh Dilithium sequence; keys issued before encryption are
  already materialized in the wallet and need no recovery, but they cannot
  be re-derived by scanning the new sequence.

- Legacy (non-descriptor) wallets now derive P2MR change keys from the
  internal keychain: new change keys use `m/0'/1'/n'` where they previously
  used the external path `m/0'/0'/n'`. Existing keys are unaffected.
