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

bool CDilithiumKey::GenerateFromEntropy(Span<const unsigned char> entropy)
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

CPrivKey CDilithiumKey::Serialize() const
{
    if (!IsValid()) {
        return {};
    }

    return CPrivKey(keydata->begin(), keydata->end());
}

// CDilithiumExtKey implementation
//
// The state is the 32-byte HD seed + BIP32 chaincode + position.
// The 2560/1312-byte expanded keypair is regenerated from the seed every time
// it is needed via `key.GenerateFromEntropy(seed)`. This avoids storing
// 3872 redundant bytes per node and - critically - keeps the relationship
// between seed and (sk, pk) deterministic and verifiable.

namespace {
/** Mask used to test whether a BIP32 child index is hardened. */
constexpr uint32_t DILITHIUM_HARDENED_BIT = 0x80000000u;
} // namespace

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
    const bool ok = out.key.GenerateFromEntropy(out.seed);
    if (!ok) {
        return false;
    }
    return out.key.IsValid();
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

    const bool ok = key.GenerateFromEntropy(seed);
    if (!ok) {
        key = CDilithiumKey();
    }
}
