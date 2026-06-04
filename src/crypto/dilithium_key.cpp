// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_key.h>

#include <crypto/common.h>
#include <crypto/hmac_sha512.h>
#include <hash.h>
#include <random.h>
#include <support/cleanse.h>
#include <util/strencodings.h>

#include <cassert>
#include <cstring>
#include <cstdio>

extern "C" {
#include "dilithium_wrapper.h"
}

// CDilithiumKey implementation

namespace {
/**
 * Build (sk, pk) deterministically from a 32-byte seed and store the
 * concatenation `sk || pk` into `out`. Returns true on success.
 *
 * This is the single point of contact with the (now seeded) Dilithium
 * reference implementation. Both MakeNewKey() and GenerateFromEntropy() route
 * through this helper so they cannot diverge.
 */
bool DilithiumExpandSeedIntoKeydata(const unsigned char seed[BTQ_DILITHIUM_SEED_SIZE],
                                    unsigned char* out /* SECRET_KEY_SIZE + PUBLIC_KEY_SIZE bytes */)
{
    std::array<unsigned char, DilithiumConstants::PUBLIC_KEY_SIZE> pk{};
    std::array<unsigned char, DilithiumConstants::SECRET_KEY_SIZE> sk{};
    if (btq_dilithium_keypair_from_seed(pk.data(), sk.data(), seed) != 0) {
        memory_cleanse(sk.data(), sk.size());
        return false;
    }
    memcpy(out, sk.data(), DilithiumConstants::SECRET_KEY_SIZE);
    memcpy(out + DilithiumConstants::SECRET_KEY_SIZE, pk.data(), DilithiumConstants::PUBLIC_KEY_SIZE);
    memory_cleanse(sk.data(), sk.size());
    return true;
}
} // namespace

bool CDilithiumKey::MakeNewKey()
{
    // Sample exactly 32 bytes of strong randomness; the Dilithium reference
    // implementation internally expands those into (rho, key, rhoprime) via
    // SHAKE256 - no need to over-collect entropy as the prior code did.
    std::array<unsigned char, BTQ_DILITHIUM_SEED_SIZE> seed;
    try {
        GetStrongRandBytes(Span<unsigned char>(seed.data(), seed.size()));
    } catch (...) {
        ClearKeyData();
        return false;
    }

    MakeKeyData();
    if (!DilithiumExpandSeedIntoKeydata(seed.data(), keydata->data())) {
        ClearKeyData();
        memory_cleanse(seed.data(), seed.size());
        return false;
    }
    memory_cleanse(seed.data(), seed.size());
    return true;
}

bool CDilithiumKey::GenerateFromEntropy(const std::vector<unsigned char>& entropy)
{
    // We require exactly 32 bytes: this is the SEEDBYTES the Dilithium ref
    // impl consumes directly. Previously the code expanded entropy with
    // HMAC-SHA512 into a 2560-byte buffer and then handed that buffer to
    // crypto_sign_keypair(), which silently overwrote it with randombytes()
    // - so HD derivation was effectively a coin flip every call. Fixed by
    // routing through btq_dilithium_keypair_from_seed().
    if (entropy.size() != BTQ_DILITHIUM_SEED_SIZE) {
        ClearKeyData();
        return false;
    }

    MakeKeyData();
    if (!DilithiumExpandSeedIntoKeydata(entropy.data(), keydata->data())) {
        ClearKeyData();
        return false;
    }
    return true;
}

CDilithiumPubKey CDilithiumKey::GetPubKey() const
{
    if (!IsValid()) {
        return CDilithiumPubKey(); // Return invalid pubkey
    }
    
    // Get the stored public key from the key data
    const unsigned char* pk_data = keydata->data() + DilithiumConstants::SECRET_KEY_SIZE;
    return CDilithiumPubKey(pk_data, pk_data + DilithiumConstants::PUBLIC_KEY_SIZE);
}

bool CDilithiumKey::Sign(const uint256& hash, std::vector<unsigned char>& vchSig, 
                        const std::vector<unsigned char>& context) const
{
    if (!IsValid()) {
        return false;
    }
    
    // Use the hash as the message to sign
    return SignMessage(Span<const unsigned char>(hash.begin(), hash.size()), vchSig, context);
}

bool CDilithiumKey::SignMessage(Span<const unsigned char> message, std::vector<unsigned char>& vchSig,
                               const std::vector<unsigned char>& context) const
{
    if (!IsValid()) {
        return false;
    }
    
    // Prepare signature buffer
    vchSig.resize(DilithiumConstants::SIGNATURE_SIZE);
    size_t siglen = 0;
    
    // Create signature using Dilithium
    int result = btq_dilithium_sign(
        vchSig.data(), &siglen,
        message.data(), message.size(),
        context.data(), context.size(),
        keydata->data()
    );
    
    
    if (result != 0) {
        vchSig.clear();
        return false;
    }
    
    // Resize to actual signature length
    vchSig.resize(siglen);
    return true;
}

