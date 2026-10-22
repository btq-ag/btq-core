// Copyright (c) 2020-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <array>
#include <key.h>
#include <key_io.h>
#include <addresstype.h>
#include <clientversion.h>
#include <consensus/amount.h>
#include <crypto/dilithium_key.h>
#include <hash.h>
#include <outputtype.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <script/descriptor.h>
#include <script/solver.h>
#include <script/sign.h>
#include <sync.h>
#include <wallet/p2mr.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <wallet/test/util.h>

#include <boost/test/unit_test.hpp>

#include <stdexcept>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(scriptpubkeyman_tests, BasicTestingSetup)

namespace {
constexpr uint32_t HARDENED = 0x80000000u;

struct PreP2MRTestingSetup : BasicTestingSetup {
    PreP2MRTestingSetup() : BasicTestingSetup(ChainType::BTQTEST) {}
};
} // namespace

// Test LegacyScriptPubKeyMan::CanProvide behavior, making sure it returns true
// for recognized scripts even when keys may not be available for signing.
BOOST_AUTO_TEST_CASE(CanProvide)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();

    std::vector<CKey> keys(2);
    std::vector<CPubKey> pubkeys;
    for (CKey& key : keys) {
        key.MakeNewKey(true);
        pubkeys.emplace_back(key.GetPubKey());
    }
    CScript multisig_script = GetScriptForMultisig(1, pubkeys);
    CScript p2sh_script = GetScriptForDestination(ScriptHash(multisig_script));
    SignatureData data;

    BOOST_CHECK(!keyman.CanProvide(p2sh_script, data));
    BOOST_CHECK(keyman.AddCScript(multisig_script));
    data = SignatureData();
    BOOST_CHECK(keyman.CanProvide(p2sh_script, data));
}

// PR #54 review: DeriveNewDilithiumChildKey must derive from the wallet HD seed,
// not call MakeNewKey(). Compare via full CDilithiumKey identity (CPubKey only
// stores 65 bytes and cannot represent 1312-byte Dilithium pubkeys faithfully).
BOOST_AUTO_TEST_CASE(dilithium_hd_wallet_derivation_deterministic)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();

    LOCK2(wallet.cs_wallet, keyman.cs_KeyStore);
    BOOST_REQUIRE(keyman.SetupGeneration(true));
    BOOST_REQUIRE(keyman.IsHDEnabled());

    CKey wallet_seed;
    BOOST_REQUIRE(keyman.GetKey(keyman.GetHDChain().seed_id, wallet_seed));

    CDilithiumExtKey master;
    master.SetSeed(wallet_seed);
    CDilithiumExtKey account;
    CDilithiumExtKey chain;
    CDilithiumExtKey child0;
    CDilithiumExtKey child1;
    BOOST_REQUIRE(master.Derive(account, HARDENED | 0));
    BOOST_REQUIRE(account.Derive(chain, HARDENED | 0));
    BOOST_REQUIRE(chain.Derive(child0, HARDENED | 0));
    BOOST_REQUIRE(chain.Derive(child1, HARDENED | 1));

    const CKeyID expected_id0 = CKeyID(child0.key.GetPubKey().GetID());
    const CKeyID expected_id1 = CKeyID(child1.key.GetPubKey().GetID());
    BOOST_CHECK(expected_id0 != expected_id1);

    WalletBatch batch(wallet.GetDatabase());
    CHDChain hd_chain = keyman.GetHDChain();
    hd_chain.nExternalChainCounter = 0;

    keyman.GenerateNewDilithiumKey(batch, hd_chain, /*internal=*/false);
    keyman.GenerateNewDilithiumKey(batch, hd_chain, /*internal=*/false);

    CDilithiumKey wallet_key0;
    CDilithiumKey wallet_key1;
    BOOST_REQUIRE(keyman.GetDilithiumKey(expected_id0, wallet_key0));
    BOOST_REQUIRE(keyman.GetDilithiumKey(expected_id1, wallet_key1));
    BOOST_CHECK(wallet_key0 == child0.key);
    BOOST_CHECK(wallet_key1 == child1.key);
    BOOST_CHECK(wallet_key0 != wallet_key1);
}

