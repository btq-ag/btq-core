// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/dilithium_wallet_manager.h>
#include <wallet/wallet.h>
#include <script/solver.h>
#include <addresstype.h>
#include <util/result.h>

namespace wallet {

bool DilithiumWalletManager::AddDilithiumKey(const CDilithiumKey& key)
{
    LOCK(m_dilithium_mutex);
    
    if (!key.IsValid()) {
        return false;
    }
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    DilithiumKeyID key_id(pubkey);
    
    m_dilithium_keys[key_id] = key;
    return true;
}

bool DilithiumWalletManager::AddDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& legacy_id)
{
    LOCK(m_dilithium_mutex);
    
    if (!key.IsValid()) {
        return false;
    }
    
    m_legacy_dilithium_keys[legacy_id] = key;
    return true;
}

bool DilithiumWalletManager::GetDilithiumKey(const DilithiumKeyID& key_id, CDilithiumKey& key_out) const
{
    LOCK(m_dilithium_mutex);
    
    auto it = m_dilithium_keys.find(key_id);
    if (it != m_dilithium_keys.end()) {
        key_out = it->second;
        return true;
    }
    return false;
}

bool DilithiumWalletManager::GetDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id, CDilithiumKey& key_out) const
{
    LOCK(m_dilithium_mutex);
    
    auto it = m_legacy_dilithium_keys.find(key_id);
    if (it != m_legacy_dilithium_keys.end()) {
        key_out = it->second;
        return true;
    }
    return false;
}

bool DilithiumWalletManager::HaveDilithiumKey(const DilithiumKeyID& key_id) const
{
    LOCK(m_dilithium_mutex);
    return m_dilithium_keys.count(key_id) > 0;
}

bool DilithiumWalletManager::HaveDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id) const
{
    LOCK(m_dilithium_mutex);
    return m_legacy_dilithium_keys.count(key_id) > 0;
}

util::Result<CTxDestination> DilithiumWalletManager::GetNewDilithiumAddress(OutputType type)
{
    // Generate a new Dilithium key
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    
    if (!dilithium_key.IsValid()) {
        return util::Error{_("Error: Failed to generate Dilithium key")};
    }
    
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    
    // Create destination based on type
    CTxDestination dest;
    if (type == OutputType::DILITHIUM_LEGACY) {
        dest = DilithiumPKHash(dilithium_pubkey);
        // Store using legacy key ID for compatibility
        DilithiumLegacyKeyID legacy_id(dilithium_pubkey);
        if (!AddDilithiumKeyLegacy(dilithium_key, legacy_id)) {
            return util::Error{_("Error: Failed to store Dilithium key")};
        }
    } else if (type == OutputType::DILITHIUM_BECH32) {
        dest = DilithiumWitnessV0KeyHash(dilithium_pubkey);
        // Store using legacy key ID for compatibility
        DilithiumLegacyKeyID legacy_id(dilithium_pubkey);
        if (!AddDilithiumKeyLegacy(dilithium_key, legacy_id)) {
            return util::Error{_("Error: Failed to store Dilithium key")};
        }
    } else {
        return util::Error{_("Error: Unsupported Dilithium output type")};
    }
    
    return dest;
}

util::Result<CTxDestination> DilithiumWalletManager::GetNewDilithiumLegacyAddress()
{
    return GetNewDilithiumAddress(OutputType::DILITHIUM_LEGACY);
}

util::Result<CTxDestination> DilithiumWalletManager::GetNewDilithiumBech32Address()
{
    return GetNewDilithiumAddress(OutputType::DILITHIUM_BECH32);
}

bool DilithiumWalletManager::GetDilithiumKeyForDestination(const CTxDestination& dest, CDilithiumKey& key_out) const
{
    LOCK(m_dilithium_mutex);
    
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

bool DilithiumWalletManager::GetDilithiumKeyForScript(const CScript& script, CDilithiumKey& key_out) const
{
    LOCK(m_dilithium_mutex);
    
    // Parse the script to determine the destination
    std::vector<std::vector<unsigned char>> vSolutions;
    TxoutType whichType = Solver(script, vSolutions);
    
    if (whichType == TxoutType::DILITHIUM_PUBKEYHASH && !vSolutions.empty()) {
        uint160 hash;
        std::copy(vSolutions[0].begin(), vSolutions[0].end(), hash.begin());
        DilithiumLegacyKeyID legacy_id(hash);
        return GetDilithiumKeyLegacy(legacy_id, key_out);
    } else if (whichType == TxoutType::DILITHIUM_WITNESS_V0_KEYHASH && !vSolutions.empty()) {
        uint160 hash;
        std::copy(vSolutions[0].begin(), vSolutions[0].end(), hash.begin());
        DilithiumLegacyKeyID legacy_id(hash);
        return GetDilithiumKeyLegacy(legacy_id, key_out);
    }
    
    return false;
}

bool DilithiumWalletManager::LoadDilithiumKey(const CDilithiumKey& key, const DilithiumKeyID& key_id)
{
    LOCK(m_dilithium_mutex);
    m_dilithium_keys[key_id] = key;
    return true;
}

bool DilithiumWalletManager::LoadDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& key_id)
{
    LOCK(m_dilithium_mutex);
    m_legacy_dilithium_keys[key_id] = key;
    return true;
}

bool DilithiumWalletManager::IsEncrypted() const
{
    return m_wallet.IsCrypted();
}

bool DilithiumWalletManager::IsLocked() const
{
    return m_wallet.IsLocked();
}

bool DilithiumWalletManager::Unlock(const SecureString& wallet_passphrase)
{
    return m_wallet.Unlock(wallet_passphrase);
}

bool DilithiumWalletManager::ChangeWalletPassphrase(const SecureString& old_passphrase, const SecureString& new_passphrase)
{
    return m_wallet.ChangeWalletPassphrase(old_passphrase, new_passphrase);
}

void DilithiumWalletManager::TopUpDilithiumKeyPool()
{
    // TODO: Implement Dilithium key pool management
}

void DilithiumWalletManager::ReserveKeyFromDilithiumPool()
{
    // TODO: Implement Dilithium key pool management
}

void DilithiumWalletManager::ReturnKeyToDilithiumPool()
{
    // TODO: Implement Dilithium key pool management
}

size_t DilithiumWalletManager::GetDilithiumKeyCount() const
{
    LOCK(m_dilithium_mutex);
    return m_dilithium_keys.size();
}

size_t DilithiumWalletManager::GetDilithiumLegacyKeyCount() const
{
    LOCK(m_dilithium_mutex);
    return m_legacy_dilithium_keys.size();
}

} // namespace wallet
