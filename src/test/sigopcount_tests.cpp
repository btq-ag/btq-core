// Copyright (c) 2012-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <crypto/dilithium_key.h>
#include <key.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/solver.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <vector>

#include <boost/test/unit_test.hpp>

// Helpers:
static std::vector<unsigned char>
Serialize(const CScript& s)
{
    std::vector<unsigned char> sSerialized(s.begin(), s.end());
    return sSerialized;
}

BOOST_FIXTURE_TEST_SUITE(sigopcount_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(GetSigOpCount)
{
    // Test CScript::GetSigOpCount()
    CScript s1;
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(false), 0U);
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(true), 0U);

    uint160 dummy;
    s1 << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << OP_2 << OP_CHECKMULTISIG;
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(true), 2U);
    s1 << OP_IF << OP_CHECKSIG << OP_ENDIF;
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(true), 3U);
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(false), 21U);

    CScript p2sh = GetScriptForDestination(ScriptHash(s1));
    CScript scriptSig;
    scriptSig << OP_0 << Serialize(s1);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(scriptSig), 3U);

    std::vector<CPubKey> keys;
    for (int i = 0; i < 3; i++)
    {
        CKey k;
        k.MakeNewKey(true);
        keys.push_back(k.GetPubKey());
    }
    CScript s2 = GetScriptForMultisig(1, keys);
    BOOST_CHECK_EQUAL(s2.GetSigOpCount(true), 3U);
    BOOST_CHECK_EQUAL(s2.GetSigOpCount(false), 20U);

    p2sh = GetScriptForDestination(ScriptHash(s2));
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(true), 0U);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(false), 0U);
    CScript scriptSig2;
    scriptSig2 << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << Serialize(s2);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(scriptSig2), 3U);
}

BOOST_AUTO_TEST_CASE(dilithium_sigop_weighting)
{
    // BTQ-AUDIT-019: Dilithium opcodes must cost more than ECDSA in GetSigOpCount.
    CScript dilithium_p2pk;
    CDilithiumKey dilithium_key;
    BOOST_REQUIRE(dilithium_key.MakeNewKey());
    dilithium_p2pk << ToByteVector(dilithium_key.GetPubKey()) << OP_CHECKSIGDILITHIUM;
    BOOST_CHECK_EQUAL(dilithium_p2pk.GetSigOpCount(true), DILITHIUM_SIGOP_COST);

    CScript dilithium_multisig;
    dilithium_multisig << OP_2 << ToByteVector(dilithium_key.GetPubKey())
                       << ToByteVector(dilithium_key.GetPubKey()) << OP_2
                       << OP_CHECKMULTISIGDILITHIUM;
    BOOST_CHECK_EQUAL(dilithium_multisig.GetSigOpCount(true), 2U * DILITHIUM_SIGOP_COST);

    // P2SH-wrapped Dilithium: GetSigOpCount(scriptSig) must recurse into the
    // redeem script and weight the hidden OP_CHECKSIGDILITHIUM accordingly.
    const CScript& dilithium_redeem = dilithium_p2pk;
    const CScript p2sh = CScript() << OP_HASH160 << ToByteVector(CScriptID(dilithium_redeem)) << OP_EQUAL;
    const CScript p2sh_scriptsig = CScript() << Serialize(dilithium_redeem);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(p2sh_scriptsig), DILITHIUM_SIGOP_COST);
    // The P2SH scriptPubKey on its own hides the redeem script, so it must report 0.
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(true), 0U);
}

/**
 * Verifies script execution of the zeroth scriptPubKey of tx output and
 * zeroth scriptSig and witness of tx input.
 */
static ScriptError VerifyWithFlag(const CTransaction& output, const CMutableTransaction& input, uint32_t flags)
{
    ScriptError error;
    CTransaction inputi(input);
    bool ret = VerifyScript(inputi.vin[0].scriptSig, output.vout[0].scriptPubKey, &inputi.vin[0].scriptWitness, flags, TransactionSignatureChecker(&inputi, 0, output.vout[0].nValue, MissingDataBehavior::ASSERT_FAIL), &error);
    BOOST_CHECK((ret == true) == (error == SCRIPT_ERR_OK));

    return error;
}

/**
 * Builds a creationTx from scriptPubKey and a spendingTx from scriptSig
 * and witness such that spendingTx spends output zero of creationTx.
 * Also inserts creationTx's output into the coins view.
 */
