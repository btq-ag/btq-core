// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/dilithium_scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <script/solver.h>
#include <addresstype.h>
#include <util/result.h>
#include <util/strencodings.h>
#include <util/bip32.h>

// BIP32 hardened key limit
static const unsigned int BIP32_HARDENED_KEY_LIMIT = 0x80000000;

namespace wallet {

DilithiumScriptPubKeyMan::DilithiumScriptPubKeyMan(CWallet& wallet, int64_t keypool_size)
    : ScriptPubKeyMan(wallet), m_dilithium_max_keypool_index(0)
{
    LogPrintf("DEBUG DILITHIUM: DilithiumScriptPubKeyMan constructor called\n");
}

bool DilithiumScriptPubKeyMan::CanGetAddresses(bool internal) const
{
    LOCK(m_dilithium_mutex);
    // Dilithium can always generate new addresses on demand
    LogPrintf("DEBUG DILITHIUM: CanGetAddresses called, returning true\n");
    return true;
}


util::Result<CTxDestination> DilithiumScriptPubKeyMan::GetNewDestination(const OutputType type)
{
    LOCK(m_dilithium_mutex);
    LogPrintf("DEBUG DILITHIUM: GetNewDestination called with type %d\n", (int)type);
    
    // Only handle Dilithium output types
    if (type != OutputType::DILITHIUM_LEGACY && type != OutputType::DILITHIUM_BECH32) {
        LogPrintf("DEBUG DILITHIUM: Rejecting non-Dilithium type %d\n", (int)type);
        return util::Error{_("Error: Unsupported output type for Dilithium")};
    }
    
    // Generate a new Dilithium key
    CDilithiumKey dilithium_key;
    if (IsHDEnabled()) {
        DeriveNewDilithiumChildKey(dilithium_key, false);
    } else {
        // Generate a random Dilithium key (no HD derivation)
        dilithium_key.MakeNewKey();
        LogPrintf("DEBUG DILITHIUM: Generated new random Dilithium key\n");
    }
    
    if (!dilithium_key.IsValid()) {
        return util::Error{_("Error: Failed to generate Dilithium key")};
    }
    
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    
    // Create destination based on type
    CTxDestination dest;
    DilithiumLegacyKeyID legacy_id(dilithium_pubkey);
    
    if (type == OutputType::DILITHIUM_LEGACY) {
        dest = DilithiumPKHash(dilithium_pubkey);
    } else { // DILITHIUM_BECH32
        dest = DilithiumWitnessV0KeyHash(dilithium_pubkey);
    }
    
    // Store the key using Dilithium key ID system
    if (!AddDilithiumKeyLegacy(dilithium_key, legacy_id)) {
        return util::Error{_("Error: Failed to store Dilithium key")};
    }
    
    return dest;
}

util::Result<CTxDestination> DilithiumScriptPubKeyMan::GetReservedDestination(const OutputType type, bool internal, int64_t& index, CKeyPool& keypool)
{
    // TODO: Implement Dilithium key pool reservation
    return util::Error{_("Error: Dilithium key pool reservation not implemented")};
}

void DilithiumScriptPubKeyMan::ReturnDestination(int64_t index, bool internal, const CTxDestination& dest)
{
    // TODO: Implement Dilithium key pool return
}


std::vector<WalletDestination> DilithiumScriptPubKeyMan::MarkUnusedAddresses(const CScript& script)
{
    // TODO: Implement Dilithium address marking
    return {};
}

bool DilithiumScriptPubKeyMan::IsHDEnabled() const
{
    LOCK(m_dilithium_mutex);
    return !m_dilithium_hd_chain.seed_id.IsNull();
}


void DilithiumScriptPubKeyMan::SetHDSeed(const DilithiumExtKey& seed)
{
    LOCK(m_dilithium_mutex);
    m_dilithium_hd_chain.master_key = seed;
    m_dilithium_hd_chain.seed_id = DilithiumKeyID(seed.GetPubKey());
    
    // Derive account key
    DilithiumExtKey account_key;
    if (seed.Derive(account_key, 0 | BIP32_HARDENED_KEY_LIMIT)) {
        m_dilithium_hd_chain.account_key = account_key;
    }
}

void DilithiumScriptPubKeyMan::SetHDSeed(const DilithiumExtPubKey& seed)
{
    LOCK(m_dilithium_mutex);
    m_dilithium_hd_chain.seed_id = DilithiumKeyID(seed.GetPubKey());
    
    // Can't derive account key from public key only
    // This would need to be set separately
}

bool DilithiumScriptPubKeyMan::AddDilithiumKey(const CDilithiumKey& key)
{
    LOCK(m_dilithium_mutex);
    
    if (!key.IsValid()) {
        return false;
    }
    
    DilithiumKeyID key_id(key.GetPubKey());
    m_dilithium_keys[key_id] = key;
    return true;
}

bool DilithiumScriptPubKeyMan::AddDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& legacy_id)
{
    LOCK(m_dilithium_mutex);
    
    if (!key.IsValid()) {
        return false;
    }
    
    m_legacy_dilithium_keys[legacy_id] = key;
    return true;
}

bool DilithiumScriptPubKeyMan::GetDilithiumKey(const DilithiumKeyID& key_id, CDilithiumKey& key_out) const
{
    LOCK(m_dilithium_mutex);
    
    auto it = m_dilithium_keys.find(key_id);
    if (it != m_dilithium_keys.end()) {
        key_out = it->second;
        return true;
    }
    return false;
}

bool DilithiumScriptPubKeyMan::GetDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id, CDilithiumKey& key_out) const
{
    LOCK(m_dilithium_mutex);
    
    auto it = m_legacy_dilithium_keys.find(key_id);
    if (it != m_legacy_dilithium_keys.end()) {
        key_out = it->second;
        return true;
    }
    return false;
}

