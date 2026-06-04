// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <addresstype.h>
#include <crypto/dilithium_key.h>
#include <key.h>
#include <key_io.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/vector.h>
#include <wallet/p2mr.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
BOOST_AUTO_TEST_SUITE(p2mr_tests)

static UniValue MakeOpTrueTreeJSON()
{
    UniValue leaf(UniValue::VOBJ);
    leaf.pushKV("depth", 0);
    leaf.pushKV("leaf_version", 192);
    leaf.pushKV("script", "51"); // OP_TRUE
    UniValue tree(UniValue::VARR);
    tree.push_back(leaf);
    return tree;
}

static std::vector<P2MRTreeLeaf> MakeOpTrueTree()
{
    auto parsed = ParseP2MRTreeChecked(MakeOpTrueTreeJSON());
    BOOST_REQUIRE(parsed);
    return *parsed;
}

static std::unique_ptr<CWallet> MakeP2MRTestWallet(interfaces::Chain& chain)
{
    auto wallet = std::make_unique<CWallet>(&chain, "", CreateMockableWalletDatabase());
    wallet->LoadWallet();
    return wallet;
}

// Round-trips the OP_TRUE template through ParseP2MRTreeChecked,
// BuildP2MRTreeChecked, and P2MRTreeToUniValue to confirm shape parity.
BOOST_AUTO_TEST_CASE(parse_and_build_op_true)
{
    auto parsed = ParseP2MRTreeChecked(MakeOpTrueTreeJSON());
    BOOST_REQUIRE(parsed);
    BOOST_REQUIRE_EQUAL(parsed->size(), 1u);
    BOOST_CHECK_EQUAL(int(parsed->at(0).depth), 0);
    BOOST_CHECK_EQUAL(int(parsed->at(0).leaf_version), 192);
    BOOST_REQUIRE_EQUAL(parsed->at(0).script.size(), 1u);
    BOOST_CHECK_EQUAL(int(parsed->at(0).script[0]), 0x51);

    auto built = BuildP2MRTreeChecked(*parsed);
    BOOST_REQUIRE(built);
    BOOST_CHECK(built->IsValid());
    BOOST_CHECK(built->IsComplete());

    auto round = P2MRTreeToUniValue(*parsed);
    BOOST_REQUIRE(round.isArray());
    BOOST_REQUIRE_EQUAL(round.size(), 1u);
    BOOST_CHECK_EQUAL(round[0]["depth"].getInt<int>(), 0);
    BOOST_CHECK_EQUAL(round[0]["leaf_version"].getInt<int>(), 192);
    BOOST_CHECK_EQUAL(round[0]["script"].get_str(), "51");
}

BOOST_AUTO_TEST_CASE(reject_empty_tree)
{
    UniValue empty(UniValue::VARR);
    auto parsed = ParseP2MRTreeChecked(empty);
    BOOST_CHECK(!parsed);
}

BOOST_AUTO_TEST_CASE(reject_non_array_tree)
{
    UniValue obj(UniValue::VOBJ);
    auto parsed = ParseP2MRTreeChecked(obj);
    BOOST_CHECK(!parsed);
}

BOOST_AUTO_TEST_CASE(reject_missing_fields)
{
    UniValue leaf(UniValue::VOBJ);
    leaf.pushKV("depth", 0);
    // missing leaf_version + script
    UniValue tree(UniValue::VARR);
    tree.push_back(leaf);
    auto parsed = ParseP2MRTreeChecked(tree);
    BOOST_CHECK(!parsed);
}

BOOST_AUTO_TEST_CASE(reject_out_of_range_depth)
{
    UniValue leaf(UniValue::VOBJ);
    leaf.pushKV("depth", 129);
    leaf.pushKV("leaf_version", 192);
    leaf.pushKV("script", "51");
    UniValue tree(UniValue::VARR);
    tree.push_back(leaf);
    auto parsed = ParseP2MRTreeChecked(tree);
    BOOST_CHECK(!parsed);
}

BOOST_AUTO_TEST_CASE(reject_invalid_hex_script)
{
    UniValue leaf(UniValue::VOBJ);
    leaf.pushKV("depth", 0);
    leaf.pushKV("leaf_version", 192);
    leaf.pushKV("script", "zz");
    UniValue tree(UniValue::VARR);
    tree.push_back(leaf);
    auto parsed = ParseP2MRTreeChecked(tree);
    BOOST_CHECK(!parsed);
}

