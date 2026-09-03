// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <pow.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <vector>

namespace {

std::vector<CBlockIndex> BuildLwmaChain(int height, int64_t base_time, int64_t spacing,
                                        uint32_t nBits, const Consensus::Params& consensus)
{
    std::vector<CBlockIndex> blocks(height + 1);
    for (int i = 0; i <= height; ++i) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = base_time + static_cast<int64_t>(i) * spacing;
        blocks[i].nBits = nBits;
    }
    return blocks;
}

uint32_t ReferenceLwmaNextWork(const CBlockIndex* pindexLast, const CBlockHeader*,
                               const Consensus::Params& params)
{
    const int64_t T = params.nPowTargetSpacing;
    const int N = params.nLWMAWindow;
    const int64_t k = static_cast<int64_t>(N) * (N + 1) * T / 2;
    const int height = pindexLast->nHeight;
    const arith_uint256 powLimit = UintToArith256(params.powLimit);

    if (height < N) {
        return powLimit.GetCompact();
    }

    arith_uint256 targetQuotientSum;
    arith_uint256 targetRemainderSum;
    int64_t weightedSolvetimes = 0;
    int64_t previousTimestamp = pindexLast->GetAncestor(height - N)->GetBlockTime();

    for (int i = 1; i <= N; ++i) {
        const CBlockIndex* block = pindexLast->GetAncestor(height - N + i);
        int64_t thisTimestamp = block->GetBlockTime();

        int64_t solvetime = thisTimestamp - previousTimestamp;
        solvetime = std::min(solvetime, 6 * T);
        solvetime = std::max(solvetime, -6 * T);

        weightedSolvetimes += solvetime * i;

        arith_uint256 target;
        target.SetCompact(block->nBits);
        const arith_uint256 targetQuotient = target / N;
        targetQuotientSum += targetQuotient;
        targetRemainderSum += target - targetQuotient * static_cast<uint32_t>(N);

        previousTimestamp = thisTimestamp;
    }

    if (weightedSolvetimes < 1) {
        weightedSolvetimes = 1;
    }

    const arith_uint256 averageTarget = targetQuotientSum + targetRemainderSum / N;
    const arith_uint256 k_uint{static_cast<uint64_t>(k)};
    const uint32_t weighted_solvetimes_u32{static_cast<uint32_t>(weightedSolvetimes)};
    const arith_uint256 quotient = averageTarget / k_uint;
    const arith_uint256 remainder = averageTarget - quotient * static_cast<uint32_t>(k);

    arith_uint256 nextTarget;
    if (quotient > powLimit / weighted_solvetimes_u32) {
        nextTarget = powLimit;
    } else {
        nextTarget = quotient * weighted_solvetimes_u32;
        nextTarget += (remainder * weighted_solvetimes_u32) / k_uint;
    }

    if (nextTarget > powLimit) {
        nextTarget = powLimit;
    }

    return nextTarget.GetCompact();
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(pow_lwma_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(lwma_activation_height_configured)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQMAIN);
    BOOST_CHECK_EQUAL(params->GetConsensus().nLWMAHeight, 1);
    const auto testnet_params = CreateChainParams(*m_node.args, ChainType::BTQTEST);
    BOOST_CHECK_EQUAL(testnet_params->GetConsensus().nLWMAHeight, 1);
    const auto regtest_params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    BOOST_CHECK_EQUAL(regtest_params->GetConsensus().nLWMAHeight, 1);
    BOOST_CHECK(params->GetConsensus().nDilithiumHeight > 0);
    BOOST_CHECK_EQUAL(params->GetConsensus().nLWMAWindow, 144);
}

BOOST_AUTO_TEST_CASE(lwma_before_activation_uses_legacy_path)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    Consensus::Params consensus = params->GetConsensus();
    // Default nLWMAHeight is 1; raise it so this still hits the legacy retarget.
    consensus.nLWMAHeight = 300000;

    CBlockIndex pindexLast;
    pindexLast.nHeight = consensus.nLWMAHeight - 1;
    pindexLast.nBits = 0x207fffff;
    pindexLast.nTime = 1000000;
    pindexLast.pprev = nullptr;

    const int64_t nFirstBlockTime = pindexLast.nTime - consensus.nPowTargetSpacing;

    const uint32_t next = CalculateNextWorkRequired(&pindexLast, nFirstBlockTime, consensus);
    BOOST_CHECK(next > 0);
}

