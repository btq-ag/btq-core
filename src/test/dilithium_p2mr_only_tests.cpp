// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/tx_verify.h>
#include <crypto/dilithium_key.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/sign.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

#include <limits>

BOOST_FIXTURE_TEST_SUITE(dilithium_p2mr_only_tests, BasicTestingSetup)

static uint32_t FlagsPreActivation()
{
    // Historical consensus path: Dilithium enabled, P2MR-only restriction not yet active.
    return (STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_DILITHIUM) & ~SCRIPT_VERIFY_DILITHIUM_P2MR_ONLY;
}

static uint32_t FlagsPostActivation()
{
    return STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_DILITHIUM | SCRIPT_VERIFY_DILITHIUM_P2MR_ONLY;
}

BOOST_AUTO_TEST_CASE(standard_flags_include_p2mr_only_policy)
{
    BOOST_CHECK(STANDARD_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_DILITHIUM_P2MR_ONLY);
    BOOST_CHECK(!(MANDATORY_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_DILITHIUM_P2MR_ONLY));
    BOOST_CHECK_EQUAL(SCRIPT_VERIFY_DILITHIUM_P2MR_ONLY, (1U << 23));
}

BOOST_AUTO_TEST_CASE(chainparams_dilithium_p2mr_heights)
{
    BOOST_CHECK_EQUAL(CreateChainParams(*m_node.args, ChainType::BTQMAIN)->GetConsensus().nDilithiumP2MRHeight, 1);
    BOOST_CHECK_EQUAL(CreateChainParams(*m_node.args, ChainType::BTQREGTEST)->GetConsensus().nDilithiumP2MRHeight, 1);
    BOOST_CHECK_EQUAL(CreateChainParams(*m_node.args, ChainType::BTQSIGNET)->GetConsensus().nDilithiumP2MRHeight, 1);
    BOOST_CHECK_EQUAL(CreateChainParams(*m_node.args, ChainType::BTQTEST)->GetConsensus().nDilithiumP2MRHeight,
                      std::numeric_limits<int>::max());
}

