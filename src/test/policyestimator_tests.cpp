// Copyright (c) 2011-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/fees.h>
#include <policy/policy.h>
#include <streams.h>
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

    // Populate vectors of increasing fees
    for (int j = 0; j < 10; j++) {
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

    // Preserve the original arrival/confirmation distribution in wall-clock time:
    // one former 600-second block is ten BTQ blocks. Insert the same four
    // transactions per fee level at the start of each epoch, then mine nine
    // empty blocks before its scheduled confirmation block. This retains the
    // exact original fee-band assertions rather than relaxing their bounds.
    constexpr int TIME_SCALE{10};
    auto mine_epoch = [&](const std::vector<CTransactionRef>& transactions, int epoch) {
        const std::vector<CTransactionRef> empty;
        for (int offset = 1; offset < TIME_SCALE; ++offset) {
            mpool.removeForBlock(empty, (epoch - 1) * TIME_SCALE + offset);
        }
        mpool.removeForBlock(transactions, epoch * TIME_SCALE);
    };

    // Loop through 200 epochs
    // Four transactions per fee level per ten-block epoch is 0.4 per block,
    // 40 times the SUFFICIENT_FEETXS rate of 0.01 per block (the same ratio as
    // upstream's 4 per block against 0.1).
    while (blocknum < 200) {
        for (int j = 0; j < 10; j++) { // For each fee
            for (int k = 0; k < 4; k++) { // add 4 fee txs
                tx.vin[0].prevout.n = 10000*blocknum+100*j+k; // make transaction unique
                uint256 hash = tx.GetHash();
                mpool.addUnchecked(entry.Fee(feeV[j]).Time(Now<NodeSeconds>()).Height(blocknum * TIME_SCALE).FromTx(tx));
                txHashes[j].push_back(hash);
            }
        }
        //Create blocks where higher fee txs are included more often
        for (int h = 0; h <= blocknum%10; h++) {
            // 10/10 blocks add highest fee transactions
            // 9/10 blocks add 2nd highest and so on until ...
            // 1/10 blocks add lowest fee transactions
            while (txHashes[9-h].size()) {
                CTransactionRef ptx = mpool.get(txHashes[9-h].back());
                if (ptx)
                    block.push_back(ptx);
                txHashes[9-h].pop_back();
            }
        }
        mine_epoch(block, ++blocknum);
        block.clear();
        // Check after just a few txs that combining buckets works as expected
        if (blocknum == 3) {
            // At this point we should need to combine 3 buckets to get enough data points
            // So estimateFee(1) should fail and estimateFee(2) should return somewhere around
            // 9*baserate.  estimateFee(2) %'s are 100,100,90 = average 97%
            BOOST_CHECK(feeEst.estimateFee(1) == CFeeRate(0));
            BOOST_CHECK(feeEst.estimateFee(2 * TIME_SCALE).GetFeePerK() < 9*baseRate.GetFeePerK() + deltaFee);
            BOOST_CHECK(feeEst.estimateFee(2 * TIME_SCALE).GetFeePerK() > 9*baseRate.GetFeePerK() - deltaFee);
        }
    }

    std::vector<CAmount> origFeeEst;
    // Highest feerate is 10*baseRate and gets in all blocks,
    // second highest feerate is 9*baseRate and gets in 9/10 blocks = 90%,
    // third highest feerate is 8*base rate, and gets in 8/10 blocks = 80%,
    // so estimateFee(1) would return 10*baseRate but is hardcoded to return failure
    // Second highest feerate has 100% chance of being included by 2 blocks,
    // so estimateFee(2) should return 9*baseRate etc...
    for (int i = 1; i < 10;i++) {
        origFeeEst.push_back(feeEst.estimateFee(i * TIME_SCALE).GetFeePerK());
        if (i > 2) { // Fee estimates should be monotonically decreasing
            BOOST_CHECK(origFeeEst[i-1] <= origFeeEst[i-2]);
        }
        int mult = 11-i;
        if (i % 2 == 0) { // At scale 20, test logic is only correct for even ten-block targets
            BOOST_CHECK(origFeeEst[i-1] < mult*baseRate.GetFeePerK() + deltaFee);
            BOOST_CHECK(origFeeEst[i-1] > mult*baseRate.GetFeePerK() - deltaFee);
        }
    }
    // Fill out rest of the original estimates
    for (int i = 10; i <= 48; i++) {
        origFeeEst.push_back(feeEst.estimateFee(i * TIME_SCALE).GetFeePerK());
    }

    // Mine 50 more ten-block epochs with no transactions happening, estimates shouldn't change
    // We haven't decayed the moving average enough so we still have enough data points in every bucket
    while (blocknum < 250)
        mine_epoch(block, ++blocknum);

    BOOST_CHECK(feeEst.estimateFee(1) == CFeeRate(0));
    for (int i = 2; i < 10;i++) {
        BOOST_CHECK(feeEst.estimateFee(i * TIME_SCALE).GetFeePerK() < origFeeEst[i-1] + deltaFee);
        BOOST_CHECK(feeEst.estimateFee(i * TIME_SCALE).GetFeePerK() > origFeeEst[i-1] - deltaFee);
    }


    // Mine 15 more ten-block epochs with lots of transactions happening and not getting mined
    // Estimates should go up
    while (blocknum < 265) {
        for (int j = 0; j < 10; j++) { // For each fee multiple
            for (int k = 0; k < 4; k++) { // add 4 fee txs
                tx.vin[0].prevout.n = 10000*blocknum+100*j+k;
                uint256 hash = tx.GetHash();
                mpool.addUnchecked(entry.Fee(feeV[j]).Time(Now<NodeSeconds>()).Height(blocknum * TIME_SCALE).FromTx(tx));
                txHashes[j].push_back(hash);
            }
        }
        mine_epoch(block, ++blocknum);
    }

    for (int i = 1; i < 10;i++) {
        BOOST_CHECK(feeEst.estimateFee(i * TIME_SCALE) == CFeeRate(0) || feeEst.estimateFee(i * TIME_SCALE).GetFeePerK() > origFeeEst[i-1] - deltaFee);
    }

    // Mine all those transactions
    // Estimates should still not be below original
    for (int j = 0; j < 10; j++) {
        while(txHashes[j].size()) {
            CTransactionRef ptx = mpool.get(txHashes[j].back());
            if (ptx)
                block.push_back(ptx);
            txHashes[j].pop_back();
        }
    }
    mine_epoch(block, ++blocknum);
    block.clear();
    BOOST_CHECK(feeEst.estimateFee(1) == CFeeRate(0));
    for (int i = 2; i < 10;i++) {
        BOOST_CHECK(feeEst.estimateFee(i * TIME_SCALE) == CFeeRate(0) || feeEst.estimateFee(i * TIME_SCALE).GetFeePerK() > origFeeEst[i-1] - deltaFee);
    }

    // Mine 400 more epochs where everything is mined in the final block
    // Estimates should be below original estimates
    while (blocknum < 665) {
        for (int j = 0; j < 10; j++) { // For each fee multiple
            for (int k = 0; k < 4; k++) { // add 4 fee txs
                tx.vin[0].prevout.n = 10000*blocknum+100*j+k;
                uint256 hash = tx.GetHash();
                mpool.addUnchecked(entry.Fee(feeV[j]).Time(Now<NodeSeconds>()).Height(blocknum * TIME_SCALE).FromTx(tx));
                CTransactionRef ptx = mpool.get(hash);
                if (ptx)
                    block.push_back(ptx);

            }
        }
        mine_epoch(block, ++blocknum);
        block.clear();
    }
    BOOST_CHECK(feeEst.estimateFee(1) == CFeeRate(0));
    for (int i = 2; i < 9; i++) { // At 90 blocks, the original estimate was already at the bottom (scale = 20)
        BOOST_CHECK(feeEst.estimateFee(i * TIME_SCALE).GetFeePerK() < origFeeEst[i-1] - deltaFee);
    }
}