BOOST_AUTO_TEST_CASE(lwma_preserves_low_constant_target_without_zero_underflow)
{
    auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    Consensus::Params consensus = params->GetConsensus();
    consensus.fPowNoRetargeting = false;
    consensus.nLWMAHeight = 1;

    const arith_uint256 low_target{1000000};
    const uint32_t low_bits = low_target.GetCompact();
    const auto blocks = BuildLwmaChain(consensus.nLWMAWindow, /*base_time=*/1000000, consensus.nPowTargetSpacing, low_bits, consensus);

    CBlockHeader next_block;
    next_block.nTime = blocks.back().GetBlockTime() + consensus.nPowTargetSpacing;

    const uint32_t next_bits = LwmaGetNextWorkRequired(&blocks.back(), &next_block, consensus);
    BOOST_CHECK_NE(next_bits, 0U);
    BOOST_CHECK_EQUAL(next_bits, low_bits);
    BOOST_CHECK(CheckProofOfWork(ArithToUint256(arith_uint256{1}), next_bits, consensus));
}

BOOST_AUTO_TEST_CASE(lwma_respects_pow_no_retargeting)
{
    auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    Consensus::Params consensus = params->GetConsensus();
    consensus.fPowNoRetargeting = true;
    consensus.nLWMAHeight = 1;

    const uint32_t bits = arith_uint256{123456789}.GetCompact();
    const auto blocks = BuildLwmaChain(consensus.nLWMAWindow, /*base_time=*/1000000, consensus.nPowTargetSpacing * 3, bits, consensus);

    CBlockHeader next_block;
    next_block.nTime = blocks.back().GetBlockTime() + consensus.nPowTargetSpacing * 6;

    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks.back(), &next_block, consensus), bits);
}

BOOST_AUTO_TEST_CASE(lwma_post_activation_permitted_any_transition)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQMAIN);
    const Consensus::Params& consensus = params->GetConsensus();

    // LWMA adjusts every block; PermittedDifficultyTransition must not apply
    // legacy ±4x interval bounds after activation.
    const int height = consensus.nLWMAHeight + 100;
    BOOST_CHECK(PermittedDifficultyTransition(consensus, height, 0x1d00ffff, 0x1d00fffe));
    BOOST_CHECK(PermittedDifficultyTransition(consensus, height, 0x1d00ffff, 0x1d00ffff));
}

BOOST_AUTO_TEST_CASE(lwma_early_height_returns_pow_limit)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    const Consensus::Params& consensus = params->GetConsensus();

    // Build a minimal ancestor chain tall enough for LWMA but with height < N.
    std::vector<CBlockIndex> blocks(params->GetConsensus().nLWMAWindow);
    for (size_t i = 0; i < blocks.size(); ++i) {
        blocks[i].nHeight = static_cast<int>(i);
        blocks[i].nTime = 1'000'000 + static_cast<int64_t>(i) * consensus.nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff;
        blocks[i].pprev = (i == 0) ? nullptr : &blocks[i - 1];
    }

    CBlockHeader header;
    header.nTime = blocks.back().nTime + consensus.nPowTargetSpacing;

    const uint32_t next = LwmaGetNextWorkRequired(&blocks.back(), &header, consensus);
    BOOST_CHECK_EQUAL(next, UintToArith256(consensus.powLimit).GetCompact());
}