bool CDilithiumKey::VerifyPubKey(const CDilithiumPubKey& pubkey) const
{
    if (!IsValid() || !pubkey.IsValid()) {
        return false;
    }
    
    // Get our public key and compare
    CDilithiumPubKey our_pubkey = GetPubKey();
    return our_pubkey == pubkey;
}

bool CDilithiumKey::KeyDataSelfChecks() const
{
    if (!IsValid()) return false;

    const CDilithiumPubKey pubkey = GetPubKey();
    if (!pubkey.IsValid() || !pubkey.IsFullyValid()) return false;

    std::vector<unsigned char> signature;
    if (!Sign(uint256::ONE, signature)) return false;
    return pubkey.Verify(uint256::ONE, signature);
}

bool CDilithiumKey::Load(Span<const unsigned char> privkey)
{
    if (privkey.size() != GetKeySize()) {
        ClearKeyData();
        return false;
    }
    
    MakeKeyData();
    memcpy(keydata->data(), privkey.data(), privkey.size());
    if (!KeyDataSelfChecks()) {
        ClearKeyData();
        return false;
    }
    return true;
}

std::vector<unsigned char> CDilithiumKey::Serialize() const
{
    if (!IsValid()) {
        return {};
    }
    
    return std::vector<unsigned char>(keydata->begin(), keydata->end());
}

// Global initialization functions

void DilithiumInit()
{
    // Initialize any global Dilithium state if needed
    // For the reference implementation, no special initialization is required
}

