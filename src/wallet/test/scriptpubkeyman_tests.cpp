// Copyright (c) 2020-2021 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <addresstype.h>
#include <crypto/dilithium_key.h>
#include <outputtype.h>
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

    const util::Result<CTxDestination> dest = keyman.GetNewDestination(OutputType::DILITHIUM_LEGACY);
    BOOST_REQUIRE(dest);
    BOOST_REQUIRE(std::holds_alternative<DilithiumPKHash>(*dest));

    CDilithiumKey wallet_key;
    BOOST_REQUIRE(keyman.GetDilithiumKey(expected_id, wallet_key));
    BOOST_CHECK(wallet_key == child0.key);
    BOOST_CHECK(std::get<DilithiumPKHash>(*dest) == DilithiumPKHash(child0.key.GetPubKey().GetID()));
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