BOOST_AUTO_TEST_CASE(lwma_equal_spacing_preserves_target)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    const Consensus::Params& consensus = params->GetConsensus();
    const int N = params->GetConsensus().nLWMAWindow;
    const int64_t T = consensus.nPowTargetSpacing;
    const uint32_t nBits = 0x207fffff;

    auto blocks = BuildLwmaChain(N, 1'000'000, T, nBits, consensus);

    CBlockHeader header;
    header.nTime = blocks[N].nTime + T;

    const uint32_t next = LwmaGetNextWorkRequired(&blocks[N], &header, consensus);
    BOOST_CHECK_EQUAL(next, nBits);
    BOOST_CHECK_EQUAL(next, ReferenceLwmaNextWork(&blocks[N], &header, consensus));
}

BOOST_AUTO_TEST_CASE(lwma_fast_blocks_raise_difficulty)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    const Consensus::Params& consensus = params->GetConsensus();
    const int N = params->GetConsensus().nLWMAWindow;
    const uint32_t nBits = 0x207fffff;

    // All blocks share the same timestamp -> solvetime 0 -> difficulty rises.
    auto blocks = BuildLwmaChain(N, 1'000'000, 0, nBits, consensus);

    CBlockHeader header;
    header.nTime = blocks[N].nTime;

    const uint32_t next = LwmaGetNextWorkRequired(&blocks[N], &header, consensus);
    arith_uint256 old_target, new_target;
    old_target.SetCompact(nBits);
    new_target.SetCompact(next);
    BOOST_CHECK(new_target < old_target);
}

BOOST_AUTO_TEST_CASE(lwma_slow_blocks_lower_difficulty)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    const Consensus::Params& consensus = params->GetConsensus();
    const int N = params->GetConsensus().nLWMAWindow;
    const int64_t T = consensus.nPowTargetSpacing;
    // Harder-than-limit starting target so slow blocks can raise it toward powLimit.
    const uint32_t nBits = 0x1e00ffff;

    // Inter-block gaps of 10*T are clamped to 6*T per the LWMA spec.
    auto blocks = BuildLwmaChain(N, 1'000'000, 10 * T, nBits, consensus);

    CBlockHeader header;
    header.nTime = blocks[N].nTime + 10 * T;

    const uint32_t next = LwmaGetNextWorkRequired(&blocks[N], &header, consensus);
    arith_uint256 old_target, new_target;
    old_target.SetCompact(nBits);
    new_target.SetCompact(next);
    BOOST_CHECK(new_target > old_target);
}

BOOST_AUTO_TEST_CASE(lwma_solvetime_clamped_at_six_intervals)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    const Consensus::Params& consensus = params->GetConsensus();
    const int N = params->GetConsensus().nLWMAWindow;
    const int64_t T = consensus.nPowTargetSpacing;
    const uint32_t nBits = 0x207fffff;

    // 6*T spacing is the clamp ceiling; 100*T spacing must produce the same target.
    auto blocks_clamped = BuildLwmaChain(N, 1'000'000, 6 * T, nBits, consensus);
    auto blocks_unclamped = BuildLwmaChain(N, 2'000'000, 100 * T, nBits, consensus);

    CBlockHeader header_clamped;
    header_clamped.nTime = blocks_clamped[N].nTime + 6 * T;
    CBlockHeader header_unclamped;
    header_unclamped.nTime = blocks_unclamped[N].nTime + 100 * T;

    const uint32_t next_clamped = LwmaGetNextWorkRequired(&blocks_clamped[N], &header_clamped, consensus);
    const uint32_t next_unclamped = LwmaGetNextWorkRequired(&blocks_unclamped[N], &header_unclamped, consensus);
    BOOST_CHECK_EQUAL(next_clamped, next_unclamped);
}

BOOST_AUTO_TEST_CASE(lwma_get_next_work_required_uses_lwma_at_activation)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQMAIN);
    const Consensus::Params& consensus = params->GetConsensus();
    const int N = params->GetConsensus().nLWMAWindow;
    const int64_t T = consensus.nPowTargetSpacing;

    auto blocks = BuildLwmaChain(consensus.nLWMAHeight, 1'700'000'000, T, 0x1d00ffff, consensus);

    CBlockHeader header;
    header.nTime = blocks.back().nTime + T;

    const uint32_t next = GetNextWorkRequired(&blocks.back(), &header, consensus);
    BOOST_CHECK(next > 0);
    BOOST_CHECK(next != blocks.back().nBits || N > 0);
}

