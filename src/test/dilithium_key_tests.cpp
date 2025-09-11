// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_key.h>

#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_key_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dilithium_key_generation)
{
    // Test basic key generation
    CDilithiumKey key;
    BOOST_CHECK(!key.IsValid()); // Should be invalid initially
    
    key.MakeNewKey();
    BOOST_CHECK(key.IsValid()); // Should be valid after generation
    BOOST_CHECK(key.size() == CDilithiumKey::GetKeySize());
}

BOOST_AUTO_TEST_CASE(dilithium_pubkey_derivation)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    // Get public key
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_CHECK(pubkey.IsValid());
    BOOST_CHECK(pubkey.size() == CDilithiumPubKey::SIZE);
    
    // Verify the private key corresponds to the public key
    BOOST_CHECK(key.VerifyPubKey(pubkey));
}

BOOST_AUTO_TEST_CASE(dilithium_signing_and_verification)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Create a test message hash
    uint256 hash;
    hash.SetHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    
    // Sign the hash
    std::vector<unsigned char> signature;
    BOOST_CHECK(key.Sign(hash, signature));
    BOOST_CHECK(!signature.empty());
    BOOST_CHECK(signature.size() <= CDilithiumKey::MAX_SIGNATURE_SIZE);
    
    // Verify the signature
    BOOST_CHECK(pubkey.Verify(hash, signature));
    
    // Verify with wrong hash should fail
    uint256 wrong_hash;
    wrong_hash.SetHex("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");
    BOOST_CHECK(!pubkey.Verify(wrong_hash, signature));
    
    // Verify with corrupted signature should fail
    if (!signature.empty()) {
        signature[0] ^= 0x01; // Flip a bit
        BOOST_CHECK(!pubkey.Verify(hash, signature));
    }
}

BOOST_AUTO_TEST_CASE(dilithium_message_signing)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Test message signing
    std::string message = "Hello, Dilithium!";
    std::vector<unsigned char> msg_bytes(message.begin(), message.end());
    
    std::vector<unsigned char> signature;
    BOOST_CHECK(key.SignMessage(Span<const unsigned char>(msg_bytes), signature));
    BOOST_CHECK(!signature.empty());
    
    // Verify the message signature
    BOOST_CHECK(pubkey.VerifyMessage(Span<const unsigned char>(msg_bytes), signature));
    
    // Different message should fail verification
    std::string different_message = "Hello, World!";
    std::vector<unsigned char> different_bytes(different_message.begin(), different_message.end());
    BOOST_CHECK(!pubkey.VerifyMessage(Span<const unsigned char>(different_bytes), signature));
}

BOOST_AUTO_TEST_CASE(dilithium_context_signing)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    uint256 hash;
    hash.SetHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    
    // Sign with context
    std::vector<unsigned char> context = {'B', 'T', 'Q', 'v', '1'};
    std::vector<unsigned char> signature_with_context;
    BOOST_CHECK(key.Sign(hash, signature_with_context, context));
    
    // Verify with same context should succeed
    BOOST_CHECK(pubkey.Verify(hash, signature_with_context, context));
    
    // Verify without context should fail
    BOOST_CHECK(!pubkey.Verify(hash, signature_with_context));
    
    // Verify with different context should fail
    std::vector<unsigned char> different_context = {'B', 'T', 'Q', 'v', '2'};
    BOOST_CHECK(!pubkey.Verify(hash, signature_with_context, different_context));
}

BOOST_AUTO_TEST_CASE(dilithium_serialization)
{
    CDilithiumKey key1;
    key1.MakeNewKey();
    BOOST_REQUIRE(key1.IsValid());
    
    // Serialize the key
    std::vector<unsigned char> serialized = key1.Serialize();
    BOOST_CHECK(serialized.size() == CDilithiumKey::GetKeySize());
    
    // Load into a new key
    CDilithiumKey key2;
    BOOST_CHECK(key2.Load(Span<const unsigned char>(serialized)));
    BOOST_CHECK(key2.IsValid());
    
    // Keys should be equal
    BOOST_CHECK(key1 == key2);
    
    // Public keys should also be equal
    CDilithiumPubKey pubkey1 = key1.GetPubKey();
    CDilithiumPubKey pubkey2 = key2.GetPubKey();
    BOOST_CHECK(pubkey1 == pubkey2);
}

BOOST_AUTO_TEST_CASE(dilithium_pubkey_operations)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Test hash and ID generation
    uint256 hash1 = pubkey.GetHash();
    uint256 hash2 = pubkey.GetHash();
    BOOST_CHECK(hash1 == hash2); // Should be deterministic
    BOOST_CHECK(!hash1.IsNull());
    
    uint160 id1 = pubkey.GetID();
    uint160 id2 = pubkey.GetID();
    BOOST_CHECK(id1 == id2); // Should be deterministic
    BOOST_CHECK(!id1.IsNull());
    
    // Test address derivation
    std::vector<unsigned char> address = pubkey.GetAddress();
    BOOST_CHECK(address.size() == 20); // Hash160 size
}

BOOST_AUTO_TEST_CASE(dilithium_key_equality)
{
    CDilithiumKey key1, key2, key3;
    
    // Invalid keys should be equal
    BOOST_CHECK(key1 == key2);
    BOOST_CHECK(!(key1 != key2));
    
    key1.MakeNewKey();
    key2.MakeNewKey();
    
    // Different keys should not be equal
    BOOST_CHECK(key1 != key2);
    BOOST_CHECK(!(key1 == key2));
    
    // Copy should be equal
    key3 = key1;
    BOOST_CHECK(key1 == key3);
    BOOST_CHECK(!(key1 != key3));
}

BOOST_AUTO_TEST_CASE(dilithium_pubkey_equality)
{
    CDilithiumKey key1, key2;
    key1.MakeNewKey();
    key2.MakeNewKey();
    
    CDilithiumPubKey pubkey1 = key1.GetPubKey();
    CDilithiumPubKey pubkey2 = key2.GetPubKey();
    CDilithiumPubKey pubkey1_copy = key1.GetPubKey();
    
    // Same key should produce same pubkey
    BOOST_CHECK(pubkey1 == pubkey1_copy);
    
    // Different keys should produce different pubkeys
    BOOST_CHECK(pubkey1 != pubkey2);
    
    // Test ordering
    BOOST_CHECK((pubkey1 < pubkey2) != (pubkey2 < pubkey1));
}

BOOST_AUTO_TEST_CASE(dilithium_sanity_check)
{
    // Test the global sanity check function
    BOOST_CHECK(DilithiumSanityCheck());
}

BOOST_AUTO_TEST_SUITE_END()