bool DilithiumSanityCheck()
{
    // Perform a basic sanity check by generating a key pair and signing/verifying
    try {
        CDilithiumKey key;
        key.MakeNewKey();
        
        if (!key.IsValid()) {
            return false;
        }
        
        CDilithiumPubKey pubkey = key.GetPubKey();
        if (!pubkey.IsValid()) {
            return false;
        }
        
        // Test signing and verification
        uint256 test_hash;
        test_hash.SetHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        
        std::vector<unsigned char> signature;
        if (!key.Sign(test_hash, signature)) {
            return false;
        }
        
        if (!pubkey.Verify(test_hash, signature)) {
            return false;
        }
        
        // Test with different message should fail
        uint256 different_hash;
        different_hash.SetHex("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");
        
        if (pubkey.Verify(different_hash, signature)) {
            return false; // Should have failed
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

// CDilithiumExtKey implementation
//
// The persisted state is the 32-byte HD seed + BIP32 chaincode + position.
// The 2560/1312-byte expanded keypair is regenerated from the seed every time
// it is needed via `key.GenerateFromEntropy(seed_vec)`. This avoids storing
// 3872 redundant bytes per node and - critically - keeps the relationship
// between seed and (sk, pk) deterministic and verifiable.

namespace {
/** Mask used to test whether a BIP32 child index is hardened. */
constexpr uint32_t DILITHIUM_HARDENED_BIT = 0x80000000u;
} // namespace

void CDilithiumExtKey::Encode(unsigned char code[DILITHIUM_EXTKEY_SIZE]) const
{
    code[0] = nDepth;
    memcpy(code + 1, vchFingerprint, 4);
    WriteBE32(code + 5, nChild);
    memcpy(code + 9, chaincode.begin(), 32);
    memcpy(code + 41, seed.data(), SEED_SIZE);
}

void CDilithiumExtKey::Decode(const unsigned char code[DILITHIUM_EXTKEY_SIZE])
{
    nDepth = code[0];
    memcpy(vchFingerprint, code + 1, 4);
    nChild = ReadBE32(code + 5);
    memcpy(chaincode.begin(), code + 9, 32);
    memcpy(seed.data(), code + 41, SEED_SIZE);

    // Master keys MUST have the BIP32 invariants nChild == 0 and
    // fingerprint == 0; reject anything else as corrupt rather than silently
    // promoting it to a master key.
    const bool master_ok = (nDepth != 0) || (nChild == 0 && ReadLE32(vchFingerprint) == 0);
    const bool child_metadata_ok = (nDepth == 0) || ((nChild & DILITHIUM_HARDENED_BIT) != 0);
    if (!master_ok || !child_metadata_ok) {
        // Wipe to a clearly-invalid state.
        nDepth = 0;
        memset(vchFingerprint, 0, 4);
        nChild = 0;
        chaincode = ChainCode{};
        seed.fill(0);
        key = CDilithiumKey();
        return;
    }

    // Regenerate the expanded keypair from the seed.
    std::vector<unsigned char> seed_vec(seed.begin(), seed.end());
    if (!key.GenerateFromEntropy(seed_vec)) {
        key = CDilithiumKey();
    }
}

bool CDilithiumExtKey::Derive(CDilithiumExtKey& out, unsigned int child_index) const
{
    if (nDepth == std::numeric_limits<unsigned char>::max()) return false;
    if (!key.IsValid()) return false;

    // Dilithium's lattice structure does not admit homomorphic public-key
    // derivation, so the only sound mode is fully hardened paths. Refuse
    // non-hardened indices rather than silently producing a keypair that
    // cannot be reconstructed from an extended pubkey.
    if ((child_index & DILITHIUM_HARDENED_BIT) == 0) return false;

    // BIP32-style derivation, adapted for Dilithium:
    //   I = HMAC-SHA512(key = parent_chaincode,
    //                   data = 0x00 || parent_seed (32B) || ser32(child_index))
    //   I_L (32B) -> child seed; I_R (32B) -> child chaincode.
    //
    // We use the parent SEED (not the 2560-byte packed sk) as the parent
    // material: seeds are stable, compact, and the durable source of truth.
    CHMAC_SHA512 hmac(chaincode.begin(), chaincode.size());
    const unsigned char zero_byte = 0x00;
    hmac.Write(&zero_byte, 1);
    hmac.Write(seed.data(), seed.size());
    uint32_t child_be = htobe32(child_index);
    hmac.Write(reinterpret_cast<const unsigned char*>(&child_be), sizeof(child_be));

    unsigned char I[64];
    hmac.Finalize(I);

    // Populate child metadata.
    out.nDepth = nDepth + 1;
    uint160 parent_id = key.GetPubKey().GetID();
    memcpy(out.vchFingerprint, &parent_id, 4);
    out.nChild = child_index;
    memcpy(out.seed.data(), I, SEED_SIZE);
    memcpy(out.chaincode.begin(), I + 32, 32);
    memory_cleanse(I, sizeof(I));

    // Expand the seed into the full keypair.
    std::vector<unsigned char> child_seed_vec(out.seed.begin(), out.seed.end());
    if (!out.key.GenerateFromEntropy(child_seed_vec)) {
        return false;
    }
    return out.key.IsValid();
}

CDilithiumExtPubKey CDilithiumExtKey::Neuter() const
{
    CDilithiumExtPubKey ret;
    ret.nDepth = nDepth;
    memcpy(ret.vchFingerprint, vchFingerprint, 4);
    ret.nChild = nChild;
    ret.chaincode = chaincode;
    ret.pubkey = key.GetPubKey();
    return ret;
}

void CDilithiumExtKey::SetSeed(Span<const std::byte> hd_seed)
{
    // Master key derivation: I = HMAC-SHA512("Dilithium seed", hd_seed).
    //   I_L (32B) -> master Dilithium seed.
    //   I_R (32B) -> master chaincode.
    static const unsigned char hashkey[] = {'D','i','l','i','t','h','i','u','m',' ','s','e','e','d'};
    CHMAC_SHA512 hmac(hashkey, sizeof(hashkey));
    hmac.Write(reinterpret_cast<const unsigned char*>(hd_seed.data()), hd_seed.size());

    unsigned char I[64];
    hmac.Finalize(I);

    memcpy(seed.data(), I, SEED_SIZE);
    memcpy(chaincode.begin(), I + 32, 32);
    memory_cleanse(I, sizeof(I));

    nDepth = 0;
    memset(vchFingerprint, 0, 4);
    nChild = 0;

    std::vector<unsigned char> seed_vec(seed.begin(), seed.end());
    if (!key.GenerateFromEntropy(seed_vec)) {
        key = CDilithiumKey();
    }
}

// CDilithiumExtPubKey implementation

void CDilithiumExtPubKey::Encode(unsigned char code[DILITHIUM_EXTPUBKEY_SIZE]) const
{
    code[0] = nDepth;
    memcpy(code + 1, vchFingerprint, 4);
    WriteBE32(code + 5, nChild);
    memcpy(code + 9, chaincode.begin(), 32);
    static_assert(DilithiumConstants::PUBLIC_KEY_SIZE == 1312,
                  "extpubkey layout assumes Dilithium2 (1312-byte) pubkey");
    memcpy(code + 41, pubkey.begin(), DilithiumConstants::PUBLIC_KEY_SIZE);
}

void CDilithiumExtPubKey::Decode(const unsigned char code[DILITHIUM_EXTPUBKEY_SIZE])
{
    nDepth = code[0];
    memcpy(vchFingerprint, code + 1, 4);
    nChild = ReadBE32(code + 5);
    memcpy(chaincode.begin(), code + 9, 32);
    pubkey.Set(code + 41, code + 41 + DilithiumConstants::PUBLIC_KEY_SIZE);
    const bool master_ok = (nDepth != 0) || (nChild == 0 && ReadLE32(vchFingerprint) == 0);
    const bool child_metadata_ok = (nDepth == 0) || ((nChild & DILITHIUM_HARDENED_BIT) != 0);
    if (!master_ok || !child_metadata_ok || !pubkey.IsFullyValid()) {
        nDepth = 0;
        memset(vchFingerprint, 0, 4);
        nChild = 0;
        chaincode = ChainCode{};
        pubkey = CDilithiumPubKey();
    }
}

bool CDilithiumExtPubKey::Derive(CDilithiumExtPubKey& /*out*/, unsigned int /*child_index*/) const
{
    // Dilithium's lattice structure has no analogue of secp256k1's group law,
    // so there is no public-only derivation that preserves the invariant
    // ExtPubKey.Derive(n).pubkey == ExtKey.Derive(n).GetPubKey(). Refusing
    // here is the honest behavior - callers that need child pubkeys must
    // derive from CDilithiumExtKey and call Neuter().
    return false;
}
