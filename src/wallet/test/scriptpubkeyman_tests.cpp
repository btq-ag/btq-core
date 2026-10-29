// Copyright (c) 2020-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockfilter.h>
#include <key.h>
#include <key_io.h>
#include <addresstype.h>
#include <consensus/amount.h>
#include <crypto/dilithium_key.h>
#include <outputtype.h>
#include <primitives/transaction.h>
#include <test/util/setup_common.h>
#include <script/descriptor.h>
#include <script/interpreter.h>
#include <script/solver.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <wallet/test/util.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
class DilithiumRescanTestAccess {
public:
    static bool AddToWalletIfInvolvingMe(CWallet& wallet, const CTransactionRef& tx, const SyncTxState& state, bool fUpdate, bool rescanning_old_block)
        EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
    {
        return wallet.AddToWalletIfInvolvingMe(tx, state, fUpdate, rescanning_old_block);
    }
};

BOOST_FIXTURE_TEST_SUITE(scriptpubkeyman_tests, BasicTestingSetup)

namespace {
constexpr uint32_t HARDENED = 0x80000000u;

CScript SingleLeafP2MRScript(const CDilithiumPubKey& pubkey)
{
    const CScript leaf = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    const std::vector<unsigned char> leaf_bytes(leaf.begin(), leaf.end());
    P2MRBuilder builder;
    builder.Add(/*depth=*/0, leaf_bytes, TAPROOT_LEAF_TAPSCRIPT);
    builder.Finalize();
    return GetScriptForDestination(builder.GetOutput());
}

// Plant index 0 so DeriveNewDilithiumChildKey retries, then check the key
// that was kept has one derivation in key_origin.path (LOW-27).
void CheckDilithiumOriginAfterCollision(CWallet& wallet, LegacyScriptPubKeyMan& keyman, bool internal)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet, keyman.cs_KeyStore)
{
    const uint32_t chain_index = internal ? 1u : 0u;

    CKey wallet_seed;
    BOOST_REQUIRE(keyman.GetKey(keyman.GetHDChain().seed_id, wallet_seed));

    CDilithiumExtKey master;
    master.SetSeed(wallet_seed);
    CDilithiumExtKey account;
    CDilithiumExtKey chain;
    CDilithiumExtKey child0;
    CDilithiumExtKey child1;
    BOOST_REQUIRE(master.Derive(account, HARDENED | 0));
    BOOST_REQUIRE(account.Derive(chain, HARDENED | chain_index));
    BOOST_REQUIRE(chain.Derive(child0, HARDENED | 0));
    BOOST_REQUIRE(chain.Derive(child1, HARDENED | 1));

    const CKeyID collided_id = CKeyID(child0.key.GetPubKey().GetID());
    const CKeyID expected_id = CKeyID(child1.key.GetPubKey().GetID());
    BOOST_REQUIRE(collided_id != expected_id);
    BOOST_REQUIRE(keyman.LoadDilithiumKey(child0.key));
    BOOST_REQUIRE(keyman.HaveDilithiumKey(collided_id));

    WalletBatch batch(wallet.GetDatabase());
    CHDChain hd_chain = keyman.GetHDChain();
    if (internal) {
        hd_chain.nInternalChainCounter = 0;
    } else {
        hd_chain.nExternalChainCounter = 0;
    }

    const CDilithiumPubKey pubkey = keyman.GenerateNewDilithiumKey(batch, hd_chain, internal);
    BOOST_REQUIRE(pubkey.IsValid());
    BOOST_CHECK(CKeyID(pubkey.GetID()) == expected_id);
    BOOST_CHECK(internal ? hd_chain.nInternalChainCounter == 2 : hd_chain.nExternalChainCounter == 2);

    const auto meta = keyman.mapKeyMetadata.find(expected_id);
    BOOST_REQUIRE(meta != keyman.mapKeyMetadata.end());
    BOOST_CHECK(meta->second.has_key_origin);
    BOOST_CHECK(meta->second.hdKeypath == (internal ? "m/0'/1'/1'" : "m/0'/0'/1'"));
    const std::vector<uint32_t> expected_origin = {
        HARDENED | 0u,
        HARDENED | chain_index,
        HARDENED | 1u,
    };
    BOOST_CHECK(meta->second.key_origin.path == expected_origin);
}
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

    LOCK(keyman.cs_KeyStore);
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

BOOST_AUTO_TEST_CASE(dilithium_hd_retry_clears_key_origin_path)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();
    // Internal derivation is dropped unless the wallet can do HD split.
    wallet.SetMinVersion(FEATURE_HD_SPLIT);

    LOCK2(wallet.cs_wallet, keyman.cs_KeyStore);
    BOOST_REQUIRE(keyman.SetupGeneration(true));
    BOOST_REQUIRE(keyman.IsHDEnabled());
    BOOST_REQUIRE(wallet.CanSupportFeature(FEATURE_HD_SPLIT));

    CheckDilithiumOriginAfterCollision(wallet, keyman, /*internal=*/false);
    CheckDilithiumOriginAfterCollision(wallet, keyman, /*internal=*/true);
}

