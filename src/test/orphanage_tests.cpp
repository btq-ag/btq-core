// Copyright (c) 2011-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <pubkey.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <txorphanage.h>

#include <array>
#include <cstdint>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(orphanage_tests, TestingSetup)

class TxOrphanageTest : public TxOrphanage
{
public:
    using TxOrphanage::TxOrphanage;

    inline size_t CountOrphans() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        return const_cast<TxOrphanageTest*>(this)->Size();
    }

    CTransactionRef RandomOrphan() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        LOCK(m_mutex);
        auto it = m_orphans.lower_bound(InsecureRand256());
        if (it == m_orphans.end()) it = m_orphans.begin();
        return it->second.tx;
    }
};

static void MakeNewKeyWithFastRandomContext(CKey& key)
{
    std::vector<unsigned char> keydata;
    keydata = g_insecure_rand_ctx.randbytes(32);
    key.Set(keydata.data(), keydata.data() + keydata.size(), /*fCompressedIn=*/true);
    assert(key.IsValid());
}

static CTransactionRef MakeOrphanTx(const CKey& key, const uint256& prevhash)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.n = 0;
    tx.vin[0].prevout.hash = prevhash;
    tx.vin[0].scriptSig << OP_1;
    tx.vout.resize(1);
    tx.vout[0].nValue = 1 * CENT;
    tx.vout[0].scriptPubKey = GetScriptForDestination(PKHash(key.GetPubKey()));
    return MakeTransactionRef(tx);
}

BOOST_AUTO_TEST_CASE(DoS_mapOrphans)
{
    g_insecure_rand_ctx = FastRandomContext{uint256{33}};

    TxOrphanageTest orphanage;
    CKey key;
    MakeNewKeyWithFastRandomContext(key);
    FillableSigningProvider keystore;
    BOOST_CHECK(keystore.AddKey(key));

    for (int i = 0; i < 50; i++) {
        orphanage.AddTx(MakeOrphanTx(key, InsecureRand256()), i);
    }

    for (int i = 0; i < 50; i++) {
        CTransactionRef txPrev = orphanage.RandomOrphan();

        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = txPrev->GetHash();
        tx.vout.resize(1);
        tx.vout[0].nValue = 1 * CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(PKHash(key.GetPubKey()));
        SignatureData empty;
        BOOST_CHECK(SignSignature(keystore, *txPrev, tx, 0, SIGHASH_ALL, empty));

        orphanage.AddTx(MakeTransactionRef(tx), i);
    }

    for (int i = 0; i < 10; i++) {
        CTransactionRef txPrev = orphanage.RandomOrphan();

        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1 * CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(PKHash(key.GetPubKey()));
        tx.vin.resize(2777);
        for (unsigned int j = 0; j < tx.vin.size(); j++) {
            tx.vin[j].prevout.n = j;
            tx.vin[j].prevout.hash = txPrev->GetHash();
        }
        SignatureData empty;
        BOOST_CHECK(SignSignature(keystore, *txPrev, tx, 0, SIGHASH_ALL, empty));
        for (unsigned int j = 1; j < tx.vin.size(); j++)
            tx.vin[j].scriptSig = tx.vin[0].scriptSig;

        BOOST_CHECK(!orphanage.AddTx(MakeTransactionRef(tx), i));
    }

    for (NodeId i = 0; i < 3; i++) {
        size_t sizeBefore = orphanage.CountOrphans();
        orphanage.EraseForPeer(i);
        BOOST_CHECK(orphanage.CountOrphans() < sizeBefore);
    }
    orphanage.SanityCheck();
}

BOOST_AUTO_TEST_CASE(announcer_and_weight_trim)
{
    // Tiny limits so a handful of orphans trip the weight cap.
    TxOrphanageTest orphanage(/*max_global_usage=*/2000, /*max_latency_score=*/3000, /*reserved_usage_per_peer=*/400);
    CKey key;
    MakeNewKeyWithFastRandomContext(key);
    FastRandomContext rng{uint256{1}};

    CTransactionRef first;
    for (int i = 0; i < 8; i++) {
        auto tx = MakeOrphanTx(key, InsecureRand256());
        if (!first) first = tx;
        BOOST_CHECK(orphanage.AddTx(tx, /*peer=*/0));
    }
    const size_t before = orphanage.CountOrphans();
    BOOST_CHECK(before >= 1);

    // A second peer announcing the same orphan does not create a new entry.
    BOOST_CHECK(orphanage.AddAnnouncer(first->GetWitnessHash(), /*peer=*/1));
    BOOST_CHECK(orphanage.HaveTxFromPeer(first->GetWitnessHash(), 1));
    BOOST_CHECK_EQUAL(orphanage.CountOrphans(), before);
    BOOST_CHECK(orphanage.UsageByPeer(1) > 0);

    // Disconnecting peer 1 keeps the orphan because peer 0 still announces it.
    orphanage.EraseForPeer(1);
    BOOST_CHECK(orphanage.HaveTx(GenTxid::Wtxid(first->GetWitnessHash())));
    BOOST_CHECK(!orphanage.HaveTxFromPeer(first->GetWitnessHash(), 1));

    orphanage.LimitOrphans(rng);
    BOOST_CHECK(orphanage.TotalOrphanUsage() <= 2000);
    orphanage.SanityCheck();
}

BOOST_AUTO_TEST_SUITE_END()
