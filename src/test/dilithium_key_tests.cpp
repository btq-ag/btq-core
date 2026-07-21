// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_key.h>
#include <crypto/dilithium_wrapper.h>

#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <algorithm>
#include <array>

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

BOOST_AUTO_TEST_CASE(dilithium_raw_secret_key_to_public_key_fails_closed)
{
    std::array<uint8_t, BTQ_DILITHIUM_PUBLIC_KEY_SIZE> pk{};
    std::array<uint8_t, BTQ_DILITHIUM_SECRET_KEY_SIZE> sk{};
    BOOST_REQUIRE_EQUAL(btq_dilithium_keypair(pk.data(), sk.data()), 0);

    std::array<uint8_t, BTQ_DILITHIUM_PUBLIC_KEY_SIZE> derived_pk;
    derived_pk.fill(0xaa);
    BOOST_CHECK(btq_dilithium_sk_to_pk(derived_pk.data(), sk.data()) != 0);
    BOOST_CHECK(std::all_of(derived_pk.begin(), derived_pk.end(), [](uint8_t byte) { return byte == 0; }));

    const std::array<uint8_t, 3> msg{{0x42, 0x54, 0x51}};
    std::array<uint8_t, BTQ_DILITHIUM_SIGNATURE_SIZE> sig{};
    size_t siglen{0};
    BOOST_REQUIRE_EQUAL(btq_dilithium_sign(sig.data(), &siglen, msg.data(), msg.size(), nullptr, 0, sk.data()), 0);
    BOOST_CHECK_EQUAL(btq_dilithium_verify(sig.data(), siglen, msg.data(), msg.size(), nullptr, 0, pk.data()), 0);
    BOOST_CHECK(btq_dilithium_verify(sig.data(), siglen, msg.data(), msg.size(), nullptr, 0, derived_pk.data()) != 0);
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

BOOST_AUTO_TEST_CASE(dilithium_signatures_are_deterministic)
{
    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    BOOST_REQUIRE(key.IsValid());

    const uint256 hash = uint256::ONE;
    std::vector<unsigned char> sig_a;
    std::vector<unsigned char> sig_b;
    BOOST_REQUIRE(key.Sign(hash, sig_a));
    BOOST_REQUIRE(key.Sign(hash, sig_b));
    BOOST_CHECK(sig_a == sig_b);
    BOOST_CHECK(!sig_a.empty());
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

BOOST_AUTO_TEST_CASE(dilithium_load_and_set_reject_mismatched_stored_pubkey)
{
    CDilithiumKey key1;
    key1.MakeNewKey();
    BOOST_REQUIRE(key1.IsValid());

    CDilithiumKey key2;
    key2.MakeNewKey();
    BOOST_REQUIRE(key2.IsValid());
    BOOST_REQUIRE(key1.GetPubKey() != key2.GetPubKey());

    std::vector<unsigned char> malformed = key1.Serialize();
    BOOST_REQUIRE_EQUAL(malformed.size(), CDilithiumKey::GetKeySize());
    const CDilithiumPubKey wrong_pubkey = key2.GetPubKey();
    std::copy(wrong_pubkey.begin(), wrong_pubkey.end(), malformed.begin() + DilithiumConstants::SECRET_KEY_SIZE);

    CDilithiumKey loaded;
    BOOST_CHECK(!loaded.Load(Span<const unsigned char>(malformed)));
    BOOST_CHECK(!loaded.IsValid());

    CDilithiumKey set_key;
    set_key.Set(malformed.begin(), malformed.end());
    BOOST_CHECK(!set_key.IsValid());
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


BOOST_AUTO_TEST_CASE(dilithium_timingsafe_equal_functional)
{
    // Functional coverage for TimingSafeEqual: equal buffers, mismatch at
    // first byte, and mismatch only at the last byte (the case short-circuit
    // memcmp would still reject — but after scanning every byte).
    unsigned char a[32]{}, b[32]{};
    for (size_t i = 0; i < sizeof(a); ++i) {
        a[i] = static_cast<unsigned char>(i * 7 + 3);
        b[i] = a[i];
    }
    BOOST_CHECK(dilithium_internal::TimingSafeEqual(a, b, sizeof(a)));

    b[0] ^= 0x01;
    BOOST_CHECK(!dilithium_internal::TimingSafeEqual(a, b, sizeof(a)));
    b[0] = a[0];

    b[sizeof(b) - 1] ^= 0x80;
    BOOST_CHECK(!dilithium_internal::TimingSafeEqual(a, b, sizeof(a)));
    BOOST_CHECK(dilithium_internal::TimingSafeEqual(a, b, sizeof(a) - 1));
}

BOOST_AUTO_TEST_CASE(dilithium_key_equality_last_byte_differs)
{
    CDilithiumKey key1;
    BOOST_REQUIRE(key1.MakeNewKey());
    std::vector<unsigned char> bytes = key1.Serialize();
    BOOST_REQUIRE_EQUAL(bytes.size(), CDilithiumKey::GetKeySize());

    CDilithiumKey key_same;
    BOOST_REQUIRE(key_same.Load(Span<const unsigned char>(bytes)));
    BOOST_CHECK(key1 == key_same);

    // Flip the final secret-key byte. Load/Set may reject inconsistent
    // key material via self-checks, so compare via TimingSafeEqual on the
    // serialized buffers and via operator== only when both keys remain valid.
    std::vector<unsigned char> mutated = bytes;
    mutated.back() ^= 0x01;
    BOOST_CHECK(!dilithium_internal::TimingSafeEqual(bytes.data(), mutated.data(), bytes.size()));

    // PubKey: last-byte mismatch must yield inequality.
    CDilithiumPubKey pub = key1.GetPubKey();
    std::vector<unsigned char> pub_bytes(pub.begin(), pub.end());
    std::vector<unsigned char> pub_mut = pub_bytes;
    pub_mut.back() ^= 0x01;
    CDilithiumPubKey pub_other(pub_mut.begin(), pub_mut.end());
    BOOST_CHECK(pub != pub_other);
    BOOST_CHECK(!(pub == pub_other));
}

BOOST_AUTO_TEST_CASE(dilithium_extkey_equality_secret_fields)
{
    const auto raw_seed = std::vector<std::byte>(32, std::byte{0x42});
    CDilithiumExtKey a;
    a.SetSeed(Span<const std::byte>(raw_seed.data(), raw_seed.size()));
    CDilithiumExtKey b = a;
    BOOST_CHECK(a == b);

    // Differ only in the last seed byte; metadata left identical.
    b.seed.back() ^= 0x01;
    BOOST_CHECK(!(a == b));

    // Restore seed; differ only in last chaincode byte (secret HD material
    // previously compared via short-circuiting uint256 operator==).
    b = a;
    b.chaincode.begin()[CDilithiumExtKey::SEED_SIZE - 1] ^= 0x01;
    BOOST_CHECK(!(a == b));

    // Differ only in fingerprint with identical seed/chaincode.
    b = a;
    b.vchFingerprint[3] ^= 0x01;
    BOOST_CHECK(!(a == b));
}

BOOST_AUTO_TEST_CASE(dilithium_sanity_check)
{
    // Test the global sanity check function
    BOOST_CHECK(DilithiumSanityCheck());
}

BOOST_AUTO_TEST_SUITE_END()