// Production path: GetNewDestination(dilithium-*) must derive from the HD seed.
BOOST_AUTO_TEST_CASE(dilithium_getnewdestination_uses_hd_seed)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LegacyScriptPubKeyMan& keyman = *wallet.GetOrCreateLegacyScriptPubKeyMan();

    LOCK(keyman.cs_KeyStore);
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
        LOCK(legacy_keyman.cs_KeyStore);
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

// INFO-26: the LEGACY descriptor used one next_index for ECDSA and Dilithium.
// ECDSA generation must leave the Dilithium counter alone. A rescan that sees
// a used single-leaf P2MR before it is IsMine must store the keys it walks
// past and advance the Dilithium counter without moving the ECDSA lookahead.
BOOST_AUTO_TEST_CASE(descriptor_dilithium_index_independent_of_ecdsa)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    DescriptorScriptPubKeyMan* keyman{nullptr};
    DescriptorScriptPubKeyMan* internal{nullptr};
    {
        LOCK(wallet.cs_wallet);
        // Gap is the keypool. Index 2 is inside it. The internal manager scans
        // the same window for a script that is not its own.
        wallet.m_keypool_size = 8;
        wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
        wallet.SetupDescriptorScriptPubKeyMans();
        keyman = dynamic_cast<DescriptorScriptPubKeyMan*>(wallet.GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/false));
        internal = dynamic_cast<DescriptorScriptPubKeyMan*>(wallet.GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/true));
    }
    BOOST_REQUIRE(keyman);
    BOOST_REQUIRE(internal);

    const auto ecdsa_dest = keyman->GetNewDestination(OutputType::LEGACY);
    BOOST_REQUIRE(ecdsa_dest);
    {
        LOCK(keyman->cs_desc_man);
        BOOST_CHECK_EQUAL(keyman->GetWalletDescriptor().next_index, 1);
    }
    BOOST_CHECK_EQUAL(keyman->GetDilithiumNextIndex(), 0);

    const auto key0 = keyman->GetDilithiumKeyForIndex(0);
    const auto key1 = keyman->GetDilithiumKeyForIndex(1);
    const auto key2 = keyman->GetDilithiumKeyForIndex(2);
    BOOST_REQUIRE(key0);
    BOOST_REQUIRE(key1);
    BOOST_REQUIRE(key2);
    const CKeyID id0{key0->GetPubKey().GetID()};
    const CKeyID id1{key1->GetPubKey().GetID()};
    const CKeyID id2{key2->GetPubKey().GetID()};

    // Fast rescan builds this set before any P2MR metadata exists. Index 2 is
    // inside the keypool gap of 8. Index 8 is the first index past it.
    const CScript index2_p2mr = SingleLeafP2MRScript(key2->GetPubKey());
    const CScript index2_pkh = GetScriptForDestination(DilithiumPKHash(key2->GetPubKey()));
    const auto key_past_gap = keyman->GetDilithiumKeyForIndex(8);
    BOOST_REQUIRE(key_past_gap);
    const CScript past_gap_p2mr = SingleLeafP2MRScript(key_past_gap->GetPubKey());
    std::map<uint256, int32_t> filled;
    {
        LOCK(wallet.cs_wallet);
        BOOST_CHECK(wallet.ListP2MRMetadata().empty());
    }
    const std::vector<CScript> lookahead = wallet.FetchDilithiumRescanScripts(filled);
    GCSFilter::ElementSet filter_set;
    for (const CScript& script : lookahead) {
        filter_set.emplace(script.begin(), script.end());
    }
    BOOST_CHECK(filter_set.count(GCSFilter::Element(index2_p2mr.begin(), index2_p2mr.end())) == size_t{1});
    BOOST_CHECK(filter_set.count(GCSFilter::Element(index2_pkh.begin(), index2_pkh.end())) == size_t{1});
    BOOST_CHECK(filter_set.count(GCSFilter::Element(past_gap_p2mr.begin(), past_gap_p2mr.end())) == size_t{0});
    BOOST_CHECK(wallet.FetchDilithiumRescanScripts(filled).empty());

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
    mtx.vout.emplace_back(COIN, SingleLeafP2MRScript(key2->GetPubKey()));
    const CTransactionRef tx = MakeTransactionRef(mtx);

    {
        LOCK(wallet.cs_wallet);
        BOOST_CHECK(!wallet.IsMine(*tx));
        {
            LOCK(keyman->cs_desc_man);
            BOOST_CHECK(!keyman->HaveDilithiumKey(id2));
        }

        // Ordinary arrival does not import a script the wallet cannot yet recognize.
        BOOST_CHECK(!DilithiumRescanTestAccess::AddToWalletIfInvolvingMe(wallet, tx, TxStateInactive{}, /*fUpdate=*/true, /*rescanning_old_block=*/false));
        BOOST_CHECK(!wallet.GetWalletTx(tx->GetHash()));
        BOOST_CHECK_EQUAL(keyman->GetDilithiumNextIndex(), 0);
        {
            LOCK(keyman->cs_desc_man);
            BOOST_CHECK(!keyman->HaveDilithiumKey(id2));
            BOOST_CHECK_EQUAL(keyman->GetWalletDescriptor().next_index, 1);
        }

        BOOST_CHECK(DilithiumRescanTestAccess::AddToWalletIfInvolvingMe(wallet, tx, TxStateInactive{}, /*fUpdate=*/true, /*rescanning_old_block=*/true));
        BOOST_CHECK(wallet.GetWalletTx(tx->GetHash()));
        BOOST_CHECK(wallet.IsMine(*tx));
        BOOST_CHECK_EQUAL(keyman->GetDilithiumNextIndex(), 3);
        BOOST_CHECK_EQUAL(internal->GetDilithiumNextIndex(), 0);
        {
            LOCK(keyman->cs_desc_man);
            BOOST_CHECK_EQUAL(keyman->GetWalletDescriptor().next_index, 1);
            BOOST_CHECK(keyman->HaveDilithiumKey(id0));
            BOOST_CHECK(keyman->HaveDilithiumKey(id1));
            BOOST_CHECK(keyman->HaveDilithiumKey(id2));
        }
    }

    // Counter moved to 3, so the next fetch is only the newly opened gap.
    const std::vector<CScript> extended = wallet.FetchDilithiumRescanScripts(filled);
    bool extended_has_index2 = false;
    for (const CScript& script : extended) {
        if (script == index2_p2mr) extended_has_index2 = true;
    }
    BOOST_CHECK(!extended.empty());
    BOOST_CHECK(!extended_has_index2);

    // An ECDSA descriptor script still moves next_index and leaves the Dilithium counter.
    CScript ecdsa_ahead;
    {
        LOCK(keyman->cs_desc_man);
        FlatSigningProvider out;
        std::vector<CScript> scripts;
        const WalletDescriptor desc = keyman->GetWalletDescriptor();
        BOOST_REQUIRE(desc.descriptor->ExpandFromCache(2, desc.cache, scripts, out));
        BOOST_REQUIRE(!scripts.empty());
        ecdsa_ahead = scripts[0];
    }
    keyman->MarkUnusedAddresses(ecdsa_ahead);
    {
        LOCK(keyman->cs_desc_man);
        BOOST_CHECK_EQUAL(keyman->GetWalletDescriptor().next_index, 3);
    }
    BOOST_CHECK_EQUAL(keyman->GetDilithiumNextIndex(), 3);

    // The next Dilithium key is index 3, not an index the ECDSA calls consumed.
    const auto minted = keyman->GenerateNewDilithiumKey();
    BOOST_REQUIRE(minted);
    const auto expect3 = keyman->GetDilithiumKeyForIndex(3);
    BOOST_REQUIRE(expect3);
    BOOST_CHECK(minted->GetID() == expect3->GetPubKey().GetID());
    BOOST_CHECK_EQUAL(keyman->GetDilithiumNextIndex(), 4);
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
        LOCK(keyman.cs_KeyStore);
        BOOST_REQUIRE(keyman.SetupGeneration(true));

        WalletBatch batch(wallet.GetDatabase());
        CHDChain hd_chain = keyman.GetHDChain();
        const CDilithiumPubKey pubkey = keyman.GenerateNewDilithiumKey(batch, hd_chain, /*internal=*/false);
        key_id = CKeyID(pubkey.GetID());
        BOOST_REQUIRE(keyman.GetDilithiumKey(key_id, before_encrypt));
    }

    BOOST_REQUIRE(wallet.EncryptWallet("encrypt"));
    BOOST_CHECK(wallet.IsCrypted());
    BOOST_CHECK(wallet.IsLocked());
    BOOST_CHECK(keyman.HaveDilithiumKey(key_id));

    BOOST_REQUIRE(wallet.Unlock("encrypt"));
    CDilithiumKey after_encrypt;
    BOOST_REQUIRE(keyman.GetDilithiumKey(key_id, after_encrypt));
    BOOST_CHECK(after_encrypt == before_encrypt);
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
        LOCK(keyman->cs_desc_man);
        BOOST_REQUIRE(keyman->GetDilithiumKey(key_id, before_encrypt));
    }

    BOOST_REQUIRE(wallet.EncryptWallet("encrypt"));
    BOOST_CHECK(wallet.IsCrypted());
    BOOST_CHECK(wallet.IsLocked());
    {
        LOCK(keyman->cs_desc_man);
        CDilithiumKey locked_key;
        BOOST_CHECK(keyman->HaveDilithiumKey(key_id));
        BOOST_CHECK(!keyman->GetDilithiumKey(key_id, locked_key));
    }

    BOOST_REQUIRE(wallet.Unlock("encrypt"));
    CDilithiumKey after_encrypt;
    {
        LOCK(keyman->cs_desc_man);
        BOOST_REQUIRE(keyman->GetDilithiumKey(key_id, after_encrypt));
    }
    BOOST_CHECK(after_encrypt == before_encrypt);
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
        LOCK(keyman.cs_KeyStore);
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

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