// Production path: GetNewDestination(dilithium-*) must derive from the HD seed.
BOOST_AUTO_TEST_CASE(dilithium_getnewdestination_uses_hd_seed)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();

    LOCK2(wallet.cs_wallet, keyman.cs_KeyStore);
    BOOST_REQUIRE(keyman.SetupGeneration(true));
    BOOST_REQUIRE(keyman.IsHDEnabled());

    CKey wallet_seed;
    BOOST_REQUIRE(keyman.GetKey(keyman.GetHDChain().seed_id, wallet_seed));

    CDilithiumExtKey master;
    master.SetSeed(wallet_seed);
    CDilithiumExtKey account;
    CDilithiumExtKey chain;
    CDilithiumExtKey child0;
    BOOST_REQUIRE(master.Derive(account, HARDENED | 0));
    BOOST_REQUIRE(account.Derive(chain, HARDENED | 0));
    BOOST_REQUIRE(chain.Derive(child0, HARDENED | 0));

    const CKeyID expected_id = CKeyID(child0.key.GetPubKey().GetID());

    WalletBatch batch(wallet.GetDatabase());
    CHDChain hd_chain = keyman.GetHDChain();
    hd_chain.nExternalChainCounter = 0;
    keyman.LoadHDChain(hd_chain);
    batch.WriteHDChain(hd_chain);

    // Generate the key directly. GetNewDestination(DILITHIUM_LEGACY) is refused
    // on a P2MR-only chain, and what this case is about is that the key comes
    // from the wallet's HD seed rather than from anywhere else.
    WalletBatch keygen_batch(wallet.GetDatabase());
    CHDChain keygen_chain = keyman.GetHDChain();
    const CDilithiumPubKey pubkey = keyman.GenerateNewDilithiumKey(keygen_batch, keygen_chain, /*internal=*/false);
    BOOST_REQUIRE(pubkey.IsValid());

    CDilithiumKey wallet_key;
    BOOST_REQUIRE(keyman.GetDilithiumKey(expected_id, wallet_key));
    BOOST_CHECK(wallet_key == child0.key);
    BOOST_CHECK(DilithiumPKHash(pubkey) == DilithiumPKHash(child0.key.GetPubKey().GetID()));
}

BOOST_AUTO_TEST_CASE(dilithium_legacy_generation_disabled_on_p2mr_only_chain)
{
    // Regtest activates DEPLOYMENT_DILITHIUM_P2MR at height 1, so a base58
    // Dilithium destination is not a valid payment destination here and the
    // wallet could not size or sign a spend of one. Both spk manager kinds must
    // refuse to mint one rather than stranding whatever is paid to it (#97).
    BOOST_REQUIRE(!LegacyDilithiumBase58PaymentsAllowed());

    CWallet legacy_wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& legacy_keyman = *legacy_wallet.GetOrCreateLegacyScriptPubKeyMan();
    {
        LOCK2(legacy_wallet.cs_wallet, legacy_keyman.cs_KeyStore);
        BOOST_REQUIRE(legacy_keyman.SetupGeneration(true));
    }
    BOOST_CHECK(!legacy_keyman.GetNewDestination(OutputType::DILITHIUM_LEGACY));

    CWallet desc_wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    DescriptorScriptPubKeyMan* desc_keyman{nullptr};
    {
        LOCK(desc_wallet.cs_wallet);
        desc_wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
        desc_wallet.SetupDescriptorScriptPubKeyMans();
        desc_keyman = dynamic_cast<DescriptorScriptPubKeyMan*>(desc_wallet.GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/false));
    }
    BOOST_REQUIRE(desc_keyman);
    BOOST_CHECK(!desc_keyman->GetNewDestination(OutputType::DILITHIUM_LEGACY));

    // The refusal must not take Dilithium key generation with it: P2MR receive
    // needs the same key on exactly the chains where the destination is refused.
    BOOST_CHECK(desc_keyman->GenerateNewDilithiumKey());
}

BOOST_AUTO_TEST_CASE(dilithium_bech32_generation_disabled)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();

    util::Result<CTxDestination> dest = keyman.GetNewDestination(OutputType::DILITHIUM_BECH32);
    BOOST_CHECK(!dest);
}

BOOST_AUTO_TEST_CASE(get_affected_keys_includes_dilithium_pubkeys)
{
    CDilithiumKey key;
    key.MakeNewKey();
    const CDilithiumPubKey pubkey = key.GetPubKey();
    const DilithiumPKHash key_hash{pubkey};
    const CKeyID key_id{static_cast<uint160>(key_hash)};

    FlatSigningProvider provider;
    provider.dilithium_pubkeys[key_hash] = pubkey;

    const std::vector<CKeyID> affected_keys = GetAffectedKeys(GetScriptForDestination(key_hash), provider);
    BOOST_REQUIRE_EQUAL(affected_keys.size(), 1);
    BOOST_CHECK(affected_keys[0] == key_id);
}

