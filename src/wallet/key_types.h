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
    
    // Generate new key
    void MakeNewKey(KeyType type, bool fCompressed = true) {
        m_type = type;
        switch (type) {
            case KeyType::ECDSA:
                m_key = ECDSAKey{};
                std::get<ECDSAKey>(m_key).MakeNewKey(fCompressed);
                break;
            case KeyType::DILITHIUM:
                m_key = DilithiumKey{};
                std::get<DilithiumKey>(m_key).MakeNewKey();
                break;
        }
    }
    
    // Get public key
    CPubKey GetPubKey() const {
        switch (m_type) {
            case KeyType::ECDSA:
                return std::get<ECDSAKey>(m_key).GetPubKey();
            case KeyType::DILITHIUM:
                // For Dilithium, we need to return a dummy CPubKey since Dilithium keys are much larger
                // This is a workaround for compatibility with existing wallet code
                std::vector<unsigned char> dummy_data(33, 0x00);
                dummy_data[0] = 0x02; // Compressed pubkey prefix
                return CPubKey(dummy_data);
        }
        return CPubKey{}; // Should never reach here
    }
    
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
    
    // Get key ID for wallet storage
    CKeyID GetID() const {
        return GetPubKey().GetID();
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
    
    // Serialization support
    template<typename Stream>
    void Serialize(Stream& s) const {
        s << static_cast<uint8_t>(m_type);
        std::visit([&s](const auto& key) { s << key; }, m_key);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
        uint8_t type_byte;
        s >> type_byte;
        m_type = static_cast<KeyType>(type_byte);
        
        switch (m_type) {
            case KeyType::ECDSA:
                m_key = ECDSAKey{};
                s >> std::get<ECDSAKey>(m_key);
                break;
            case KeyType::DILITHIUM:
                m_key = DilithiumKey{};
                s >> std::get<DilithiumKey>(m_key);
                break;
        }
    }
    
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
    
    // Serialization support
    template<typename Stream>
    void Serialize(Stream& s) const {
        s << static_cast<uint8_t>(m_type);
        std::visit([&s](const auto& extkey) { s << extkey; }, m_extkey);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
        uint8_t type_byte;
        s >> type_byte;
        m_type = static_cast<KeyType>(type_byte);
        
        switch (m_type) {
            case KeyType::ECDSA:
                m_extkey = ECDSAExtKey{};
                s >> std::get<ECDSAExtKey>(m_extkey);
                break;
            case KeyType::DILITHIUM:
                m_extkey = DilithiumExtKey{};
                s >> std::get<DilithiumExtKey>(m_extkey);
                break;
        }
    }
};

} // namespace wallet

#endif // BTQ_WALLET_KEY_TYPES_H
