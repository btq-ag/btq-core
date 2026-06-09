// Copyright (c) 2024-2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_key.h>
#include <crypto/hmac_sha512.h>
#include <key_io.h>
#include <wallet/crypter.h>

#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include <boost/test/unit_test.hpp>

extern "C" {
#include <crypto/dilithium_wrapper.h>
}

using wallet::CKeyingMaterial;
using wallet::DeriveDilithiumKeyIV;
using wallet::EncryptDilithiumSecret;
using wallet::DecryptDilithiumSecret;
using wallet::DecryptDilithiumKey;
using wallet::KeyIDToIV;

BOOST_FIXTURE_TEST_SUITE(dilithium_wallet_tests, BasicTestingSetup)

namespace {
constexpr uint32_t HARDENED = 0x80000000u;

std::vector<unsigned char> MakeSeed(unsigned char fill)
{
    return std::vector<unsigned char>(BTQ_DILITHIUM_SEED_SIZE, fill);
}

/** Recompute the BIP32 I_L / I_R split for SetSeed outside the implementation. */
void ComputeSetSeedSplit(Span<const std::byte> hd_seed,
                         std::array<unsigned char, CDilithiumExtKey::SEED_SIZE>& out_seed,
                         ChainCode& out_chaincode)
{
    static const unsigned char hashkey[] = {'D','i','l','i','t','h','i','u','m',' ','s','e','e','d'};
    CHMAC_SHA512 hmac(hashkey, sizeof(hashkey));
    hmac.Write(reinterpret_cast<const unsigned char*>(hd_seed.data()), hd_seed.size());
    unsigned char I[64];
    hmac.Finalize(I);
    memcpy(out_seed.data(), I, CDilithiumExtKey::SEED_SIZE);
    memcpy(out_chaincode.begin(), I + CDilithiumExtKey::SEED_SIZE, CDilithiumExtKey::SEED_SIZE);
}

/** Recompute the BIP32 I_L / I_R split for Derive outside the implementation. */
void ComputeDeriveSplit(const CDilithiumExtKey& parent,
                        uint32_t child_index,
                        std::array<unsigned char, CDilithiumExtKey::SEED_SIZE>& out_seed,
                        ChainCode& out_chaincode)
{
    CHMAC_SHA512 hmac(parent.chaincode.begin(), parent.chaincode.size());
    const unsigned char zero_byte = 0x00;
    hmac.Write(&zero_byte, 1);
    hmac.Write(parent.seed.data(), parent.seed.size());
    const uint32_t child_be = htobe32(child_index);
    hmac.Write(reinterpret_cast<const unsigned char*>(&child_be), sizeof(child_be));
    unsigned char I[64];
    hmac.Finalize(I);
    // BIP32: I_L -> key material (seed), I_R -> chaincode.
    memcpy(out_seed.data(), I, CDilithiumExtKey::SEED_SIZE);
    memcpy(out_chaincode.begin(), I + CDilithiumExtKey::SEED_SIZE, CDilithiumExtKey::SEED_SIZE);
}
} // namespace

BOOST_AUTO_TEST_CASE(dilithium_key_wif_encoding)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());

    std::string wif = EncodeDilithiumSecret(key);
    BOOST_CHECK(!wif.empty());

    CDilithiumKey decoded_key = DecodeDilithiumSecret(wif);
    BOOST_CHECK(decoded_key.IsValid());
    BOOST_CHECK(decoded_key == key);

    CDilithiumPubKey original_pubkey = key.GetPubKey();
    CDilithiumPubKey decoded_pubkey = decoded_key.GetPubKey();
    BOOST_CHECK(original_pubkey == decoded_pubkey);
}

BOOST_AUTO_TEST_CASE(dilithium_key_encryption)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());

    CKeyingMaterial master_key(32, 0);
    for (int i = 0; i < 32; i++) {
        master_key[i] = i;
    }

    // Production path derives a 32-byte IV from the 20-byte key id; decrypt must match.
    const CKeyID key_id = CKeyID(key.GetPubKey().GetID());
    const uint256 iv{DeriveDilithiumKeyIV(key_id)};

    std::vector<unsigned char> encrypted_secret;
    CKeyingMaterial secret(key.begin(), key.end());

    BOOST_CHECK(EncryptDilithiumSecret(master_key, secret, iv, encrypted_secret));
    BOOST_CHECK(!encrypted_secret.empty());

    CKeyingMaterial decrypted_secret;
    BOOST_CHECK(DecryptDilithiumSecret(master_key, encrypted_secret, iv, decrypted_secret));
    BOOST_CHECK(decrypted_secret.size() == CDilithiumKey::GetKeySize());
    BOOST_CHECK(std::equal(secret.begin(), secret.end(), decrypted_secret.begin()));

    CDilithiumKey decrypted_key;
    BOOST_CHECK(DecryptDilithiumKey(master_key, encrypted_secret, key_id, decrypted_key));
    BOOST_CHECK(decrypted_key.IsValid());
    BOOST_CHECK(decrypted_key == key);
}