BOOST_AUTO_TEST_CASE(reject_leaf_version_with_control_parity_bit)
{
    UniValue leaf(UniValue::VOBJ);
    leaf.pushKV("depth", 0);
    leaf.pushKV("leaf_version", 193); // 0xc1: control-block parity bit is not part of the leaf version
    leaf.pushKV("script", "51");
    UniValue tree(UniValue::VARR);
    tree.push_back(leaf);

    auto parsed = ParseP2MRTreeChecked(tree);
    BOOST_CHECK(!parsed);

    std::vector<P2MRTreeLeaf> leaves{{0, 193, {0x51}}};
    auto built = BuildP2MRTreeChecked(leaves);
    BOOST_CHECK(!built);
}

BOOST_AUTO_TEST_CASE(build_rejects_empty_leaves_vector)
{
    std::vector<P2MRTreeLeaf> empty;
    auto built = BuildP2MRTreeChecked(empty);
    BOOST_CHECK(!built);
}

BOOST_AUTO_TEST_CASE(build_rejects_out_of_range_depth)
{
    std::vector<P2MRTreeLeaf> leaves{{129, 192, {0x51}}};
    auto built = BuildP2MRTreeChecked(leaves);
    BOOST_CHECK(!built);
}

BOOST_AUTO_TEST_CASE(build_rejects_incomplete_tree_without_asserting)
{
    std::vector<P2MRTreeLeaf> leaves{{1, 192, {0x51}}};
    auto built = BuildP2MRTreeChecked(leaves);
    BOOST_CHECK(!built);
}

// Regression test: a depth field stored as a string (e.g. corrupt or hand-
// written metadata) used to escape ParseP2MRTreeChecked as a UniValue
// type_error exception. It now returns a clean Result error.
BOOST_AUTO_TEST_CASE(reject_typeerror_on_depth_string)
{
    UniValue leaf(UniValue::VOBJ);
    leaf.pushKV("depth", std::string("zero"));
    leaf.pushKV("leaf_version", 192);
    leaf.pushKV("script", "51");
    UniValue tree(UniValue::VARR);
    tree.push_back(leaf);
    auto parsed = ParseP2MRTreeChecked(tree);
    BOOST_CHECK(!parsed);
}

BOOST_AUTO_TEST_CASE(reject_typeerror_on_script_int)
{
    UniValue leaf(UniValue::VOBJ);
    leaf.pushKV("depth", 0);
    leaf.pushKV("leaf_version", 192);
    leaf.pushKV("script", 51); // integer instead of hex string
    UniValue tree(UniValue::VARR);
    tree.push_back(leaf);
    auto parsed = ParseP2MRTreeChecked(tree);
    BOOST_CHECK(!parsed);
}

BOOST_FIXTURE_TEST_CASE(create_is_idempotent_for_identical_tree, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const auto leaves = MakeOpTrueTree();

    auto first = CreateP2MR(*wallet, leaves, "first");
    BOOST_REQUIRE(first);
    auto second = CreateP2MR(*wallet, leaves, "second");
    BOOST_REQUIRE(second);

    BOOST_CHECK_EQUAL(second->id, first->id);
    BOOST_CHECK_EQUAL(second->address, first->address);
    BOOST_CHECK_EQUAL(HexStr(second->script_pub_key), HexStr(first->script_pub_key));
    BOOST_CHECK_EQUAL(ListP2MR(*wallet).size(), 1U);
}

