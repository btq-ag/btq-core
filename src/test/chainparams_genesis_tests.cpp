// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <pow.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <set>

BOOST_FIXTURE_TEST_SUITE(chainparams_genesis_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(unique_genesis_hashes)
{
    std::set<uint256> seen;
    const auto check = [&](ChainType chain) {
        const uint256 hash = CreateChainParams(*m_node.args, chain)->GenesisBlock().GetHash();
        BOOST_CHECK(seen.insert(hash).second);
    };
    check(ChainType::BTQMAIN);
    check(ChainType::BTQTEST);
    check(ChainType::BTQSIGNET);
    check(ChainType::BTQREGTEST);
}

BOOST_AUTO_TEST_CASE(mainnet_minimum_chainwork_at_genesis)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQMAIN);
    const Consensus::Params& consensus = params->GetConsensus();

    CBlockIndex genesis;
    genesis.nBits = params->GenesisBlock().nBits;
    const arith_uint256 genesis_work = GetBlockProof(genesis);

    BOOST_CHECK(UintToArith256(consensus.nMinimumChainWork) >= genesis_work);
    BOOST_CHECK_EQUAL(consensus.defaultAssumeValid, params->GenesisBlock().GetHash());
}

BOOST_AUTO_TEST_CASE(taproot_always_active_on_mainnet)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQMAIN);
    const Consensus::Params& consensus = params->GetConsensus();
    BOOST_CHECK_EQUAL(
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime,
        Consensus::BIP9Deployment::ALWAYS_ACTIVE);
}

BOOST_AUTO_TEST_CASE(signet_wif_prefix_distinct_from_p2sh)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::BTQSIGNET);
    const auto& wif_prefix = params->Base58Prefix(CChainParams::SECRET_KEY);
    const auto& p2sh_prefix = params->Base58Prefix(CChainParams::SCRIPT_ADDRESS);
    BOOST_REQUIRE_EQUAL(wif_prefix.size(), 1);
    BOOST_REQUIRE_EQUAL(p2sh_prefix.size(), 1);
    BOOST_CHECK_NE(wif_prefix[0], p2sh_prefix[0]);
    BOOST_CHECK_EQUAL(wif_prefix[0], 239);
}

BOOST_AUTO_TEST_SUITE_END()