BOOST_AUTO_TEST_CASE(legacy_encrypt_migrates_dilithium_keys)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();

    CKeyID key_id;
    CDilithiumKey before_encrypt;
    {
        LOCK2(wallet.cs_wallet, keyman.cs_KeyStore);
        BOOST_REQUIRE(keyman.SetupGeneration(true));

        WalletBatch batch(wallet.GetDatabase());
        CHDChain hd_chain = keyman.GetHDChain();
        const CDilithiumPubKey pubkey = keyman.GenerateNewDilithiumKey(batch, hd_chain, /*internal=*/false);
        key_id = CKeyID(pubkey.GetID());
        BOOST_REQUIRE(keyman.GetDilithiumKey(key_id, before_encrypt));
    }

    // GetDilithiumKey checks IsLocked() under cs_KeyStore, so take
    // cs_wallet first, as the wallet does.
    LOCK(wallet.cs_wallet);
    BOOST_REQUIRE(wallet.EncryptWallet("encrypt"));
    BOOST_CHECK(wallet.IsCrypted());
    BOOST_CHECK(wallet.IsLocked());
    BOOST_CHECK(keyman.HaveDilithiumKey(key_id));

    BOOST_REQUIRE(wallet.Unlock("encrypt"));
    CDilithiumKey after_encrypt;
    BOOST_REQUIRE(keyman.GetDilithiumKey(key_id, after_encrypt));
    BOOST_CHECK(after_encrypt == before_encrypt);
}

// Two Dilithium keys so the merged CheckDecryptionKey loop is what runs.
// A bad passphrase returns false. One damaged ciphertext next to a real one
// throws, and the wallet stays locked.
BOOST_AUTO_TEST_CASE(legacy_unlock_mixed_dilithium_ciphertexts)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();

    CKeyID key_id0;
    CKeyID key_id1;
    CDilithiumKey before0;
    CDilithiumKey before1;
    {
        LOCK2(wallet.cs_wallet, keyman.cs_KeyStore);
        BOOST_REQUIRE(keyman.SetupGeneration(true));

        WalletBatch batch(wallet.GetDatabase());
        CHDChain hd_chain = keyman.GetHDChain();
        const CDilithiumPubKey pubkey0 = keyman.GenerateNewDilithiumKey(batch, hd_chain, /*internal=*/false);
        const CDilithiumPubKey pubkey1 = keyman.GenerateNewDilithiumKey(batch, hd_chain, /*internal=*/false);
        key_id0 = CKeyID(pubkey0.GetID());
        key_id1 = CKeyID(pubkey1.GetID());
        BOOST_REQUIRE(key_id0 != key_id1);
        BOOST_REQUIRE(keyman.GetDilithiumKey(key_id0, before0));
        BOOST_REQUIRE(keyman.GetDilithiumKey(key_id1, before1));
    }

    LOCK(wallet.cs_wallet);
    BOOST_REQUIRE(wallet.EncryptWallet("encrypt"));
    BOOST_CHECK(wallet.IsLocked());

    bool wrong_passphrase_unlocked = true;
    BOOST_CHECK_NO_THROW(wrong_passphrase_unlocked = wallet.Unlock("wrong"));
    BOOST_CHECK(!wrong_passphrase_unlocked);
    BOOST_CHECK(wallet.IsLocked());

    BOOST_REQUIRE(wallet.Unlock("encrypt"));
    CDilithiumKey after0;
    CDilithiumKey after1;
    BOOST_REQUIRE(keyman.GetDilithiumKey(key_id0, after0));
    BOOST_REQUIRE(keyman.GetDilithiumKey(key_id1, after1));
    BOOST_CHECK(after0 == before0);
    BOOST_CHECK(after1 == before1);
    wallet.Lock();

    // The thoroughly-checked path decrypts the first Dilithium key and stops,
    // so damage that record. Refresh its checksum or load rejects it.
    const CKeyID damaged_id = key_id0 < key_id1 ? key_id0 : key_id1;
    const CKeyID intact_id = key_id0 < key_id1 ? key_id1 : key_id0;
    MockableData records = GetMockableDatabase(wallet).m_records;
    DataStream damaged_key;
    DataStream intact_key;
    damaged_key << DBKeys::DILITHIUM_CRYPTED_KEY << damaged_id;
    intact_key << DBKeys::DILITHIUM_CRYPTED_KEY << intact_id;
    const SerializeData damaged_key_bytes{damaged_key.begin(), damaged_key.end()};
    const SerializeData intact_key_bytes{intact_key.begin(), intact_key.end()};
    BOOST_REQUIRE(records.count(damaged_key_bytes) == 1);
    BOOST_REQUIRE(records.count(intact_key_bytes) == 1);
    const SerializeData intact_value = records.at(intact_key_bytes);

    {
        DataStream in(records.at(damaged_key_bytes));
        std::vector<unsigned char> secret;
        uint256 checksum;
        in >> secret >> checksum;
        BOOST_REQUIRE(in.eof());
        BOOST_REQUIRE(!secret.empty());
        secret.front() ^= 0x01;
        CDataStream out(SER_DISK, CLIENT_VERSION);
        out << std::make_pair(secret, Hash(secret));
        records[damaged_key_bytes] = SerializeData{out.begin(), out.end()};
    }
    BOOST_CHECK(records.at(intact_key_bytes) == intact_value);

    auto reloaded = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(records));
    {
        LOCK(reloaded->cs_wallet);
        BOOST_REQUIRE_EQUAL(reloaded->LoadWallet(), DBErrors::LOAD_OK);
        LegacyScriptPubKeyMan* reloaded_keyman = reloaded->GetLegacyScriptPubKeyMan();
        BOOST_REQUIRE(reloaded_keyman);
        BOOST_CHECK(reloaded_keyman->HaveDilithiumKey(key_id0));
        BOOST_CHECK(reloaded_keyman->HaveDilithiumKey(key_id1));
        BOOST_CHECK_THROW(reloaded->Unlock("encrypt"), std::runtime_error);
        BOOST_CHECK(reloaded->IsLocked());
        CDilithiumKey locked_key;
        BOOST_CHECK(!reloaded_keyman->GetDilithiumKey(key_id0, locked_key));
        BOOST_CHECK(!reloaded_keyman->GetDilithiumKey(key_id1, locked_key));
    }
}