BOOST_AUTO_TEST_CASE(dilithium_key_decryption_accepts_legacy_keyid_iv)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());

    CKeyingMaterial master_key(32, 0);
    for (int i = 0; i < 32; i++) {
        master_key[i] = 0xff - i;
    }

    const CKeyID key_id = CKeyID(key.GetPubKey().GetID());
    uint256 legacy_iv;
    std::copy(key_id.begin(), key_id.end(), legacy_iv.begin());

    std::vector<unsigned char> encrypted_secret;
    CKeyingMaterial secret(key.begin(), key.end());
    BOOST_REQUIRE(EncryptDilithiumSecret(master_key, secret, legacy_iv, encrypted_secret));

    CDilithiumKey decrypted_key;
    BOOST_CHECK(DecryptDilithiumKey(master_key, encrypted_secret, key_id, decrypted_key));
    BOOST_CHECK(decrypted_key == key);
}

BOOST_AUTO_TEST_CASE(dilithium_key_encryption_wrong_iv_fails)
{
    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());

    CKeyingMaterial master_key(32, 0);
    for (int i = 0; i < 32; i++) {
        master_key[i] = static_cast<unsigned char>(i);
    }

    const CKeyID key_id = CKeyID(key.GetPubKey().GetID());
    const uint256 iv = KeyIDToIV(key_id);

    std::vector<unsigned char> encrypted_secret;
    CKeyingMaterial secret(key.begin(), key.end());
    BOOST_REQUIRE(EncryptDilithiumSecret(master_key, secret, iv, encrypted_secret));

    CDilithiumKey decrypted_key;
    CKeyID wrong_id = key_id;
    wrong_id.begin()[0] ^= 0x01;
    BOOST_CHECK(!DecryptDilithiumKey(master_key, encrypted_secret, wrong_id, decrypted_key));
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
    BOOST_CHECK_EQUAL(sig_a.size(), sig_b.size());
    BOOST_CHECK(sig_a == sig_b);
}

BOOST_AUTO_TEST_CASE(dilithium_key_storage)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());

    CPrivKey privkey = key.GetPrivKey();
    BOOST_CHECK(!privkey.empty());
    BOOST_CHECK(privkey.size() == CDilithiumKey::GetKeySize());

    CDilithiumKey loaded_key;
    BOOST_CHECK(loaded_key.Load(Span<const unsigned char>(privkey.data(), privkey.size())));
    BOOST_CHECK(loaded_key.IsValid());
    BOOST_CHECK(loaded_key == key);
}

// ---- Regression coverage for issue #53 (HD wallet) ---------------------

// BTQ-AUDIT-019 / issue #53 (1): GenerateFromEntropy must be deterministic.
// Pre-fix the entropy was discarded inside the Dilithium reference impl, so
// the same seed produced different keys on every call.
BOOST_AUTO_TEST_CASE(dilithium_generate_from_entropy_is_deterministic)
{
    const auto seed = MakeSeed(0x42);

    CDilithiumKey a, b;
    BOOST_REQUIRE(a.GenerateFromEntropy(seed));
    BOOST_REQUIRE(b.GenerateFromEntropy(seed));

    BOOST_CHECK(a.IsValid());
    BOOST_CHECK(b.IsValid());
    BOOST_CHECK(a == b);
    BOOST_CHECK(a.GetPubKey() == b.GetPubKey());

    // A different seed must yield a different key.
    CDilithiumKey c;
    BOOST_REQUIRE(c.GenerateFromEntropy(MakeSeed(0x43)));
    BOOST_CHECK(c != a);

    // Wrong-size entropy is rejected on a never-before-used key.
    CDilithiumKey d;
    BOOST_CHECK(!d.GenerateFromEntropy(std::vector<unsigned char>(31, 0)));
    BOOST_CHECK(!d.IsValid());
    BOOST_CHECK(!d.GenerateFromEntropy(std::vector<unsigned char>(33, 0)));
    BOOST_CHECK(!d.IsValid());
}

