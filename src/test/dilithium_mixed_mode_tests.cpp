// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <coins.h>
#include <crypto/dilithium_key.h>
#include <key.h>
#include <key_io.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/sign.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_mixed_mode_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(mixed_mode_transaction_creation)
{
    CKey ecdsa_key;
    ecdsa_key.MakeNewKey(true);
    const CPubKey ecdsa_pubkey = ecdsa_key.GetPubKey();

    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    const CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();

    const CScript ecdsa_script = GetScriptForDestination(PKHash{ecdsa_pubkey});
    const CScript dilithium_script = GetScriptForDestination(DilithiumPKHash{dilithium_pubkey});

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{uint256::ONE, 0});
    mtx.vin.emplace_back(COutPoint{uint256::ONE, 1});
    mtx.vout.emplace_back(1, ecdsa_script);
    mtx.vout.emplace_back(2, dilithium_script);

    BOOST_CHECK_EQUAL(mtx.vin.size(), 2);
    BOOST_CHECK_EQUAL(mtx.vout.size(), 2);
    BOOST_CHECK(!ecdsa_script.empty());
    BOOST_CHECK(!dilithium_script.empty());
    BOOST_CHECK(ecdsa_script != dilithium_script);
}

BOOST_AUTO_TEST_CASE(mixed_mode_raw_key_signatures_remain_distinct)
{
    CKey ecdsa_key;
    ecdsa_key.MakeNewKey(true);
    const CPubKey ecdsa_pubkey = ecdsa_key.GetPubKey();

    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    const CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();

    const uint256 test_hash = uint256::ONE;

    std::vector<unsigned char> ecdsa_sig;
    BOOST_REQUIRE(ecdsa_key.Sign(test_hash, ecdsa_sig));
    BOOST_CHECK(ecdsa_pubkey.Verify(test_hash, ecdsa_sig));

    std::vector<unsigned char> dilithium_sig;
    BOOST_REQUIRE(dilithium_key.Sign(test_hash, dilithium_sig));
    BOOST_REQUIRE_EQUAL(dilithium_sig.size(), BTQ_DILITHIUM_SIGNATURE_SIZE);
    BOOST_CHECK(dilithium_pubkey.Verify(test_hash, dilithium_sig));

    BOOST_CHECK_NE(ecdsa_sig.size(), dilithium_sig.size());
    BOOST_CHECK_GT(dilithium_sig.size(), ecdsa_sig.size());
}