BOOST_FIXTURE_TEST_CASE(wallet_is_mine_recognizes_valid_p2mr_metadata, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const auto leaves = MakeOpTrueTree();

    auto builder = BuildP2MRTreeChecked(leaves);
    BOOST_REQUIRE(builder);
    const CScript tracked_script = GetScriptForDestination(builder->GetOutput());

    BOOST_CHECK_EQUAL(wallet->IsMine(tracked_script), ISMINE_NO);

    auto created = CreateP2MR(*wallet, leaves, "tracked");
    BOOST_REQUIRE(created);
    BOOST_CHECK_EQUAL(HexStr(created->script_pub_key), HexStr(tracked_script));
    BOOST_CHECK_EQUAL(wallet->IsMine(created->script_pub_key), ISMINE_SPENDABLE);

    P2MRBuilder corrupt_builder;
    corrupt_builder.Add(/*depth=*/0, std::vector<unsigned char>{static_cast<unsigned char>(OP_0)}, TAPROOT_LEAF_TAPSCRIPT).Finalize();
    BOOST_REQUIRE(corrupt_builder.IsValid());
    const CTxDestination corrupt_dest = corrupt_builder.GetOutput();
    const CScript corrupt_script = GetScriptForDestination(corrupt_dest);

    BOOST_REQUIRE(wallet->SetAddressBook(corrupt_dest, "corrupt", AddressPurpose::RECEIVE));
    UniValue corrupt_meta(UniValue::VOBJ);
    corrupt_meta.pushKV("id", "corrupt");
    corrupt_meta.pushKV("address", EncodeDestination(corrupt_dest));
    corrupt_meta.pushKV("scriptPubKey", HexStr(corrupt_script));
    corrupt_meta.pushKV("merkle_root", "");
    corrupt_meta.pushKV("created_at", int64_t{1});
    corrupt_meta.pushKV("label", "corrupt");
    corrupt_meta.pushKV("state", "created");
    UniValue empty_tree(UniValue::VARR);
    corrupt_meta.pushKV("tree", empty_tree);

    WalletBatch batch(wallet->GetDatabase(), /*fFlushOnClose=*/false);
    BOOST_REQUIRE(wallet->SetP2MRMetadata(batch, corrupt_dest, "corrupt", corrupt_meta.write()));
    BOOST_CHECK_EQUAL(wallet->IsMine(corrupt_script), ISMINE_NO);
}

BOOST_FIXTURE_TEST_CASE(tracked_balance_deduplicates_legacy_duplicate_metadata, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const auto leaves = MakeOpTrueTree();

    auto created = CreateP2MR(*wallet, leaves, "original");
    BOOST_REQUIRE(created);

    UniValue duplicate_meta(UniValue::VOBJ);
    duplicate_meta.pushKV("id", "legacy-duplicate");
    duplicate_meta.pushKV("address", created->address);
    duplicate_meta.pushKV("scriptPubKey", HexStr(created->script_pub_key));
    duplicate_meta.pushKV("merkle_root", HexStr(created->merkle_root));
    duplicate_meta.pushKV("created_at", int64_t{1});
    duplicate_meta.pushKV("label", "duplicate");
    duplicate_meta.pushKV("state", "created");
    duplicate_meta.pushKV("tree", P2MRTreeToUniValue(leaves));

    WalletBatch batch(wallet->GetDatabase(), /*fFlushOnClose=*/false);
    BOOST_REQUIRE(wallet->SetP2MRMetadata(batch, created->dest, "legacy-duplicate", duplicate_meta.write()));
    BOOST_REQUIRE_EQUAL(ListP2MR(*wallet).size(), 2U);

    const CAmount amount{5 * COIN};
    CMutableTransaction tx;
    tx.nLockTime = 1;
    tx.vout.emplace_back(amount, created->script_pub_key);
    const uint256 txid = tx.GetHash();
    auto inserted = wallet->mapWallet.emplace(std::piecewise_construct,
        std::forward_as_tuple(txid),
        std::forward_as_tuple(MakeTransactionRef(std::move(tx)), TxStateInactive{}));
    BOOST_REQUIRE(inserted.second);

    auto original_entry = GetP2MR(*wallet, created->id);
    BOOST_REQUIRE(original_entry);
    BOOST_CHECK_EQUAL(GetP2MREntryBalance(*wallet, *original_entry, /*min_depth=*/0), amount);

    auto duplicate_entry = GetP2MR(*wallet, "legacy-duplicate");
    BOOST_REQUIRE(duplicate_entry);
    BOOST_CHECK_EQUAL(GetP2MREntryBalance(*wallet, *duplicate_entry, /*min_depth=*/0), amount);

    BOOST_CHECK_EQUAL(GetTrackedP2MRBalance(*wallet, /*min_depth=*/0), amount);
}

