// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//! Tests for the -maxmempool ceiling that closes CVE-2025-46597.
//!
//! On a 32-bit build the address space is 4 GiB, and a mempool large enough to
//! reconstruct a compact block of over 1 GB overflows the size arithmetic done
//! before the block is written to disk. Capping -maxmempool removes the
//! precondition.
//!
//! The check takes the architecture as an argument precisely so it can be
//! exercised here. Testing sizeof(void*) inline would leave the 32-bit branch
//! compiled out and unrun on every machine this suite is likely to run on,
//! which is the same shape of untested guard the cap exists to replace.

#include <node/mempool_args.h>

#include <test/util/setup_common.h>
#include <util/result.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>

BOOST_FIXTURE_TEST_SUITE(mempool_args_tests, BasicTestingSetup)

//! A 64-bit build takes any size, including ones far past the 32-bit ceiling.
BOOST_AUTO_TEST_CASE(maxmempool_is_unrestricted_on_64bit)
{
    for (const int64_t mb : std::initializer_list<int64_t>{0, 5, 300, MAX_32BIT_MEMPOOL_MB, MAX_32BIT_MEMPOOL_MB + 1, 4000, 100000}) {
        BOOST_CHECK_MESSAGE(CheckMaxMempoolSize(mb, /*is_32bit=*/false),
                            "-maxmempool=" + std::to_string(mb) + " was rejected on a 64-bit build");
    }
}

//! A 32-bit build accepts everything up to and including the ceiling.
BOOST_AUTO_TEST_CASE(maxmempool_allows_up_to_the_ceiling_on_32bit)
{
    for (const int64_t mb : std::initializer_list<int64_t>{0, 1, 300, MAX_32BIT_MEMPOOL_MB - 1, MAX_32BIT_MEMPOOL_MB}) {
        BOOST_CHECK_MESSAGE(CheckMaxMempoolSize(mb, /*is_32bit=*/true),
                            "-maxmempool=" + std::to_string(mb) + " was rejected below the 32-bit ceiling");
    }
}

//! And refuses anything past it. Without this the node starts, fills a mempool
//! it cannot address, and dies later on a block it cannot write.
BOOST_AUTO_TEST_CASE(maxmempool_is_capped_on_32bit)
{
    for (const int64_t mb : std::initializer_list<int64_t>{MAX_32BIT_MEMPOOL_MB + 1, 1000, 3001, 100000}) {
        const auto result{CheckMaxMempoolSize(mb, /*is_32bit=*/true)};
        BOOST_CHECK_MESSAGE(!result,
                            "-maxmempool=" + std::to_string(mb) + " was accepted on a 32-bit build");
        // The operator has to be told what to change, not just that it failed.
        const std::string message{util::ErrorString(result).original};
        BOOST_CHECK_MESSAGE(message.find(std::to_string(MAX_32BIT_MEMPOOL_MB)) != std::string::npos,
                            "error does not mention the limit: " + message);
        BOOST_CHECK_MESSAGE(message.find("32-bit") != std::string::npos,
                            "error does not mention the architecture: " + message);
    }
}

BOOST_AUTO_TEST_SUITE_END()