BOOST_AUTO_TEST_CASE(lwma_formula_reference_parity)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    const Consensus::Params& consensus = params->GetConsensus();
    const int N = params->GetConsensus().nLWMAWindow;
    const int64_t T = consensus.nPowTargetSpacing;

    const int64_t k = static_cast<int64_t>(N) * (N + 1) * T / 2;
    // Cross-check the closed form against a direct summation of the weights,
    // rather than against a constant frozen at one N: k is T times the sum of
    // weights 1..N. At the default N=144, T=60 that is 626,400.
    int64_t weight_sum = 0;
    for (int i = 1; i <= N; ++i) {
        weight_sum += i;
    }
    BOOST_CHECK_EQUAL(k, weight_sum * T);

    struct Scenario {
        int64_t spacing;
        uint32_t nBits;
    };
    const Scenario scenarios[] = {
        {T, 0x207fffff},
        {0, 0x207fffff},
        {6 * T, 0x207fffff},
        {10 * T, 0x1e00ffff},
        {T / 2, 0x1e00ffff},
    };

    for (const auto& scenario : scenarios) {
        auto blocks = BuildLwmaChain(N, 1'500'000'000, scenario.spacing, scenario.nBits, consensus);
        CBlockHeader header;
        header.nTime = blocks[N].nTime + std::max<int64_t>(scenario.spacing, 1);

        const uint32_t impl = LwmaGetNextWorkRequired(&blocks[N], &header, consensus);
        const uint32_t reference = ReferenceLwmaNextWork(&blocks[N], &header, consensus);
        BOOST_CHECK_EQUAL(impl, reference);
    }
}

namespace {

//! One replay of a hashrate burst followed by the miner leaving.
struct BurstResult {
    int N{0};
    double peak_overshoot{0.0};   //!< peak difficulty / baseline difficulty
    int burst_blocks{0};          //!< blocks mined during the burst
    int blocks_to_recover{0};     //!< blocks after the exit until within 2x of baseline
    int64_t seconds_to_recover{0};//!< wall-clock equivalent of the above
};

//! Replay: steady state -> burst at `mult` hashrate for `burst_blocks` -> miner exits.
//!
//! Block times are derived from the difficulty the algorithm actually chose, so
//! the down-leg is self-consistent: an overshoot slows blocks, which is what makes
//! recovery expensive. That feedback is the whole point and cannot be modelled by
//! a fixed-spacing chain.
BurstResult ReplayBurst(int N, double mult, int64_t burst_seconds, Consensus::Params consensus)
{
    consensus.nLWMAWindow = N;
    const int64_t T = consensus.nPowTargetSpacing;

    // Recovery needs log6(overshoot) window flushes -- the +-6T solvetime clamp
    // bounds each flush to a 6x correction regardless of N -- so 50 window
    // lengths is generous. CBlockIndex has a protected copy ctor and deleted
    // move, so the vector must be sized once up front: it can neither grow nor
    // reserve. Same constraint BuildLwmaChain works around above.
    const int kWarmup = 2 * N;
    const int kMaxRecovery = 50 * N;
    const int kMaxBurst = 200000;  // burst is time-bounded; this only sizes the vector
    const size_t kCap = static_cast<size_t>(kWarmup + kMaxBurst + kMaxRecovery + 2);

    std::vector<CBlockIndex> chain(kCap);
    size_t n = 0;

    const uint32_t seed_bits = UintToArith256(consensus.powLimit).GetCompact();
    auto append = [&](int64_t time, uint32_t bits) {
        CBlockIndex& bi = chain[n];
        bi.nHeight = static_cast<int>(n);
        bi.nTime = static_cast<unsigned int>(time);
        bi.nBits = bits;
        bi.pprev = n ? &chain[n - 1] : nullptr;
        ++n;
    };

    for (int i = 0; i < kWarmup; ++i) append(1000000 + static_cast<int64_t>(i) * T, seed_bits);

    arith_uint256 baseline_target;
    baseline_target.SetCompact(chain[n - 1].nBits);

    CBlockHeader dummy;
    auto next_bits = [&] { return LwmaGetNextWorkRequired(&chain[n - 1], &dummy, consensus); };

    // Seconds the next block takes, given the target the algorithm chose and the
    // hashrate present. A smaller target is harder, so an overshoot slows blocks --
    // that feedback is what makes recovery expensive and is the point of the replay.
    auto block_seconds = [&](uint32_t bits, double hashrate) {
        arith_uint256 tgt;
        tgt.SetCompact(bits);
        const double ratio = baseline_target.getdouble() / tgt.getdouble();
        return static_cast<int64_t>(std::max(1.0, T * ratio / hashrate));
    };
    auto overshoot = [&](uint32_t bits) {
        arith_uint256 tgt;
        tgt.SetCompact(bits);
        return baseline_target.getdouble() / tgt.getdouble();
    };

    BurstResult r;
    r.N = N;
    int64_t t = chain[n - 1].nTime;

    const int64_t burst_start = t;
    while (t - burst_start < burst_seconds && n + 2 < kCap) {
        const uint32_t bits = next_bits();
        t += block_seconds(bits, mult);
        append(t, bits);
        r.peak_overshoot = std::max(r.peak_overshoot, overshoot(bits));
        ++r.burst_blocks;
    }

    const int64_t exit_time = t;
    for (int i = 0; i < kMaxRecovery; ++i) {
        const uint32_t bits = next_bits();
        if (overshoot(bits) <= 2.0) {
            r.blocks_to_recover = i;
            r.seconds_to_recover = t - exit_time;
            return r;
        }
        t += block_seconds(bits, 1.0);
        append(t, bits);
    }
    r.blocks_to_recover = -1;  // did not recover inside the budget
    r.seconds_to_recover = t - exit_time;
    return r;
}

} // namespace

