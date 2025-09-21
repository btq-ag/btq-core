// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_key.h>
#include <key_io.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <wallet/crypter.h>

#include <test/util/setup_common.h>
#include <test/util/wallet.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_wallet_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dilithium_key_wif_encoding)
{
    // Test Dilithium key WIF encoding/decoding
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    // Test encoding
    std::string wif = EncodeDilithiumSecret(key);
    BOOST_CHECK(!wif.empty());
    
    // Test decoding
    CDilithiumKey decoded_key = DecodeDilithiumSecret(wif);
    BOOST_CHECK(decoded_key.IsValid());
    BOOST_CHECK(decoded_key == key);
    
    // Test that the decoded key produces the same public key
    CDilithiumPubKey original_pubkey = key.GetPubKey();
    CDilithiumPubKey decoded_pubkey = decoded_key.GetPubKey();
    BOOST_CHECK(original_pubkey == decoded_pubkey);
}

BOOST_AUTO_TEST_CASE(dilithium_key_encryption)
{
    // Test Dilithium key encryption/decryption
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    // Create a test master key
    CKeyingMaterial master_key(32, 0);
    for (int i = 0; i < 32; i++) {
        master_key[i] = i;
    }
    
    // Test encryption
    std::vector<unsigned char> encrypted_secret;
    uint256 iv = uint256::ONE; // Use a test IV
    CKeyingMaterial secret(key.begin(), key.end());
    
    BOOST_CHECK(EncryptDilithiumSecret(master_key, secret, iv, encrypted_secret));
    BOOST_CHECK(!encrypted_secret.empty());
    
    // Test decryption
    CKeyingMaterial decrypted_secret;
    BOOST_CHECK(DecryptDilithiumSecret(master_key, encrypted_secret, iv, decrypted_secret));
    BOOST_CHECK(decrypted_secret.size() == CDilithiumKey::GetKeySize());
    
    // Verify the decrypted secret matches the original
    BOOST_CHECK(std::equal(secret.begin(), secret.end(), decrypted_secret.begin()));
    
    // Test full key decryption
    CDilithiumKey decrypted_key;
    BOOST_CHECK(DecryptDilithiumKey(master_key, encrypted_secret, CPubKey(), decrypted_key));
    BOOST_CHECK(decrypted_key.IsValid());
    BOOST_CHECK(decrypted_key == key);
}

BOOST_AUTO_TEST_CASE(dilithium_key_storage)
{
    // Test Dilithium key storage in wallet database
    // This test would require a more complex setup with actual wallet database
    // For now, we'll test the basic key operations
    
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    // Test key serialization
    CPrivKey privkey = key.GetPrivKey();
    BOOST_CHECK(!privkey.empty());
    BOOST_CHECK(privkey.size() == CDilithiumKey::GetKeySize());
    
    // Test key loading from serialized data
    CDilithiumKey loaded_key;
    BOOST_CHECK(loaded_key.Load(Span<const unsigned char>(privkey.begin(), privkey.end())));
    BOOST_CHECK(loaded_key.IsValid());
    BOOST_CHECK(loaded_key == key);
}

BOOST_AUTO_TEST_CASE(dilithium_extended_keys)
{
    // Test Dilithium extended key functionality
    CDilithiumExtKey ext_key;
    ext_key.SetSeed(Span<const std::byte>(reinterpret_cast<const std::byte*>("test seed"), 9));
    BOOST_CHECK(ext_key.key.IsValid());
    
    // Test key derivation
    CDilithiumExtKey derived_key;
    BOOST_CHECK(ext_key.Derive(derived_key, 0));
    BOOST_CHECK(derived_key.key.IsValid());
    BOOST_CHECK(derived_key.key != ext_key.key);
    
    // Test extended public key
    CDilithiumExtPubKey ext_pubkey = ext_key.Neuter();
    BOOST_CHECK(ext_pubkey.pubkey.IsValid());
    BOOST_CHECK(ext_pubkey.pubkey == ext_key.key.GetPubKey());
}

BOOST_AUTO_TEST_CASE(dilithium_key_signature_verification)
{
    // Test Dilithium key signing and verification
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Create a test message hash
    uint256 message_hash = uint256::ONE;
    
    // Sign the message
    std::vector<unsigned char> signature;
    BOOST_CHECK(key.Sign(message_hash, signature));
    BOOST_CHECK(!signature.empty());
    
    // Verify the signature
    BOOST_CHECK(pubkey.Verify(message_hash, signature));
    
    // Test with wrong message
    uint256 wrong_hash = uint256::ZERO;
    BOOST_CHECK(!pubkey.Verify(wrong_hash, signature));
}

BOOST_AUTO_TEST_SUITE_END()
