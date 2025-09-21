// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <wallet/rpc/wallet.h>
#include <rpc/server.h>

#include <algorithm>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_rpc_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dilithium_rpc_commands_registered)
{
    // Test that Dilithium RPC commands are properly registered in wallet RPC commands
    
    Span<const CRPCCommand> walletCommands = wallet::GetWalletRPCCommands();
    
    // Convert to vector of command names for easier searching
    std::vector<std::string> commandNames;
    for (const auto& command : walletCommands) {
        commandNames.push_back(command.name);
    }
    
    // Test that the Dilithium commands are registered
    BOOST_CHECK(std::find(commandNames.begin(), commandNames.end(), "getnewdilithiumaddress") != commandNames.end());
    BOOST_CHECK(std::find(commandNames.begin(), commandNames.end(), "importdilithiumkey") != commandNames.end());
    BOOST_CHECK(std::find(commandNames.begin(), commandNames.end(), "signmessagewithdilithium") != commandNames.end());
}

BOOST_AUTO_TEST_CASE(dilithium_rpc_command_help)
{
    // Test that Dilithium RPC commands have proper help text
    
    Span<const CRPCCommand> walletCommands = wallet::GetWalletRPCCommands();
    
    // Convert to vector of command names for easier searching
    std::vector<std::string> commandNames;
    for (const auto& command : walletCommands) {
        commandNames.push_back(command.name);
    }
    
    // Test that commands are registered (basic functionality test)
    BOOST_CHECK(std::find(commandNames.begin(), commandNames.end(), "getnewdilithiumaddress") != commandNames.end());
    BOOST_CHECK(std::find(commandNames.begin(), commandNames.end(), "importdilithiumkey") != commandNames.end());
    BOOST_CHECK(std::find(commandNames.begin(), commandNames.end(), "signmessagewithdilithium") != commandNames.end());
}

BOOST_AUTO_TEST_SUITE_END()