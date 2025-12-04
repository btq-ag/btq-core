// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_WALLET_DILITHIUM_SCRIPTPUBKEYMAN_H
#define BTQ_WALLET_DILITHIUM_SCRIPTPUBKEYMAN_H

#include <wallet/scriptpubkeyman.h>
#include <crypto/dilithium_key.h>
#include <crypto/dilithium_key_id.h>
#include <crypto/dilithium_hd_key.h>
#include <addresstype.h>

#include <map>
#include <set>

namespace wallet {

/** Dilithium ScriptPubKeyMan - completely parallel to Bitcoin's system */
class DilithiumScriptPubKeyMan : public ScriptPubKeyMan
{
private:
    // Dilithium key storage using DilithiumKeyID (not CKeyID!)
    std::map<DilithiumKeyID, CDilithiumKey> m_dilithium_keys;
    std::map<DilithiumLegacyKeyID, CDilithiumKey> m_legacy_dilithium_keys;
    
    // Encrypted Dilithium key storage
    std::map<DilithiumKeyID, std::pair<CDilithiumPubKey, std::vector<unsigned char>>> m_crypted_dilithium_keys;
    std::map<DilithiumLegacyKeyID, std::pair<CDilithiumPubKey, std::vector<unsigned char>>> m_crypted_legacy_dilithium_keys;
    
    // Dilithium HD chain
    DilithiumHDChain m_dilithium_hd_chain;
    
    // Key pool for Dilithium keys
    std::set<int64_t> m_dilithium_keypool;
    std::map<DilithiumLegacyKeyID, int64_t> m_dilithium_pool_key_to_index;
    int64_t m_dilithium_max_keypool_index;
    
    // Key metadata
    std::map<DilithiumLegacyKeyID, CKeyMetadata> m_dilithium_key_metadata;
    
    mutable RecursiveMutex m_dilithium_mutex;

public:
    explicit DilithiumScriptPubKeyMan(CWallet& wallet, int64_t keypool_size = 1000);
    virtual ~DilithiumScriptPubKeyMan() = default;
    
    // Override base class methods
    bool CanGetAddresses(bool internal = false) const override;
    util::Result<CTxDestination> GetNewDestination(const OutputType type) override;
    util::Result<CTxDestination> GetReservedDestination(const OutputType type, bool internal, int64_t& index, CKeyPool& keypool) override;
    void ReturnDestination(int64_t index, bool internal, const CTxDestination& dest) override;
    std::vector<WalletDestination> MarkUnusedAddresses(const CScript& script) override;
    bool IsHDEnabled() const override;
    void SetHDSeed(const DilithiumExtKey& seed);
    void SetHDSeed(const DilithiumExtPubKey& seed);
    
    // Dilithium-specific key management
    bool AddDilithiumKey(const CDilithiumKey& key);
    bool AddDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& legacy_id);
    bool GetDilithiumKey(const DilithiumKeyID& key_id, CDilithiumKey& key_out) const;
    bool GetDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id, CDilithiumKey& key_out) const;
    bool HaveDilithiumKey(const DilithiumKeyID& key_id) const;
    bool HaveDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id) const;
    
    // Key generation
    CDilithiumKey GenerateNewDilithiumKey();
    CDilithiumKey GenerateNewDilithiumKeyLegacy();
    
    // HD key derivation
    void DeriveNewDilithiumChildKey(CDilithiumKey& secret, bool internal);
    
    // Key pool management
    void TopUpDilithiumKeyPool();
    void NewDilithiumKeyPool();
    void ReserveDilithiumKeyFromPool();
    void ReturnDilithiumKeyToPool();
    
    // Signing provider interface
    bool GetDilithiumKey(const CKeyID& keyid, CDilithiumKey& dilithium_key) const;
    bool HaveDilithiumKey(const CKeyID& keyid) const;
    
    // Database operations
    bool LoadDilithiumKey(const CDilithiumKey& key, const DilithiumKeyID& key_id);
    bool LoadDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& key_id);
    
    // Encryption support
    bool IsEncrypted() const;
    bool IsLocked() const;
    bool Unlock(const SecureString& wallet_passphrase);
    bool ChangeWalletPassphrase(const SecureString& old_passphrase, const SecureString& new_passphrase);
    
    // Statistics
    size_t GetDilithiumKeyCount() const;
    size_t GetDilithiumLegacyKeyCount() const;
    
    // Get unique ID
    uint256 GetID() const override;
};

} // namespace wallet

#endif // BTQ_WALLET_DILITHIUM_SCRIPTPUBKEYMAN_H
