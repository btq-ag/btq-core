// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <crypto/dilithium_key.h>
#include <script/script.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <policy/fees.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_network_policy_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dilithium_transaction_weight_limits)
{
    // Test that Dilithium transactions respect network weight limits
    
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    
    // Create a simple transaction
    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.nLockTime = 0;
    
    // Add input
    CTxIn txin;
    txin.prevout = COutPoint(uint256::ONE, 0);
    txin.nSequence = CTxIn::SEQUENCE_FINAL;
    mtx.vin.push_back(txin);
    
    // Add output with Dilithium script
    CTxOut txout;
    txout.nValue = 1000000;
    txout.scriptPubKey = CScript() << ToByteVector(dilithium_pubkey) << OP_CHECKSIGDILITHIUM;
    mtx.vout.push_back(txout);
    
    CTransaction tx(mtx);
    
    // Test transaction weight
    int64_t weight = GetTransactionWeight(tx);
    BOOST_CHECK_GT(weight, 0);
    BOOST_CHECK_LE(weight, MAX_STANDARD_TX_WEIGHT);
    
    // Test serialization size
    int64_t serialized_size = GetSerializeSize(tx, PROTOCOL_VERSION);
    BOOST_CHECK_GT(serialized_size, 0);
    
    // Test that transaction is standard
    std::string reason;
    BOOST_CHECK(IsStandardTx(tx, std::nullopt, true, CFeeRate(1000), reason));
}

BOOST_AUTO_TEST_CASE(dilithium_signature_size_limits)
{
    // Test that Dilithium signatures fit within script limits
    
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    
    // Test signature size
    uint256 test_hash = uint256::ONE;
    std::vector<unsigned char> signature;
    BOOST_CHECK(dilithium_key.Sign(test_hash, signature));
    
    // Verify signature fits within script limits
    BOOST_CHECK_GT(signature.size(), 0);
    BOOST_CHECK_LE(signature.size(), MAX_SCRIPT_ELEMENT_SIZE);
    
    // Test public key size
    std::vector<unsigned char> pubkey_bytes = dilithium_pubkey.GetAddress();
    BOOST_CHECK_GT(pubkey_bytes.size(), 0);
    BOOST_CHECK_LE(pubkey_bytes.size(), MAX_SCRIPT_ELEMENT_SIZE);
}

BOOST_AUTO_TEST_CASE(dilithium_script_verification_flags)
{
    // Test that Dilithium verification flags are properly set
    
    // Check that Dilithium verification flag is in mandatory flags
    BOOST_CHECK(MANDATORY_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_DILITHIUM);
    
    // Check that Dilithium verification flag is in standard flags
    BOOST_CHECK(STANDARD_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_DILITHIUM);
    
    // Test that the flag value is correct
    BOOST_CHECK_EQUAL(SCRIPT_VERIFY_DILITHIUM, (1U << 21));
}

BOOST_AUTO_TEST_SUITE_END()
