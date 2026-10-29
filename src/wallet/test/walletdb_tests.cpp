// Copyright (c) 2012-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <clientversion.h>
#include <crypto/dilithium_key.h>
#include <hash.h>
#include <outputtype.h>
#include <streams.h>
#include <uint256.h>
#include <wallet/p2mr.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <optional>

namespace wallet {
namespace {
SerializeData DilithiumKeyRecordKey(const CKeyID& id)
{
    DataStream s;
    s << DBKeys::DILITHIUM_KEY << id;
    return {s.begin(), s.end()};
}

SerializeData DilithiumKeyRecordValue(const CPrivKey& key, const std::optional<uint256>& checksum)
{
    DataStream s;
    s << key;
    if (checksum) s << *checksum;
    return {s.begin(), s.end()};
}

//! Hash(keyID || key), spelled out rather than shared with the loader.
uint256 DilithiumKeyChecksum(const CKeyID& id, const CPrivKey& key)
{
    std::vector<unsigned char> material(id.begin(), id.end());
    material.insert(material.end(), key.begin(), key.end());
    return Hash(material);
}
} // namespace

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

        CPrivKey hashed(key_id.begin(), key_id.end());
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

BOOST_AUTO_TEST_CASE(walletdb_checks_dilithium_key_checksum_and_id)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CKeyID key_id{key.GetPubKey().GetID()};
    const CPrivKey secret{key.begin(), key.end()};

    CDilithiumKey other;
    other.MakeNewKey();
    BOOST_REQUIRE(other.IsValid());
    const CKeyID other_id{other.GetPubKey().GetID()};
    const CPrivKey other_secret{other.begin(), other.end()};

    MockableData written;
    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
        LOCK(wallet->cs_wallet);
        wallet->SetupLegacyScriptPubKeyMan();
        WalletBatch batch{wallet->GetDatabase()};
        BOOST_REQUIRE(batch.WriteDilithiumKeyByID(key_id, secret, CKeyMetadata{123456789}));
        wallet->Flush();
        written = GetMockableDatabase(*wallet).m_records;
    }
    const SerializeData record_key{DilithiumKeyRecordKey(key_id)};
    BOOST_REQUIRE(written.count(record_key));
    BOOST_REQUIRE(written.at(record_key) == DilithiumKeyRecordValue(secret, DilithiumKeyChecksum(key_id, secret)));

    // Replace the written key record with one (key, value) pair and reload.
    const auto load_with = [&](const SerializeData& rec_key, const SerializeData& rec_value) {
        MockableData records{written};
        records.erase(record_key);
        records[rec_key] = rec_value;
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(records));
        {
            LOCK(wallet->cs_wallet);
            wallet->SetupLegacyScriptPubKeyMan();
        }
        const DBErrors result{wallet->LoadWallet()};
        return std::make_pair(result, wallet);
    };
    const auto has_key = [](CWallet& wallet, const CKeyID& id) {
        CDilithiumKey loaded;
        LegacyScriptPubKeyMan* spk_man = wallet.GetLegacyScriptPubKeyMan();
        return spk_man && spk_man->GetDilithiumKey(id, loaded);
    };

    {
        auto [result, wallet] = load_with(record_key, written.at(record_key));
        BOOST_CHECK_EQUAL(result, DBErrors::LOAD_OK);
        BOOST_CHECK(has_key(*wallet, key_id));
    }
    {
        auto [result, wallet] = load_with(record_key, DilithiumKeyRecordValue(secret, std::nullopt));
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, key_id));
    }
    {
        SerializeData value{written.at(record_key)};
        value.pop_back();
        auto [result, wallet] = load_with(record_key, value);
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, key_id));
    }
    {
        auto [result, wallet] = load_with(record_key, DilithiumKeyRecordValue(secret, uint256{}));
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, key_id));
    }
    {
        SerializeData value{written.at(record_key)};
        value.back() ^= std::byte{0x01};
        auto [result, wallet] = load_with(record_key, value);
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, key_id));
    }
    {
        // A matching checksum does not stand in for the key self-checks.
        CPrivKey damaged{secret};
        damaged[0] ^= 0x01;
        auto [result, wallet] = load_with(record_key, DilithiumKeyRecordValue(damaged, DilithiumKeyChecksum(key_id, damaged)));
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, key_id));
    }
    {
        // Another key's record, trailer included, moved under this id.
        auto [result, wallet] = load_with(record_key, DilithiumKeyRecordValue(other_secret, DilithiumKeyChecksum(other_id, other_secret)));
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, other_id));
    }
    {
        // A key filed under another id with a checksum that matches that id.
        auto [result, wallet] = load_with(DilithiumKeyRecordKey(other_id), DilithiumKeyRecordValue(secret, DilithiumKeyChecksum(other_id, secret)));
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, key_id));
        BOOST_CHECK(!has_key(*wallet, other_id));
    }
    {
        auto [result, wallet] = load_with(DilithiumKeyRecordKey(other_id), DilithiumKeyRecordValue(secret, std::nullopt));
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, key_id));
        BOOST_CHECK(!has_key(*wallet, other_id));
    }
    {
        // Bytes after the key id: encryption would erase a different record key.
        SerializeData longer_key{record_key};
        longer_key.push_back(std::byte{0x00});
        auto [result, wallet] = load_with(longer_key, written.at(record_key));
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, key_id));
    }
    {
        // A failed self-check leaves the all-zero public key. Filed under that
        // key's id with a matching checksum, only the self-check rejects it.
        const CKeyID cleared_id{CDilithiumPubKey{}.GetID()};
        const CPrivKey garbage(secret.size(), 0x5a);
        auto [result, wallet] = load_with(DilithiumKeyRecordKey(cleared_id), DilithiumKeyRecordValue(garbage, DilithiumKeyChecksum(cleared_id, garbage)));
        BOOST_CHECK_EQUAL(result, DBErrors::CORRUPT);
        BOOST_CHECK(!has_key(*wallet, cleared_id));
    }
}

