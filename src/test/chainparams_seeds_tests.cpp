// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//! Tests for the bootstrap seed configuration.
//!
//! A node with an empty peers.dat has exactly two ways to find its first peer:
//! DNS seeds, and the hardcoded fixed seeds that exist to cover the case where
//! DNS seeding fails. Neither is exercised by any other test, and a mistake in
//! either is invisible until a fresh node cannot join the network -- which is
//! the situation described in issue #114.

#include <chainparams.h>
#include <kernel/chainparams.h>
#include <netaddress.h>
#include <protocol.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(chainparams_seeds_tests, BasicTestingSetup)

namespace {
//! Deserialise a fixed-seed blob the same way CConnman::ThreadOpenConnections
//! does, so this tests the bytes that actually reach addrman rather than a
//! reimplementation of the format.
std::vector<CService> DecodeFixedSeeds(const std::vector<uint8_t>& blob)
{
    std::vector<CService> out;
    DataStream underlying_stream{blob};
    ParamsStream s{CAddress::V2_NETWORK, underlying_stream};
    while (!s.eof()) {
        CService endpoint;
        s >> endpoint;
        out.push_back(endpoint);
    }
    return out;
}

//! The predicate net.cpp effectively applies: anything failing this is dropped
//! before it reaches addrman.
bool IsUsableSeed(const CService& seed)
{
    return seed.IsValid() && seed.IsRoutable() && seed.GetPort() != 0;
}
} // namespace