static void BuildTxs(CMutableTransaction& spendingTx, CCoinsViewCache& coins, CMutableTransaction& creationTx, const CScript& scriptPubKey, const CScript& scriptSig, const CScriptWitness& witness)
{
    creationTx.nVersion = 1;
    creationTx.vin.resize(1);
    creationTx.vin[0].prevout.SetNull();
    creationTx.vin[0].scriptSig = CScript();
    creationTx.vout.resize(1);
    creationTx.vout[0].nValue = 1;
    creationTx.vout[0].scriptPubKey = scriptPubKey;

    spendingTx.nVersion = 1;
    spendingTx.vin.resize(1);
    spendingTx.vin[0].prevout.hash = creationTx.GetHash();
    spendingTx.vin[0].prevout.n = 0;
    spendingTx.vin[0].scriptSig = scriptSig;
    spendingTx.vin[0].scriptWitness = witness;
    spendingTx.vout.resize(1);
    spendingTx.vout[0].nValue = 1;
    spendingTx.vout[0].scriptPubKey = CScript();

    AddCoins(coins, CTransaction(creationTx), 0);
}

BOOST_AUTO_TEST_CASE(GetTxSigOpCost)
{
    // Transaction creates outputs
    CMutableTransaction creationTx;
    // Transaction that spends outputs and whose
    // sig op cost is going to be tested
    CMutableTransaction spendingTx;

    // Create utxo set
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    // Create key
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    // Default flags
    const uint32_t flags{SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH};

    // Multisig script (legacy counting)
    {
        CScript scriptPubKey = CScript() << 1 << ToByteVector(pubkey) << ToByteVector(pubkey) << 2 << OP_CHECKMULTISIGVERIFY;
        // Do not use a valid signature to avoid using wallet operations.
        CScript scriptSig = CScript() << OP_0 << OP_0;

        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, CScriptWitness());
        // Legacy counting only includes signature operations in scriptSigs and scriptPubKeys
        // of a transaction and does not take the actual executed sig operations into account.
        // spendingTx in itself does not contain a signature operation.
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags) == 0);
        // creationTx contains two signature operations in its scriptPubKey, but legacy counting
        // is not accurate.
        assert(GetTransactionSigOpCost(CTransaction(creationTx), coins, flags) == MAX_PUBKEYS_PER_MULTISIG * WITNESS_SCALE_FACTOR);
        // Sanity check: script verification fails because of an invalid signature.
        assert(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) == SCRIPT_ERR_CHECKMULTISIGVERIFY);
    }

    // Multisig nested in P2SH
    {
        CScript redeemScript = CScript() << 1 << ToByteVector(pubkey) << ToByteVector(pubkey) << 2 << OP_CHECKMULTISIGVERIFY;
        CScript scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        CScript scriptSig = CScript() << OP_0 << OP_0 << ToByteVector(redeemScript);

        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, CScriptWitness());
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags) == 2 * WITNESS_SCALE_FACTOR);
        assert(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) == SCRIPT_ERR_CHECKMULTISIGVERIFY);
    }

    // P2WPKH witness program
    {
        CScript scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(pubkey));
        CScript scriptSig = CScript();
        CScriptWitness scriptWitness;
        scriptWitness.stack.emplace_back(0);
        scriptWitness.stack.emplace_back(CPubKey::COMPRESSED_SIZE, 0);


        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, scriptWitness);
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags) == 1);
        // No signature operations if we don't verify the witness.
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags & ~SCRIPT_VERIFY_WITNESS) == 0);
        assert(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) == SCRIPT_ERR_EQUALVERIFY);

        // The sig op cost for witness version != 0 is zero.
        assert(scriptPubKey[0] == 0x00);
        scriptPubKey[0] = 0x51;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, scriptWitness);
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags) == 0);
        scriptPubKey[0] = 0x00;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, scriptWitness);

        // The witness of a coinbase transaction is not taken into account.
        spendingTx.vin[0].prevout.SetNull();
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags) == 0);
    }

    // P2WPKH nested in P2SH
    {
        CScript scriptSig = GetScriptForDestination(WitnessV0KeyHash(pubkey));
        CScript scriptPubKey = GetScriptForDestination(ScriptHash(scriptSig));
        scriptSig = CScript() << ToByteVector(scriptSig);
        CScriptWitness scriptWitness;
        scriptWitness.stack.emplace_back(0);
        scriptWitness.stack.emplace_back(CPubKey::COMPRESSED_SIZE, 0);

        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, scriptWitness);
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags) == 1);
        assert(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) == SCRIPT_ERR_EQUALVERIFY);
    }

    // P2WSH witness program
    {
        CScript witnessScript = CScript() << 1 << ToByteVector(pubkey) << ToByteVector(pubkey) << 2 << OP_CHECKMULTISIGVERIFY;
        CScript scriptPubKey = GetScriptForDestination(WitnessV0ScriptHash(witnessScript));
        CScript scriptSig = CScript();
        CScriptWitness scriptWitness;
        scriptWitness.stack.emplace_back(0);
        scriptWitness.stack.emplace_back(CPubKey::COMPRESSED_SIZE, 0);
        scriptWitness.stack.emplace_back(witnessScript.begin(), witnessScript.end());

        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, scriptWitness);
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags) == 2);
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags & ~SCRIPT_VERIFY_WITNESS) == 0);
        assert(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) == SCRIPT_ERR_CHECKMULTISIGVERIFY);
    }

    // P2WSH nested in P2SH
    {
        CScript witnessScript = CScript() << 1 << ToByteVector(pubkey) << ToByteVector(pubkey) << 2 << OP_CHECKMULTISIGVERIFY;
        CScript redeemScript = GetScriptForDestination(WitnessV0ScriptHash(witnessScript));
        CScript scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        CScript scriptSig = CScript() << ToByteVector(redeemScript);
        CScriptWitness scriptWitness;
        scriptWitness.stack.emplace_back(0);
        scriptWitness.stack.emplace_back(CPubKey::COMPRESSED_SIZE, 0);
        scriptWitness.stack.emplace_back(witnessScript.begin(), witnessScript.end());

        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, scriptWitness);
        assert(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags) == 2);
        assert(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) == SCRIPT_ERR_CHECKMULTISIGVERIFY);
    }
}