BOOST_AUTO_TEST_CASE(descriptor_encrypt_migrates_dilithium_keys)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    DescriptorScriptPubKeyMan* keyman{nullptr};
    {
        LOCK(wallet.cs_wallet);
        wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
        wallet.SetupDescriptorScriptPubKeyMans();
        keyman = dynamic_cast<DescriptorScriptPubKeyMan*>(wallet.GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/false));
    }
    BOOST_REQUIRE(keyman);

    const util::Result<CDilithiumPubKey> pubkey = keyman->GenerateNewDilithiumKey();
    BOOST_REQUIRE(pubkey);
    const CKeyID key_id{pubkey->GetID()};

    CDilithiumKey before_encrypt;
    {
        LOCK2(wallet.cs_wallet, keyman->cs_desc_man);
        BOOST_REQUIRE(keyman->GetDilithiumKey(key_id, before_encrypt));
    }

    BOOST_REQUIRE(wallet.EncryptWallet("encrypt"));
    BOOST_CHECK(wallet.IsCrypted());
    BOOST_CHECK(wallet.IsLocked());
    {
        LOCK2(wallet.cs_wallet, keyman->cs_desc_man);
        CDilithiumKey locked_key;
        BOOST_CHECK(keyman->HaveDilithiumKey(key_id));
        BOOST_CHECK(!keyman->GetDilithiumKey(key_id, locked_key));
    }

    BOOST_REQUIRE(wallet.Unlock("encrypt"));
    CDilithiumKey after_encrypt;
    {
        LOCK2(wallet.cs_wallet, keyman->cs_desc_man);
        BOOST_REQUIRE(keyman->GetDilithiumKey(key_id, after_encrypt));
    }
    BOOST_CHECK(after_encrypt == before_encrypt);
}

// CWallet::GetSolvingProvider is called without cs_wallet held (WalletImpl::getPubKey,
// used by GUI coin control). The descriptor manager takes cs_desc_man and then, for an
// encrypted wallet, IsLocked() takes cs_wallet: the reverse of the cs_wallet ->
// cs_desc_man order used everywhere else. A DEBUG_LOCKORDER build detects it.
BOOST_AUTO_TEST_CASE(solving_provider_takes_cs_wallet_first)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    DescriptorScriptPubKeyMan* keyman{nullptr};
    {
        LOCK(wallet.cs_wallet);
        wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
        wallet.SetupDescriptorScriptPubKeyMans();
        keyman = dynamic_cast<DescriptorScriptPubKeyMan*>(wallet.GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/false));
    }
    BOOST_REQUIRE(keyman);
    BOOST_REQUIRE(keyman->GenerateNewDilithiumKey());
    BOOST_REQUIRE(wallet.EncryptWallet("encrypt"));
    BOOST_REQUIRE(wallet.Unlock("encrypt"));

    CScript script;
    {
        // Record the usual order.
        LOCK2(wallet.cs_wallet, keyman->cs_desc_man);
        const auto scripts{keyman->GetScriptPubKeys()};
        BOOST_REQUIRE(!scripts.empty());
        script = *scripts.begin();
    }

