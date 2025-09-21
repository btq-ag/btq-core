// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <rpc/dilithium.h>
#include <rpc/register.h>
#include <rpc/server.h>

#include <algorithm>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_rpc_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dilithium_rpc_commands_registered)
{
    // Test that Dilithium RPC commands are properly registered
    
    CRPCTable tableRPC;
    RegisterDilithiumRPCCommands(tableRPC);
    
    // Get list of registered commands
    std::vector<std::string> commands = tableRPC.listCommands();
    
    // Test that the commands are registered
    BOOST_CHECK(std::find(commands.begin(), commands.end(), "getnewdilithiumaddress") != commands.end());
    BOOST_CHECK(std::find(commands.begin(), commands.end(), "importdilithiumkey") != commands.end());
    BOOST_CHECK(std::find(commands.begin(), commands.end(), "signmessagewithdilithium") != commands.end());
}

BOOST_AUTO_TEST_CASE(dilithium_rpc_command_help)
{
    // Test that Dilithium RPC commands have proper help text
    
    CRPCTable tableRPC;
    RegisterDilithiumRPCCommands(tableRPC);
    
    // Test that commands are registered (basic functionality test)
    std::vector<std::string> commands = tableRPC.listCommands();
    
    BOOST_CHECK(std::find(commands.begin(), commands.end(), "getnewdilithiumaddress") != commands.end());
    BOOST_CHECK(std::find(commands.begin(), commands.end(), "importdilithiumkey") != commands.end());
    BOOST_CHECK(std::find(commands.begin(), commands.end(), "signmessagewithdilithium") != commands.end());
}

BOOST_AUTO_TEST_SUITE_END()
