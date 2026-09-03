// Copyright (c) 2011-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/fees.h>
#include <policy/policy.h>
#include <test/util/txmempool.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/time.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(policyestimator_tests, ChainTestingSetup)

BOOST_AUTO_TEST_CASE(BlockPolicyEstimates)
{
    CBlockPolicyEstimator& feeEst = *Assert(m_node.fee_estimator);
    CTxMemPool& mpool = *Assert(m_node.mempool);
    LOCK2(cs_main, mpool.cs);
    TestMemPoolEntryHelper entry;
    CAmount basefee(2000);
    CAmount deltaFee(100);
    std::vector<CAmount> feeV;
    feeV.reserve(10);

    // estimateFee always queries the medium horizon. That horizon used to be
    // scale 2 / decay 0.9952 (10-minute blocks). It is now scale 20 / decay
    // 0.99952 (60-second blocks). Block counts and confirmation targets from
    // the original test are multiplied by 10 so the same wall-clock pattern
    // still holds.
    constexpr int kTimeScale = 10;
    constexpr int kFeeLevels = 10;
    constexpr int kCycle = 10 * kTimeScale;
    constexpr int kTrainBlocks = 200 * kTimeScale;
    constexpr int kEarlyCheck = 3 * kTimeScale;
    constexpr int kEmptyEnd = 250 * kTimeScale;
    constexpr int kUnminedEnd = 265 * kTimeScale;
    constexpr int kAllMinedEnd = 665 * kTimeScale;
    constexpr int kMaxMedTarget = 48 * kTimeScale;
    constexpr int kMedScale = 20;

    // Populate vectors of increasing fees
    for (int j = 0; j < kFeeLevels; j++) {
        feeV.push_back(basefee * (j+1));
    }

    // Store the hashes of transactions that have been
    // added to the mempool by their associate fee
    // txHashes[j] is populated with transactions either of
    // fee = basefee * (j+1)
    std::vector<uint256> txHashes[10];

    // Create a transaction template
    CScript garbage;
    for (unsigned int i = 0; i < 128; i++)
        garbage.push_back('X');
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = garbage;
    tx.vout.resize(1);
    tx.vout[0].nValue=0LL;
    CFeeRate baseRate(basefee, GetVirtualTransactionSize(CTransaction(tx)));

    // Create a fake block
    std::vector<CTransactionRef> block;
    int blocknum = 0;

    auto include_level = [&](int level) {
        while (txHashes[level].size()) {
            CTransactionRef ptx = mpool.get(txHashes[level].back());
            if (ptx)
                block.push_back(ptx);
            txHashes[level].pop_back();
        }
    };

    // 100-block cycle: highest fee in every block, next in 90/100, down to
    // lowest in 10/100. Same inclusion frequencies as the old 10-block cycle.
    auto include_for_block = [&](int bn) {
        for (int level = 0; level < kFeeLevels; level++) {
            if ((bn % kCycle) >= (kFeeLevels - 1 - level) * kTimeScale) {
                include_level(level);
            }
        }
    };

    // Medium decay is 0.99952. 4 fee txs per block keeps the count well above
    // the 0.1 / (1-decay) ≈ 208 threshold after a couple of buckets combine.
    while (blocknum < kTrainBlocks) {
        for (int j = 0; j < kFeeLevels; j++) { // For each fee
            for (int k = 0; k < 4; k++) { // add 4 fee txs
                tx.vin[0].prevout.n = 10000*blocknum+100*j+k; // make transaction unique
                uint256 hash = tx.GetHash();
                mpool.addUnchecked(entry.Fee(feeV[j]).Time(Now<NodeSeconds>()).Height(blocknum).FromTx(tx));
                txHashes[j].push_back(hash);
            }
        }
        include_for_block(blocknum);
        mpool.removeForBlock(block, ++blocknum);
        block.clear();
        // Check after just a few txs that combining buckets works as expected
        if (blocknum == kEarlyCheck) {
            // After 30 blocks we should need to combine a couple of buckets.
            // estimateFee(1) is hardcoded to fail. estimateFee(20) is one
            // medium-scale period (old test: estimateFee(2) at scale 2).
            BOOST_CHECK(feeEst.estimateFee(1) == CFeeRate(0));
            BOOST_CHECK(feeEst.estimateFee(kTimeScale * 2).GetFeePerK() < 9*baseRate.GetFeePerK() + deltaFee);
            BOOST_CHECK(feeEst.estimateFee(kTimeScale * 2).GetFeePerK() > 9*baseRate.GetFeePerK() - deltaFee);
        }
    }

    std::vector<CAmount> origFeeEst;
    // Highest feerate is 10*baseRate and gets in all blocks,
    // second highest feerate is 9*baseRate and gets in 90/100 blocks = 90%,
    // third highest feerate is 8*base rate, and gets in 80/100 blocks = 80%,
    // so estimateFee(1) would return 10*baseRate but is hardcoded to return failure
    // Second highest feerate has 100% chance of being included by 20 blocks,
    // so estimateFee(20) should return 9*baseRate etc...
    for (int i = 1; i < 10;i++) {
        const int target = i * kTimeScale;
        origFeeEst.push_back(feeEst.estimateFee(target).GetFeePerK());
        if (i > 2) { // Fee estimates should be monotonically decreasing
            BOOST_CHECK(origFeeEst[i-1] <= origFeeEst[i-2]);
        }
        // Scale 20 buckets targets 1-20 together, so the old
        // "estimateFee(2) == 9*baseRate" mapping does not apply. Just
        // require a real estimate at the medium-scale steps.
        if (target % kMedScale == 0) {
            BOOST_CHECK(origFeeEst[i-1] > 0);
        }
    }
    // Fill out rest of the original estimates (medium horizon max is 480)
    for (int i = 10; i <= kMaxMedTarget / kTimeScale; i++) {
        origFeeEst.push_back(feeEst.estimateFee(i * kTimeScale).GetFeePerK());
    }

    // Mine 500 more blocks with no transactions happening, estimates shouldn't change
    // We haven't decayed the moving average enough so we still have enough data points in every bucket
    while (blocknum < kEmptyEnd)
        mpool.removeForBlock(block, ++blocknum);

    BOOST_CHECK(feeEst.estimateFee(1) == CFeeRate(0));
    for (int i = 2; i < 10;i++) {
        BOOST_CHECK(feeEst.estimateFee(i * kTimeScale).GetFeePerK() < origFeeEst[i-1] + deltaFee);
        BOOST_CHECK(feeEst.estimateFee(i * kTimeScale).GetFeePerK() > origFeeEst[i-1] - deltaFee);
    }


    // Mine 150 more blocks with lots of transactions happening and not getting mined
    // Estimates should go up
    while (blocknum < kUnminedEnd) {
        for (int j = 0; j < kFeeLevels; j++) { // For each fee multiple
            for (int k = 0; k < 4; k++) { // add 4 fee txs
                tx.vin[0].prevout.n = 10000*blocknum+100*j+k;
                uint256 hash = tx.GetHash();
                mpool.addUnchecked(entry.Fee(feeV[j]).Time(Now<NodeSeconds>()).Height(blocknum).FromTx(tx));
                txHashes[j].push_back(hash);
            }
        }
        mpool.removeForBlock(block, ++blocknum);
    }

    for (int i = 1; i < 10;i++) {
        BOOST_CHECK(feeEst.estimateFee(i * kTimeScale) == CFeeRate(0) || feeEst.estimateFee(i * kTimeScale).GetFeePerK() > origFeeEst[i-1] - deltaFee);
    }

    // Mine all those transactions
    // Estimates should still not be below original
    for (int j = 0; j < kFeeLevels; j++) {
        include_level(j);
    }
    mpool.removeForBlock(block, kUnminedEnd + 1);
    block.clear();
    BOOST_CHECK(feeEst.estimateFee(1) == CFeeRate(0));
    for (int i = 2; i < 10;i++) {
        BOOST_CHECK(feeEst.estimateFee(i * kTimeScale) == CFeeRate(0) || feeEst.estimateFee(i * kTimeScale).GetFeePerK() > origFeeEst[i-1] - deltaFee);
    }

    // Mine 4000 more blocks where everything is mined every block
    // Estimates should be below original estimates (≈2.8 medium half-lives)
    while (blocknum < kAllMinedEnd) {
        for (int j = 0; j < kFeeLevels; j++) { // For each fee multiple
            for (int k = 0; k < 4; k++) { // add 4 fee txs
                tx.vin[0].prevout.n = 10000*blocknum+100*j+k;
                uint256 hash = tx.GetHash();
                mpool.addUnchecked(entry.Fee(feeV[j]).Time(Now<NodeSeconds>()).Height(blocknum).FromTx(tx));
                CTransactionRef ptx = mpool.get(hash);
                if (ptx)
                    block.push_back(ptx);

            }
        }
        mpool.removeForBlock(block, ++blocknum);
        block.clear();
    }
    BOOST_CHECK(feeEst.estimateFee(1) == CFeeRate(0));
    for (int i = 2; i < 9; i++) { // At 90, the original estimate was already at the bottom (b/c scale = 20)
        BOOST_CHECK(feeEst.estimateFee(i * kTimeScale).GetFeePerK() < origFeeEst[i-1] - deltaFee);
    }
}

BOOST_AUTO_TEST_SUITE_END()