// PR #54 review: wrong-size entropy must clear a previously valid key rather
// than leaving stale signing material behind.
BOOST_AUTO_TEST_CASE(dilithium_generate_from_entropy_clears_stale_key)
{
    CDilithiumKey key;
    const auto good_seed = MakeSeed(0x42);
    BOOST_REQUIRE(key.GenerateFromEntropy(good_seed));
    BOOST_REQUIRE(key.IsValid());
    const CDilithiumKey good_key = key;

    BOOST_CHECK(!key.GenerateFromEntropy(std::vector<unsigned char>(31, 0)));
    BOOST_CHECK(!key.IsValid());

    BOOST_CHECK(!key.GenerateFromEntropy(std::vector<unsigned char>(33, 0)));
    BOOST_CHECK(!key.IsValid());

    // Object must remain usable after a rejected call.
    BOOST_REQUIRE(key.GenerateFromEntropy(good_seed));
    BOOST_CHECK(key == good_key);
}

// PR #54 review: GenerateFromEntropy must route the 32-byte seed directly to
// btq_dilithium_keypair_from_seed (pre-fix it expanded via HMAC then called
// keypair() which overwrote the buffer with randombytes()).
BOOST_AUTO_TEST_CASE(dilithium_generate_from_entropy_matches_keypair_from_seed)
{
    const auto seed = MakeSeed(0x5a);

    CDilithiumKey key;
    BOOST_REQUIRE(key.GenerateFromEntropy(seed));

    std::array<unsigned char, DilithiumConstants::PUBLIC_KEY_SIZE> pk{};
    std::array<unsigned char, DilithiumConstants::SECRET_KEY_SIZE> sk{};
    BOOST_REQUIRE_EQUAL(btq_dilithium_keypair_from_seed(pk.data(), sk.data(), seed.data()), 0);

    BOOST_CHECK(key.GetPubKey() == CDilithiumPubKey(pk.data(), pk.data() + pk.size()));
}

// PR #54 review: Derive() had I_L/I_R swapped relative to SetSeed and BIP32.
// Self-consistency tests would not catch this; compare against an independent
// HMAC recomputation that assigns I_L -> seed and I_R -> chaincode.
BOOST_AUTO_TEST_CASE(dilithium_extkey_bip32_il_ir_split)
{
    const auto raw_seed = std::vector<std::byte>(32, std::byte{0x99});
    const Span<const std::byte> seed_span(raw_seed.data(), raw_seed.size());

    CDilithiumExtKey master;
    master.SetSeed(seed_span);

    std::array<unsigned char, CDilithiumExtKey::SEED_SIZE> expected_seed{};
    ChainCode expected_chaincode;
    ComputeSetSeedSplit(seed_span, expected_seed, expected_chaincode);
    BOOST_CHECK_EQUAL_COLLECTIONS(master.seed.begin(), master.seed.end(),
                                  expected_seed.begin(), expected_seed.end());
    BOOST_CHECK(master.chaincode == expected_chaincode);

    const uint32_t child_index = HARDENED | 42;
    CDilithiumExtKey child;
    BOOST_REQUIRE(master.Derive(child, child_index));

    ComputeDeriveSplit(master, child_index, expected_seed, expected_chaincode);
    BOOST_CHECK_EQUAL_COLLECTIONS(child.seed.begin(), child.seed.end(),
                                  expected_seed.begin(), expected_seed.end());
    BOOST_CHECK(child.chaincode == expected_chaincode);
}

// PR #54 review: WIF decode used max_ret_len=34 and silently truncated the
// 3872-byte Dilithium secret. Verify the full packed key round-trips.
BOOST_AUTO_TEST_CASE(dilithium_wif_full_key_roundtrip)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    BOOST_CHECK_EQUAL(CDilithiumKey::GetKeySize(),
                      DilithiumConstants::SECRET_KEY_SIZE + DilithiumConstants::PUBLIC_KEY_SIZE);

    const std::string wif = EncodeDilithiumSecret(key);
    BOOST_CHECK(!wif.empty());

    const CDilithiumKey decoded = DecodeDilithiumSecret(wif);
    BOOST_REQUIRE(decoded.IsValid());
    BOOST_CHECK(decoded == key);
    BOOST_CHECK_EQUAL(decoded.GetPrivKey().size(), CDilithiumKey::GetKeySize());
}