bool DilithiumScriptPubKeyMan::HaveDilithiumKey(const DilithiumKeyID& key_id) const
{
    LOCK(m_dilithium_mutex);
    return m_dilithium_keys.count(key_id) > 0;
}

bool DilithiumScriptPubKeyMan::HaveDilithiumKeyLegacy(const DilithiumLegacyKeyID& key_id) const
{
    LOCK(m_dilithium_mutex);
    return m_legacy_dilithium_keys.count(key_id) > 0;
}

CDilithiumKey DilithiumScriptPubKeyMan::GenerateNewDilithiumKey()
{
    CDilithiumKey key;
    key.MakeNewKey();
    return key;
}

CDilithiumKey DilithiumScriptPubKeyMan::GenerateNewDilithiumKeyLegacy()
{
    return GenerateNewDilithiumKey();
}

void DilithiumScriptPubKeyMan::DeriveNewDilithiumChildKey(CDilithiumKey& secret, bool internal)
{
    LOCK(m_dilithium_mutex);
    
    if (!IsHDEnabled()) {
        throw std::runtime_error("HD not enabled for Dilithium");
    }
    
    DilithiumExtKey master_key = m_dilithium_hd_chain.master_key;
    DilithiumExtKey account_key;
    DilithiumExtKey chain_child_key;
    DilithiumExtKey child_key;
    
    // Derive m/0'
    if (!master_key.Derive(account_key, 0 | BIP32_HARDENED_KEY_LIMIT)) {
        throw std::runtime_error("Could not derive account key");
    }
    
    // Derive m/0'/0' (external) or m/0'/1' (internal)
    uint32_t chain_index = internal ? 1 : 0;
    if (!account_key.Derive(chain_child_key, (chain_index | BIP32_HARDENED_KEY_LIMIT))) {
        throw std::runtime_error("Could not derive chain child key");
    }
    
    // Derive child key at next index
    uint32_t child_index = internal ? m_dilithium_hd_chain.nInternalChainCounter : m_dilithium_hd_chain.nExternalChainCounter;
    child_index |= BIP32_HARDENED_KEY_LIMIT;
    
    if (!chain_child_key.Derive(child_key, child_index)) {
        throw std::runtime_error("Could not derive child key");
    }
    
    secret = child_key.GetPrivKey();
    
    // Update counters
    if (internal) {
        m_dilithium_hd_chain.nInternalChainCounter++;
    } else {
        m_dilithium_hd_chain.nExternalChainCounter++;
    }
}

void DilithiumScriptPubKeyMan::TopUpDilithiumKeyPool()
{
    // TODO: Implement Dilithium key pool management
}

void DilithiumScriptPubKeyMan::NewDilithiumKeyPool()
{
    // TODO: Implement Dilithium key pool management
}

void DilithiumScriptPubKeyMan::ReserveDilithiumKeyFromPool()
{
    // TODO: Implement Dilithium key pool management
}

void DilithiumScriptPubKeyMan::ReturnDilithiumKeyToPool()
{
    // TODO: Implement Dilithium key pool management
}

bool DilithiumScriptPubKeyMan::GetDilithiumKey(const CKeyID& keyid, CDilithiumKey& dilithium_key) const
{
    // Convert CKeyID to DilithiumLegacyKeyID for compatibility
    DilithiumLegacyKeyID legacy_id{uint160(keyid)};
    return GetDilithiumKeyLegacy(legacy_id, dilithium_key);
}

bool DilithiumScriptPubKeyMan::HaveDilithiumKey(const CKeyID& keyid) const
{
    // Convert CKeyID to DilithiumLegacyKeyID for compatibility
    DilithiumLegacyKeyID legacy_id{uint160(keyid)};
    return HaveDilithiumKeyLegacy(legacy_id);
}

bool DilithiumScriptPubKeyMan::LoadDilithiumKey(const CDilithiumKey& key, const DilithiumKeyID& key_id)
{
    LOCK(m_dilithium_mutex);
    m_dilithium_keys[key_id] = key;
    return true;
}

bool DilithiumScriptPubKeyMan::LoadDilithiumKeyLegacy(const CDilithiumKey& key, const DilithiumLegacyKeyID& key_id)
{
    LOCK(m_dilithium_mutex);
    m_legacy_dilithium_keys[key_id] = key;
    return true;
}

bool DilithiumScriptPubKeyMan::IsEncrypted() const
{
    return m_storage.HasEncryptionKeys();
}

bool DilithiumScriptPubKeyMan::IsLocked() const
{
    return m_storage.IsLocked();
}

bool DilithiumScriptPubKeyMan::Unlock(const SecureString& wallet_passphrase)
{
    // TODO: Implement wallet unlock for Dilithium
    return false;
}

bool DilithiumScriptPubKeyMan::ChangeWalletPassphrase(const SecureString& old_passphrase, const SecureString& new_passphrase)
{
    // TODO: Implement wallet passphrase change for Dilithium
    return false;
}

size_t DilithiumScriptPubKeyMan::GetDilithiumKeyCount() const
{
    LOCK(m_dilithium_mutex);
    return m_dilithium_keys.size();
}

size_t DilithiumScriptPubKeyMan::GetDilithiumLegacyKeyCount() const
{
    LOCK(m_dilithium_mutex);
    return m_legacy_dilithium_keys.size();
}

uint256 DilithiumScriptPubKeyMan::GetID() const
{
    // Generate a unique ID for this Dilithium ScriptPubKeyMan
    static std::atomic<uint64_t> counter{0};
    uint64_t instance_id = counter.fetch_add(1);
    
    std::string id_string = "DilithiumScriptPubKeyMan_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" + std::to_string(instance_id);
    return Hash(id_string);
}

} // namespace wallet
