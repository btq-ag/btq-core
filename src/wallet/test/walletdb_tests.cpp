// Copyright (c) 2012-2021 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <clientversion.h>
#include <crypto/dilithium_key.h>
#include <streams.h>
#include <uint256.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(walletdb_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(walletdb_readkeyvalue)
{
    /**
     * When ReadKeyValue() reads from either a "key" or "wkey" it first reads the CDataStream steam into a
     * CPrivKey or CWalletKey respectively and then reads a hash of the pubkey and privkey into a uint256.
     * Wallets from 0.8 or before do not store the pubkey/privkey hash, trying to read the hash from old
     * wallets throws an exception, for backwards compatibility this read is wrapped in a try block to
     * silently fail. The test here makes sure the type of exception thrown from CDataStream::read()
     * matches the type we expect, otherwise we need to update the "key"/"wkey" exception type caught.
     */
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    uint256 dummy;
    BOOST_CHECK_THROW(ssValue >> dummy, std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(walletdb_read_write_deadlock)
{
    // Exercises a db read write operation that shouldn't deadlock.
    for (const DatabaseFormat& db_format : DATABASE_FORMATS) {
        // Context setup
        DatabaseOptions options;
        options.require_format = db_format;
        DatabaseStatus status;
        bilingual_str error_string;
        std::unique_ptr<WalletDatabase> db = MakeDatabase(m_path_root / strprintf("wallet_%d_.dat", db_format).c_str(), options, status, error_string);
        BOOST_CHECK_EQUAL(status, DatabaseStatus::SUCCESS);

        std::shared_ptr<CWallet> wallet(new CWallet(m_node.chain.get(), "", std::move(db)));
        wallet->m_keypool_size = 4;

        // Create legacy spkm
        LOCK(wallet->cs_wallet);
        auto legacy_spkm = wallet->GetOrCreateLegacyScriptPubKeyMan();
        BOOST_CHECK(legacy_spkm->SetupGeneration(true));
        wallet->Flush();

        // Now delete all records, which performs a read write operation.
        BOOST_CHECK(wallet->GetLegacyScriptPubKeyMan()->DeleteRecords());
    }
}

BOOST_AUTO_TEST_CASE(walletdb_loads_dilithium_key_metadata)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CKeyID key_id{key.GetPubKey().GetID()};

    CKeyMetadata metadata{123456789};
    metadata.hdKeypath = "m/0'/0'/7'";

    MockableData records;
    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
        LOCK(wallet->cs_wallet);
        wallet->SetupLegacyScriptPubKeyMan();
        WalletBatch batch{wallet->GetDatabase()};
        const std::vector<unsigned char> secret{key.begin(), key.end()};
        BOOST_REQUIRE(batch.WriteDilithiumKeyByID(key_id, secret, metadata));
        wallet->Flush();
        records = GetMockableDatabase(*wallet).m_records;
    }

    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(records));
        {
            LOCK(wallet->cs_wallet);
            wallet->SetupLegacyScriptPubKeyMan();
        }
        BOOST_CHECK_EQUAL(wallet->LoadWallet(), DBErrors::LOAD_OK);

        LegacyScriptPubKeyMan* spk_man = wallet->GetLegacyScriptPubKeyMan();
        BOOST_REQUIRE(spk_man);
        CDilithiumKey loaded_key;
        BOOST_REQUIRE(spk_man->GetDilithiumKey(key_id, loaded_key));
        BOOST_CHECK(loaded_key == key);

        LOCK(spk_man->cs_KeyStore);
        const auto it = spk_man->mapKeyMetadata.find(key_id);
        BOOST_REQUIRE(it != spk_man->mapKeyMetadata.end());
        BOOST_CHECK_EQUAL(it->second.nCreateTime, metadata.nCreateTime);
        BOOST_CHECK_EQUAL(it->second.hdKeypath, metadata.hdKeypath);
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