//! The exact bytes mainnet and testnet shipped until #114, described in a
//! comment as an ignored placeholder.
//!
//! This is what gives fixed_seeds_are_routable below its teeth. That test
//! iterates the seeds we ship, and we currently ship none, so it would pass
//! vacuously and keep passing if the guard itself were broken. Running the
//! known-bad entry through the same decoder proves the guard detects the class
//! of defect it exists for.
BOOST_AUTO_TEST_CASE(historical_placeholder_seed_is_rejected)
{
    const std::vector<uint8_t> placeholder{0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const std::vector<CService> decoded{DecodeFixedSeeds(placeholder)};

    // It parses as a well-formed record, which is why it was never noticed.
    BOOST_REQUIRE_EQUAL(decoded.size(), 1U);
    BOOST_CHECK_EQUAL(decoded[0].ToStringAddrPort(), "0.0.0.0:0");

    // And is then discarded, so it contributed nothing but a misleading
    // "Added 0 fixed seeds from reachable networks" at runtime.
    BOOST_CHECK(!IsUsableSeed(decoded[0]));
}

//! A well-formed entry must survive the same path, so the guard is not simply
//! rejecting everything.
BOOST_AUTO_TEST_CASE(a_real_seed_is_accepted)
{
    // An arbitrary routable IPv4 address and port. Nothing connects to it; it
    // only has to survive the filter. Note that the RFC 5737 documentation
    // ranges cannot be used here, because IsRoutable() rejects them by design.
    const std::vector<uint8_t> good{0x01, 0x04, 1, 2, 3, 4, 0x20, 0x8D};
    const std::vector<CService> decoded{DecodeFixedSeeds(good)};

    BOOST_REQUIRE_EQUAL(decoded.size(), 1U);
    BOOST_CHECK_EQUAL(decoded[0].ToStringAddrPort(), "1.2.3.4:8333");
    BOOST_CHECK(IsUsableSeed(decoded[0]));
}

//! Every fixed seed we ship must be a routable address on a real port.
//!
//! net.cpp discards unroutable fixed seeds silently, so a malformed or
//! placeholder entry does not fail anything -- it just quietly reduces the
//! usable seed count, and the node logs "Added 0 fixed seeds from reachable
//! networks" as though seeding had failed at runtime. Mainnet shipped exactly
//! such an entry (0.0.0.0:0) until #114; this is the assertion that would have
//! caught it, and that keeps the next one from landing.
BOOST_AUTO_TEST_CASE(fixed_seeds_are_routable)
{
    const std::vector<std::pair<std::string, std::unique_ptr<const CChainParams>>> chains = [] {
        std::vector<std::pair<std::string, std::unique_ptr<const CChainParams>>> v;
        v.emplace_back("main", CChainParams::Main());
        v.emplace_back("test", CChainParams::TestNet());
        v.emplace_back("signet", CChainParams::SigNet({}));
        v.emplace_back("regtest", CChainParams::RegTest({}));
        return v;
    }();

    for (const auto& [name, params] : chains) {
        std::vector<CService> seeds;
        // A truncated or misaligned blob throws here rather than yielding
        // garbage, so this is an assertion even when the list is empty.
        BOOST_CHECK_NO_THROW(seeds = DecodeFixedSeeds(params->FixedSeeds()));

        for (const CService& seed : seeds) {
            BOOST_CHECK_MESSAGE(IsUsableSeed(seed),
                                name + ": fixed seed " + seed.ToStringAddrPort() +
                                    " is not a routable address on a real port, so net.cpp will "
                                    "silently discard it");
        }
    }
}

//! Mainnet has no fixed seeds yet. This is a known launch blocker, not an
//! accident, and it is asserted here so that provisioning them is a deliberate
//! change that has to come past this test rather than something that can drift
//! back to a placeholder.
//!
//! When mainnet seeds are generated (see src/chainparamsseeds.h), this
//! assertion is the one to invert: require a nonzero count, and keep
//! fixed_seeds_are_routable above as the standing guard on their contents.
BOOST_AUTO_TEST_CASE(mainnet_fixed_seeds_are_not_yet_provisioned)
{
    const std::unique_ptr<const CChainParams> main{CChainParams::Main()};
    const std::vector<CService> seeds{DecodeFixedSeeds(main->FixedSeeds())};

    BOOST_CHECK_MESSAGE(seeds.empty(),
                        "mainnet now ships " + std::to_string(seeds.size()) +
                            " fixed seed(s). If these are real, invert this assertion to require a "
                            "nonzero count and resolve issue #114.");
}

//! Bootstrap needs at least one source. Mainnet currently relies entirely on
//! DNS seeding because it has no fixed seeds, so losing the seed list would
//! leave a fresh node with nothing at all.
//!
//! This cannot check that the hostnames resolve -- a unit test must not depend
//! on DNS -- so resolution is verified out of band and tracked in #114.
BOOST_AUTO_TEST_CASE(networks_have_a_bootstrap_source)
{
    for (const auto& [name, params] : [] {
             std::vector<std::pair<std::string, std::unique_ptr<const CChainParams>>> v;
             v.emplace_back("main", CChainParams::Main());
             v.emplace_back("test", CChainParams::TestNet());
             return v;
         }()) {
        const bool has_dns{!params->DNSSeeds().empty()};
        const bool has_fixed{!params->FixedSeeds().empty()};
        BOOST_CHECK_MESSAGE(has_dns || has_fixed,
                            name + ": no DNS seeds and no fixed seeds, so a node with an empty "
                                   "peers.dat cannot bootstrap");
    }
}

//! Regtest must never reach the network. Its seed is a deliberately
//! unresolvable sentinel; a real hostname here would have test nodes calling
//! out to the internet.
BOOST_AUTO_TEST_CASE(regtest_does_not_seed_from_the_network)
{
    const std::unique_ptr<const CChainParams> regtest{CChainParams::RegTest({})};
    BOOST_CHECK(regtest->FixedSeeds().empty());
    for (const std::string& seed : regtest->DNSSeeds()) {
        BOOST_CHECK_MESSAGE(seed.find(".invalid") != std::string::npos,
                            "regtest DNS seed " + seed + " is not an .invalid sentinel");
    }
}

BOOST_AUTO_TEST_SUITE_END()