BOOST_AUTO_TEST_CASE(dilithium_witness_v0_sigop_weighting)
{
    // Dilithium-sized v0 keyhash witnesses keep historical DILITHIUM_SIGOP_COST
    // until SCRIPT_VERIFY_DILITHIUM_P2MR_ONLY activates (then count as P2WPKH=1).
    CMutableTransaction creationTx;
    CMutableTransaction spendingTx;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    const uint32_t flags{SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH};

    CDilithiumKey dilithium_key;
    BOOST_REQUIRE(dilithium_key.MakeNewKey());
    const CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();

    CScript scriptPubKey = GetScriptForDestination(DilithiumWitnessV0KeyHash(dilithium_pubkey));
    CScriptWitness scriptWitness;
    scriptWitness.stack.push_back(std::vector<unsigned char>(DilithiumConstants::SIGNATURE_SIZE, 0x01));
    scriptWitness.stack.push_back(std::vector<unsigned char>(dilithium_pubkey.begin(), dilithium_pubkey.end()));

    BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, scriptWitness);
    BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags),
                      static_cast<int64_t>(DILITHIUM_SIGOP_COST));
    BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins,
                                              flags | SCRIPT_VERIFY_DILITHIUM_P2MR_ONLY),
                      1);

    // Classical P2WPKH with compressed pubkey witness must remain weight 1.
    CKey ecdsa_key;
    ecdsa_key.MakeNewKey(true);
    scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(ecdsa_key.GetPubKey()));
    scriptWitness.stack.clear();
    scriptWitness.stack.push_back(std::vector<unsigned char>(72, 0x01));
    scriptWitness.stack.push_back(std::vector<unsigned char>(ecdsa_key.GetPubKey().begin(), ecdsa_key.GetPubKey().end()));

    BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, scriptWitness);
    BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags), 1);
}

/** Dummy P2MR scriptPubKey: WitnessSigOps only checks version + 32-byte program. */
static CScript MakeP2MRScriptPubKey()
{
    return GetScriptForDestination(WitnessV2P2MR(uint256{}));
}

/**
 * Build a P2MR-shaped witness: [args...] [leaf_script] [control] [optional annex].
 * Control/merkle validity is irrelevant for sigop counting.
 */
static CScriptWitness MakeP2MRWitness(const CScript& leaf_script,
                                      const std::vector<std::vector<unsigned char>>& args = {},
                                      bool with_annex = false)
{
    CScriptWitness witness;
    for (const auto& arg : args) {
        witness.stack.push_back(arg);
    }
    witness.stack.emplace_back(leaf_script.begin(), leaf_script.end());
    // P2MR control base: leaf version | parity bit 1 (required by VerifyWitnessProgram).
    witness.stack.push_back(std::vector<unsigned char>{static_cast<unsigned char>(TAPROOT_LEAF_TAPSCRIPT | 0x01)});
    if (with_annex) {
        witness.stack.push_back(std::vector<unsigned char>{static_cast<unsigned char>(ANNEX_TAG), 0x00});
    }
    return witness;
}

