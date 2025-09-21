// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <crypto/dilithium_key.h>
#include <script/script.h>
#include <script/interpreter.h>
#include <policy/policy.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_basic_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dilithium_script_verification_flags)
{
    // Test that SCRIPT_VERIFY_DILITHIUM flag is properly set
    BOOST_CHECK(MANDATORY_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_DILITHIUM);
    BOOST_CHECK(STANDARD_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_DILITHIUM);
}

BOOST_AUTO_TEST_CASE(dilithium_key_basic_operations)
{
    // Test basic Dilithium key operations
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    BOOST_CHECK(dilithium_key.IsValid());
    
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    BOOST_CHECK(dilithium_pubkey.IsValid());
    
    // Test signature creation and verification
    uint256 test_hash = uint256::ONE;
    std::vector<unsigned char> signature;
    BOOST_CHECK(dilithium_key.Sign(test_hash, signature));
    BOOST_CHECK(dilithium_pubkey.Verify(test_hash, signature));
}

BOOST_AUTO_TEST_CASE(dilithium_script_opcodes)
{
    // Test that Dilithium opcodes are defined
    BOOST_CHECK_EQUAL(OP_CHECKSIGDILITHIUM, 0xbb);
    BOOST_CHECK_EQUAL(OP_CHECKSIGDILITHIUMVERIFY, 0xbc);
}

BOOST_AUTO_TEST_CASE(dilithium_signature_sizes)
{
    // Test Dilithium signature and key sizes
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    
    // Test signature size
    uint256 test_hash = uint256::ONE;
    std::vector<unsigned char> signature;
    BOOST_CHECK(dilithium_key.Sign(test_hash, signature));
    BOOST_CHECK_GT(signature.size(), 0);
    BOOST_CHECK_LE(signature.size(), MAX_SCRIPT_ELEMENT_SIZE);
    
    // Test public key size
    std::vector<unsigned char> pubkey_bytes = dilithium_pubkey.GetAddress();
    BOOST_CHECK_GT(pubkey_bytes.size(), 0);
    BOOST_CHECK_LE(pubkey_bytes.size(), MAX_SCRIPT_ELEMENT_SIZE);
}

BOOST_AUTO_TEST_CASE(dilithium_script_limits)
{
    // Test that Dilithium scripts respect size limits
    BOOST_CHECK_GE(MAX_SCRIPT_ELEMENT_SIZE, 15000);
    BOOST_CHECK_GE(MAX_SCRIPT_SIZE, 100000);
    
    // Test that Dilithium signatures fit within limits
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    
    uint256 test_hash = uint256::ONE;
    std::vector<unsigned char> signature;
    BOOST_CHECK(dilithium_key.Sign(test_hash, signature));
    BOOST_CHECK_LE(signature.size(), MAX_SCRIPT_ELEMENT_SIZE);
}

BOOST_AUTO_TEST_SUITE_END()