BOOST_FIXTURE_TEST_CASE(produce_signature_preserves_p2mr_witness_stack, BasicTestingSetup)
{
    CKey key;
    key.MakeNewKey(/*fCompressedIn=*/true);
    const CPubKey pubkey = key.GetPubKey();
    const XOnlyPubKey xonly_pubkey{pubkey};

    const CScript leaf_script = CScript() << ToByteVector(xonly_pubkey) << OP_CHECKSIG;
    const std::vector<unsigned char> leaf_bytes{leaf_script.begin(), leaf_script.end()};

    P2MRBuilder builder;
    builder.Add(/*depth=*/0, leaf_bytes, TAPROOT_LEAF_TAPSCRIPT).Finalize();
    BOOST_REQUIRE(builder.IsValid());
    BOOST_REQUIRE(builder.IsComplete());

    const WitnessV2P2MR output = builder.GetOutput();
    const CScript script_pubkey = GetScriptForDestination(output);
    const CAmount amount = COIN;

    FlatSigningProvider provider;
    provider.keys.emplace(pubkey.GetID(), key);
    provider.pubkeys.emplace(pubkey.GetID(), pubkey);
    provider.p2mr_trees.emplace(output, builder);

    CMutableTransaction tx_to;
    tx_to.nVersion = 2;
    tx_to.vin.emplace_back(COutPoint(uint256::ONE, 0));
    tx_to.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);

    std::vector<CTxOut> spent_outputs;
    spent_outputs.emplace_back(amount, script_pubkey);
    PrecomputedTransactionData txdata;
    txdata.Init(tx_to, std::move(spent_outputs), /*force=*/true);

    SignatureData sigdata;
    MutableTransactionSignatureCreator creator(tx_to, /*input_idx=*/0, amount, &txdata, SIGHASH_DEFAULT);
    BOOST_REQUIRE(ProduceSignature(provider, creator, script_pubkey, sigdata));
    BOOST_CHECK(sigdata.complete);
    BOOST_CHECK(sigdata.witness);
    BOOST_REQUIRE_EQUAL(sigdata.scriptWitness.stack.size(), 3U);
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[0].size(), 64U);
    BOOST_CHECK_EQUAL(HexStr(sigdata.scriptWitness.stack[1]), HexStr(leaf_script));
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[2].size(), P2MR_CONTROL_BASE_SIZE);

    UpdateInput(tx_to.vin[0], sigdata);
    const CTransaction signed_tx{tx_to};
    TransactionSignatureChecker checker(&signed_tx, /*nInIn=*/0, amount, txdata, MissingDataBehavior::FAIL);
    ScriptError serror{SCRIPT_ERR_OK};
    BOOST_CHECK_MESSAGE(
        VerifyScript(tx_to.vin[0].scriptSig, script_pubkey, &tx_to.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &serror),
        ScriptErrorString(serror));
}

