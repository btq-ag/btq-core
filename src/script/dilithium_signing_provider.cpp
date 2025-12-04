// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/dilithium_signing_provider.h>
#include <addresstype.h>
#include <script/solver.h>

bool DilithiumSigningProvider::AddDilithiumKey(const CDilithiumKey& key)
{
    if (!key.IsValid()) {
        return false;
    }
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    DilithiumKeyID key_id(pubkey);
    
    m_dilithium_keys[key_id] = key;
    return true;
}

bool DilithiumSigningProvider::AddDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& legacy_id)
{
    if (!key.IsValid()) {
        return false;
    }
    
    m_legacy_dilithium_keys[legacy_id] = key;
    return true;
}

bool DilithiumSigningProvider::GetDilithiumKey(const DilithiumKeyID& key_id, CDilithiumKey& key_out) const
{
    auto it = m_dilithium_keys.find(key_id);
    if (it != m_dilithium_keys.end()) {
        key_out = it->second;
        return true;
    }
    return false;
}

bool DilithiumSigningProvider::GetDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id, CDilithiumKey& key_out) const
{
    auto it = m_legacy_dilithium_keys.find(key_id);
    if (it != m_legacy_dilithium_keys.end()) {
        key_out = it->second;
        return true;
    }
    return false;
}

bool DilithiumSigningProvider::HaveDilithiumKey(const DilithiumKeyID& key_id) const
{
    return m_dilithium_keys.count(key_id) > 0;
}

bool DilithiumSigningProvider::HaveDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id) const
{
    return m_legacy_dilithium_keys.count(key_id) > 0;
}

bool DilithiumSigningProvider::GetDilithiumKeyForDestination(const CTxDestination& dest, CDilithiumKey& key_out) const
{
    // Try to get the key based on the destination type
    if (auto dilithium_pkhash = std::get_if<DilithiumPKHash>(&dest)) {
        DilithiumLegacyKeyID legacy_id(uint160(*dilithium_pkhash));
        return GetDilithiumKeyLegacy(legacy_id, key_out);
    } else if (auto dilithium_witness_keyhash = std::get_if<DilithiumWitnessV0KeyHash>(&dest)) {
        DilithiumLegacyKeyID legacy_id(uint160(*dilithium_witness_keyhash));
        return GetDilithiumKeyLegacy(legacy_id, key_out);
    }
    
    return false;
}

bool DilithiumSigningProvider::GetDilithiumKey(const CKeyID& keyid, CDilithiumKey& dilithium_key) const
{
    // Convert CKeyID to DilithiumLegacyKeyID for compatibility
    DilithiumLegacyKeyID legacy_id(uint160(keyid));
    return GetDilithiumKeyLegacy(legacy_id, dilithium_key);
}

bool DilithiumSigningProvider::HaveDilithiumKey(const CKeyID& keyid) const
{
    // Convert CKeyID to DilithiumLegacyKeyID for compatibility
    DilithiumLegacyKeyID legacy_id(uint160(keyid));
    return HaveDilithiumKeyLegacy(legacy_id);
}

void DilithiumSigningProvider::Merge(const DilithiumSigningProvider& other)
{
    // Merge Dilithium keys
    for (const auto& [key_id, key] : other.m_dilithium_keys) {
        m_dilithium_keys[key_id] = key;
    }
    
    // Merge legacy Dilithium keys
    for (const auto& [key_id, key] : other.m_legacy_dilithium_keys) {
        m_legacy_dilithium_keys[key_id] = key;
    }
    
    // Merge encrypted Dilithium keys
    for (const auto& [key_id, pair] : other.m_crypted_dilithium_keys) {
        m_crypted_dilithium_keys[key_id] = pair;
    }
    
    // Merge encrypted legacy Dilithium keys
    for (const auto& [key_id, pair] : other.m_crypted_legacy_dilithium_keys) {
        m_crypted_legacy_dilithium_keys[key_id] = pair;
    }
}
