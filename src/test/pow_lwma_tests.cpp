// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chainparams.h>
#include <chain.h>
#include <pow.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <vector>

BOOST_FIXTURE_TEST_SUITE(pow_lwma_tests, BasicTestingSetup)

namespace {
std::vector<CBlockIndex> BuildLwmaWindow(uint32_t bits, int64_t start_time, int64_t spacing)
{
    const int N = Consensus::Params::LWMA_WINDOW;
    std::vector<CBlockIndex> blocks(N + 1);
    for (int i = 0; i <= N; ++i) {
        blocks[i].pprev = i > 0 ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = start_time + i * spacing;
        blocks[i].nBits = bits;
    }
    return blocks;
}
} // namespace

BOOST_AUTO_TEST_CASE(lwma_activation_height_configured)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQMAIN);
    BOOST_CHECK(params->GetConsensus().nLWMAHeight > 0);
    BOOST_CHECK(params->GetConsensus().nDilithiumHeight > 0);
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
    const auto blocks = BuildLwmaWindow(low_bits, /*start_time=*/1000000, consensus.nPowTargetSpacing);

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
    const auto blocks = BuildLwmaWindow(bits, /*start_time=*/1000000, consensus.nPowTargetSpacing * 3);

    CBlockHeader next_block;
    next_block.nTime = blocks.back().GetBlockTime() + consensus.nPowTargetSpacing * 6;

    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks.back(), &next_block, consensus), bits);
}

BOOST_AUTO_TEST_SUITE_END()
