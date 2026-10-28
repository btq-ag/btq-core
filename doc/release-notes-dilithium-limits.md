Dilithium block limits (consensus)
----------------------------------

A new buried deployment, `dilithium_limits`, changes three consensus rules:

- The block sigop cost limit goes from 80000 to 160000.
  `getblocktemplate` reports the limit for the block being templated.
  P2MR leaves are not included in this count.
- Each P2MR Dilithium signature check is charged 1439 validation weight
  instead of 500, so each extra check of one signature needs about 1439 more
  witness bytes. A minimal single-key leaf can check its signature twice,
  down from seven. `OP_CHECKMULTISIGDILITHIUM` charges once per key tried,
  and a 1-of-20 signed by the first key tries all 20. That spend's witness
  budget is 28785, so 1439 is the highest charge that still fits. 1500 does
  not. Mempool policy applies 1439 on every network.
- Sigop counts no longer wrap at 2^32. ~268 KB of
  `OP_CHECKMULTISIGDILITHIUM` in an output script used to wrap the legacy
  cost, cancelling sigops elsewhere in the transaction. A P2MR leaf is not
  added to the block sigop total, so its size cannot wrap that count.

Mainnet, the new testnet, signet, and regtest enforce all three from height 1.
This branch's testnet is the launch chain, so the rule is not left unscheduled
and there is no `-testnetdilithiumlimitsheight`. Regtest can still move the
height with `-testactivationheight=dilithium_limits@<n>`. Blocks below the
height are unaffected. From it, an unspent P2MR output whose every usable leaf
makes more Dilithium checks than its witness pays for at 1439 each (a minimal
leaf checking one signature three times, say) can't be spent. Leaves are
hidden until spent, so no chain scan can find these in advance.

`getdeploymentinfo` lists the new deployment, and regtest accepts
`-testactivationheight=dilithium_limits@<n>`. `libbtqconsensus` gains
`btqconsensus_SCRIPT_FLAGS_VERIFY_DILITHIUM_LIMITS`.