BOOST_AUTO_TEST_CASE(walletdb_encryption_erases_plaintext_dilithium_keys)
{
    const auto key_ids = [](const MockableData& records, const std::string& type) {
        std::vector<CKeyID> ids;
        for (const auto& [rec_key, rec_value] : records) {
            DataStream s{rec_key};
            std::string rec_type;
            s >> rec_type;
            if (rec_type != type) continue;
            CKeyID id;
            s >> id;
            ids.push_back(id);
        }
        return ids;
    };
    const auto reload = [&](const MockableData& records, bool descriptors) {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(records));
        if (!descriptors) {
            LOCK(wallet->cs_wallet);
            wallet->SetupLegacyScriptPubKeyMan();
        }
        BOOST_CHECK_EQUAL(wallet->LoadWallet(), DBErrors::LOAD_OK);
        return wallet;
    };

    for (const bool descriptors : {false, true}) {
        BOOST_TEST_MESSAGE(strprintf("descriptors=%d", descriptors));
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
        wallet->LoadWallet();
        {
            LOCK(wallet->cs_wallet);
            if (descriptors) {
                wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
                wallet->SetupDescriptorScriptPubKeyMans();
            } else {
                wallet->SetupLegacyScriptPubKeyMan();
                LegacyScriptPubKeyMan* spk_man = wallet->GetLegacyScriptPubKeyMan();
                BOOST_REQUIRE(WITH_LOCK(spk_man->cs_KeyStore, return spk_man->SetupGeneration(true)));
            }
            // Both writers a user reaches: a derived receive key and an import.
            BOOST_REQUIRE(wallet->GetNewDestination(OutputType::P2MR, ""));
            CDilithiumKey imported;
            BOOST_REQUIRE(imported.MakeNewKey());
            BOOST_REQUIRE(ImportDilithiumKeyAsP2MR(*wallet, imported, "imported"));
        }
        wallet->Flush();
        const MockableData plain{GetMockableDatabase(*wallet).m_records};
        const std::vector<CKeyID> ids{key_ids(plain, DBKeys::DILITHIUM_KEY)};
        BOOST_REQUIRE_GE(ids.size(), 2U);
        reload(plain, descriptors);

        BOOST_REQUIRE(wallet->EncryptWallet("pass"));
        wallet->Flush();
        const MockableData encrypted{GetMockableDatabase(*wallet).m_records};
        BOOST_CHECK(key_ids(encrypted, DBKeys::DILITHIUM_KEY).empty());
        const std::vector<CKeyID> crypted_ids{key_ids(encrypted, DBKeys::DILITHIUM_CRYPTED_KEY)};
        for (const CKeyID& id : ids) {
            BOOST_CHECK(std::find(crypted_ids.begin(), crypted_ids.end(), id) != crypted_ids.end());
        }

        auto reloaded = reload(encrypted, descriptors);
        BOOST_CHECK(reloaded->Unlock("pass"));
        LOCK(reloaded->cs_wallet);
        for (const CKeyID& id : ids) {
            BOOST_CHECK(WalletHaveDilithiumKey(*reloaded, id));
        }
    }
}

BOOST_AUTO_TEST_CASE(walletdb_descriptor_dilithium_keys_pass_load_checks)
{
    MockableData written;
    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
        wallet->LoadWallet();
        LOCK(wallet->cs_wallet);
        wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
        wallet->SetupDescriptorScriptPubKeyMans();
        BOOST_REQUIRE(wallet->GetNewDestination(OutputType::P2MR, ""));
        BOOST_REQUIRE(wallet->GetNewChangeDestination(OutputType::P2MR));
        wallet->Flush();
        written = GetMockableDatabase(*wallet).m_records;
    }

    std::vector<SerializeData> key_records;
    for (const auto& [rec_key, rec_value] : written) {
        DataStream s{rec_key};
        std::string type;
        s >> type;
        if (type == DBKeys::DILITHIUM_KEY) key_records.push_back(rec_key);
    }
    BOOST_REQUIRE(!key_records.empty());

    const auto load = [&](const MockableData& records) {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(records));
        return wallet->LoadWallet();
    };
    // Every record the descriptor wallet wrote itself passes the new checks.
    BOOST_CHECK_EQUAL(load(written), DBErrors::LOAD_OK);
    for (const auto& rec_key : key_records) {
        MockableData tampered{written};
        tampered.at(rec_key).back() ^= std::byte{0x01};
        BOOST_CHECK_EQUAL(load(tampered), DBErrors::CORRUPT);
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
