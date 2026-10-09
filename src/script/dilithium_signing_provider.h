// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_SCRIPT_DILITHIUM_SIGNING_PROVIDER_H
#define BTQ_SCRIPT_DILITHIUM_SIGNING_PROVIDER_H

#include <script/signingprovider.h>
#include <crypto/dilithium_key.h>
#include <crypto/dilithium_key_id.h>
#include <addresstype.h>

#include <map>

/** Dilithium-specific signing provider that doesn't use CKeyID */
class DilithiumSigningProvider : public SigningProvider
{
private:
    // Dilithium key storage using DilithiumKeyID
    std::map<DilithiumKeyID, CDilithiumKey> m_dilithium_keys;
    std::map<DilithiumLegacyKeyID, CDilithiumKey> m_legacy_dilithium_keys;
    
    // Encrypted Dilithium key storage
    std::map<DilithiumKeyID, std::pair<CDilithiumPubKey, std::vector<unsigned char>>> m_crypted_dilithium_keys;
    std::map<DilithiumLegacyKeyID, std::pair<CDilithiumPubKey, std::vector<unsigned char>>> m_crypted_legacy_dilithium_keys;

public:
    // Dilithium key management
    bool AddDilithiumKey(const CDilithiumKey& key);
    bool AddDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& legacy_id);
    bool GetDilithiumKey(const DilithiumKeyID& key_id, CDilithiumKey& key_out) const;
    bool GetDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id, CDilithiumKey& key_out) const;
    bool HaveDilithiumKey(const DilithiumKeyID& key_id) const;
    bool HaveDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id) const;
    
    // Key lookup by destination
    bool GetDilithiumKeyForDestination(const CTxDestination& dest, CDilithiumKey& key_out) const;
    
    // Override base class methods for Dilithium support
    bool GetDilithiumKey(const CKeyID& keyid, CDilithiumKey& dilithium_key) const override;
    bool HaveDilithiumKey(const CKeyID& keyid) const override;
    
    // Merge with other providers
    void Merge(const DilithiumSigningProvider& other);
};

#endif // BTQ_SCRIPT_DILITHIUM_SIGNING_PROVIDER_H