BOOST_AUTO_TEST_CASE(mixed_mode_script_execution_requires_both_signature_families)
{
    CKey ecdsa_key;
    ecdsa_key.MakeNewKey(true);
    const CPubKey ecdsa_pubkey = ecdsa_key.GetPubKey();

    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    const CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();

    CMutableTransaction spending_tx;
    spending_tx.nVersion = 1;
    spending_tx.vin.emplace_back(COutPoint{uint256::ONE, 0});
    spending_tx.vout.emplace_back(1, CScript() << OP_TRUE);

    const CAmount amount{1};
    const CScript mixed_script = CScript()
        << OP_TOALTSTACK
        << ToByteVector(ecdsa_pubkey) << OP_CHECKSIG
        << OP_FROMALTSTACK
        << ToByteVector(dilithium_pubkey) << OP_CHECKSIGDILITHIUM
        << OP_BOOLAND;

    const uint256 sighash = SignatureHash(mixed_script, spending_tx, 0, SIGHASH_ALL, amount, SigVersion::BASE);

    std::vector<unsigned char> ecdsa_sig;
    BOOST_REQUIRE(ecdsa_key.Sign(sighash, ecdsa_sig));
    ecdsa_sig.push_back(SIGHASH_ALL);

    std::vector<unsigned char> dilithium_sig;
    BOOST_REQUIRE(dilithium_key.Sign(sighash, dilithium_sig));
    BOOST_REQUIRE_EQUAL(dilithium_sig.size(), BTQ_DILITHIUM_SIGNATURE_SIZE);
    dilithium_sig.push_back(SIGHASH_ALL);

    spending_tx.vin[0].scriptSig = CScript() << ecdsa_sig << dilithium_sig;
    const CTransaction ctx{spending_tx};

    ScriptError error{SCRIPT_ERR_OK};
    BOOST_CHECK(VerifyScript(
        ctx.vin[0].scriptSig,
        mixed_script,
        nullptr,
        STANDARD_SCRIPT_VERIFY_FLAGS,
        TransactionSignatureChecker(&ctx, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);

    CMutableTransaction missing_dilithium_tx{spending_tx};
    missing_dilithium_tx.vin[0].scriptSig = CScript() << ecdsa_sig << std::vector<unsigned char>{};
    const CTransaction missing_dilithium_ctx{missing_dilithium_tx};
    error = SCRIPT_ERR_OK;
    BOOST_CHECK(!VerifyScript(
        missing_dilithium_ctx.vin[0].scriptSig,
        mixed_script,
        nullptr,
        STANDARD_SCRIPT_VERIFY_FLAGS,
        TransactionSignatureChecker(&missing_dilithium_ctx, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &error));
    BOOST_CHECK_NE(error, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(mixed_mode_transaction_signing)
{
    CKey ecdsa_key;
    ecdsa_key.MakeNewKey(true);
    const CPubKey ecdsa_pubkey = ecdsa_key.GetPubKey();

    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    const CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    const DilithiumPKHash dilithium_keyid{dilithium_pubkey};

    FlatSigningProvider provider;
    provider.keys.emplace(ecdsa_pubkey.GetID(), ecdsa_key);
    provider.pubkeys.emplace(ecdsa_pubkey.GetID(), ecdsa_pubkey);
    provider.dilithium_keys.emplace(dilithium_keyid, dilithium_key);
    provider.dilithium_pubkeys.emplace(dilithium_keyid, dilithium_pubkey);

    const CScript ecdsa_script = CScript() << ToByteVector(ecdsa_pubkey) << OP_CHECKSIG;
    const CScript dilithium_script = CScript() << ToByteVector(dilithium_pubkey) << OP_CHECKSIGDILITHIUM;
    const CAmount ecdsa_amount{1};
    const CAmount dilithium_amount{2};

    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.vin.emplace_back(COutPoint{uint256::ONE, 0});
    mtx.vin.emplace_back(COutPoint{uint256::ONE, 1});
    mtx.vout.emplace_back(ecdsa_amount + dilithium_amount, CScript() << OP_TRUE);

    SignatureData ecdsa_sigdata;
    BOOST_REQUIRE(ProduceSignature(
        provider,
        MutableTransactionSignatureCreator{mtx, 0, ecdsa_amount, SIGHASH_ALL},
        ecdsa_script,
        ecdsa_sigdata));
    BOOST_REQUIRE(ecdsa_sigdata.complete);
    mtx.vin[0].scriptSig = ecdsa_sigdata.scriptSig;

    SignatureData dilithium_sigdata;
    BOOST_REQUIRE(ProduceSignature(
        provider,
        MutableTransactionSignatureCreator{mtx, 1, dilithium_amount, SIGHASH_ALL},
        dilithium_script,
        dilithium_sigdata));
    BOOST_REQUIRE(dilithium_sigdata.complete);
    BOOST_REQUIRE_EQUAL(dilithium_sigdata.dilithium_signatures.size(), 1);
    BOOST_REQUIRE_EQUAL(dilithium_sigdata.dilithium_signatures.begin()->second.second.size(), BTQ_DILITHIUM_SIGNATURE_SIZE + 1);
    mtx.vin[1].scriptSig = dilithium_sigdata.scriptSig;

    const CTransaction ctx{mtx};
    ScriptError error{SCRIPT_ERR_OK};
    BOOST_CHECK(VerifyScript(
        ctx.vin[0].scriptSig,
        ecdsa_script,
        nullptr,
        STANDARD_SCRIPT_VERIFY_FLAGS,
        TransactionSignatureChecker(&ctx, 0, ecdsa_amount, MissingDataBehavior::ASSERT_FAIL),
        &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);

    error = SCRIPT_ERR_OK;
    BOOST_CHECK(VerifyScript(
        ctx.vin[1].scriptSig,
        dilithium_script,
        nullptr,
        STANDARD_SCRIPT_VERIFY_FLAGS,
        TransactionSignatureChecker(&ctx, 1, dilithium_amount, MissingDataBehavior::ASSERT_FAIL),
        &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_SUITE_END()