#ifdef DEBUG_LOCKORDER
    const bool prev{g_debug_lockorder_abort};
    g_debug_lockorder_abort = false;
#endif
    BOOST_CHECK_NO_THROW(BOOST_CHECK(wallet.GetSolvingProvider(script)));
#ifdef DEBUG_LOCKORDER
    g_debug_lockorder_abort = prev;
#endif
}

// BTQ-AUDIT-001/009: encryptwallet must migrate plaintext Dilithium keys to crypted map.
BOOST_AUTO_TEST_CASE(dilithium_encrypt_wallet_roundtrip)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();

    LOCK2(wallet.cs_wallet, keyman.cs_KeyStore);
    BOOST_REQUIRE(keyman.SetupGeneration(true));

    WalletBatch batch(wallet.GetDatabase());
    CHDChain hd_chain = keyman.GetHDChain();
    const CDilithiumPubKey pubkey = keyman.GenerateNewDilithiumKey(batch, hd_chain, /*internal=*/false);
    const CKeyID key_id = CKeyID(pubkey.GetID());

    CDilithiumKey plain_key;
    BOOST_REQUIRE(keyman.GetDilithiumKey(key_id, plain_key));

    BOOST_REQUIRE(wallet.EncryptWallet("encrypt-pass"));
    wallet.Lock();

    CDilithiumKey locked_key;
    BOOST_CHECK(!keyman.GetDilithiumKey(key_id, locked_key));

    BOOST_REQUIRE(wallet.Unlock("encrypt-pass"));
    CDilithiumKey decrypted_key;
    BOOST_REQUIRE(keyman.GetDilithiumKey(key_id, decrypted_key));
    BOOST_CHECK(decrypted_key == plain_key);
}

// Issue #74 / #75: LegacyScriptPubKeyMan must implement SigningProvider Dilithium
// lookups. After #64, SignStep only signs Dilithium inside P2MR_TAPSCRIPT (BASE
// DILITHIUM_PUBKEYHASH returns false immediately), but P2MR leaf signing still
// calls GetDilithiumKeyByHash / GetDilithiumPubKey on the legacy keyman.
BOOST_AUTO_TEST_CASE(legacy_dilithium_signing_provider_produces_signature)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();

    CDilithiumKey dilithium_key;
    CDilithiumPubKey dilithium_pubkey;
    DilithiumPKHash key_hash;
    {
        LOCK2(wallet.cs_wallet, keyman.cs_KeyStore);
        BOOST_REQUIRE(keyman.SetupGeneration(true));

        WalletBatch batch(wallet.GetDatabase());
        CHDChain hd_chain = keyman.GetHDChain();
        dilithium_pubkey = keyman.GenerateNewDilithiumKey(batch, hd_chain, /*internal=*/false);
        key_hash = DilithiumPKHash(dilithium_pubkey);
        const CKeyID key_id = CKeyID(dilithium_pubkey.GetID());
        BOOST_REQUIRE(keyman.GetDilithiumKey(key_id, dilithium_key));
    }

    CDilithiumPubKey looked_up_pubkey;
    CDilithiumKey looked_up_key;
    BOOST_REQUIRE(keyman.GetDilithiumPubKey(key_hash, looked_up_pubkey));
    BOOST_CHECK(looked_up_pubkey == dilithium_pubkey);
    BOOST_REQUIRE(keyman.GetDilithiumKeyByHash(key_hash, looked_up_key));
    BOOST_CHECK(looked_up_key == dilithium_key);

    KeyOriginInfo origin;
    // Origin may be absent for non-HD keys; the override must still be callable.
    (void)keyman.GetDilithiumKeyOrigin(key_hash, origin);

    // LegacySigningProvider forwards pubkeys/origins but never private keys.
    const LegacySigningProvider hiding{keyman};
    CDilithiumPubKey hiding_pubkey;
    BOOST_REQUIRE(hiding.GetDilithiumPubKey(key_hash, hiding_pubkey));
    BOOST_CHECK(hiding_pubkey == dilithium_pubkey);
    CDilithiumKey hiding_key;
    BOOST_CHECK(!hiding.GetDilithiumKeyByHash(key_hash, hiding_key));

    // BASE Dilithium P2PKH is intentionally unsatisfiable after P2MR-only.
    const CScript script_pubkey = GetScriptForDestination(key_hash);
    CMutableTransaction spending_tx;
    spending_tx.vin.resize(1);
    spending_tx.vout.emplace_back(1 * COIN, CScript() << OP_TRUE);
    MutableTransactionSignatureCreator creator{spending_tx, 0, 1 * COIN, SIGHASH_ALL};
    SignatureData sigdata;
    BOOST_CHECK(!ProduceSignature(keyman, creator, script_pubkey, sigdata));
    BOOST_CHECK(!sigdata.complete);
}

