// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_WALLET_KEY_TYPES_H
#define BTQ_WALLET_KEY_TYPES_H

#include <key.h>
#include <crypto/dilithium_key.h>
#include <pubkey.h>
#include <serialize.h>
#include <support/allocators/secure.h>
#include <uint256.h>

#include <variant>
#include <memory>

namespace wallet {

/**
 * Key type enumeration for different cryptographic schemes
 */
enum class KeyType {
    ECDSA,      // secp256k1 ECDSA keys
    DILITHIUM   // Dilithium post-quantum keys
};

/**
 * Unified key container that can hold either ECDSA or Dilithium keys
 */
class CUnifiedKey {
public:
    using ECDSAKey = CKey;
    using DilithiumKey = CDilithiumKey;
    
    // Variant to hold either key type
    using KeyVariant = std::variant<ECDSAKey, DilithiumKey>;
    
private:
    KeyType m_type;
    KeyVariant m_key;
    
public:
    // Constructors
    CUnifiedKey() : m_type(KeyType::ECDSA), m_key(ECDSAKey{}) {}
    explicit CUnifiedKey(const ECDSAKey& key) : m_type(KeyType::ECDSA), m_key(key) {}
    explicit CUnifiedKey(const DilithiumKey& key) : m_type(KeyType::DILITHIUM), m_key(key) {}
    
    // Copy constructor
    CUnifiedKey(const CUnifiedKey& other) : m_type(other.m_type), m_key(other.m_key) {}
    
    // Assignment operator
    CUnifiedKey& operator=(const CUnifiedKey& other) {
        if (this != &other) {
            m_type = other.m_type;
            m_key = other.m_key;
        }
        return *this;
    }
    
    // Get key type
    KeyType GetType() const { return m_type; }
    
    // Check if key is valid
    bool IsValid() const {
        return std::visit([](const auto& key) { return key.IsValid(); }, m_key);
    }
    
    // Generate new key. Returns false if key generation failed (e.g. RNG failure);
    // the resulting key is then invalid.
    bool MakeNewKey(KeyType type, bool fCompressed = true) {
        m_type = type;
        switch (type) {
            case KeyType::ECDSA:
                m_key = ECDSAKey{};
                std::get<ECDSAKey>(m_key).MakeNewKey(fCompressed);
                return std::get<ECDSAKey>(m_key).IsValid();
            case KeyType::DILITHIUM:
                m_key = DilithiumKey{};
                return std::get<DilithiumKey>(m_key).MakeNewKey();
        }
        return false;
    }
    
    // There is deliberately no GetPubKey()/GetID() here. A CPubKey cannot
    // represent a Dilithium public key, and the old implementation returned
    // a hardcoded dummy compressed secp256k1 key for Dilithium (so GetID
    // hashed the same dummy for every Dilithium key, Quarks F2.15). Use
    // GetDilithiumPubKey() for Dilithium keys.

    // Get Dilithium public key (only valid for Dilithium keys)
    CDilithiumPubKey GetDilithiumPubKey() const {
        if (m_type != KeyType::DILITHIUM) {
            throw std::runtime_error("Key is not a Dilithium key");
        }
        return std::get<DilithiumKey>(m_key).GetPubKey();
    }
    
    // Get Dilithium private key (only valid for Dilithium keys)
    DilithiumKey GetDilithiumKey() const {
        if (m_type != KeyType::DILITHIUM) {
            throw std::runtime_error("Key is not a Dilithium key");
        }
        return std::get<DilithiumKey>(m_key);
    }
    
    // Sign with appropriate algorithm
    bool Sign(const uint256& hash, std::vector<unsigned char>& vchSig, bool grind = true, uint32_t test_case = 0) const {
        switch (m_type) {
            case KeyType::ECDSA:
                return std::get<ECDSAKey>(m_key).Sign(hash, vchSig, grind, test_case);
            case KeyType::DILITHIUM:
                return std::get<DilithiumKey>(m_key).Sign(hash, vchSig);
        }
        return false; // Should never reach here
    }
    
    // Verify public key
    bool VerifyPubKey(const CPubKey& pubkey) const {
        switch (m_type) {
            case KeyType::ECDSA:
                return std::get<ECDSAKey>(m_key).VerifyPubKey(pubkey);
            case KeyType::DILITHIUM:
                // For Dilithium, we need to check if the pubkey is a Dilithium pubkey
                if (pubkey.size() == CDilithiumPubKey::SIZE) {
                    CDilithiumPubKey dilithium_pubkey(pubkey);
                    return std::get<DilithiumKey>(m_key).VerifyPubKey(dilithium_pubkey);
                }
                return false;
        }
        return false; // Should never reach here
    }
    
    // Get private key for serialization
    CPrivKey GetPrivKey() const {
        switch (m_type) {
            case KeyType::ECDSA:
                return std::get<ECDSAKey>(m_key).GetPrivKey();
            case KeyType::DILITHIUM:
                // For Dilithium, we need to serialize the key differently
                CPrivKey privkey;
                auto dilithium_serialized = std::get<DilithiumKey>(m_key).Serialize();
                privkey.assign(dilithium_serialized.begin(), dilithium_serialized.end());
                return privkey;
        }
        return CPrivKey{}; // Should never reach here
    }
    
