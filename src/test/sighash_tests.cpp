// Copyright (c) 2013-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/system.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <hash.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <test/data/sighash.json.h>
#include <test/util/json.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <version.h>

#include <iostream>

#include <boost/test/unit_test.hpp>

#include <univalue.h>

// Old script.cpp SignatureHash function
uint256 static SignatureHashOld(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType)
{
    if (nIn >= txTo.vin.size())
    {
        return uint256::ONE;
    }
    CMutableTransaction txTmp(txTo);

    // In case concatenating two scripts ends up with two codeseparators,
    // or an extra one at the end, this prevents all those possible incompatibilities.
    FindAndDelete(scriptCode, CScript(OP_CODESEPARATOR));

    // Blank out other inputs' signatures
    for (unsigned int i = 0; i < txTmp.vin.size(); i++)
        txTmp.vin[i].scriptSig = CScript();
    txTmp.vin[nIn].scriptSig = scriptCode;

    // Blank out some of the outputs
    if ((nHashType & 0x1f) == SIGHASH_NONE)
    {
        // Wildcard payee
        txTmp.vout.clear();

        // Let the others update at will
        for (unsigned int i = 0; i < txTmp.vin.size(); i++)
            if (i != nIn)
                txTmp.vin[i].nSequence = 0;
    }
    else if ((nHashType & 0x1f) == SIGHASH_SINGLE)
    {
        // Only lock-in the txout payee at same index as txin
        unsigned int nOut = nIn;
        if (nOut >= txTmp.vout.size())
        {
            return uint256::ONE;
        }
        txTmp.vout.resize(nOut+1);
        for (unsigned int i = 0; i < nOut; i++)
            txTmp.vout[i].SetNull();

        // Let the others update at will
        for (unsigned int i = 0; i < txTmp.vin.size(); i++)
            if (i != nIn)
                txTmp.vin[i].nSequence = 0;
    }

    // Blank out other inputs completely, not recommended for open transactions
    if (nHashType & SIGHASH_ANYONECANPAY)
    {
        txTmp.vin[0] = txTmp.vin[nIn];
        txTmp.vin.resize(1);
    }

    // Serialize and hash
    CHashWriter ss{SERIALIZE_TRANSACTION_NO_WITNESS};
    ss << txTmp << nHashType;
    return ss.GetHash();
}

void static RandomScript(CScript &script) {
    static const opcodetype oplist[] = {OP_FALSE, OP_1, OP_2, OP_3, OP_CHECKSIG, OP_IF, OP_VERIF, OP_RETURN, OP_CODESEPARATOR};
    script = CScript();
    int ops = (InsecureRandRange(10));
    for (int i=0; i<ops; i++)
        script << oplist[InsecureRandRange(std::size(oplist))];
}

void static RandomTransaction(CMutableTransaction& tx, bool fSingle)
{
    tx.nVersion = int(InsecureRand32());
    tx.vin.clear();
    tx.vout.clear();
    tx.nLockTime = (InsecureRandBool()) ? InsecureRand32() : 0;
    int ins = (InsecureRandBits(2)) + 1;
    int outs = fSingle ? ins : (InsecureRandBits(2)) + 1;
    for (int in = 0; in < ins; in++) {
        tx.vin.emplace_back();
        CTxIn &txin = tx.vin.back();
        txin.prevout.hash = InsecureRand256();
        txin.prevout.n = InsecureRandBits(2);
        RandomScript(txin.scriptSig);
        txin.nSequence = (InsecureRandBool()) ? InsecureRand32() : std::numeric_limits<uint32_t>::max();
    }
    for (int out = 0; out < outs; out++) {
        tx.vout.emplace_back();
        CTxOut &txout = tx.vout.back();
        txout.nValue = InsecureRandMoneyAmount();
        RandomScript(txout.scriptPubKey);
    }
}

BOOST_FIXTURE_TEST_SUITE(sighash_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(sighash_test)
{
    #if defined(PRINT_SIGHASH_JSON)
    std::cout << "[\n";
    std::cout << "\t[\"raw_transaction, script, input_index, hashType, signature_hash (result)\"],\n";
    int nRandomTests = 500;
    #else
    int nRandomTests = 50000;
    #endif
    for (int i=0; i<nRandomTests; i++) {
        int nHashType{int(InsecureRand32())};
        CMutableTransaction txTo;
        RandomTransaction(txTo, (nHashType & 0x1f) == SIGHASH_SINGLE);
        CScript scriptCode;
        RandomScript(scriptCode);
        int nIn = InsecureRandRange(txTo.vin.size());

        uint256 sh, sho;
        sho = SignatureHashOld(scriptCode, CTransaction(txTo), nIn, nHashType);
        sh = SignatureHash(scriptCode, txTo, nIn, nHashType, 0, SigVersion::BASE);
        #if defined(PRINT_SIGHASH_JSON)
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << txTo;

        std::cout << "\t[\"" ;
        std::cout << HexStr(ss) << "\", \"";
        std::cout << HexStr(scriptCode) << "\", ";
        std::cout << nIn << ", ";
        std::cout << nHashType << ", \"";
        std::cout << sho.GetHex() << "\"]";
        if (i+1 != nRandomTests) {
          std::cout << ",";
        }
        std::cout << "\n";
        #endif
        BOOST_CHECK(sh == sho);
    }
    #if defined(PRINT_SIGHASH_JSON)
    std::cout << "]\n";
    #endif
}

// Goal: check that SignatureHash generates correct hash
BOOST_AUTO_TEST_CASE(sighash_from_data)
{
    UniValue tests = read_json(json_tests::sighash);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 1) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        if (test.size() == 1) continue; // comment

        std::string raw_tx, raw_script, sigHashHex;
        int nIn, nHashType;
        uint256 sh;
        CTransactionRef tx;
        CScript scriptCode = CScript();

        try {
          // deserialize test data
          raw_tx = test[0].get_str();
          raw_script = test[1].get_str();
          nIn = test[2].getInt<int>();
          nHashType = test[3].getInt<int>();
          sigHashHex = test[4].get_str();

          CDataStream stream(ParseHex(raw_tx), SER_NETWORK, PROTOCOL_VERSION);
          stream >> tx;

          TxValidationState state;
          BOOST_CHECK_MESSAGE(CheckTransaction(*tx, state), strTest);
          BOOST_CHECK(state.IsValid());

          std::vector<unsigned char> raw = ParseHex(raw_script);
          scriptCode.insert(scriptCode.end(), raw.begin(), raw.end());
        } catch (...) {
          BOOST_ERROR("Bad test, couldn't deserialize data: " << strTest);
          continue;
        }

        sh = SignatureHash(scriptCode, *tx, nIn, nHashType, 0, SigVersion::BASE);
        BOOST_CHECK_MESSAGE(sh.GetHex() == sigHashHex, strTest);
    }
}

