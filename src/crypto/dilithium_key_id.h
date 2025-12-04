// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_CRYPTO_DILITHIUM_KEY_ID_H
#define BTQ_CRYPTO_DILITHIUM_KEY_ID_H

#include <crypto/dilithium_key.h>
#include <uint256.h>
#include <util/hash_type.h>

/** Dilithium Key ID - uses full 256-bit hash instead of 160-bit CKeyID */
struct DilithiumKeyID : public BaseHash<uint256>
{
    DilithiumKeyID() : BaseHash() {}
    explicit DilithiumKeyID(const uint256& hash) : BaseHash(hash) {}
    explicit DilithiumKeyID(const CDilithiumPubKey& pubkey);
    
    // Convert to/from string for debugging
    std::string ToString() const;
    static DilithiumKeyID FromString(const std::string& str);
    
    // Check if null
    bool IsNull() const { return *this == DilithiumKeyID(); }
};

/** Dilithium Key ID for legacy addresses (160-bit hash) */
struct DilithiumLegacyKeyID : public BaseHash<uint160>
{
    DilithiumLegacyKeyID() : BaseHash() {}
    explicit DilithiumLegacyKeyID(const uint160& hash) : BaseHash(hash) {}
    explicit DilithiumLegacyKeyID(const CDilithiumPubKey& pubkey);
    
    // Convert to/from string for debugging
    std::string ToString() const;
    static DilithiumLegacyKeyID FromString(const std::string& str);
};

#endif // BTQ_CRYPTO_DILITHIUM_KEY_ID_H
