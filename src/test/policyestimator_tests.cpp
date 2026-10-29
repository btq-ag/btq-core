// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <serialize.h>
#include <streams.h>
#include <test/util/txmempool.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/serfloat.h>
#include <util/time.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>

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
    auto mine_epoch = [&](const std::vector<CTransactionRef>& transactions, int epoch) EXCLUSIVE_LOCKS_REQUIRED(mpool.cs) {
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
    // Snapshot before the format-marker loop below overwrites the first int.
    // Patching scale in that overwritten file would reject on the marker and
    // never reach the horizon check.
    std::vector<unsigned char> raw;
    unsigned char buf[4096];
    size_t nread{0};
    while ((nread = std::fread(buf, 1, sizeof(buf), cache.Get())) > 0) {
        raw.insert(raw.end(), buf, buf + nread);
    }
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

    // Decode a valid format-3 file so every rejection case is otherwise
    // well-formed and reaches the horizon compatibility checks.
    CDataStream cursor{MakeByteSpan(raw), SER_DISK, CLIENT_VERSION};
    int file_format, file_version;
    unsigned int best, hist_first, hist_best;
    cursor >> file_format >> file_version >> best >> hist_first >> hist_best;
    BOOST_REQUIRE_EQUAL(file_format, 3);
    std::vector<uint64_t> buckets;
    cursor >> buckets;
    const size_t header_size{raw.size() - cursor.size()};
    // Encoded doubles are serialized as little-endian uint64_t values.
    struct FileHorizon {
        uint64_t decay;
        unsigned int scale;
        std::vector<uint64_t> feerate_avg, tx_count_avg;
        std::vector<std::vector<uint64_t>> conf_avg, fail_avg;
    };
    std::array<FileHorizon, 3> horizons;
    for (auto& horizon : horizons) {
        cursor >> horizon.decay >> horizon.scale >> horizon.feerate_avg
               >> horizon.tx_count_avg >> horizon.conf_avg >> horizon.fail_avg;
    }
    BOOST_REQUIRE(cursor.empty());
    const std::array<std::pair<int, FeeEstimateHorizon>, 3> raw_targets{{
        {2, FeeEstimateHorizon::SHORT_HALFLIFE},
        {20, FeeEstimateHorizon::MED_HALFLIFE},
        {240, FeeEstimateHorizon::LONG_HALFLIFE},
    }};
    std::array<CAmount, 3> raw_estimates;
    for (size_t i = 0; i < raw_targets.size(); ++i) {
        raw_estimates[i] = restored.estimateRawFee(raw_targets[i].first, 0.85, raw_targets[i].second).GetFeePerK();
        BOOST_REQUIRE_GT(raw_estimates[i], 0);
    }
    auto check_file = [&](const auto& windows, bool accepted) {
        CDataStream bytes{SER_DISK, CLIENT_VERSION};
        bytes.write(MakeByteSpan(raw).first(header_size));
        for (const auto& horizon : windows) {
            auto fee_avg{horizon.feerate_avg};
            if (!accepted) {
                // If a rejected file partially replaced live state, these
                // changed averages would alter the restored estimate.
                for (auto& encoded : fee_avg) encoded = EncodeDouble(DecodeDouble(encoded) * 2);
            }
            bytes << horizon.decay << horizon.scale << fee_avg
                  << horizon.tx_count_avg << horizon.conf_avg << horizon.fail_avg;
        }
        AutoFile file{std::tmpfile()};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_REQUIRE_EQUAL(std::fwrite(bytes.data(), 1, bytes.size(), file.Get()), bytes.size());
        std::rewind(file.Get());
        BOOST_CHECK_EQUAL(restored.Read(file), accepted);
        BOOST_CHECK_EQUAL(restored.estimateSmartFee(2, nullptr, false).GetFeePerK(), estimate.GetFeePerK());
        for (size_t i = 0; i < raw_targets.size(); ++i) {
            BOOST_CHECK_EQUAL(restored.estimateRawFee(raw_targets[i].first, 0.85, raw_targets[i].second).GetFeePerK(), raw_estimates[i]);
        }
    };
    // Positive control: decoding and rebuilding must not corrupt the fixture.
    check_file(horizons, true);
    for (size_t i = 0; i < horizons.size(); ++i) {
        BOOST_TEST_CONTEXT("horizon " << i) {
            auto changed{horizons};
            changed[i].decay = EncodeDouble(DecodeDouble(changed[i].decay) * 0.99); // Still in (0, 1).
            check_file(changed, false);
            changed = horizons;
            // Stay inside the old 10080-confirm sanity limit, including long.
            changed[i].scale = changed[i].scale == 1 ? 2 : changed[i].scale - 1;
            check_file(changed, false);
            changed = horizons;
            changed[i].conf_avg.pop_back();
            changed[i].fail_avg.pop_back();
            check_file(changed, false);
        }
    }
    // The old short geometry is internally valid and spans the same 120
    // blocks, but ten-block buckets cannot answer a two-block target.
    BOOST_REQUIRE_EQUAL(horizons[1].scale, 1U);
    BOOST_REQUIRE_EQUAL(horizons[1].conf_avg.size(), 120U);
    auto old_short{horizons};
    old_short[1].scale = 10;
    old_short[1].conf_avg.resize(12);
    old_short[1].fail_avg.resize(12);
    check_file(old_short, false);
}