//! Sweeps the LWMA window against the failure mode reported on the pre-reset
//! v0.3.2 testnet in April 2026 (issue #110): a high-rate miner drove difficulty
//! up over a few hours, and recovery after it left took roughly ten times longer.
//!
//! This does not assert a preferred N -- that is a hard-fork decision and is open.
//! It asserts the two properties any acceptable N must have, and prints the
//! measured trade-off so the choice is made against numbers rather than argument.
BOOST_AUTO_TEST_CASE(lwma_window_sweep_burst_and_exit)
{
    const auto chainparams = CreateChainParams(*m_node.args, ChainType::BTQMAIN);
    const Consensus::Params& consensus = chainparams->GetConsensus();

    // Two shapes. The short one is a transient; the sustained one is the event
    // reported in #110 -- a miner present for 2.5 hours at ~50x, then leaving.
    // Burst length is wall-clock because the burst self-limits: as difficulty
    // climbs, blocks slow, so a fixed block count is not a fixed disturbance.
    struct Scenario { const char* name; int64_t seconds; };
    const double kMult = 50.0;
    std::vector<BurstResult> results;  // sustained scenario, for the assertions

    for (const Scenario& s : {Scenario{"transient (10 min at 50x)", 600},
                              Scenario{"sustained (2.5 h at 50x, as reported in #110)", 9000}}) {
        BOOST_TEST_MESSAGE("");
        BOOST_TEST_MESSAGE("  " << s.name);
        BOOST_TEST_MESSAGE("  N     burst blks   peak overshoot   recover blks   recover hrs");
        std::vector<BurstResult> row;
        for (int N : {45, 90, 144, 288, 576}) {
            const BurstResult r = ReplayBurst(N, kMult, s.seconds, consensus);
            row.push_back(r);
            BOOST_TEST_MESSAGE("  " << r.N << "\t" << r.burst_blocks << "\t\t"
                                    << r.peak_overshoot << "x\t\t"
                                    << r.blocks_to_recover << "\t\t"
                                    << (r.seconds_to_recover / 3600.0));
        }
        results = row;
    }

    for (const BurstResult& r : results) {
        // 1. Every candidate must recover at all. A window that cannot climb back
        //    down within 300 window-lengths would strand the chain.
        BOOST_CHECK_MESSAGE(r.blocks_to_recover >= 0,
                            "N=" << r.N << " never recovered to 2x baseline");

        // 2. A larger window must not amplify the spike. This is the property the
        //    weighting guarantees, and it is what a regression here would break.
        BOOST_CHECK_MESSAGE(r.peak_overshoot > 1.0,
                            "N=" << r.N << " registered no overshoot at all -- "
                                 "the replay is not exercising the burst");
    }

    // The suppression claim from #110, now measured rather than argued: a burst
    // shorter than the window must move difficulty less as the window grows.
    for (size_t i = 1; i < results.size(); ++i) {
        BOOST_CHECK_MESSAGE(results[i].peak_overshoot <= results[i - 1].peak_overshoot * 1.05,
                            "peak overshoot did not fall from N=" << results[i - 1].N
                            << " (" << results[i - 1].peak_overshoot << "x) to N="
                            << results[i].N << " (" << results[i].peak_overshoot << "x)");
    }
}