// BTQ-AUDIT-017 / issue #53 (2,3): SetSeed must produce a valid master key
// and the same input HD seed must always produce the same master key + chaincode.
BOOST_AUTO_TEST_CASE(dilithium_extkey_setseed_is_deterministic)
{
    const std::array<std::byte, 16> raw_seed{};
    Span<const std::byte> seed_span(raw_seed.data(), raw_seed.size());

    CDilithiumExtKey a, b;
    a.SetSeed(seed_span);
    b.SetSeed(seed_span);

    BOOST_REQUIRE(a.key.IsValid());
    BOOST_REQUIRE(b.key.IsValid());
    BOOST_CHECK(a == b);
    BOOST_CHECK(a.key == b.key);
    BOOST_CHECK_EQUAL(a.nDepth, 0);
    BOOST_CHECK_EQUAL(a.nChild, 0u);
}

// BTQ-AUDIT-025 / issue #53 (4): non-hardened derivation must be refused,
// hardened derivation must be deterministic and produce a distinct child.
BOOST_AUTO_TEST_CASE(dilithium_extkey_hardened_only_derivation)
{
    CDilithiumExtKey master;
    const auto raw_seed = std::vector<std::byte>(32, std::byte{0x11});
    master.SetSeed(Span<const std::byte>(raw_seed.data(), raw_seed.size()));
    BOOST_REQUIRE(master.key.IsValid());

    // Non-hardened indices are refused.
    CDilithiumExtKey child_nh;
    BOOST_CHECK(!master.Derive(child_nh, 0));
    BOOST_CHECK(!master.Derive(child_nh, 5));
    BOOST_CHECK(!master.Derive(child_nh, 0x7fffffff));

    // Hardened derivation succeeds and is deterministic.
    CDilithiumExtKey child_a, child_b;
    BOOST_REQUIRE(master.Derive(child_a, HARDENED | 0));
    BOOST_REQUIRE(master.Derive(child_b, HARDENED | 0));
    BOOST_CHECK(child_a == child_b);
    BOOST_CHECK(child_a.key.IsValid());
    BOOST_CHECK(child_a.key != master.key);
    BOOST_CHECK_EQUAL(child_a.nDepth, 1);
    BOOST_CHECK_EQUAL(child_a.nChild, HARDENED | 0u);

    // Different child indices yield different keys.
    CDilithiumExtKey child_c;
    BOOST_REQUIRE(master.Derive(child_c, HARDENED | 1));
    BOOST_CHECK(!(child_c == child_a));

    // Multi-level path m/0'/0' is also deterministic.
    CDilithiumExtKey grand_a, grand_b;
    BOOST_REQUIRE(child_a.Derive(grand_a, HARDENED | 0));
    BOOST_REQUIRE(child_b.Derive(grand_b, HARDENED | 0));
    BOOST_CHECK(grand_a == grand_b);
    BOOST_CHECK(grand_a.key.IsValid());
    BOOST_CHECK_EQUAL(grand_a.nDepth, 2);
}

// BTQ-AUDIT-021 / issue #53 (5): Encode/Decode must round-trip without
// crashing (previously Encode asserted key.size() == 2560 on a 3872-byte
// buffer and Decode handed 2560 bytes to a Set() that required 3872).
BOOST_AUTO_TEST_CASE(dilithium_extkey_encode_decode_roundtrip)
{
    CDilithiumExtKey master;
    const auto raw_seed = std::vector<std::byte>(32, std::byte{0x77});
    master.SetSeed(Span<const std::byte>(raw_seed.data(), raw_seed.size()));
    BOOST_REQUIRE(master.key.IsValid());

    std::array<unsigned char, DILITHIUM_EXTKEY_SIZE> buf{};
    master.Encode(buf.data());

    CDilithiumExtKey decoded;
    decoded.Decode(buf.data());

    BOOST_CHECK(decoded == master);
    BOOST_CHECK(decoded.key.IsValid());
    BOOST_CHECK(decoded.key == master.key);
    BOOST_CHECK(decoded.key.GetPubKey() == master.key.GetPubKey());
}