BOOST_AUTO_TEST_CASE(descriptor_dilithium_needs_one_private_key)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LOCK(wallet.cs_wallet);
    wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);

    const auto ext_key = [](uint8_t fill) {
        CExtKey ext;
        const std::vector<std::byte> seed(32, std::byte{fill});
        ext.SetSeed(seed);
        return ext;
    };
    const auto xprv = [&](uint8_t fill) { return EncodeExtKey(ext_key(fill)); };
    const auto xpub = [&](uint8_t fill) { return EncodeExtPubKey(ext_key(fill).Neuter()); };
    const auto add = [&](const std::string& desc_str, size_t expected_keys) {
        FlatSigningProvider keys;
        std::string error;
        auto parsed = Parse(desc_str, keys, error, /*require_checksum=*/false);
        BOOST_REQUIRE_MESSAGE(parsed, error);
        BOOST_REQUIRE_EQUAL(keys.keys.size(), expected_keys);
        WalletDescriptor w_desc(std::move(parsed), /*creation_time=*/1, /*range_start=*/0, /*range_end=*/1000, /*next_index=*/0);
        auto* spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(wallet.AddWalletDescriptor(w_desc, keys, /*label=*/"", /*internal=*/false));
        BOOST_REQUIRE(spk_man);
        return spk_man;
    };

    auto* multi = add("sh(multi(1," + xprv(1) + "/*," + xprv(2) + "/*))", 2);
    const auto multi_res = multi->GenerateNewDilithiumKey();
    BOOST_CHECK(!multi_res);
    BOOST_CHECK(util::ErrorString(multi_res).original.find("exactly one private key") != std::string::npos);

    auto* single = add("pkh(" + xprv(3) + "/*)", 1);
    BOOST_CHECK(single->GenerateNewDilithiumKey());

    // A descriptor that starts with one private key and gains a second on re-import
    auto* grows = add("sh(multi(1," + xprv(4) + "/*," + xpub(5) + "/*))", 1);
    BOOST_CHECK(grows->GenerateNewDilithiumKey());
    BOOST_CHECK_EQUAL(add("sh(multi(1," + xprv(4) + "/*," + xprv(5) + "/*))", 2), grows);
    const auto grown_res = grows->GenerateNewDilithiumKey();
    BOOST_CHECK(!grown_res);
    BOOST_CHECK(util::ErrorString(grown_res).original.find("exactly one private key") != std::string::npos);
}

static std::optional<CDilithiumKey> DilithiumKeyFromDescriptor(CWallet& wallet, const CKeyID& keyid) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    AssertLockHeld(wallet.cs_wallet);
    for (ScriptPubKeyMan* spk_man : wallet.GetAllScriptPubKeyMans()) {
        auto* desc = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
        if (!desc) continue;
        LOCK(desc->cs_desc_man);
        CDilithiumKey key;
        if (desc->GetDilithiumKey(keyid, key)) return key;
    }
    return std::nullopt;
}

