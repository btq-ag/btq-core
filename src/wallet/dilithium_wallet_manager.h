// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_WALLET_DILITHIUM_WALLET_MANAGER_H
#define BTQ_WALLET_DILITHIUM_WALLET_MANAGER_H

#include <crypto/dilithium_key.h>
#include <crypto/dilithium_key_id.h>
#include <addresstype.h>
#include <script/script.h>
#include <wallet/wallet.h>
#include <wallet/scriptpubkeyman.h>

#include <map>
#include <memory>
#include <mutex>

namespace wallet {

/** Dilithium-specific wallet manager that doesn't use CKeyID */
class DilithiumWalletManager
{
private:
    // Dilithium key storage - uses DilithiumKeyID instead of CKeyID
    std::map<DilithiumKeyID, CDilithiumKey> m_dilithium_keys;
    std::map<DilithiumKeyID, std::pair<CDilithiumPubKey, std::vector<unsigned char>>> m_crypted_dilithium_keys;
    
    // Legacy key storage for compatibility with existing address system
    std::map<DilithiumLegacyKeyID, CDilithiumKey> m_legacy_dilithium_keys;
    std::map<DilithiumLegacyKeyID, std::pair<CDilithiumPubKey, std::vector<unsigned char>>> m_crypted_legacy_dilithium_keys;
    
    mutable std::mutex m_dilithium_mutex;
    
    // Reference to the main wallet
    CWallet& m_wallet;

public:
    explicit DilithiumWalletManager(CWallet& wallet) : m_wallet(wallet) {}
    
    // Key management
    bool AddDilithiumKey(const CDilithiumKey& key);
    bool AddDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& legacy_id);
    bool GetDilithiumKey(const DilithiumKeyID& key_id, CDilithiumKey& key_out) const;
    bool GetDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id, CDilithiumKey& key_out) const;
    bool HaveDilithiumKey(const DilithiumKeyID& key_id) const;
    bool HaveDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id) const;
    
    // Address generation
    util::Result<CTxDestination> GetNewDilithiumAddress(OutputType type);
    util::Result<CTxDestination> GetNewDilithiumLegacyAddress();
    util::Result<CTxDestination> GetNewDilithiumBech32Address();
    
    // Key lookup by destination
    bool GetDilithiumKeyForDestination(const CTxDestination& dest, CDilithiumKey& key_out) const;
    
    // Signing provider interface
    bool GetDilithiumKeyForScript(const CScript& script, CDilithiumKey& key_out) const;
    
    // Database operations
    bool LoadDilithiumKey(const CDilithiumKey& key, const DilithiumKeyID& key_id);
    bool LoadDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& key_id);
    
    // Encryption support
    bool IsEncrypted() const;
    bool IsLocked() const;
    bool Unlock(const SecureString& wallet_passphrase);
    bool ChangeWalletPassphrase(const SecureString& old_passphrase, const SecureString& new_passphrase);
    
    // Key pool management
    void TopUpDilithiumKeyPool();
    void ReserveKeyFromDilithiumPool();
    void ReturnKeyToDilithiumPool();
    
    // Statistics
    size_t GetDilithiumKeyCount() const;
    size_t GetDilithiumLegacyKeyCount() const;
};

} // namespace wallet

#endif // BTQ_WALLET_DILITHIUM_WALLET_MANAGER_H