BOOST_FIXTURE_TEST_CASE(produce_signature_signs_dilithium_p2mr_leaf, BasicTestingSetup)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());

    const CScript leaf_script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    const std::vector<unsigned char> leaf_bytes{leaf_script.begin(), leaf_script.end()};

    P2MRBuilder builder;
    builder.Add(/*depth=*/0, leaf_bytes, TAPROOT_LEAF_TAPSCRIPT).Finalize();
    BOOST_REQUIRE(builder.IsValid());
    BOOST_REQUIRE(builder.IsComplete());

    const WitnessV2P2MR output = builder.GetOutput();
    const CScript script_pubkey = GetScriptForDestination(output);
    const CAmount amount = COIN;

    FlatSigningProvider provider;
    provider.dilithium_pubkeys.emplace(DilithiumPKHash(pubkey), pubkey);
    provider.dilithium_keys.emplace(DilithiumPKHash(pubkey), key);
    provider.p2mr_trees.emplace(output, builder);

    CMutableTransaction tx_to;
    tx_to.nVersion = 2;
    tx_to.vin.emplace_back(COutPoint(uint256::ONE, 0));
    tx_to.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);

    std::vector<CTxOut> spent_outputs;
    spent_outputs.emplace_back(amount, script_pubkey);
    PrecomputedTransactionData txdata;
    txdata.Init(tx_to, std::move(spent_outputs), /*force=*/true);

    SignatureData sigdata;
    MutableTransactionSignatureCreator creator(tx_to, /*input_idx=*/0, amount, &txdata, SIGHASH_ALL);
    BOOST_REQUIRE(ProduceSignature(provider, creator, script_pubkey, sigdata));
    BOOST_CHECK(sigdata.complete);
    BOOST_CHECK(sigdata.witness);
    BOOST_REQUIRE_EQUAL(sigdata.scriptWitness.stack.size(), 3U);
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[0].size(), DilithiumConstants::SIGNATURE_SIZE + 1);
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[0].back(), SIGHASH_ALL);
    BOOST_CHECK_EQUAL(HexStr(sigdata.scriptWitness.stack[1]), HexStr(leaf_script));
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[2].size(), P2MR_CONTROL_BASE_SIZE);

    UpdateInput(tx_to.vin[0], sigdata);
    const CTransaction signed_tx{tx_to};
    TransactionSignatureChecker checker(&signed_tx, /*nInIn=*/0, amount, txdata, MissingDataBehavior::FAIL);
    ScriptError serror{SCRIPT_ERR_OK};
    BOOST_CHECK_MESSAGE(
        VerifyScript(tx_to.vin[0].scriptSig, script_pubkey, &tx_to.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &serror),
        ScriptErrorString(serror));

    std::vector<CTxOut> auto_spent_outputs;
    auto_spent_outputs.emplace_back(amount, script_pubkey);
    PrecomputedTransactionData auto_txdata;
    auto_txdata.Init(signed_tx, std::move(auto_spent_outputs), /*force=*/false);
    TransactionSignatureChecker auto_checker(&signed_tx, /*nInIn=*/0, amount, auto_txdata, MissingDataBehavior::FAIL);
    serror = SCRIPT_ERR_OK;
    BOOST_CHECK_MESSAGE(
        VerifyScript(tx_to.vin[0].scriptSig, script_pubkey, &tx_to.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, auto_checker, &serror),
        ScriptErrorString(serror));

    std::vector<CTxOut> wrong_amount_outputs;
    wrong_amount_outputs.emplace_back(amount + 1, script_pubkey);
    PrecomputedTransactionData wrong_amount_txdata;
    wrong_amount_txdata.Init(signed_tx, std::move(wrong_amount_outputs), /*force=*/false);
    TransactionSignatureChecker wrong_amount_checker(&signed_tx, /*nInIn=*/0, amount + 1, wrong_amount_txdata, MissingDataBehavior::FAIL);
    serror = SCRIPT_ERR_OK;
    BOOST_CHECK(!VerifyScript(tx_to.vin[0].scriptSig, script_pubkey, &tx_to.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, wrong_amount_checker, &serror));

    CScriptWitness mutated_witness = tx_to.vin[0].scriptWitness;
    BOOST_REQUIRE(!mutated_witness.stack[0].empty());
    mutated_witness.stack[0][0] ^= 0x01;
    serror = SCRIPT_ERR_OK;
    BOOST_CHECK(!VerifyScript(tx_to.vin[0].scriptSig, script_pubkey, &mutated_witness, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &serror));

    mutated_witness = tx_to.vin[0].scriptWitness;
    BOOST_REQUIRE(!mutated_witness.stack[1].empty());
    mutated_witness.stack[1][mutated_witness.stack[1].size() - 1] = OP_TRUE;
    serror = SCRIPT_ERR_OK;
    BOOST_CHECK(!VerifyScript(tx_to.vin[0].scriptSig, script_pubkey, &mutated_witness, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &serror));
}

