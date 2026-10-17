# Functional coverage and fee corrections

## Scope and evidence

This work closes the reproducible Linux functional backlog in [#104](https://github.com/btq-ag/btq-core/issues/104), not every possible BTQ behavior or platform.
The reference integration is local commit `07c20edca09f526a1ce70fd7982aee7ce421ac9b` (not pushed to GitHub), based on master `475473e817cffe3d88613d9dc6c46322a90e591b` plus the existing PR185, PR187, PR193 and PR233 changes.
Those existing production changes remain owned by their original PRs; the publication series separates new production fixes from test repairs.
Integrated results must not be presented as a clean run of any individual split PR.

The normal extended suite completed 315 invocations: **308 passed, zero failed, seven skipped**, with all RPC command names exercised and all 40 database crash/recovery iterations completed.
The wallet-disabled extended suite initially recorded 170 passed, six failed and 139 skipped; isolated reruns after fixing test funding and pruning setup give **176 passed, zero unresolved failures and 139 skipped**. Five original failures were port collisions. This is aggregated evidence, not a single clean wallet-disabled run.
Boost passed 788 wallet-enabled and 663 wallet-disabled cases; framework, harness, utility and rpcauth checks passed 17, nine, 95 and three cases respectively.

## Production defects and applicability

| Defect | Provenance and BTQ consequence | Disposition |
| --- | --- | --- |
| Fee cache compares format requirement 149900 with product version 500 | Inherited Bitcoin serialization was coupled to product versions. BTQ renumbering in `a01f1c9b949611823a5cc0985046ed4dc65da070` made its own cache unreadable; v0.5.0 remains affected. Restart loses learned fee estimates. | Reproduced on master; existing [PR193](https://github.com/btq-ag/btq-core/pull/193) introduces explicit format 2. Do not duplicate that fix. |
| Two-block estimates conflate confirmations through block ten | Introduced by unmerged PR193 commit `7ecb931dd`, not current master: 12 ten-block short periods preserve time span but erase short-target resolution. The regression quoted about 5,263 sat/kvB when the qualifying cohort required about 52,631. | Separate follow-up based on PR193: 120 one-block periods and format 3. |
| Taproot descriptors assume only a 66-weight key-path satisfaction | Inherited from Bitcoin Core (`fa7c46b50`, present in v26.0). Large script-path witnesses exceed the estimate; BTQ's weight scale makes the inherited assumption especially visible. Automatic-fee sends can fail minimum-relay checks. | Independent wallet fix ([PR256](https://github.com/btq-ag/btq-core/pull/256)) bounds the largest known key/script satisfaction, and uses the key-path weight when the wallet can sign the key path; reproduced before, passes after. No consensus or signature-validation change. |

These fee defects affect wallet reliability and confirmation expectations. Testing did not establish theft, a consensus split or a cryptographic break. Consensus validation, crash recovery, wallet integrity and persistence receive priority over cosmetic/API fixture parity.
Other production dependencies remain [PR185](https://github.com/btq-ag/btq-core/pull/185) (package limits), [PR187](https://github.com/btq-ag/btq-core/pull/187) (v2 transport defaults) and [PR233](https://github.com/btq-ag/btq-core/pull/233) (upload timing). Their fixes are not reproduced in new master-targeted PRs.

## Fee policy choices and cache migration

Retention and target resolution are separate. Keep PR193's decay and medium/long horizons, but retain one-block resolution through 120 short periods. This costs approximately 326 KiB of additional estimator counters and increases cache size and per-block work.
Using a ten-block bucket for a two-block target, or weakening the regression's expected fee bands, is not acceptable. Reverting all horizon tuning would undo PR193's intended 60-second-chain adaptation.

The failure rule is upstream's: an unconfirmed removal counts as a failure only once it is at least one full period old (`blocksAgo >= scale`). With one-block short periods, the short horizon still counts a removal after one block; the medium and long horizons wait for their own period length.

Format 3 is deliberate: loading old serialized scales would silently restore the defective geometry even after changing constructor defaults. Older caches are rejected nonfatally and rewritten. Only learned estimation history resets; keys, wallets, transactions and chainstate do not.
Until the estimator relearns enough history, callers may receive no estimate or use configured fallback fees. Downgrades also reject a newer cache. Preserving the old scale or pretending its counters have a different resolution would provide misleading estimates.

For Taproot, include satisfaction bytes, the CompactSize-prefixed script, and the CompactSize-prefixed depth-dependent control block. Witness bytes have unit weight; the caller adds the stack-count prefix once and applies BTQ virtual-size rules. Unknown bounds return no estimate.
The descriptor bound is the maximum over all paths. The wallet then uses the key-path weight instead when it holds the private internal or output key, because Taproot signing tries the key path first. Locked and watch-only wallets cannot sign the key path, so they keep the maximum and may pay for a larger script path than the spend uses.

## Fork sources and alternatives

- [Dogecoin v1.14.9 fee estimator](https://github.com/dogecoin/dogecoin/blob/v1.14.9/src/policy/fees.h) preserves per-block confirmation counters while tuning decay and sampling separately. Its constants were not copied.
- [Dogecoin v1.14.7 release notes](https://github.com/dogecoin/dogecoin/blob/v1.14.7/doc/release-notes.md#maintain-rpc-fee-estimation-facilities) document discarding estimator caches after changing tracked fee bounds and spacing; their custom-version warning supports an explicit format marker.
- [Litecoin v0.21.4](https://github.com/litecoin-project/litecoin/blob/v0.21.4/src/policy/fees.h) retains one-block short periods and coarser longer horizons. That supports preserving resolution, not blindly copying wall-clock constants.
- [BCH Node v29.0.0](https://github.com/bitcoin-cash-node/bitcoin-cash-node/blob/v29.0.0/src/txmempool.cpp) estimates from relay/mempool floors without confirmation-target history. That alternative does not satisfy BTQ's target-sensitive estimator contract; BCH is not a Taproot sizing precedent.
- [Bitcoin Core v26.0 descriptors](https://github.com/bitcoin/bitcoin/blob/v26.0/src/script/descriptor.cpp) contain the inherited key-path-only FIXME. [Proposal 26573](https://github.com/bitcoin/bitcoin/pull/26573) is design/review context, not a claimed merged upstream fix. BTQ's caller and weight rules were checked directly.

## Regression coverage and remaining boundaries

The estimator regression trains fast/high-fee and slow/low-fee cohorts over 300 blocks, checks two-block quotes, preserves strict original fee bands using ten-block epochs, verifies an exact format-3 round trip, rejects old headers without discarding live estimates, and exercises a genuine format-2 cache through startup/reset/restart.
Taproot units cover key-only, script-prefix, control-depth six/seven and stack-count 252/253 boundaries. A deterministic functional case forces 251 satisfaction items (253 witness elements), an 8,536-byte script and a 257-byte control block, then checks signed witness shape, actual fee sufficiency, mempool acceptance and confirmation. It fails against the old binary at fee sufficiency. The previously failing randomized seed also passes after the fix. A second functional case funds a key-path-signable output with a 999-key script leaf and checks that 12-input `send` and 13-input `sendall` produce one-item key-path witnesses with a fee within twice the relay minimum.

Fixture repairs retain behavioral assertions while adapting subsidy, address prefixes, genesis hashes, activation rules, weight/virtual size and 60-second timing. Large pruning/upload blocks remain consensus-valid; fee replacements retain all 250 samples and their exact fee expectations. The no-wallet pruning setup is now independent of wallet setup.
Harness corrections reject truncated/duplicate inventories, missing variants, missing RPC-name coverage and all-skipped runs; framework unit failures now exit nonzero. Existing known failures remain visible until their own dependencies are resolved.

Regtest-only checkpoint injection and an assumeutxo snapshot entry enable validation coverage; they do not change public-network parameters. Historical tests use pinned, source-built BTQ v0.4.3 and v0.5.0 binaries for old/current/old/current wallet and chainstate checks.
Ancient Bitcoin wallet and chainstate migrations require authentic BTQ fixtures and an applicability decision; a synthetic obsolete-database rejection fixture is not evidence of successful old-format migration.
The seven host skips are five USDT cases requiring tracing support/privileges and two opt-in routable-interface cases. Wallet-disabled skips are configuration limits. Qt, fuzz, sanitizer and other platform lanes remain separate unverified scope. No public networks, real wallets or funds were used.