    // Load key from serialized data
    bool Load(const CPrivKey& privkey, const CPubKey& pubkey, bool fSkipCheck = false) {
        // Determine key type from public key size
        if (pubkey.size() == CDilithiumPubKey::SIZE) {
            m_type = KeyType::DILITHIUM;
            m_key = DilithiumKey{};
            CDilithiumPubKey dilithium_pubkey(pubkey);
            return std::get<DilithiumKey>(m_key).Load(Span<const unsigned char>(privkey.data(), privkey.size()));
        } else {
            m_type = KeyType::ECDSA;
            m_key = ECDSAKey{};
            return std::get<ECDSAKey>(m_key).Load(privkey, pubkey, fSkipCheck);
        }
    }
    
    // Check if key is compressed (only relevant for ECDSA)
    bool IsCompressed() const {
        if (m_type == KeyType::ECDSA) {
            return std::get<ECDSAKey>(m_key).IsCompressed();
        }
        return true; // Dilithium keys are always "compressed"
    }
    
    // Get key size
    size_t size() const {
        switch (m_type) {
            case KeyType::ECDSA:
                return std::get<ECDSAKey>(m_key).size();
            case KeyType::DILITHIUM:
                return std::get<DilithiumKey>(m_key).size();
        }
        return 0; // Should never reach here
    }
    
    // Get key data
    const unsigned char* begin() const {
        switch (m_type) {
            case KeyType::ECDSA:
                return std::get<ECDSAKey>(m_key).begin();
            case KeyType::DILITHIUM:
                return std::get<DilithiumKey>(m_key).begin();
        }
        return nullptr; // Should never reach here
    }
    
    const unsigned char* end() const {
        return begin() + size();
    }
    
    // No stream serialization. The old Serialize/Unserialize pair never
    // compiled (CKey and CDilithiumKey have no stream serializers) and
    // Unserialize cast the type byte into KeyType with no range check
    // (Quarks F2.7). Any future implementation must validate the type
    // byte and throw std::ios_base::failure on unknown values.

    // Comparison operators
    bool operator==(const CUnifiedKey& other) const {
        return m_type == other.m_type && m_key == other.m_key;
    }
    
    bool operator!=(const CUnifiedKey& other) const {
        return !(*this == other);
    }
};

/**
 * Extended key for HD wallet support
 */
class CUnifiedExtKey {
public:
    using ECDSAExtKey = CExtKey;
    using DilithiumExtKey = CDilithiumExtKey;
    
    // Variant to hold either extended key type
    using ExtKeyVariant = std::variant<ECDSAExtKey, DilithiumExtKey>;
    
private:
    KeyType m_type;
    ExtKeyVariant m_extkey;
    
public:
    // Constructors
    CUnifiedExtKey() : m_type(KeyType::ECDSA), m_extkey(ECDSAExtKey{}) {}
    explicit CUnifiedExtKey(const ECDSAExtKey& extkey) : m_type(KeyType::ECDSA), m_extkey(extkey) {}
    explicit CUnifiedExtKey(const DilithiumExtKey& extkey) : m_type(KeyType::DILITHIUM), m_extkey(extkey) {}
    
    // Get key type
    KeyType GetType() const { return m_type; }
    
    // Get the underlying key
    CUnifiedKey GetKey() const {
        switch (m_type) {
            case KeyType::ECDSA:
                return CUnifiedKey(std::get<ECDSAExtKey>(m_extkey).key);
            case KeyType::DILITHIUM:
                return CUnifiedKey(std::get<DilithiumExtKey>(m_extkey).key);
        }
        return CUnifiedKey{}; // Should never reach here
    }
    
    // Derive child key
    bool Derive(CUnifiedExtKey& out, unsigned int nChild) const {
        switch (m_type) {
            case KeyType::ECDSA:
                out.m_type = KeyType::ECDSA;
                out.m_extkey = ECDSAExtKey{};
                return std::get<ECDSAExtKey>(m_extkey).Derive(std::get<ECDSAExtKey>(out.m_extkey), nChild);
            case KeyType::DILITHIUM:
                out.m_type = KeyType::DILITHIUM;
                out.m_extkey = DilithiumExtKey{};
                return std::get<DilithiumExtKey>(m_extkey).Derive(std::get<DilithiumExtKey>(out.m_extkey), nChild);
        }
        return false; // Should never reach here
    }
    
    // Set seed
    void SetSeed(Span<const std::byte> seed, KeyType type) {
        m_type = type;
        switch (type) {
            case KeyType::ECDSA:
                m_extkey = ECDSAExtKey{};
                std::get<ECDSAExtKey>(m_extkey).SetSeed(seed);
                break;
            case KeyType::DILITHIUM:
                m_extkey = DilithiumExtKey{};
                std::get<DilithiumExtKey>(m_extkey).SetSeed(seed);
                break;
        }
    }
    
    // No stream serialization; see the note on CUnifiedKey.
};

} // namespace wallet

#endif // BTQ_WALLET_KEY_TYPES_H