namespace {
void AddFeeTx(CTxMemPool& pool, CMutableTransaction& tx, TestMemPoolEntryHelper& entry,
              CAmount fee, unsigned int height, uint32_t& unique, std::vector<CTransactionRef>* keep)
    EXCLUSIVE_LOCKS_REQUIRED(pool.cs, cs_main)
{
    tx.vin[0].prevout.n = ++unique;
    pool.addUnchecked(entry.Fee(fee).Time(Now<NodeSeconds>()).Height(height).FromTx(tx));
    if (keep) keep->push_back(pool.get(tx.GetHash()));
}

void Mine(CTxMemPool& pool, const std::vector<CTransactionRef>& txs, unsigned int height)
    EXCLUSIVE_LOCKS_REQUIRED(pool.cs)
{
    pool.removeForBlock(txs, height);
}
} // namespace

BOOST_AUTO_TEST_CASE(UnconfirmedCountsDoNotAliasBeforeTheWindowFills)
{
    CBlockPolicyEstimator& estimator = *Assert(m_node.fee_estimator);
    CTxMemPool& pool = *Assert(m_node.mempool);
    LOCK2(cs_main, pool.cs);
    TestMemPoolEntryHelper entry;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(128, 'X');
    const CAmount fee{5000};
    uint32_t unique{0};
    constexpr unsigned int tip{20};
    constexpr int stuck_count{5};
    std::vector<CTransactionRef> stuck;
    for (unsigned int height = 0; height < tip; ++height) {
        if (height == 10) {
            for (int i = 0; i < stuck_count; ++i) {
                AddFeeTx(pool, tx, entry, fee, height, unique, &stuck);
            }
        }
        std::vector<CTransactionRef> block;
        for (int i = 0; i < 30; ++i) {
            AddFeeTx(pool, tx, entry, fee, height, unique, &block);
        }
        Mine(pool, block, height + 1);
    }
    // Height 20, five txs still unconfirmed since height 10. The short
    // horizon's circular buffer is 120 entries. Without the tip check,
    // confirmation distance 26 aliases distance 10 and counts them twice.
    EstimationResult result;
    const auto rate = estimator.estimateRawFee(2, 0.85, FeeEstimateHorizon::SHORT_HALFLIFE, &result);
    BOOST_CHECK(rate.GetFeePerK() > 0);
    BOOST_CHECK_EQUAL(result.pass.inMempool, stuck_count);
}