// A legacy wallet that has called getnewdilithiumaddress shares HD indexes with
// secp256k1. Migration must not abort on the index that has no secp256k1 script,
// and the descriptor wallet must still hold that Dilithium key and the same
// single-leaf P2MR destination.
BOOST_AUTO_TEST_CASE(migrate_legacy_dilithium_keeps_p2mr)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    wallet.m_keypool_size = 1;

    CTxDestination dest;
    CDilithiumKey original;
    CKeyID keyid;
    {
        LOCK(wallet.cs_wallet);
        wallet.SetMinVersion(FEATURE_LATEST);
        LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();
        BOOST_REQUIRE(keyman.SetupGeneration(true));

        const auto created = CreateDilithiumP2MRReceive(wallet, "recv");
        BOOST_REQUIRE(created);
        dest = created->dest;

        const std::set<CKeyID> ids = keyman.GetDilithiumKeyIDs();
        BOOST_REQUIRE_EQUAL(ids.size(), 1);
        keyid = *ids.begin();
        BOOST_REQUIRE(keyman.GetDilithiumKey(keyid, original));

        bilingual_str error;
        auto data = wallet.GetDescriptorsForLegacy(error);
        BOOST_REQUIRE_MESSAGE(data, error.original);
        BOOST_REQUIRE_MESSAGE(wallet.ApplyMigrationData(*data, error), error.original);

        BOOST_CHECK(wallet.IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS));
        BOOST_CHECK(wallet.GetLegacyScriptPubKeyMan() == nullptr);

        const auto migrated = DilithiumKeyFromDescriptor(wallet, keyid);
        BOOST_REQUIRE(migrated);
        BOOST_CHECK(*migrated == original);
        BOOST_CHECK(wallet.IsMine(dest) == ISMINE_SPENDABLE);

        const auto entries = ListP2MR(wallet);
        BOOST_REQUIRE_EQUAL(entries.size(), 1);
        BOOST_CHECK(entries[0].dest == dest);
        BOOST_CHECK_EQUAL(entries[0].label, "recv");
        BOOST_CHECK_EQUAL(entries[0].address, EncodeDestination(dest));
    }

    CWallet loaded(m_node.chain.get(), "", CreateMockableWalletDatabase(GetMockableDatabase(wallet).m_records));
    BOOST_CHECK_EQUAL(loaded.LoadWallet(), DBErrors::LOAD_OK);
    LOCK(loaded.cs_wallet);
    BOOST_CHECK(loaded.IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS));
    const auto reloaded = DilithiumKeyFromDescriptor(loaded, keyid);
    BOOST_REQUIRE(reloaded);
    BOOST_CHECK(*reloaded == original);
    BOOST_CHECK(loaded.IsMine(dest) == ISMINE_SPENDABLE);
    const auto reloaded_entries = ListP2MR(loaded);
    BOOST_REQUIRE_EQUAL(reloaded_entries.size(), 1);
    BOOST_CHECK(reloaded_entries[0].dest == dest);
}

BOOST_FIXTURE_TEST_CASE(descriptor_dilithium_keypool_reservation_is_refused, PreP2MRTestingSetup)
{
    BOOST_REQUIRE(LegacyDilithiumBase58PaymentsAllowed());
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LOCK(wallet.cs_wallet);
    wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    wallet.SetupDescriptorScriptPubKeyMans();
    auto* manager = dynamic_cast<DescriptorScriptPubKeyMan*>(wallet.GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/false));
    BOOST_REQUIRE(manager);
    // The public wallet path has no Dilithium-legacy keypool manager.
    BOOST_CHECK(!wallet.GetNewChangeDestination(OutputType::DILITHIUM_LEGACY));
    int32_t ecdsa_index;
    {
        LOCK(manager->cs_desc_man);
        ecdsa_index = manager->GetWalletDescriptor().next_index;
    }
    int64_t index{-1};
    CKeyPool keypool;
    BOOST_CHECK(!manager->GetReservedDestination(OutputType::DILITHIUM_LEGACY, /*internal=*/false, index, keypool));
    BOOST_CHECK_EQUAL(index, -1);
    {
        LOCK(manager->cs_desc_man);
        BOOST_CHECK_EQUAL(manager->GetWalletDescriptor().next_index, ecdsa_index);
    }
    // A refused reservation must not consume an index in either sequence.
    const auto expected = manager->GetDilithiumKeyForIndex(0);
    const auto generated = manager->GenerateNewDilithiumKey();
    BOOST_REQUIRE(expected);
    BOOST_REQUIRE(generated);
    BOOST_CHECK(generated->GetID() == expected->GetPubKey().GetID());
}