//! The cache must key on everything the midstate depends on. Upstream omits the
//! sig version, on the grounds that no input can reach both the BASE and the
//! WITNESS_V0 path; that does not hold here, because CheckDilithiumSignature
//! dispatches on sigversion within a single input, so we key on it too. A cache
//! that ignored it would hand a BASE midstate to a WITNESS_V0 signature and
//! validate a signature over the wrong message.
BOOST_AUTO_TEST_CASE(sighash_cache_keying)
{
    const CScript script_a = CScript() << OP_1;
    const CScript script_b = CScript() << OP_2;

    HashWriter stored{};
    stored << uint256::ONE;
    const uint256 expected{HashWriter{stored}.GetHash()};

    SigHashCache cache;
    cache.Store(SIGHASH_ALL, SigVersion::BASE, script_a, stored);

    HashWriter out{};
    BOOST_CHECK(cache.Load(SIGHASH_ALL, SigVersion::BASE, script_a, out));
    BOOST_CHECK_EQUAL(out.GetHash().GetHex(), expected.GetHex());

    // The midstate stops short of the type byte, so any type in the same mode is
    // a hit. That is the whole point: one entry serves all 256 of them.
    out = HashWriter{};
    BOOST_CHECK(cache.Load(SIGHASH_ALL | 0x40, SigVersion::BASE, script_a, out));
    BOOST_CHECK_EQUAL(out.GetHash().GetHex(), expected.GetHex());

    // Anything else must miss.
    BOOST_CHECK(!cache.Load(SIGHASH_ALL, SigVersion::BASE, script_b, out));
    BOOST_CHECK(!cache.Load(SIGHASH_ALL, SigVersion::WITNESS_V0, script_a, out));
    BOOST_CHECK(!cache.Load(SIGHASH_NONE, SigVersion::BASE, script_a, out));
    BOOST_CHECK(!cache.Load(SIGHASH_SINGLE, SigVersion::BASE, script_a, out));
    BOOST_CHECK(!cache.Load(SIGHASH_ALL | SIGHASH_ANYONECANPAY, SigVersion::BASE, script_a, out));
}

//! A cached run must produce byte-identical sighashes to an uncached one.
BOOST_AUTO_TEST_CASE(sighash_caching)
{
    for (int i = 0; i < 300; i++) {
        CMutableTransaction tx;
        RandomTransaction(tx, /*fSingle=*/InsecureRandBool());
        const unsigned int nIn = InsecureRandRange(tx.vin.size());
        const CAmount amount{InsecureRandMoneyAmount()};

        std::vector<CScript> script_codes(3);
        for (auto& script_code : script_codes) RandomScript(script_code);

        // One cache per input, matching the lifetime the checker gives it.
        SigHashCache cache;

        auto check = [&](const CScript& script_code, int nHashType, SigVersion sigversion) {
            const uint256 uncached{SignatureHash(script_code, tx, nIn, nHashType, amount, sigversion, nullptr, nullptr)};
            const uint256 cached{SignatureHash(script_code, tx, nIn, nHashType, amount, sigversion, nullptr, &cache)};
            BOOST_CHECK_EQUAL(uncached.GetHex(), cached.GetHex());
        };

        for (const SigVersion sigversion : {SigVersion::BASE, SigVersion::WITNESS_V0}) {
            // Walk every mode with a fixed script code, varying only the bits
            // that do not select the mode. Every call after the first in each
            // group is a guaranteed hit, so this exercises reuse rather than
            // just repeatedly missing.
            for (const int base : {int{SIGHASH_ALL}, int{SIGHASH_NONE}, int{SIGHASH_SINGLE}}) {
                for (const int acp : {0, int{SIGHASH_ANYONECANPAY}}) {
                    for (const int spare : {0x00, 0x20, 0x40, 0x60}) {
                        check(script_codes[0], base | acp | spare, sigversion);
                    }
                }
            }
        }

        // Now churn the key so entries are evicted and refilled out of order.
        for (int round = 0; round < 20; round++) {
            const SigVersion sigversion{InsecureRandBool() ? SigVersion::BASE : SigVersion::WITNESS_V0};
            check(script_codes[InsecureRandRange(script_codes.size())], int(InsecureRand32()), sigversion);
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()