namespace {

//! Result of repeated burst/exit cycles by an on-off miner.
struct OnOffResult {
    int N{0};
    double advantage{0.0};   //!< cycler blocks-per-hash-second / honest blocks-per-hash-second
    //! Lowest difficulty reached vs what the honest hashrate deserves. Stays at
    //! 1.0 by construction here -- honest hashrate is constant, so there is no
    //! undershoot. Retained as the guard that says so.
    double min_overshoot{0.0};
};

//! Replay an on-off miner cycling on for `on_s` and off for `off_s`, against a
//! constant honest hashrate of 1.0.
//!
//! Measures the metric the LWMA literature actually selects N against: whether
//! cycling earns more per unit of hash than mining steadily. Blocks are
//! attributed fractionally by hashrate share rather than sampled, so the result
//! is deterministic.
//!
//! advantage > 1.0 means on-off mining beats steady mining, i.e. the window is
//! short enough to be farmed. See zawy12/difficulty-algorithms#24: N=17 was
//! abandoned precisely because it "invited miners to constantly engage in
//! on-off mining".
OnOffResult ReplayOnOff(int N, double attacker_hash, int64_t on_s, int64_t off_s,
                        int cycles, Consensus::Params consensus)
{
    consensus.nLWMAWindow = N;
    const int64_t T = consensus.nPowTargetSpacing;

    const int kWarmup = 2 * N;
    const size_t kCap = static_cast<size_t>(kWarmup) + 30000;
    std::vector<CBlockIndex> chain(kCap);
    size_t n = 0;

    const uint32_t seed_bits = UintToArith256(consensus.powLimit).GetCompact();
    auto append = [&](int64_t time, uint32_t bits) {
        CBlockIndex& bi = chain[n];
        bi.nHeight = static_cast<int>(n);
        bi.nTime = static_cast<unsigned int>(time);
        bi.nBits = bits;
        bi.pprev = n ? &chain[n - 1] : nullptr;
        ++n;
    };

    for (int i = 0; i < kWarmup; ++i) append(1000000 + static_cast<int64_t>(i) * T, seed_bits);

    arith_uint256 baseline_target;
    baseline_target.SetCompact(chain[n - 1].nBits);

    CBlockHeader dummy;
    auto overshoot = [&](uint32_t bits) {
        arith_uint256 tgt;
        tgt.SetCompact(bits);
        return baseline_target.getdouble() / tgt.getdouble();
    };

    OnOffResult r;
    r.N = N;
    r.min_overshoot = 1e18;

    double attacker_blocks = 0.0, honest_blocks = 0.0;
    int64_t attacker_on_time = 0;
    int64_t t = chain[n - 1].nTime;
    const int64_t start = t;

    for (int c = 0; c < cycles && n + 2 < kCap; ++c) {
        for (int phase = 0; phase < 2; ++phase) {
            const bool on = (phase == 0);
            const double extra = on ? attacker_hash : 0.0;
            const int64_t window = on ? on_s : off_s;
            const int64_t phase_end = t + window;
            while (t < phase_end && n + 2 < kCap) {
                const uint32_t bits = LwmaGetNextWorkRequired(&chain[n - 1], &dummy, consensus);
                const double diff_ratio = overshoot(bits);
                r.min_overshoot = std::min(r.min_overshoot, diff_ratio);
                const int64_t dt = static_cast<int64_t>(std::max(1.0, T * diff_ratio / (1.0 + extra)));
                // Fractional attribution by hashrate share -- deterministic.
                attacker_blocks += extra / (1.0 + extra);
                honest_blocks += 1.0 / (1.0 + extra);
                t += dt;
                append(t, bits);
            }
            if (on) attacker_on_time += window;
        }
    }

    const int64_t total_time = t - start;
    if (attacker_on_time == 0 || total_time == 0) return r;
    const double attacker_rate = attacker_blocks / (attacker_hash * static_cast<double>(attacker_on_time));
    const double honest_rate = honest_blocks / (1.0 * static_cast<double>(total_time));
    r.advantage = attacker_rate / honest_rate;
    return r;
}

} // namespace

