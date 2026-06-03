// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <crypto/dilithium_key.h>
#include <key.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/interpreter.h>
#include <script/sign.h>
#include <policy/policy.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_basic_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dilithium_script_verification_flags)
{
    // Dilithium verification is gated by nDilithiumHeight in GetBlockScriptFlags,
    // not MANDATORY_SCRIPT_VERIFY_FLAGS (allows future soft-fork activation).
    BOOST_CHECK(!(MANDATORY_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_DILITHIUM));
    BOOST_CHECK(STANDARD_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_DILITHIUM);
}

BOOST_AUTO_TEST_CASE(dilithium_key_basic_operations)
{
    // Test basic Dilithium key operations
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    BOOST_CHECK(dilithium_key.IsValid());
    
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    BOOST_CHECK(dilithium_pubkey.IsValid());
    
    // Test signature creation and verification
    uint256 test_hash = uint256::ONE;
    std::vector<unsigned char> signature;
    BOOST_CHECK(dilithium_key.Sign(test_hash, signature));
    BOOST_CHECK(dilithium_pubkey.Verify(test_hash, signature));
}

BOOST_AUTO_TEST_CASE(dilithium_script_opcodes)
{
    // Test that Dilithium opcodes are defined
    BOOST_CHECK_EQUAL(OP_CHECKSIGDILITHIUM, 0xbb);
    BOOST_CHECK_EQUAL(OP_CHECKSIGDILITHIUMVERIFY, 0xbc);
}

BOOST_AUTO_TEST_CASE(dilithium_signature_sizes)
{
    // Test Dilithium signature and key sizes
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    
    // Test signature size
    uint256 test_hash = uint256::ONE;
    std::vector<unsigned char> signature;
    BOOST_CHECK(dilithium_key.Sign(test_hash, signature));
    BOOST_CHECK_GT(signature.size(), 0);
    BOOST_CHECK_LE(signature.size(), MAX_SCRIPT_ELEMENT_SIZE);
    
    // Test public key size
    std::vector<unsigned char> pubkey_bytes = dilithium_pubkey.GetAddress();
    BOOST_CHECK_GT(pubkey_bytes.size(), 0);
    BOOST_CHECK_LE(pubkey_bytes.size(), MAX_SCRIPT_ELEMENT_SIZE);
}

BOOST_AUTO_TEST_CASE(dilithium_script_limits)
{
    // Test that Dilithium scripts respect size limits
    BOOST_CHECK_GE(MAX_SCRIPT_ELEMENT_SIZE, 15000);
    BOOST_CHECK_GE(MAX_SCRIPT_SIZE, 100000);
    
    // Test that Dilithium signatures fit within limits
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    
    uint256 test_hash = uint256::ONE;
    std::vector<unsigned char> signature;
    BOOST_CHECK(dilithium_key.Sign(test_hash, signature));
    BOOST_CHECK_LE(signature.size(), MAX_SCRIPT_ELEMENT_SIZE);
}

