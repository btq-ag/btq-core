// Copyright (c) 2020-2021 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <addresstype.h>
#include <consensus/amount.h>
#include <crypto/dilithium_key.h>
#include <outputtype.h>
#include <primitives/transaction.h>
#include <test/util/setup_common.h>
#include <script/solver.h>
#include <script/sign.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <wallet/test/util.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(scriptpubkeyman_tests, BasicTestingSetup)

namespace {
constexpr uint32_t HARDENED = 0x80000000u;
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

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
