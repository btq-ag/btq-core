// Copyright (c) 2012-2021 The Bitcoin Core developers
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
        const CPrivKey secret{key.begin(), key.end()};
        BOOST_REQUIRE(batch.WriteDilithiumKeyByID(key_id, secret, metadata));
        wallet->Flush();
        records = GetMockableDatabase(*wallet).m_records;
    }

    {
        // On-disk layout: CompactSize(len) || key bytes || Hash(key_id || key bytes)
        DataStream record_key;
        record_key << DBKeys::DILITHIUM_KEY << key_id;
        const auto it = records.find(SerializeData{record_key.begin(), record_key.end()});
        BOOST_REQUIRE(it != records.end());

        std::vector<unsigned char> hashed(key_id.begin(), key_id.end());
        hashed.insert(hashed.end(), key.begin(), key.end());
        DataStream expected;
        WriteCompactSize(expected, key.size());
        expected.write(MakeByteSpan(Span{key.begin(), key.end()}));
        expected << Hash(hashed);
        BOOST_CHECK(it->second == SerializeData(expected.begin(), expected.end()));
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

BOOST_AUTO_TEST_CASE(walletdb_checks_dilithium_key_checksum)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CKeyID key_id{key.GetPubKey().GetID()};
    CKeyMetadata metadata{123456789};
    const CPrivKey secret{key.begin(), key.end()};

    MockableData records;
    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
        LOCK(wallet->cs_wallet);
        wallet->SetupLegacyScriptPubKeyMan();
        WalletBatch batch{wallet->GetDatabase()};
        BOOST_REQUIRE(batch.WriteDilithiumKeyByID(key_id, secret, metadata));
        wallet->Flush();
        records = GetMockableDatabase(*wallet).m_records;
    }

    // Locate the dilithiumkey record so the trailing checksum can be mangled.
    const std::string type{DBKeys::DILITHIUM_KEY};
    auto record = records.end();
    for (auto it = records.begin(); it != records.end(); ++it) {
        if (it->first.size() > type.size() &&
            std::equal(type.begin(), type.end(), reinterpret_cast<const char*>(it->first.data()) + 1)) {
            record = it;
            break;
        }
    }
    BOOST_REQUIRE(record != records.end());

    {
        // Flipped checksum byte: load must fail.
        MockableData tampered{records};
        tampered.at(record->first).back() ^= std::byte{0x01};
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(tampered));
        {
            LOCK(wallet->cs_wallet);
            wallet->SetupLegacyScriptPubKeyMan();
        }
        BOOST_CHECK_EQUAL(wallet->LoadWallet(), DBErrors::CORRUPT);
    }

    {
        // Record without the trailing hash (pre-checksum wallets): still loads.
        MockableData truncated{records};
        auto& value = truncated.at(record->first);
        BOOST_REQUIRE(value.size() > 32);
        value.resize(value.size() - 32);
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(truncated));
        {
            LOCK(wallet->cs_wallet);
            wallet->SetupLegacyScriptPubKeyMan();
        }
        BOOST_CHECK_EQUAL(wallet->LoadWallet(), DBErrors::LOAD_OK);
        CDilithiumKey loaded_key;
        BOOST_REQUIRE(wallet->GetLegacyScriptPubKeyMan()->GetDilithiumKey(key_id, loaded_key));
        BOOST_CHECK(loaded_key == key);
    }
}

BOOST_AUTO_TEST_CASE(walletdb_rejects_checksumless_dilithium_key_wrong_id)
{
    // A real secret stored under a different key id, with the trailing
    // hash removed, used to load. The legacy manager ignores the record
    // id and inserts the key under the derived id. That must fail, and
    // the key must not be installed under either id. A correct-id record
    // with the hash removed still loads; see the case above.
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CKeyID real_id{key.GetPubKey().GetID()};
    const CKeyID wrong_id{uint160(std::vector<unsigned char>(20, 0x11))};
    BOOST_REQUIRE(wrong_id != real_id);
    CKeyMetadata metadata{123456789};
    const CPrivKey secret{key.begin(), key.end()};

    MockableData records;
    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
        LOCK(wallet->cs_wallet);
        wallet->SetupLegacyScriptPubKeyMan();
        WalletBatch batch{wallet->GetDatabase()};
        BOOST_REQUIRE(batch.WriteDilithiumKeyByID(wrong_id, secret, metadata));
        wallet->Flush();
        records = GetMockableDatabase(*wallet).m_records;
    }

    const std::string type{DBKeys::DILITHIUM_KEY};
    auto record = records.end();
    for (auto it = records.begin(); it != records.end(); ++it) {
        if (it->first.size() > type.size() &&
            std::equal(type.begin(), type.end(), reinterpret_cast<const char*>(it->first.data()) + 1)) {
            record = it;
            break;
        }
    }
    BOOST_REQUIRE(record != records.end());

    MockableData truncated{records};
    auto& value = truncated.at(record->first);
    BOOST_REQUIRE(value.size() > 32);
    value.resize(value.size() - 32);

    auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(truncated));
    {
        LOCK(wallet->cs_wallet);
        wallet->SetupLegacyScriptPubKeyMan();
    }
    BOOST_CHECK_EQUAL(wallet->LoadWallet(), DBErrors::CORRUPT);

    LegacyScriptPubKeyMan* spk_man = wallet->GetLegacyScriptPubKeyMan();
    BOOST_REQUIRE(spk_man);
    CDilithiumKey loaded_key;
    BOOST_CHECK(!spk_man->GetDilithiumKey(wrong_id, loaded_key));
    BOOST_CHECK(!spk_man->GetDilithiumKey(real_id, loaded_key));
}

BOOST_AUTO_TEST_CASE(walletdb_rejects_invalid_dilithium_key_record)
{
    // A record with the right length but garbage key material makes
    // CDilithiumKey::Set fail its self-checks and clear the key. Loading
    // must report corruption instead of installing an invalid key.
    const CKeyID key_id{uint160(std::vector<unsigned char>(20, 0x11))};
    CKeyMetadata metadata{123456789};
    const CPrivKey garbage(DilithiumConstants::SECRET_KEY_SIZE + DilithiumConstants::PUBLIC_KEY_SIZE, 0x5a);

    MockableData records;
    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
        LOCK(wallet->cs_wallet);
        wallet->SetupLegacyScriptPubKeyMan();
        WalletBatch batch{wallet->GetDatabase()};
        BOOST_REQUIRE(batch.WriteDilithiumKeyByID(key_id, garbage, metadata));
        wallet->Flush();
        records = GetMockableDatabase(*wallet).m_records;
    }

    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(records));
        {
            LOCK(wallet->cs_wallet);
            wallet->SetupLegacyScriptPubKeyMan();
        }
        BOOST_CHECK_EQUAL(wallet->LoadWallet(), DBErrors::CORRUPT);
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