BOOST_AUTO_TEST_CASE(p2mr_witness_v2_sigop_weighting)
{
    // P2MR Dilithium leaves must count toward MAX_BLOCK_SIGOPS_COST via WitnessSigOps.
    CMutableTransaction creationTx;
    CMutableTransaction spendingTx;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    const uint32_t flags{SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH};
    const CScript scriptPubKey = MakeP2MRScriptPubKey();

    CDilithiumKey dilithium_key;
    BOOST_REQUIRE(dilithium_key.MakeNewKey());
    const auto dilithium_pubkey = ToByteVector(dilithium_key.GetPubKey());

    // Single-sig Dilithium leaf.
    {
        const CScript leaf = CScript() << dilithium_pubkey << OP_CHECKSIGDILITHIUM;
        const CScriptWitness witness = MakeP2MRWitness(leaf, {/*args=*/{std::vector<unsigned char>{0x01}}});
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, witness);
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags),
                          static_cast<int64_t>(DILITHIUM_SIGOP_COST));
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags & ~SCRIPT_VERIFY_WITNESS), 0);
    }

    // OP_CHECKSIGDILITHIUMVERIFY counts the same as OP_CHECKSIGDILITHIUM.
    {
        const CScript leaf = CScript() << dilithium_pubkey << OP_CHECKSIGDILITHIUMVERIFY;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, MakeP2MRWitness(leaf));
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags),
                          static_cast<int64_t>(DILITHIUM_SIGOP_COST));
    }

    // Accurate Dilithium multisig: OP_2 ... OP_2 OP_CHECKMULTISIGDILITHIUM → 2 * cost.
    {
        const CScript leaf = CScript() << OP_2 << dilithium_pubkey << dilithium_pubkey << OP_2
                                       << OP_CHECKMULTISIGDILITHIUM;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, MakeP2MRWitness(leaf));
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags),
                          static_cast<int64_t>(2U * DILITHIUM_SIGOP_COST));
        BOOST_CHECK_EQUAL(leaf.GetSigOpCount(true), 2U * DILITHIUM_SIGOP_COST);
        // Inaccurate path would charge MAX_PUBKEYS_PER_MULTISIG * cost — must not match that.
        BOOST_CHECK(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags) !=
                    static_cast<int64_t>(MAX_PUBKEYS_PER_MULTISIG * DILITHIUM_SIGOP_COST));
    }

    // Annex present: still locate leaf at control_idx - 1 and count the same.
    {
        const CScript leaf = CScript() << dilithium_pubkey << OP_CHECKSIGDILITHIUM;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{},
                 MakeP2MRWitness(leaf, {/*args=*/{std::vector<unsigned char>{0x01}}}, /*with_annex=*/true));
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags),
                          static_cast<int64_t>(DILITHIUM_SIGOP_COST));
    }

    // ECDSA OP_CHECKSIG in a P2MR leaf counts via GetSigOpCount (1), matching P2WSH.
    {
        CKey ecdsa_key;
        ecdsa_key.MakeNewKey(true);
        const CScript leaf = CScript() << ToByteVector(ecdsa_key.GetPubKey()) << OP_CHECKSIG;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, MakeP2MRWitness(leaf));
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags), 1);
    }

    // Mixed leaf: one Dilithium + one ECDSA.
    {
        CKey ecdsa_key;
        ecdsa_key.MakeNewKey(true);
        const CScript leaf = CScript() << dilithium_pubkey << OP_CHECKSIGDILITHIUM
                                       << ToByteVector(ecdsa_key.GetPubKey()) << OP_CHECKSIG;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, MakeP2MRWitness(leaf));
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags),
                          static_cast<int64_t>(DILITHIUM_SIGOP_COST + 1));
    }

    // Too few witness elements: no script+control pair → 0.
    {
        CScriptWitness witness;
        witness.stack.push_back(std::vector<unsigned char>{0x01});
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, witness);
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags), 0);
    }

    // [control][annex] only: annex strips to a single control element → 0.
    {
        CScriptWitness witness;
        witness.stack.push_back(std::vector<unsigned char>{static_cast<unsigned char>(TAPROOT_LEAF_TAPSCRIPT | 0x01)});
        witness.stack.push_back(std::vector<unsigned char>{static_cast<unsigned char>(ANNEX_TAG)});
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, witness);
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags), 0);
    }

    // Empty leaf script → 0.
    {
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, MakeP2MRWitness(CScript{}));
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags), 0);
    }

    // Wrong program length is not P2MR; WitnessSigOps returns 0 for non-v0/non-P2MR.
    {
        CScript bad = CScript() << OP_2 << std::vector<unsigned char>(20, 0x00);
        const CScript leaf = CScript() << dilithium_pubkey << OP_CHECKSIGDILITHIUM;
        BuildTxs(spendingTx, coins, creationTx, bad, CScript{}, MakeP2MRWitness(leaf));
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags), 0);
    }

    // Coinbase witness is ignored by GetTransactionSigOpCost.
    {
        const CScript leaf = CScript() << dilithium_pubkey << OP_CHECKSIGDILITHIUM;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, CScript{}, MakeP2MRWitness(leaf));
        spendingTx.vin[0].prevout.SetNull();
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spendingTx), coins, flags), 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