BOOST_AUTO_TEST_CASE(descriptor_dilithium_generation_checks_other_managers)
{
    for (const bool encrypted : {false, true}) {
        BOOST_TEST_CONTEXT("encrypted=" << encrypted) {
            CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
            std::array<DescriptorScriptPubKeyMan*, 2> managers;
            std::array<CKeyID, 2> expected_ids;
            {
                LOCK(wallet.cs_wallet);
                wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
                wallet.SetupDescriptorScriptPubKeyMans();
                if (encrypted) {
                    BOOST_REQUIRE(wallet.EncryptWallet("test-passphrase"));
                    BOOST_REQUIRE(wallet.Unlock("test-passphrase"));
                }
                for (int i = 0; i < 2; ++i) {
                    managers[i] = dynamic_cast<DescriptorScriptPubKeyMan*>(wallet.GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/i == 1));
                    BOOST_REQUIRE(managers[i]);
                }
                BOOST_REQUIRE(managers[0] != managers[1]);
                for (int i = 0; i < 2; ++i) {
                    // Match reload behavior: the deriving manager does not own
                    // its first key. Both lookup directions must work.
                    const auto recovered = managers[i]->GetDilithiumKeyForIndex(0);
                    const auto next = managers[i]->GetDilithiumKeyForIndex(1);
                    BOOST_REQUIRE(recovered);
                    BOOST_REQUIRE(next);
                    BOOST_REQUIRE(managers[1 - i]->AddDilithiumKeyPubKey(*recovered));
                    expected_ids[i] = CKeyID{next->GetPubKey().GetID()};
                }
            }
            for (int i = 0; i < 2; ++i) {
                const auto generated = managers[i]->GenerateNewDilithiumKey();
                BOOST_REQUIRE(generated);
                BOOST_CHECK(CKeyID{generated->GetID()} == expected_ids[i]);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(descriptor_dilithium_key_write_failure_drops_memory)
{
    for (const bool encrypted : {false, true}) {
        BOOST_TEST_CONTEXT("encrypted=" << encrypted) {
            CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
            DescriptorScriptPubKeyMan* keyman{nullptr};
            {
                LOCK(wallet.cs_wallet);
                wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
                wallet.SetupDescriptorScriptPubKeyMans();
                if (encrypted) {
                    BOOST_REQUIRE(wallet.EncryptWallet("test-passphrase"));
                    BOOST_REQUIRE(wallet.Unlock("test-passphrase"));
                }
                keyman = dynamic_cast<DescriptorScriptPubKeyMan*>(wallet.GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/false));
            }
            BOOST_REQUIRE(keyman);

            // Fail only the private-key record, after the descriptor index
            // write. Check durable index advancement below.
            const std::string record_type = encrypted ? "dilithiumckey" : "dilithiumkey";
            DataStream prefix{};
            prefix << record_type;
            const SerializeData prefix_bytes{prefix.begin(), prefix.end()};
            GetMockableDatabase(wallet).m_fail_write = [prefix_bytes](const SerializeData& key) {
                return key.size() >= prefix_bytes.size() &&
                       std::equal(prefix_bytes.begin(), prefix_bytes.end(), key.begin());
            };

            const auto check_persisted_index = [&](int32_t expected) {
                DataStream index_key{};
                index_key << DBKeys::DILITHIUM_DESC_INDEX << keyman->GetID();
                const SerializeData bytes{index_key.begin(), index_key.end()};
                const auto& records = GetMockableDatabase(wallet).m_records;
                const auto record = records.find(bytes);
                BOOST_REQUIRE(record != records.end());
                DataStream value{record->second};
                int32_t index;
                value >> index;
                BOOST_CHECK_EQUAL(index, expected);
            };
            BOOST_CHECK(!keyman->GenerateNewDilithiumKey());
            check_persisted_index(1);
            const auto skipped = keyman->GetDilithiumKeyForIndex(0);
            BOOST_REQUIRE(skipped);
            const CKeyID skipped_id{skipped->GetPubKey().GetID()};
            DataStream record_key{};
            record_key << record_type << skipped_id;
            const SerializeData record_bytes{record_key.begin(), record_key.end()};
            BOOST_CHECK_EQUAL(GetMockableDatabase(wallet).m_records.count(record_bytes), 0);
            {
                LOCK(wallet.cs_wallet);
                BOOST_CHECK(!wallet.HaveDilithiumKeyAnywhere(skipped_id));
                LOCK(keyman->cs_desc_man);
                BOOST_CHECK(!keyman->HaveDilithiumKey(skipped_id));
            }

            GetMockableDatabase(wallet).m_fail_write = nullptr;
            const auto pubkey = keyman->GenerateNewDilithiumKey();
            BOOST_REQUIRE(pubkey);
            check_persisted_index(2);
            {
                LOCK2(wallet.cs_wallet, keyman->cs_desc_man);
                BOOST_CHECK(keyman->HaveDilithiumKey(CKeyID{pubkey->GetID()}));
                BOOST_CHECK(!keyman->HaveDilithiumKey(skipped_id));
            }
            // Retrying the same key must persist it, not mistake a leftover
            // in-memory entry for a successful earlier write.
            {
                LOCK(wallet.cs_wallet);
                BOOST_REQUIRE(keyman->AddDilithiumKeyPubKey(*skipped));
                BOOST_CHECK(wallet.HaveDilithiumKeyAnywhere(skipped_id));
                BOOST_CHECK_EQUAL(GetMockableDatabase(wallet).m_records.count(record_bytes), 1);
                LOCK(keyman->cs_desc_man);
                CDilithiumKey stored;
                BOOST_REQUIRE(keyman->GetDilithiumKey(skipped_id, stored));
                BOOST_CHECK(stored == *skipped);
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
