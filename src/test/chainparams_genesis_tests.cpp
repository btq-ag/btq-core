// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <common/args.h>
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

BOOST_AUTO_TEST_CASE(test_checkpoint_is_regtest_only)
{
    ArgsManager args;
    SetupChainParamsBaseOptions(args);
    const std::string hash(64, '1');
    args.ForceSetArg("-testcheckpoint", "10:" + hash);
    const auto params = CreateChainParams(args, ChainType::BTQREGTEST);
    BOOST_CHECK_EQUAL(params->Checkpoints().mapCheckpoints.size(), 2U);
    BOOST_CHECK_EQUAL(params->Checkpoints().mapCheckpoints.at(0), params->GenesisBlock().GetHash());
    BOOST_CHECK_EQUAL(params->Checkpoints().mapCheckpoints.at(10), uint256S(hash));
    for (const auto chain : {ChainType::BTQMAIN, ChainType::BTQTEST, ChainType::BTQSIGNET}) {
        BOOST_CHECK_EXCEPTION(CreateChainParams(args, chain), std::runtime_error,
            [](const auto& e) { return std::string(e.what()) == "-testcheckpoint is only available on regtest."; });
    }
    for (const auto& value : {"0:" + hash, "-1:" + hash, "2147483648:" + hash,
                              std::string{"10:abcd"}, "10:" + std::string(64, 'g'), "10:" + hash + ":extra"}) {
        args.ForceSetArg("-testcheckpoint", value);
        BOOST_CHECK_THROW(CreateChainParams(args, ChainType::BTQREGTEST), std::runtime_error);
    }
    ArgsManager duplicate_args;
    SetupChainParamsBaseOptions(duplicate_args);
    const std::string checkpoint_arg = "-testcheckpoint=10:" + hash;
    const char* argv[]{"test", checkpoint_arg.c_str(), checkpoint_arg.c_str()};
    std::string error;
    BOOST_REQUIRE(duplicate_args.ParseParameters(3, argv, error));
    BOOST_CHECK_EXCEPTION(CreateChainParams(duplicate_args, ChainType::BTQREGTEST), std::runtime_error,
        [](const auto& e) { return std::string(e.what()) == "-testcheckpoint requires exactly one height:hash value."; });
}

BOOST_AUTO_TEST_SUITE_END()