// CDilithiumExtPubKey Encode/Decode previously over-stated the pubkey size
// (1952 instead of 1312); make sure the Dilithium2 layout round-trips.
BOOST_AUTO_TEST_CASE(dilithium_extpubkey_encode_decode_roundtrip)
{
    CDilithiumExtKey master;
    const auto raw_seed = std::vector<std::byte>(32, std::byte{0x55});
    master.SetSeed(Span<const std::byte>(raw_seed.data(), raw_seed.size()));
    BOOST_REQUIRE(master.key.IsValid());

    CDilithiumExtPubKey neutered = master.Neuter();
    BOOST_REQUIRE(neutered.pubkey.IsValid());

    std::array<unsigned char, DILITHIUM_EXTPUBKEY_SIZE> buf{};
    neutered.Encode(buf.data());

    CDilithiumExtPubKey decoded;
    decoded.Decode(buf.data());

    BOOST_CHECK(decoded == neutered);
    BOOST_CHECK(decoded.pubkey == master.key.GetPubKey());
}

// Public-only derivation cannot work for Dilithium (no group law); the
// implementation must say so by returning false rather than producing a
// keypair that fails to verify.
BOOST_AUTO_TEST_CASE(dilithium_extpubkey_derive_unsupported)
{
    CDilithiumExtKey master;
    const auto raw_seed = std::vector<std::byte>(32, std::byte{0x33});
    master.SetSeed(Span<const std::byte>(raw_seed.data(), raw_seed.size()));
    CDilithiumExtPubKey neutered = master.Neuter();

    CDilithiumExtPubKey child;
    BOOST_CHECK(!neutered.Derive(child, 0));
    BOOST_CHECK(!neutered.Derive(child, HARDENED | 0));
}

// End-to-end HD path: derived key signs, neutered parent's child pubkey
// verifies, and a wrong message is rejected. Proves wallet seed
// backup/restore actually works.
BOOST_AUTO_TEST_CASE(dilithium_hd_path_sign_and_verify)
{
    CDilithiumExtKey master;
    const auto raw_seed = std::vector<std::byte>(32, std::byte{0xab});
    master.SetSeed(Span<const std::byte>(raw_seed.data(), raw_seed.size()));

    // m / 44' / 0' / 0' / 0' / 0'  (all hardened: the only valid Dilithium HD)
    CDilithiumExtKey k;
    BOOST_REQUIRE(master.Derive(k, HARDENED | 44));
    {
        CDilithiumExtKey next;
        BOOST_REQUIRE(k.Derive(next, HARDENED | 0));
        k = next;
    }
    {
        CDilithiumExtKey next;
        BOOST_REQUIRE(k.Derive(next, HARDENED | 0));
        k = next;
    }
    {
        CDilithiumExtKey next;
        BOOST_REQUIRE(k.Derive(next, HARDENED | 0));
        k = next;
    }
    {
        CDilithiumExtKey next;
        BOOST_REQUIRE(k.Derive(next, HARDENED | 0));
        k = next;
    }
    BOOST_REQUIRE(k.key.IsValid());

    // Reconstruct from a freshly seeded master: must hit the same child key.
    CDilithiumExtKey master2;
    master2.SetSeed(Span<const std::byte>(raw_seed.data(), raw_seed.size()));
    CDilithiumExtKey k2;
    BOOST_REQUIRE(master2.Derive(k2, HARDENED | 44));
    for (int i = 0; i < 4; ++i) {
        CDilithiumExtKey next;
        BOOST_REQUIRE(k2.Derive(next, HARDENED | 0));
        k2 = next;
    }
    BOOST_CHECK(k == k2);

    // Neuter() must expose the same pubkey as the derived private key.
    BOOST_CHECK(k.Neuter().pubkey == k.key.GetPubKey());

    // Sign with the derived key, verify with its pubkey.
    uint256 msg = uint256::ONE;
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(k.key.Sign(msg, sig));
    BOOST_CHECK(k.key.GetPubKey().Verify(msg, sig));
    BOOST_CHECK(!k.key.GetPubKey().Verify(uint256::ZERO, sig));
}

BOOST_AUTO_TEST_CASE(dilithium_key_signature_verification)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());

    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());

    uint256 message_hash = uint256::ONE;

    std::vector<unsigned char> signature;
    BOOST_CHECK(key.Sign(message_hash, signature));
    BOOST_CHECK(!signature.empty());

    BOOST_CHECK(pubkey.Verify(message_hash, signature));

    uint256 wrong_hash = uint256::ZERO;
    BOOST_CHECK(!pubkey.Verify(wrong_hash, signature));
}

BOOST_AUTO_TEST_SUITE_END()
