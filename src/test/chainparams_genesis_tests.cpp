// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
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

BOOST_AUTO_TEST_SUITE_END()
