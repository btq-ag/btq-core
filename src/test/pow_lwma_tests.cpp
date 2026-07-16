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
    const int N = Consensus::Params::LWMA_WINDOW;
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
    // Mainnet activates LWMA from block 1 (BTQ-AUDIT-103); the live testnet
    // keeps its scheduled height so existing history stays valid.
    BOOST_CHECK_EQUAL(params->GetConsensus().nLWMAHeight, 1);
    const auto testnet_params = CreateChainParams(*m_node.args, ChainType::BTQTEST);
    BOOST_CHECK_EQUAL(testnet_params->GetConsensus().nLWMAHeight, 300000);
    BOOST_CHECK(params->GetConsensus().nDilithiumHeight > 0);
    BOOST_CHECK_EQUAL(Consensus::Params::LWMA_WINDOW, 45);
}

BOOST_AUTO_TEST_CASE(lwma_before_activation_uses_legacy_path)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQREGTEST);
    const Consensus::Params& consensus = params->GetConsensus();

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
    const auto blocks = BuildLwmaChain(Consensus::Params::LWMA_WINDOW, /*base_time=*/1000000, consensus.nPowTargetSpacing, low_bits, consensus);

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
    const auto blocks = BuildLwmaChain(Consensus::Params::LWMA_WINDOW, /*base_time=*/1000000, consensus.nPowTargetSpacing * 3, bits, consensus);

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
    std::vector<CBlockIndex> blocks(Consensus::Params::LWMA_WINDOW);
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
    const int N = Consensus::Params::LWMA_WINDOW;
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
    const int N = Consensus::Params::LWMA_WINDOW;
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
    const int N = Consensus::Params::LWMA_WINDOW;
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
    const int N = Consensus::Params::LWMA_WINDOW;
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
    const int N = Consensus::Params::LWMA_WINDOW;
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
    const int N = Consensus::Params::LWMA_WINDOW;
    const int64_t T = consensus.nPowTargetSpacing;

    const int64_t k = static_cast<int64_t>(N) * (N + 1) * T / 2;
    BOOST_CHECK_EQUAL(k, 62100);

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

BOOST_AUTO_TEST_SUITE_END()