BOOST_AUTO_TEST_CASE(dilithium_opcodes_rejected_outside_p2mr_when_flag_set)
{
    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    const CDilithiumPubKey pubkey = key.GetPubKey();

    CMutableTransaction spending_tx;
    spending_tx.nVersion = 1;
    spending_tx.vin.resize(1);
    spending_tx.vin[0].prevout = COutPoint{uint256::ONE, 0};
    spending_tx.vout.emplace_back(1, CScript() << OP_TRUE);
    const CAmount amount{1};

    const CScript script_pubkey = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    const uint256 sighash = SignatureHash(script_pubkey, spending_tx, 0, SIGHASH_ALL, amount, SigVersion::BASE);
    std::vector<unsigned char> signature;
    BOOST_REQUIRE(key.Sign(sighash, signature));
    signature.push_back(SIGHASH_ALL);
    spending_tx.vin[0].scriptSig = CScript() << signature;
    const CTransaction ctx{spending_tx};

    ScriptError err{SCRIPT_ERR_OK};
    BOOST_CHECK(VerifyScript(
        ctx.vin[0].scriptSig, script_pubkey, nullptr, FlagsPreActivation(),
        TransactionSignatureChecker(&ctx, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);

    err = SCRIPT_ERR_OK;
    BOOST_CHECK(!VerifyScript(
        ctx.vin[0].scriptSig, script_pubkey, nullptr, FlagsPostActivation(),
        TransactionSignatureChecker(&ctx, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_TAPSCRIPT_DILITHIUM);

    // P2SH-wrapped legacy Dilithium redeem script is also rejected post-activation.
    const CScript redeem = script_pubkey;
    const CScript p2sh = GetScriptForDestination(ScriptHash(redeem));
    CMutableTransaction p2sh_tx{spending_tx};
    p2sh_tx.vin[0].scriptSig = CScript() << signature << std::vector<unsigned char>(redeem.begin(), redeem.end());
    const CTransaction p2sh_ctx{p2sh_tx};
    err = SCRIPT_ERR_OK;
    BOOST_CHECK(!VerifyScript(
        p2sh_ctx.vin[0].scriptSig, p2sh, nullptr, FlagsPostActivation(),
        TransactionSignatureChecker(&p2sh_ctx, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_TAPSCRIPT_DILITHIUM);
}

BOOST_AUTO_TEST_CASE(witness_v0_1312_pubkey_dilithium_path_gated)
{
    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    const CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE_EQUAL(pubkey.size(), DilithiumConstants::PUBLIC_KEY_SIZE);

    const DilithiumWitnessV0KeyHash wpkh{pubkey};
    const CScript script_pubkey = GetScriptForDestination(wpkh);
    // VerifyWitnessProgram rewrites v0 keyhash spends to this template for sighash / execution.
    const CScript exec_script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(wpkh)
                                          << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;

    CMutableTransaction spending_tx;
    spending_tx.nVersion = 1;
    spending_tx.vin.resize(1);
    spending_tx.vin[0].prevout = COutPoint{uint256::ONE, 0};
    spending_tx.vout.emplace_back(1, CScript() << OP_TRUE);
    const CAmount amount{1};

    const uint256 sighash = SignatureHash(exec_script, spending_tx, 0, SIGHASH_ALL, amount, SigVersion::WITNESS_V0);
    std::vector<unsigned char> signature;
    BOOST_REQUIRE(key.Sign(sighash, signature));
    signature.push_back(SIGHASH_ALL);

    CScriptWitness witness;
    witness.stack.push_back(signature);
    witness.stack.emplace_back(pubkey.begin(), pubkey.end());
    spending_tx.vin[0].scriptWitness = witness;
    const CTransaction ctx{spending_tx};

    ScriptError err{SCRIPT_ERR_OK};
    BOOST_CHECK(VerifyScript(
        CScript(), script_pubkey, &ctx.vin[0].scriptWitness, FlagsPreActivation(),
        TransactionSignatureChecker(&ctx, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);

    // Post-activation: size==1312 heuristic disabled; falls through to ECDSA/pubkeytype path.
    err = SCRIPT_ERR_OK;
    BOOST_CHECK(!VerifyScript(
        CScript(), script_pubkey, &ctx.vin[0].scriptWitness, FlagsPostActivation(),
        TransactionSignatureChecker(&ctx, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err));
    BOOST_CHECK(err == SCRIPT_ERR_WITNESS_PUBKEYTYPE || err == SCRIPT_ERR_EQUALVERIFY || err == SCRIPT_ERR_PUBKEYTYPE);
}

BOOST_AUTO_TEST_CASE(witness_v0_dilithium_sigops_gated_by_p2mr_only)
{
    CMutableTransaction creationTx;
    CMutableTransaction spendingTx;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);

    CDilithiumKey dilithium_key;
    BOOST_REQUIRE(dilithium_key.MakeNewKey());
    const CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();

    const CScript scriptPubKey = GetScriptForDestination(DilithiumWitnessV0KeyHash(dilithium_pubkey));
    CScriptWitness scriptWitness;
    scriptWitness.stack.push_back(std::vector<unsigned char>(DilithiumConstants::SIGNATURE_SIZE, 0x01));
    scriptWitness.stack.emplace_back(dilithium_pubkey.begin(), dilithium_pubkey.end());

    creationTx.nVersion = 1;
    creationTx.vin.resize(1);
    creationTx.vin[0].prevout.SetNull();
    creationTx.vout.resize(1);
    creationTx.vout[0].nValue = 1;
    creationTx.vout[0].scriptPubKey = scriptPubKey;

    spendingTx.nVersion = 1;
    spendingTx.vin.resize(1);
    spendingTx.vin[0].prevout.hash = creationTx.GetHash();
    spendingTx.vin[0].prevout.n = 0;
    spendingTx.vin[0].scriptWitness = scriptWitness;
    spendingTx.vout.resize(1);
    spendingTx.vout[0].nValue = 1;
    AddCoins(coins, CTransaction(creationTx), 0);

    const uint32_t base_flags = SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH;
    // Pre-activation (testnet today): legacy Dilithium v0 spends remain consensus-valid and
    // must keep DILITHIUM_SIGOP_COST weighting, otherwise ConnectBlock sigop accounting forks.
    BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, base_flags),
                      static_cast<int64_t>(DILITHIUM_SIGOP_COST));
    // Post-activation / STANDARD policy: v0 Dilithium path is disabled; count as P2WPKH (=1).
    BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins,
                                              base_flags | SCRIPT_VERIFY_DILITHIUM_P2MR_ONLY),
                      1);
}

BOOST_AUTO_TEST_CASE(legacy_dilithium_destinations_not_valid_payments)
{
    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    const CDilithiumPubKey pubkey = key.GetPubKey();

    BOOST_CHECK(!IsValidDestination(DilithiumPKHash{pubkey}));
    BOOST_CHECK(!IsValidDestination(DilithiumWitnessV0KeyHash{pubkey}));
    BOOST_CHECK(!IsValidDestination(DilithiumPubKeyDestination{pubkey}));
    BOOST_CHECK(IsValidDestination(WitnessV2P2MR{uint256::ONE}));
}

BOOST_AUTO_TEST_SUITE_END()