BOOST_AUTO_TEST_CASE(UnminedRemovalFollowsScale)
{
    CBlockPolicyEstimator& estimator = *Assert(m_node.fee_estimator);
    CTxMemPool& pool = *Assert(m_node.mempool);
    LOCK2(cs_main, pool.cs);
    TestMemPoolEntryHelper entry;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(128, 'X');
    uint32_t unique{0};
    unsigned int tip{0};

    auto confirm_cohort = [&](CAmount fee, int count) EXCLUSIVE_LOCKS_REQUIRED(pool.cs, cs_main) {
        std::vector<CTransactionRef> block;
        for (int i = 0; i < count; ++i) {
            AddFeeTx(pool, tx, entry, fee, tip, unique, &block);
        }
        Mine(pool, block, ++tip);
    };
    auto remove_after = [&](CAmount fee, unsigned int blocks_later, int count) EXCLUSIVE_LOCKS_REQUIRED(pool.cs, cs_main) {
        std::vector<CTransactionRef> held;
        const unsigned int entry_height{tip};
        for (int i = 0; i < count; ++i) {
            AddFeeTx(pool, tx, entry, fee, entry_height, unique, &held);
        }
        for (unsigned int step = 0; step < blocks_later; ++step) {
            Mine(pool, {}, ++tip);
        }
        for (const auto& ref : held) {
            BOOST_REQUIRE(ref);
            pool.removeRecursive(*ref, MemPoolRemovalReason::REPLACED);
        }
    };

    // Short horizon scale is 1. A removal after one block fails a 1-block
    // target and not a 2-block target. A removal after two blocks fails both.
    const CAmount short_fee{5000};
    confirm_cohort(short_fee, 50);
    remove_after(short_fee, 1, 40);
    EstimationResult one_block;
    const auto one_rate = estimator.estimateRawFee(1, 0.85, FeeEstimateHorizon::SHORT_HALFLIFE, &one_block);
    // 50 confirms against 40 failures is under 85%. The failure has to land on
    // the 1-block target, which reads failAvg[0].
    BOOST_CHECK_EQUAL(one_rate.GetFeePerK(), 0);
    BOOST_CHECK_GT(one_block.fail.leftMempool, 20);
    EstimationResult two_block;
    const auto two_rate = estimator.estimateRawFee(2, 0.85, FeeEstimateHorizon::SHORT_HALFLIFE, &two_block);
    BOOST_CHECK(two_rate.GetFeePerK() > 0);
    BOOST_CHECK_EQUAL(two_block.pass.leftMempool, 0);
    BOOST_CHECK_EQUAL(two_block.fail.leftMempool, 0);

    remove_after(short_fee, 2, 40);
    EstimationResult after_two;
    const auto after_two_rate = estimator.estimateRawFee(2, 0.85, FeeEstimateHorizon::SHORT_HALFLIFE, &after_two);
    BOOST_CHECK_EQUAL(after_two_rate.GetFeePerK(), 0);
    BOOST_CHECK_GT(after_two.fail.leftMempool, 20);
}

BOOST_AUTO_TEST_CASE(UnminedRemovalMediumIgnoresPartialPeriod)
{
    // Own fixture, so a higher feerate from another case cannot answer the
    // estimate and hide whether this cohort was counted.
    CBlockPolicyEstimator& estimator = *Assert(m_node.fee_estimator);
    CTxMemPool& pool = *Assert(m_node.mempool);
    LOCK2(cs_main, pool.cs);
    TestMemPoolEntryHelper entry;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(128, 'X');
    uint32_t unique{0};
    unsigned int tip{0};
    const CAmount fee{5000};

    auto confirm_cohort = [&](int count) EXCLUSIVE_LOCKS_REQUIRED(pool.cs, cs_main) {
        std::vector<CTransactionRef> block;
        for (int i = 0; i < count; ++i) {
            AddFeeTx(pool, tx, entry, fee, tip, unique, &block);
        }
        Mine(pool, block, ++tip);
    };
    auto remove_after = [&](unsigned int blocks_later, int count) EXCLUSIVE_LOCKS_REQUIRED(pool.cs, cs_main) {
        std::vector<CTransactionRef> held;
        const unsigned int entry_height{tip};
        for (int i = 0; i < count; ++i) {
            AddFeeTx(pool, tx, entry, fee, entry_height, unique, &held);
        }
        for (unsigned int step = 0; step < blocks_later; ++step) {
            Mine(pool, {}, ++tip);
        }
        for (const auto& ref : held) {
            BOOST_REQUIRE(ref);
            pool.removeRecursive(*ref, MemPoolRemovalReason::REPLACED);
        }
    };

    // Medium scale is 20. Nineteen blocks is not a period. Twenty is.
    // 40 confirms stay above the ~20.8 sufficiency floor after 19 decays.
    confirm_cohort(40);
    remove_after(19, 80);
    EstimationResult under_scale;
    const auto under_rate = estimator.estimateRawFee(1, 0.85, FeeEstimateHorizon::MED_HALFLIFE, &under_scale);
    BOOST_CHECK(under_rate.GetFeePerK() > 0);
    BOOST_CHECK_EQUAL(under_scale.pass.leftMempool, 0);
    BOOST_CHECK_EQUAL(under_scale.fail.leftMempool, 0);

    confirm_cohort(40);
    remove_after(20, 80);
    EstimationResult at_scale;
    const auto at_rate = estimator.estimateRawFee(1, 0.85, FeeEstimateHorizon::MED_HALFLIFE, &at_scale);
    BOOST_CHECK_EQUAL(at_rate.GetFeePerK(), 0);
    BOOST_CHECK_GT(at_scale.fail.leftMempool, 20);
}

BOOST_AUTO_TEST_SUITE_END()