BOOST_AUTO_TEST_CASE(dilithium_script_signature_with_sighash_byte)
{
    CDilithiumKey dilithium_key;
    dilithium_key.MakeNewKey();
    const CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
    const DilithiumPKHash key_id{dilithium_pubkey};

    FlatSigningProvider provider;
    provider.dilithium_pubkeys.emplace(key_id, dilithium_pubkey);
    provider.dilithium_keys.emplace(key_id, dilithium_key);

    CMutableTransaction spending_tx;
    spending_tx.nVersion = 1;
    spending_tx.vin.resize(1);
    spending_tx.vin[0].prevout = COutPoint{uint256::ONE, 0};
    spending_tx.vout.emplace_back(1, CScript() << OP_TRUE);

    const CAmount amount{1};

    const auto check_spend = [&](const CScript& script_pubkey, const CScript& script_sig, const ScriptError expected) {
        CMutableTransaction tx{spending_tx};
        tx.vin[0].scriptSig = script_sig;
        const CTransaction ctx{tx};

        ScriptError error{SCRIPT_ERR_OK};
        const bool verified = VerifyScript(
            ctx.vin[0].scriptSig,
            script_pubkey,
            nullptr,
            STANDARD_SCRIPT_VERIFY_FLAGS,
            TransactionSignatureChecker(&ctx, 0, amount, MissingDataBehavior::ASSERT_FAIL),
            &error);

        BOOST_CHECK_EQUAL(verified, expected == SCRIPT_ERR_OK);
        BOOST_CHECK_EQUAL(error, expected);
    };

    const auto sign_for_script = [&](const CScript& script_pubkey) {
        std::vector<unsigned char> signature;
        const uint256 sighash = SignatureHash(script_pubkey, spending_tx, 0, SIGHASH_ALL, amount, SigVersion::BASE);
        BOOST_REQUIRE(dilithium_key.Sign(sighash, signature));
        BOOST_REQUIRE_EQUAL(signature.size(), BTQ_DILITHIUM_SIGNATURE_SIZE);
        return signature;
    };

    const auto pushed_values = [](const CScript& script) {
        std::vector<std::vector<unsigned char>> pushes;
        CScript::const_iterator pc = script.begin();
        while (pc != script.end()) {
            opcodetype opcode;
            std::vector<unsigned char> data;
            BOOST_REQUIRE(script.GetOp(pc, opcode, data));
            BOOST_REQUIRE_LE(opcode, OP_PUSHDATA4);
            pushes.push_back(std::move(data));
        }
        return pushes;
    };

    const auto check_produced_signature = [&](const CScript& script_pubkey, const size_t expected_pushes) {
        MutableTransactionSignatureCreator creator{spending_tx, 0, amount, SIGHASH_ALL};
        SignatureData sigdata;
        BOOST_REQUIRE(ProduceSignature(provider, creator, script_pubkey, sigdata));
        BOOST_CHECK(sigdata.complete);
        BOOST_CHECK(!sigdata.witness);
        BOOST_REQUIRE_EQUAL(sigdata.dilithium_signatures.size(), 1);

        const std::vector<unsigned char>& signature = sigdata.dilithium_signatures.begin()->second.second;
        BOOST_REQUIRE_EQUAL(signature.size(), BTQ_DILITHIUM_SIGNATURE_SIZE + 1);
        BOOST_CHECK_EQUAL(signature.back(), SIGHASH_ALL);

        const auto pushes = pushed_values(sigdata.scriptSig);
        BOOST_REQUIRE_EQUAL(pushes.size(), expected_pushes);
        BOOST_REQUIRE_EQUAL(pushes.front().size(), BTQ_DILITHIUM_SIGNATURE_SIZE + 1);
        BOOST_CHECK_EQUAL(pushes.front().back(), SIGHASH_ALL);
        check_spend(script_pubkey, sigdata.scriptSig, SCRIPT_ERR_OK);
    };

    const CScript p2pk_script = CScript() << ToByteVector(dilithium_pubkey) << OP_CHECKSIGDILITHIUM;
    std::vector<unsigned char> p2pk_signature = sign_for_script(p2pk_script);

    CScript raw_signature_script_sig = CScript() << p2pk_signature;
    check_spend(p2pk_script, raw_signature_script_sig, SCRIPT_ERR_SIG_DER);

    p2pk_signature.push_back(SIGHASH_ALL);
    CScript p2pk_script_sig = CScript() << p2pk_signature;
    check_spend(p2pk_script, p2pk_script_sig, SCRIPT_ERR_OK);
    check_produced_signature(p2pk_script, 1);

    const CScript p2pkh_script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(dilithium_pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;
    std::vector<unsigned char> p2pkh_signature = sign_for_script(p2pkh_script);
    p2pkh_signature.push_back(SIGHASH_ALL);
    CScript p2pkh_script_sig = CScript() << p2pkh_signature << ToByteVector(dilithium_pubkey);
    check_spend(p2pkh_script, p2pkh_script_sig, SCRIPT_ERR_OK);
    check_produced_signature(p2pkh_script, 2);
}

BOOST_AUTO_TEST_CASE(dilithium_signature_data_merge_preserves_partial_state)
{
    CDilithiumKey first_key;
    first_key.MakeNewKey();
    const CDilithiumPubKey first_pubkey = first_key.GetPubKey();
    const DilithiumPKHash first_keyid{first_pubkey};

    CDilithiumKey second_key;
    second_key.MakeNewKey();
    const CDilithiumPubKey second_pubkey = second_key.GetPubKey();
    const DilithiumPKHash second_keyid{second_pubkey};

    std::vector<unsigned char> first_sig;
    BOOST_REQUIRE(first_key.Sign(uint256::ONE, first_sig));
    first_sig.push_back(SIGHASH_ALL);

    std::vector<unsigned char> second_sig;
    BOOST_REQUIRE(second_key.Sign(uint256::ONE, second_sig));
    second_sig.push_back(SIGHASH_ALL);

    SignatureData base;
    base.dilithium_signatures.emplace(first_keyid, std::make_pair(first_pubkey, first_sig));

    SignatureData incoming;
    incoming.dilithium_signatures.emplace(second_keyid, std::make_pair(second_pubkey, second_sig));
    incoming.missing_dilithium_pubkeys.push_back(first_keyid);
    incoming.missing_dilithium_sigs.push_back(second_keyid);

    base.MergeSignatureData(std::move(incoming));

    BOOST_REQUIRE_EQUAL(base.dilithium_signatures.size(), 2);
    BOOST_CHECK_EQUAL(base.dilithium_signatures.at(first_keyid).second.size(), BTQ_DILITHIUM_SIGNATURE_SIZE + 1);
    BOOST_CHECK_EQUAL(base.dilithium_signatures.at(second_keyid).second.size(), BTQ_DILITHIUM_SIGNATURE_SIZE + 1);
    BOOST_REQUIRE_EQUAL(base.missing_dilithium_pubkeys.size(), 1);
    BOOST_CHECK(base.missing_dilithium_pubkeys.front() == first_keyid);
    BOOST_REQUIRE_EQUAL(base.missing_dilithium_sigs.size(), 1);
    BOOST_CHECK(base.missing_dilithium_sigs.front() == second_keyid);
}

BOOST_AUTO_TEST_CASE(ecdsa_oversized_signature_rejected_under_standard_flags)
{
    CKey key;
    key.MakeNewKey(true);

    CMutableTransaction spending_tx;
    spending_tx.nVersion = 1;
    spending_tx.vin.resize(1);
    spending_tx.vin[0].prevout = COutPoint{uint256::ONE, 0};
    spending_tx.vout.emplace_back(1, CScript() << OP_TRUE);

    const CAmount amount{1};
    const CScript script_pubkey = CScript() << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
    spending_tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(600, 0);
    const CTransaction ctx{spending_tx};

    ScriptError error{SCRIPT_ERR_OK};
    const bool verified = VerifyScript(
        ctx.vin[0].scriptSig,
        script_pubkey,
        nullptr,
        STANDARD_SCRIPT_VERIFY_FLAGS,
        TransactionSignatureChecker(&ctx, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &error);

    BOOST_CHECK(!verified);
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_SIG_DER);
}

BOOST_AUTO_TEST_SUITE_END()
