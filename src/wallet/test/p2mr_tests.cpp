// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <addresstype.h>
#include <key.h>
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

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
