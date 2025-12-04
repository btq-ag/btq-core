// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_hd_key.h>
#include <crypto/hmac_sha512.h>
#include <crypto/sha256.h>
#include <util/strencodings.h>

#include <cstring>

// Dilithium-specific BIP32 hash function
static void DilithiumBIP32Hash(const DilithiumChainCode& chaincode, unsigned int nChild, 
                              const unsigned char* pubkey, size_t pubkey_size, unsigned char* out)
{
    // Use Dilithium-specific context for BIP32
    static const unsigned char dilithium_hashkey[] = {'D','i','l','i','t','h','i','u','m',' ','s','e','e','d'};
    
    std::vector<unsigned char> data;
    data.reserve(4 + 32 + pubkey_size);
    
    // Add child index (big-endian)
    data.push_back((nChild >> 24) & 0xFF);
    data.push_back((nChild >> 16) & 0xFF);
    data.push_back((nChild >> 8) & 0xFF);
    data.push_back(nChild & 0xFF);
    
    // Add chaincode
    data.insert(data.end(), chaincode.begin(), chaincode.end());
    
    // Add public key
    data.insert(data.end(), pubkey, pubkey + pubkey_size);
    
    // HMAC-SHA512 with Dilithium context
    CHMAC_SHA512{dilithium_hashkey, sizeof(dilithium_hashkey)}
        .Write(data.data(), data.size())
        .Finalize(out);
}

bool DilithiumExtKey::Derive(DilithiumExtKey& out, unsigned int nChild) const
{
    if (nDepth == std::numeric_limits<unsigned char>::max()) return false;
    
    out.nDepth = nDepth + 1;
    
    // Set fingerprint from parent key
    DilithiumKeyID parent_id(key.GetPubKey());
    memcpy(out.vchFingerprint, parent_id.begin(), 4);
    out.nChild = nChild;
    
    // Derive new key using Dilithium-specific BIP32
    std::vector<unsigned char, secure_allocator<unsigned char>> vout(64);
    
    if ((nChild >> 31) == 0) {
        // Non-hardened derivation
        CDilithiumPubKey pubkey = key.GetPubKey();
        DilithiumBIP32Hash(chaincode, nChild, pubkey.data(), pubkey.size(), vout.data());
    } else {
        // Hardened derivation - use private key
        DilithiumBIP32Hash(chaincode, nChild, reinterpret_cast<const unsigned char*>(key.data()), key.size(), vout.data());
    }
    
    // Set new chaincode
    memcpy(out.chaincode.begin(), vout.data() + 32, 32);
    
    // Derive new private key
    // For Dilithium, we need to handle the larger key size
    std::vector<unsigned char> new_key_data(key.size());
    memcpy(new_key_data.data(), key.data(), key.size());
    
    // Add the derived data to the key (simplified for Dilithium)
    // In practice, this would need Dilithium-specific key derivation
    for (size_t i = 0; i < std::min(static_cast<size_t>(key.size()), static_cast<size_t>(32)); ++i) {
        new_key_data[i] ^= vout[i];
    }
    
    out.key.Set(new_key_data.data(), new_key_data.data() + new_key_data.size());
    
    return out.key.IsValid();
}

void DilithiumExtKey::SetSeed(Span<const std::byte> seed)
{
    // Use Dilithium-specific seed generation
    static const unsigned char dilithium_hashkey[] = {'D','i','l','i','t','h','i','u','m',' ','s','e','e','d'};
    std::vector<unsigned char, secure_allocator<unsigned char>> vout(64);
    
    CHMAC_SHA512{dilithium_hashkey, sizeof(dilithium_hashkey)}
        .Write(UCharCast(seed.data()), seed.size())
        .Finalize(vout.data());
    
    // Set the key from first 32 bytes (for Dilithium, we need more data)
    // This is a simplified approach - in practice, Dilithium needs 1312 bytes
    std::vector<unsigned char> key_data(1312);
    memcpy(key_data.data(), vout.data(), 32);
    
    // Fill the rest with deterministic data
    for (size_t i = 32; i < 1312; i += 32) {
        CHMAC_SHA512{dilithium_hashkey, sizeof(dilithium_hashkey)}
            .Write(vout.data(), 64)
            .Write(reinterpret_cast<const unsigned char*>(&i), sizeof(i))
            .Finalize(vout.data());
        memcpy(key_data.data() + i, vout.data(), std::min((size_t)32, 1312 - i));
    }
    
    key.Set(key_data.data(), key_data.data() + key_data.size());
    memcpy(chaincode.begin(), vout.data() + 32, 32);
    nDepth = 0;
    nChild = 0;
    memset(vchFingerprint, 0, sizeof(vchFingerprint));
}

DilithiumExtPubKey DilithiumExtKey::Neuter() const
{
    DilithiumExtPubKey ret;
    ret.pubkey = key.GetPubKey();
    ret.chaincode = chaincode;
    ret.nDepth = nDepth;
    ret.nChild = nChild;
    memcpy(ret.vchFingerprint, vchFingerprint, sizeof(vchFingerprint));
    return ret;
}

bool DilithiumExtPubKey::Derive(DilithiumExtPubKey& out, unsigned int nChild) const
{
    if (nDepth == std::numeric_limits<unsigned char>::max()) return false;
    
    // Can't derive hardened keys from public key only
    if (nChild >> 31) return false;
    
    out.nDepth = nDepth + 1;
    
    // Set fingerprint from parent key
    DilithiumKeyID parent_id(pubkey);
    memcpy(out.vchFingerprint, parent_id.begin(), 4);
    out.nChild = nChild;
    
    // Derive new public key using Dilithium-specific BIP32
    std::vector<unsigned char, secure_allocator<unsigned char>> vout(64);
    DilithiumBIP32Hash(chaincode, nChild, pubkey.data(), pubkey.size(), vout.data());
    
    // Set new chaincode
    memcpy(out.chaincode.begin(), vout.data() + 32, 32);
    
    // For Dilithium, we need to derive the new public key
    // This is simplified - in practice, Dilithium key derivation is more complex
    std::vector<unsigned char> new_pubkey_data(pubkey.size());
    memcpy(new_pubkey_data.data(), pubkey.data(), pubkey.size());
    
    // Add the derived data to the public key
    for (size_t i = 0; i < std::min(static_cast<size_t>(pubkey.size()), static_cast<size_t>(32)); ++i) {
        new_pubkey_data[i] ^= vout[i];
    }
    
    out.pubkey.Set(new_pubkey_data.data(), new_pubkey_data.data() + new_pubkey_data.size());
    
    return out.pubkey.IsValid();
}