BOOST_FIXTURE_TEST_CASE(produce_signature_signs_dilithium_p2mr_pubkeyhash_leaf, BasicTestingSetup)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    const DilithiumPKHash keyhash(pubkey);

    const CScript leaf_script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(keyhash) << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;
    const std::vector<unsigned char> leaf_bytes{leaf_script.begin(), leaf_script.end()};

    P2MRBuilder builder;
    builder.Add(/*depth=*/0, leaf_bytes, TAPROOT_LEAF_TAPSCRIPT).Finalize();
    BOOST_REQUIRE(builder.IsValid());
    BOOST_REQUIRE(builder.IsComplete());

    const WitnessV2P2MR output = builder.GetOutput();
    const CScript script_pubkey = GetScriptForDestination(output);
    const CAmount amount = COIN;

    FlatSigningProvider provider;
    provider.dilithium_pubkeys.emplace(keyhash, pubkey);
    provider.dilithium_keys.emplace(keyhash, key);
    provider.p2mr_trees.emplace(output, builder);

    CMutableTransaction tx_to;
    tx_to.nVersion = 2;
    tx_to.vin.emplace_back(COutPoint(uint256::ONE, 0));
    tx_to.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);

    std::vector<CTxOut> spent_outputs;
    spent_outputs.emplace_back(amount, script_pubkey);
    PrecomputedTransactionData txdata;
    txdata.Init(tx_to, std::move(spent_outputs), /*force=*/true);

    SignatureData sigdata;
    MutableTransactionSignatureCreator creator(tx_to, /*input_idx=*/0, amount, &txdata, SIGHASH_ALL);
    BOOST_REQUIRE(ProduceSignature(provider, creator, script_pubkey, sigdata));
    BOOST_REQUIRE_EQUAL(sigdata.scriptWitness.stack.size(), 4U);
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[0].size(), DilithiumConstants::SIGNATURE_SIZE + 1);
    BOOST_CHECK_EQUAL(HexStr(sigdata.scriptWitness.stack[1]), HexStr(pubkey));
    BOOST_CHECK_EQUAL(HexStr(sigdata.scriptWitness.stack[2]), HexStr(leaf_script));
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[3].size(), P2MR_CONTROL_BASE_SIZE);

    UpdateInput(tx_to.vin[0], sigdata);
    const CTransaction signed_tx{tx_to};
    TransactionSignatureChecker checker(&signed_tx, /*nInIn=*/0, amount, txdata, MissingDataBehavior::FAIL);
    ScriptError serror{SCRIPT_ERR_OK};
    BOOST_CHECK_MESSAGE(
        VerifyScript(tx_to.vin[0].scriptSig, script_pubkey, &tx_to.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &serror),
        ScriptErrorString(serror));
}

BOOST_FIXTURE_TEST_CASE(p2mr_dilithium_checksig_nullfail_rejects_nonempty_invalid_signature, BasicTestingSetup)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    const CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());

    const CScript leaf_script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM << OP_NOT;
    const std::vector<unsigned char> leaf_bytes{leaf_script.begin(), leaf_script.end()};

    P2MRBuilder builder;
    builder.Add(/*depth=*/0, leaf_bytes, TAPROOT_LEAF_TAPSCRIPT).Finalize();
    BOOST_REQUIRE(builder.IsValid());
    BOOST_REQUIRE(builder.IsComplete());

    const WitnessV2P2MR output = builder.GetOutput();
    const CScript script_pubkey = GetScriptForDestination(output);
    const CAmount amount = COIN;

    P2MRSpendData spenddata = builder.GetSpendData();
    BOOST_REQUIRE_EQUAL(spenddata.scripts.size(), 1U);
    const auto& [script_key, control_blocks] = *spenddata.scripts.begin();
    BOOST_REQUIRE_EQUAL(HexStr(script_key.first), HexStr(leaf_script));
    BOOST_REQUIRE(!control_blocks.empty());

    CMutableTransaction tx_to;
    tx_to.nVersion = 2;
    tx_to.vin.emplace_back(COutPoint(uint256::ONE, 0));
    tx_to.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);

    std::vector<unsigned char> invalid_signature(DilithiumConstants::SIGNATURE_SIZE + 1, 0);
    invalid_signature.back() = SIGHASH_ALL;
    tx_to.vin[0].scriptWitness.stack.push_back(std::move(invalid_signature));
    tx_to.vin[0].scriptWitness.stack.emplace_back(leaf_script.begin(), leaf_script.end());
    tx_to.vin[0].scriptWitness.stack.push_back(*control_blocks.begin());

    const CTransaction signed_tx{tx_to};
    std::vector<CTxOut> spent_outputs;
    spent_outputs.emplace_back(amount, script_pubkey);
    PrecomputedTransactionData txdata;
    txdata.Init(signed_tx, std::move(spent_outputs), /*force=*/true);

    TransactionSignatureChecker checker(&signed_tx, /*nInIn=*/0, amount, txdata, MissingDataBehavior::FAIL);
    ScriptError serror{SCRIPT_ERR_OK};
    BOOST_CHECK_MESSAGE(
        VerifyScript(tx_to.vin[0].scriptSig, script_pubkey, &tx_to.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS & ~SCRIPT_VERIFY_NULLFAIL, checker, &serror),
        ScriptErrorString(serror));
    BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_OK);

    serror = SCRIPT_ERR_OK;
    BOOST_CHECK(!VerifyScript(tx_to.vin[0].scriptSig, script_pubkey, &tx_to.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &serror));
    BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_SIG_NULLFAIL);
}

