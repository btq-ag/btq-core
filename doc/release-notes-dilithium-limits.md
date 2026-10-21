Dilithium block limits (consensus)
----------------------------------

A new buried deployment, `dilithium_limits`, changes three consensus rules:

- The block sigop cost limit goes from 80000 to 160000. At 80000 a block of
  single-key P2MR spends hit the sigop limit before the weight limit.
  `getblocktemplate` reports the limit for the block being templated.
- Each P2MR Dilithium signature check is charged 1400 validation weight
  instead of 500, so each extra check of one signature needs about 1400 more
  witness bytes. A minimal single-key leaf can check its signature twice,
  down from seven. `OP_CHECKMULTISIGDILITHIUM` charges once per key tried,
  and a 1-of-20 signed by the first key tries all 20; it stays valid up to a
  charge of 1439.
  Mempool policy applies 1400 on every network.
- Sigop counts no longer wrap at 2^32. A P2MR leaf of ~4.3 MB of
  `OP_CHECKMULTISIGDILITHIUM` bytes used to count as a few sigops, and
  ~268 KB of them in an output script wrapped the legacy cost, cancelling
  sigops elsewhere in the transaction.

Mainnet, signet and regtest enforce all three from height 1. Testnet leaves
them unscheduled. `-testnetdilithiumlimitsheight=<n>` sets a height there.
It is a consensus change, so use it only with a coordinated height above the
current tip. Blocks below `<n>` are unaffected. From `<n>`, an unspent P2MR
output whose every usable leaf makes more Dilithium checks than its witness pays for
at 1400 each (a minimal leaf checking one signature three times, say) can't be spent. Leaves
are hidden until spent, so no chain scan can find these in advance.

`getdeploymentinfo` lists the new deployment, and regtest accepts
`-testactivationheight=dilithium_limits@<n>`. `libbtqconsensus` gains
`btqconsensus_SCRIPT_FLAGS_VERIFY_DILITHIUM_LIMITS`.
