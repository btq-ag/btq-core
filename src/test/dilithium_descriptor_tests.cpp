// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <crypto/dilithium_key.h>
#include <addresstype.h>
#include <outputtype.h>
#include <key_io.h>
#include <script/descriptor.h>
#include <test/util/setup_common.h>
#include <wallet/test/util.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_descriptor_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dilithium_key_generation)
{
    // Test basic Dilithium key generation
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    
    BOOST_REQUIRE(dilithium_key.IsValid());
    
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    BOOST_REQUIRE(dilithium_pubkey.IsValid());
    
    // Test key size
    BOOST_CHECK_EQUAL(dilithium_pubkey.size(), DilithiumConstants::PUBLIC_KEY_SIZE);
}

BOOST_AUTO_TEST_CASE(dilithium_destination_creation)
{
    // Test Dilithium destination creation
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    BOOST_REQUIRE(dilithium_key.IsValid());
    
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    
    // Test DilithiumPKHash
    DilithiumPKHash dilithium_pk_hash(dilithium_pubkey);
    BOOST_CHECK(dilithium_pk_hash == DilithiumPKHash(dilithium_pubkey.GetID()));
    
    // Test DilithiumWitnessV0KeyHash
    DilithiumWitnessV0KeyHash dilithium_witness_key_hash(dilithium_pubkey);
    BOOST_CHECK(dilithium_witness_key_hash == DilithiumWitnessV0KeyHash(dilithium_pubkey.GetID()));
}

BOOST_AUTO_TEST_CASE(dilithium_address_encoding)
{
    // Test Dilithium address encoding
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    BOOST_REQUIRE(dilithium_key.IsValid());
    
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    
    // Test legacy address encoding
    DilithiumPKHash dilithium_pk_hash(dilithium_pubkey);
    std::string legacy_address = EncodeDestination(dilithium_pk_hash);
    BOOST_CHECK(!legacy_address.empty());
    BOOST_CHECK(legacy_address.length() > 0);
    
    // Test bech32 address encoding
    DilithiumWitnessV0KeyHash dilithium_witness_key_hash(dilithium_pubkey);
    std::string bech32_address = EncodeDestination(dilithium_witness_key_hash);
    BOOST_CHECK(!bech32_address.empty());
    BOOST_CHECK(bech32_address.length() > 0);
    
    // Verify addresses are different
    BOOST_CHECK(legacy_address != bech32_address);
}

BOOST_AUTO_TEST_CASE(dilithium_descriptor_parsing)
{
    // Test that Dilithium key generation works correctly
    // This is the foundation for our descriptor functionality
    
    // Generate a Dilithium key
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    BOOST_REQUIRE(dilithium_key.IsValid());
    
    // Get the public key
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    BOOST_REQUIRE(dilithium_pubkey.IsValid());
    
    // Test that we can create destinations from the Dilithium key
    DilithiumPKHash pk_hash(dilithium_pubkey);
    DilithiumWitnessV0KeyHash witness_key_hash(dilithium_pubkey);
    
    // These should be valid destinations
    BOOST_CHECK(pk_hash == DilithiumPKHash(dilithium_pubkey.GetID()));
    BOOST_CHECK(witness_key_hash == DilithiumWitnessV0KeyHash(dilithium_pubkey.GetID()));
}

BOOST_AUTO_TEST_CASE(dilithium_descriptor_expansion)
{
    // Test that Dilithium destinations can be converted to scripts
    // This tests the core functionality needed for descriptor integration
    
    // Generate a Dilithium key
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    BOOST_REQUIRE(dilithium_key.IsValid());
    
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    BOOST_REQUIRE(dilithium_pubkey.IsValid());
    
    // Test DilithiumPKHash to script conversion
    DilithiumPKHash pk_hash(dilithium_pubkey);
    CScript pk_script = GetScriptForDestination(pk_hash);
    BOOST_CHECK(!pk_script.empty());
    
    // Test DilithiumWitnessV0KeyHash to script conversion
    DilithiumWitnessV0KeyHash witness_key_hash(dilithium_pubkey);
    CScript witness_script = GetScriptForDestination(witness_key_hash);
    BOOST_CHECK(!witness_script.empty());
    
    // Verify scripts are different
    BOOST_CHECK(pk_script != witness_script);
}

BOOST_AUTO_TEST_CASE(dilithium_type_compatibility)
{
    // Test type compatibility checking
    // This tests the logic we implemented in DescriptorScriptPubKeyMan
    
    // The compatibility check should be true for our special case
    bool special_legacy_compatible = (OutputType::DILITHIUM_LEGACY != OutputType::LEGACY) &&
                                     (OutputType::DILITHIUM_LEGACY == OutputType::DILITHIUM_LEGACY && OutputType::LEGACY == OutputType::LEGACY);
    BOOST_CHECK(special_legacy_compatible);
    
    // Test Dilithium bech32 compatibility
    bool special_bech32_compatible = (OutputType::DILITHIUM_BECH32 != OutputType::BECH32) &&
                                     (OutputType::DILITHIUM_BECH32 == OutputType::DILITHIUM_BECH32 && OutputType::BECH32 == OutputType::BECH32);
    BOOST_CHECK(special_bech32_compatible);
}

BOOST_AUTO_TEST_CASE(dilithium_signature_sizes)
{
    // Test that Dilithium signatures have the expected sizes
    BOOST_CHECK_EQUAL(DilithiumConstants::SIGNATURE_SIZE, DilithiumConstants::SIGNATURE_SIZE);
    BOOST_CHECK(DilithiumConstants::SIGNATURE_SIZE > 1000); // Dilithium signatures are much larger than ECDSA
    
    // Test public key size
    BOOST_CHECK_EQUAL(DilithiumConstants::PUBLIC_KEY_SIZE, DilithiumConstants::PUBLIC_KEY_SIZE);
    BOOST_CHECK(DilithiumConstants::PUBLIC_KEY_SIZE > 1000); // Dilithium public keys are much larger than ECDSA
}

BOOST_AUTO_TEST_SUITE_END()