BOOST_AUTO_TEST_CASE(ShortTargetSeparatesSlowConfirmations)
{
    CBlockPolicyEstimator& estimator = *Assert(m_node.fee_estimator);
    CTxMemPool& pool = *Assert(m_node.mempool);
    LOCK2(cs_main, pool.cs);
    TestMemPoolEntryHelper entry;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(128, 'X');
    const CAmount low_fee{1000};
    const CAmount high_fee{10000};
    const CFeeRate high_rate(high_fee, GetVirtualTransactionSize(CTransaction(tx)));
    std::vector<CTransactionRef> delayed;
    uint32_t unique{0};
    for (unsigned int height = 0; height < 300; ++height) {
        std::vector<CTransactionRef> block;
        for (int sample = 0; sample < 40; ++sample) {
            for (const auto fee : {low_fee, high_fee}) {
                tx.vin[0].prevout.n = ++unique;
                pool.addUnchecked(entry.Fee(fee).Time(Now<NodeSeconds>()).Height(height).FromTx(tx));
                auto ref = pool.get(tx.GetHash());
                if (fee == low_fee) delayed.push_back(ref);
                else block.push_back(ref);
            }
        }
        // High fees confirm in one block. Low fees arrive throughout each
        // ten-block window and only 20% confirm within a two-block target.
        if ((height + 1) % 10 == 0) {
            block.insert(block.end(), delayed.begin(), delayed.end());
            delayed.clear();
        }
        pool.removeForBlock(block, height + 1);
    }
    FeeCalculation calculation;
    const auto estimate = estimator.estimateSmartFee(2, &calculation, false);
    BOOST_CHECK_EQUAL(calculation.returnedTarget, 2);
    BOOST_CHECK_GE(estimate.GetFeePerK(), high_rate.GetFeePerK() - 100);

    // Persistence must preserve the one-block resolution, not just the target
    // number. Reading an incompatible file must leave live estimates intact.
    AutoFile cache{std::tmpfile()};
    BOOST_REQUIRE(!cache.IsNull());
    BOOST_REQUIRE(estimator.Write(cache));
    std::rewind(cache.Get());
    int format;
    cache >> format;
    BOOST_CHECK_EQUAL(format, 3);
    std::rewind(cache.Get());
    CBlockPolicyEstimator restored{m_args.GetDataDirNet() / "unused_fee_cache", false};
    BOOST_REQUIRE(restored.Read(cache));
    FeeCalculation restored_calculation;
    BOOST_CHECK_EQUAL(restored.estimateSmartFee(2, &restored_calculation, false).GetFeePerK(), estimate.GetFeePerK());
    BOOST_CHECK_EQUAL(restored_calculation.returnedTarget, 2);
    for (const int old_format : {149900, 1, 2}) {
        std::rewind(cache.Get());
        cache << old_format;
        std::rewind(cache.Get());
        BOOST_CHECK(!restored.Read(cache));
        BOOST_CHECK_EQUAL(restored.estimateSmartFee(2, nullptr, false).GetFeePerK(), estimate.GetFeePerK());
    }
}

BOOST_AUTO_TEST_SUITE_END()
