// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_key.h>

#include <crypto/common.h>
#include <crypto/hmac_sha512.h>
#include <hash.h>
#include <random.h>
#include <util/strencodings.h>

#include <cassert>
#include <cstring>
#include <cstdio>
#include <logging.h>

extern "C" {
#include "dilithium_wrapper.h"
}

// CDilithiumKey implementation

void CDilithiumKey::MakeNewKey()
{
    MakeKeyData();
    
    // Generate public key buffer
    std::array<unsigned char, DilithiumConstants::PUBLIC_KEY_SIZE> pk;
    
    // Generate keypair using Dilithium's btq_dilithium_keypair
    int result = btq_dilithium_keypair(pk.data(), keydata->data());
    
    if (result != 0) {
        // Key generation failed, clear the key
        ClearKeyData();
    } else {
        // Store the public key in the key object
        // We'll store it at the end of the secret key data
        memcpy(keydata->data() + DilithiumConstants::SECRET_KEY_SIZE, pk.data(), DilithiumConstants::PUBLIC_KEY_SIZE);
    }
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

bool CDilithiumKey::Load(Span<const unsigned char> privkey)
{
    if (privkey.size() != GetKeySize()) {
        ClearKeyData();
        return false;
    }
    
    MakeKeyData();
    memcpy(keydata->data(), privkey.data(), privkey.size());
    return true;
}

std::vector<unsigned char> CDilithiumKey::Serialize() const
{
    if (!IsValid()) {
        return {};
    }
    
    return std::vector<unsigned char>(keydata->begin(), keydata->end());
}

// CDilithiumPubKey implementation

uint256 CDilithiumPubKey::GetHash() const
{
    return Hash(Span{vch});
}

uint160 CDilithiumPubKey::GetID() const
{
    return Hash160(Span{vch});
}

bool CDilithiumPubKey::IsValid() const
{
    // A Dilithium public key is valid if it's not all zeros
    for (size_t i = 0; i < SIZE; ++i) {
        if (vch[i] != 0) {
            return true;
        }
    }
    return false;
}

bool CDilithiumPubKey::IsFullyValid() const
{
    // For now, we use the same validation as IsValid()
    // In a more complete implementation, we might want to validate
    // the mathematical structure of the public key
    return IsValid();
}

bool CDilithiumPubKey::Verify(const uint256& hash, const std::vector<unsigned char>& vchSig,
                             const std::vector<unsigned char>& context) const
{
    if (!IsValid() || vchSig.empty()) {
        return false;
    }
    
    // Use the hash as the message to verify
    return VerifyMessage(Span<const unsigned char>(hash.begin(), hash.size()), vchSig, context);
}

bool CDilithiumPubKey::VerifyMessage(Span<const unsigned char> message, const std::vector<unsigned char>& vchSig,
                                    const std::vector<unsigned char>& context) const
{
    if (!IsValid() || vchSig.empty()) {
        return false;
    }
    
    // Verify signature using Dilithium
    int result = btq_dilithium_verify(
        vchSig.data(), vchSig.size(),
        message.data(), message.size(),
        context.data(), context.size(),
        vch.data()
    );
    
    
    return result == 0; // 0 means success in Dilithium
}

std::vector<unsigned char> CDilithiumPubKey::GetAddress() const
{
    if (!IsValid()) {
        return {};
    }
    
    // For Bitcoin-style addresses, we typically use Hash160 of the public key
    uint160 hash = GetID();
    return std::vector<unsigned char>(hash.begin(), hash.end());
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

void CDilithiumExtKey::Encode(unsigned char code[DILITHIUM_EXTKEY_SIZE]) const
{
    code[0] = nDepth;
    memcpy(code+1, vchFingerprint, 4);
    WriteBE32(code+5, nChild);
    memcpy(code+9, chaincode.begin(), 32);
    assert(key.size() == DilithiumConstants::SECRET_KEY_SIZE);
    memcpy(code+41, key.begin(), DilithiumConstants::SECRET_KEY_SIZE);
}

void CDilithiumExtKey::Decode(const unsigned char code[DILITHIUM_EXTKEY_SIZE])
{
    nDepth = code[0];
    memcpy(vchFingerprint, code+1, 4);
    nChild = ReadBE32(code+5);
    memcpy(chaincode.begin(), code+9, 32);
    key.Set(code+41, code+41+DilithiumConstants::SECRET_KEY_SIZE);
    if ((nDepth == 0 && (nChild != 0 || ReadLE32(vchFingerprint) != 0)) || !key.IsValid()) {
        key = CDilithiumKey();
    }
}

bool CDilithiumExtKey::Derive(CDilithiumExtKey& out, unsigned int nChild) const
{
    if (nDepth == std::numeric_limits<unsigned char>::max()) return false;
    out.nDepth = nDepth + 1;
    uint160 id = key.GetPubKey().GetID();
    memcpy(out.vchFingerprint, &id, 4);
    out.nChild = nChild;
    
    // For Dilithium, we'll use a simplified derivation based on HMAC-SHA512
    // This is not cryptographically secure for production use, but provides
    // a basic HD wallet functionality
    CHMAC_SHA512 hmac(chaincode.begin(), chaincode.size());
    hmac.Write((unsigned char*)&nChild, sizeof(nChild));
    hmac.Write(key.begin(), key.size());
    
    unsigned char out_bytes[64];
    hmac.Finalize(out_bytes);
    
    // Use the first 32 bytes as the new chaincode
    memcpy(out.chaincode.begin(), out_bytes, 32);
    
    // Use the last 32 bytes to modify the key (simplified approach)
    // In a real implementation, this would need proper key derivation
    std::vector<unsigned char> new_key_data(key.begin(), key.end());
    for (size_t i = 0; i < 32 && i < new_key_data.size(); ++i) {
        new_key_data[i] ^= out_bytes[32 + i];
    }
    
    out.key.Set(new_key_data.begin(), new_key_data.end());
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

void CDilithiumExtKey::SetSeed(Span<const std::byte> seed)
{
    // Use HMAC-SHA512 to derive the master key from seed
    CHMAC_SHA512 hmac((unsigned char*)"Dilithium seed", 14);
    hmac.Write(reinterpret_cast<const unsigned char*>(seed.data()), seed.size());
    
    unsigned char out_bytes[64];
    hmac.Finalize(out_bytes);
    
    // Use first 32 bytes as chaincode
    memcpy(chaincode.begin(), out_bytes, 32);
    
    // Use last 32 bytes as key material
    std::vector<unsigned char> key_data(out_bytes + 32, out_bytes + 64);
    key.Set(key_data.begin(), key_data.end());
    
    nDepth = 0;
    memset(vchFingerprint, 0, 4);
    nChild = 0;
}

// CDilithiumExtPubKey implementation

void CDilithiumExtPubKey::Encode(unsigned char code[DILITHIUM_EXTPUBKEY_SIZE]) const
{
    code[0] = nDepth;
    memcpy(code+1, vchFingerprint, 4);
    WriteBE32(code+5, nChild);
    memcpy(code+9, chaincode.begin(), 32);
    assert(pubkey.size() == DilithiumConstants::PUBLIC_KEY_SIZE);
    memcpy(code+41, pubkey.begin(), DilithiumConstants::PUBLIC_KEY_SIZE);
}

void CDilithiumExtPubKey::Decode(const unsigned char code[DILITHIUM_EXTPUBKEY_SIZE])
{
    nDepth = code[0];
    memcpy(vchFingerprint, code+1, 4);
    nChild = ReadBE32(code+5);
    memcpy(chaincode.begin(), code+9, 32);
    pubkey.Set(code+41, code+41+DilithiumConstants::PUBLIC_KEY_SIZE);
    if ((nDepth == 0 && (nChild != 0 || ReadLE32(vchFingerprint) != 0)) || !pubkey.IsFullyValid()) {
        pubkey = CDilithiumPubKey();
    }
}

bool CDilithiumExtPubKey::Derive(CDilithiumExtPubKey& out, unsigned int nChild) const
{
    if (nDepth == std::numeric_limits<unsigned char>::max()) return false;
    out.nDepth = nDepth + 1;
    uint160 id = pubkey.GetID();
    memcpy(out.vchFingerprint, &id, 4);
    out.nChild = nChild;
    
    // For Dilithium public key derivation, we use a similar approach
    // This is simplified and not cryptographically secure for production
    CHMAC_SHA512 hmac(chaincode.begin(), chaincode.size());
    hmac.Write((unsigned char*)&nChild, sizeof(nChild));
    hmac.Write(pubkey.begin(), pubkey.size());
    
    unsigned char out_bytes[64];
    hmac.Finalize(out_bytes);
    
    // Use the first 32 bytes as the new chaincode
    memcpy(out.chaincode.begin(), out_bytes, 32);
    
    // For public key derivation, we would need the corresponding private key
    // This is a limitation of the current approach
    // In a real implementation, this would need proper key derivation
    return false; // Public key derivation not fully implemented
}
