// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <pow.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_lwma_tests, BasicTestingSetup)

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

BOOST_AUTO_TEST_SUITE_END()
