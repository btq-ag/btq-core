Wallet
------

- P2MR tree metadata now lives in its own wallet record type (`p2mrmeta`)
  instead of being stored as GUI receive requests (Quarks F2.17). Records
  written by older wallets are migrated in memory on load and rewritten in
  the new format the next time the tree's metadata is updated.

- The first `p2mrmeta` record sets a new mandatory wallet flag,
  `p2mr_metadata`. Older BTQ releases refuse to open a wallet carrying this
  flag rather than opening it and silently hiding the P2MR balance they
  cannot see. To go back to an older release, restore a backup taken before
  the wallet was opened with this version.