BOOST_FIXTURE_TEST_CASE(build_signing_provider_exports_descriptor_dilithium_p2mr_leaf, BasicTestingSetup)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    DescriptorScriptPubKeyMan* keyman{nullptr};
    {
        LOCK(wallet.cs_wallet);
        wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
        wallet.SetupDescriptorScriptPubKeyMans();
        keyman = dynamic_cast<DescriptorScriptPubKeyMan*>(wallet.GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/false));
    }
    BOOST_REQUIRE(keyman);

    const util::Result<CTxDestination> dest = keyman->GetNewDestination(OutputType::DILITHIUM_LEGACY);
    BOOST_REQUIRE(dest);
    BOOST_REQUIRE(std::holds_alternative<DilithiumPKHash>(*dest));
    const DilithiumPKHash keyhash = std::get<DilithiumPKHash>(*dest);
    const CScript leaf_script = GetScriptForDestination(*dest);

    std::vector<std::vector<unsigned char>> solutions;
    BOOST_REQUIRE(Solver(leaf_script, solutions) == TxoutType::DILITHIUM_PUBKEYHASH);
    BOOST_REQUIRE_EQUAL(solutions.size(), 1U);
    BOOST_CHECK_EQUAL(HexStr(solutions[0]), HexStr(keyhash));

    const std::vector<P2MRTreeLeaf> leaves{{/*depth=*/0, TAPROOT_LEAF_TAPSCRIPT, {leaf_script.begin(), leaf_script.end()}}};

    P2MRCreated created;
    FlatSigningProvider provider;
    {
        LOCK(wallet.cs_wallet);
        auto created_res = CreateP2MR(wallet, leaves, "descriptor-dilithium");
        BOOST_REQUIRE(created_res);
        created = std::move(*created_res);
        provider = BuildP2MRSigningProvider(wallet, created.id);
    }

    CDilithiumPubKey provider_pubkey;
    CDilithiumKey provider_key;
    BOOST_REQUIRE(provider.GetDilithiumPubKey(keyhash, provider_pubkey));
    BOOST_REQUIRE(provider.GetDilithiumKeyByHash(keyhash, provider_key));
    BOOST_CHECK(provider_pubkey == provider_key.GetPubKey());

    P2MRSpendData spenddata;
    BOOST_REQUIRE(provider.GetP2MRSpendData(std::get<WitnessV2P2MR>(created.dest), spenddata));
    BOOST_REQUIRE_EQUAL(spenddata.scripts.size(), 1U);

    constexpr CAmount amount{COIN};
    CMutableTransaction tx_to;
    tx_to.nVersion = 2;
    tx_to.vin.emplace_back(COutPoint(uint256::ONE, 0));
    tx_to.vout.emplace_back(amount - 1000, CScript() << OP_TRUE);

    std::vector<CTxOut> spent_outputs;
    spent_outputs.emplace_back(amount, created.script_pub_key);
    PrecomputedTransactionData txdata;
    txdata.Init(tx_to, std::move(spent_outputs), /*force=*/true);

    SignatureData sigdata = DataFromTransaction(tx_to, /*nIn=*/0, CTxOut(amount, created.script_pub_key));
    MutableTransactionSignatureCreator creator(tx_to, /*input_idx=*/0, amount, &txdata, SIGHASH_DEFAULT);
    BOOST_REQUIRE(ProduceSignature(provider, creator, created.script_pub_key, sigdata));
    BOOST_REQUIRE(sigdata.complete);
    BOOST_REQUIRE_EQUAL(sigdata.scriptWitness.stack.size(), 4U);
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[0].size(), DilithiumConstants::SIGNATURE_SIZE + 1);
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[0].back(), SIGHASH_ALL);
    BOOST_CHECK_EQUAL(HexStr(sigdata.scriptWitness.stack[1]), HexStr(provider_pubkey));
    BOOST_CHECK_EQUAL(HexStr(sigdata.scriptWitness.stack[2]), HexStr(leaf_script));

    UpdateInput(tx_to.vin[0], sigdata);
    const CTransaction signed_tx{tx_to};
    TransactionSignatureChecker checker(&signed_tx, /*nInIn=*/0, amount, txdata, MissingDataBehavior::FAIL);
    ScriptError serror{SCRIPT_ERR_OK};
    BOOST_CHECK_MESSAGE(
        VerifyScript(tx_to.vin[0].scriptSig, created.script_pub_key, &tx_to.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &serror),
        ScriptErrorString(serror));
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