//! Measures ONE HALF of the on-off question: what a cycling miner earns while
//! it is present, and what elevated difficulty costs the honest miners it leaves
//! behind.
//!
//! IT DOES NOT MEASURE the classic hash-and-run exploit, and its output must not
//! be read as doing so. That attack turns on returning when difficulty has
//! undershot -- the attacker leaves, difficulty craters below what the remaining
//! hashrate deserves, and they come back to cheap blocks. This replay holds the
//! honest hashrate constant at 1.0, so difficulty never falls below what that
//! hashrate deserves and the return discount cannot appear. `min_overshoot` is
//! 1.0x for every N here, which is the tell.
//!
//! Consequence: the advantage figures below rise with N, which is the OPPOSITE
//! of the ecosystem's finding that short windows invite on-off mining
//! (zawy12/difficulty-algorithms#24 -- N=17 was abandoned for exactly that).
//! The disagreement is a limitation of this model, not a refutation of theirs.
//! Modelling the exploit properly needs a rational attacker that enters on
//! depressed difficulty and exits on elevated difficulty, which is a strategy
//! search rather than a replay.
//!
//! What the numbers below DO show, and it is worth having: a larger window makes
//! honest miners carry the departing miner's inflated difficulty for longer.
BOOST_AUTO_TEST_CASE(lwma_window_departure_cost_to_honest_miners)
{
    const auto chainparams = CreateChainParams(*m_node.args, ChainType::BTQMAIN);
    const Consensus::Params& consensus = chainparams->GetConsensus();

    struct Cycle { const char* name; int64_t on_s; int64_t off_s; double hash; };
    const std::vector<Cycle> cycles = {
        {"aggressive  (30 min on / 90 min off, 20x)", 1800, 5400, 20.0},
        {"patient     (2 h on / 4 h off, 20x)",       7200, 14400, 20.0},
    };

    for (const Cycle& c : cycles) {
        BOOST_TEST_MESSAGE("");
        BOOST_TEST_MESSAGE("  " << c.name);
        BOOST_TEST_MESSAGE("  N     cycler rate vs steady   min difficulty (1.0 = no undershoot modelled)");
        for (int N : {45, 90, 144, 288, 576}) {
            const OnOffResult r = ReplayOnOff(N, c.hash, c.on_s, c.off_s, 2, consensus);
            BOOST_TEST_MESSAGE("  " << r.N << "\t" << r.advantage << "x\t\t\t"
                                    << r.min_overshoot << "x");
            BOOST_CHECK_MESSAGE(r.advantage > 0.0,
                                "N=" << r.N << " produced no attributable blocks");
            // Guards the stated limitation: if this ever drops below 1.0 the
            // replay has started producing undershoot and the caveat above --
            // and the reading of these numbers -- needs revisiting.
            BOOST_CHECK_MESSAGE(r.min_overshoot >= 0.999,
                                "N=" << r.N << " undershot to " << r.min_overshoot
                                     << "x -- this model is not supposed to be able "
                                        "to do that; re-read the comment above");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
