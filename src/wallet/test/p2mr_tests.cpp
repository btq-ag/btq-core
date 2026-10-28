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
#include <script/dilithium_leaf.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <univalue.h>
#include <util/result.h>
#include <util/strencodings.h>
#include <util/vector.h>
#include <validation.h>
#include <wallet/p2mr.h>
#include <wallet/receive.h>
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

static std::vector<P2MRTreeLeaf> MakeXOnlyChecksigTree(const XOnlyPubKey& pubkey)
{
    const CScript leaf_script = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;
    return {{/*depth=*/0, TAPROOT_LEAF_TAPSCRIPT, {leaf_script.begin(), leaf_script.end()}}};
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

BOOST_FIXTURE_TEST_CASE(create_allows_only_wallet_spendable_dilithium_leaves_by_default, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const std::vector<std::vector<P2MRTreeLeaf>> rejected_trees{
        {{0, TAPROOT_LEAF_TAPSCRIPT, {}}},
        {{0, TAPROOT_LEAF_TAPSCRIPT, {OP_TRUE}}},
        {{0, TAPROOT_LEAF_TAPSCRIPT, {OP_2}}},
        {{0, TAPROOT_LEAF_TAPSCRIPT, {OP_DROP, OP_TRUE}}},
        {{0, TAPROOT_LEAF_TAPSCRIPT, {0x50}}}, // OP_SUCCESS80
        {{0, 0xc2, {OP_TRUE}}},
    };
    for (const auto& leaves : rejected_trees) {
        auto rejected = CreateP2MR(*wallet, leaves, "unsafe");
        BOOST_CHECK(!rejected);
        BOOST_CHECK(util::ErrorString(rejected).original.find("does not participate") != std::string::npos);
    }

    auto allowed = CreateP2MR(*wallet, rejected_trees[3], "unsafe", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(allowed);
}

BOOST_FIXTURE_TEST_CASE(create_allows_threshold_accumulator_with_one_cosigner_key, BasicTestingSetup)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    auto* spk_man = wallet.GetOrCreateLegacyScriptPubKeyMan();
    BOOST_REQUIRE(spk_man);

    CDilithiumKey mine, other_a, other_b;
    mine.MakeNewKey();
    other_a.MakeNewKey();
    other_b.MakeNewKey();
    BOOST_REQUIRE(mine.IsValid() && other_a.IsValid() && other_b.IsValid());

    {
        LOCK2(wallet.cs_wallet, spk_man->cs_KeyStore);
        BOOST_REQUIRE(spk_man->AddDilithiumKeyPubKey(mine));

        const CScript leaf_script = GetScriptForDilithiumThreshold(
            2, {mine.GetPubKey(), other_a.GetPubKey(), other_b.GetPubKey()});
        const std::vector<P2MRTreeLeaf> leaves{
            {0, TAPROOT_LEAF_TAPSCRIPT, {leaf_script.begin(), leaf_script.end()}}};

        auto created = CreateP2MR(wallet, leaves, "shared-multisig");
        BOOST_REQUIRE(created);
    }

    CDilithiumKey outsider_a, outsider_b;
    outsider_a.MakeNewKey();
    outsider_b.MakeNewKey();
    const CScript outsider_script = GetScriptForDilithiumThreshold(
        2, {outsider_a.GetPubKey(), outsider_b.GetPubKey()});
    const std::vector<P2MRTreeLeaf> outsider_leaves{
        {0, TAPROOT_LEAF_TAPSCRIPT, {outsider_script.begin(), outsider_script.end()}}};
    LOCK(wallet.cs_wallet);
    auto rejected = CreateP2MR(wallet, outsider_leaves, "not-ours");
    BOOST_CHECK(!rejected);
}

BOOST_FIXTURE_TEST_CASE(create_is_idempotent_for_identical_tree, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const auto leaves = MakeOpTrueTree();

    auto first = CreateP2MR(*wallet, leaves, "first", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(first);
    auto second = CreateP2MR(*wallet, leaves, "second", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(second);

    BOOST_CHECK_EQUAL(second->id, first->id);
    BOOST_CHECK_EQUAL(second->address, first->address);
    BOOST_CHECK_EQUAL(HexStr(second->script_pub_key), HexStr(first->script_pub_key));
    BOOST_CHECK_EQUAL(ListP2MR(*wallet).size(), 1U);
    BOOST_CHECK_EQUAL(ListP2MR(*wallet)[0].label, "second");
}

BOOST_FIXTURE_TEST_CASE(restore_duplicate_replaces_creation_time, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const auto leaves = MakeOpTrueTree();

    auto created = CreateP2MR(*wallet, leaves, "restored", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(created);

    UniValue meta(UniValue::VOBJ);
    meta.pushKV("address", created->address);
    meta.pushKV("scriptPubKey", HexStr(created->script_pub_key));
    meta.pushKV("merkle_root", HexStr(created->merkle_root));
    meta.pushKV("created_at", int64_t{0});
    meta.pushKV("label", "restored");
    meta.pushKV("tree", P2MRTreeToUniValue(leaves));

    auto restored = RestoreP2MR(*wallet, meta);
    BOOST_REQUIRE(restored);
    auto entry = GetP2MR(*wallet, restored->id);
    BOOST_REQUIRE(entry);
    BOOST_CHECK_EQUAL(entry->created_at, 0);

    UniValue omit_meta(UniValue::VOBJ);
    omit_meta.pushKV("tree", P2MRTreeToUniValue(leaves));
    omit_meta.pushKV("label", "omitted-birth");
    auto omitted = RestoreP2MR(*wallet, omit_meta);
    BOOST_REQUIRE(omitted);
    auto omitted_entry = GetP2MR(*wallet, omitted->id);
    BOOST_REQUIRE(omitted_entry);
    BOOST_CHECK_EQUAL(omitted_entry->created_at, 0);

    UniValue invalid_meta(UniValue::VOBJ);
    invalid_meta.pushKV("tree", P2MRTreeToUniValue(leaves));
    invalid_meta.pushKV("merkle_root", int64_t{1});
    auto invalid = ValidateP2MRRestore(invalid_meta);
    BOOST_CHECK(!invalid);
}

BOOST_FIXTURE_TEST_CASE(restore_omitted_created_at_on_new_record_is_zero, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    UniValue meta(UniValue::VOBJ);
    meta.pushKV("tree", P2MRTreeToUniValue(MakeOpTrueTree()));
    auto restored = RestoreP2MR(*wallet, meta);
    BOOST_REQUIRE(restored);
    auto entry = GetP2MR(*wallet, restored->id);
    BOOST_REQUIRE(entry);
    BOOST_CHECK_EQUAL(entry->created_at, 0);
}

BOOST_FIXTURE_TEST_CASE(restore_duplicate_later_created_at_does_not_move_forward, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const auto leaves = MakeOpTrueTree();

    UniValue first(UniValue::VOBJ);
    first.pushKV("tree", P2MRTreeToUniValue(leaves));
    first.pushKV("created_at", int64_t{1'000});
    BOOST_REQUIRE(RestoreP2MR(*wallet, first));
    BOOST_REQUIRE_EQUAL(ListP2MR(*wallet)[0].created_at, 1'000);

    UniValue later(UniValue::VOBJ);
    later.pushKV("tree", P2MRTreeToUniValue(leaves));
    later.pushKV("created_at", int64_t{9'000});
    BOOST_REQUIRE(RestoreP2MR(*wallet, later));
    BOOST_CHECK_EQUAL(ListP2MR(*wallet)[0].created_at, 1'000);

    UniValue unknown(UniValue::VOBJ);
    unknown.pushKV("tree", P2MRTreeToUniValue(leaves));
    unknown.pushKV("created_at", int64_t{0});
    BOOST_REQUIRE(RestoreP2MR(*wallet, unknown));
    BOOST_CHECK_EQUAL(ListP2MR(*wallet)[0].created_at, 0);
}

BOOST_FIXTURE_TEST_CASE(restore_rejects_negative_created_at, BasicTestingSetup)
{
    UniValue meta(UniValue::VOBJ);
    meta.pushKV("tree", P2MRTreeToUniValue(MakeOpTrueTree()));
    meta.pushKV("created_at", int64_t{-1});
    BOOST_CHECK(!ValidateP2MRRestore(meta));
}

BOOST_FIXTURE_TEST_CASE(create_is_idempotent_for_reordered_tree, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const P2MRTreeLeaf a{/*depth=*/1, TAPROOT_LEAF_TAPSCRIPT, {OP_1}};
    const P2MRTreeLeaf b{/*depth=*/2, TAPROOT_LEAF_TAPSCRIPT, {OP_2}};
    const P2MRTreeLeaf c{/*depth=*/2, TAPROOT_LEAF_TAPSCRIPT, {OP_3}};

    auto first = CreateP2MR(*wallet, {a, b, c}, "first", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(first);
    for (const auto& order : std::vector<std::vector<P2MRTreeLeaf>>{{a, c, b}, {b, c, a}, {c, b, a}}) {
        auto again = CreateP2MR(*wallet, order, "again", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(again);
        BOOST_CHECK_EQUAL(again->id, first->id);
        BOOST_CHECK_EQUAL(HexStr(again->script_pub_key), HexStr(first->script_pub_key));
    }
    BOOST_CHECK_EQUAL(ListP2MR(*wallet).size(), 1U);

    const P2MRTreeLeaf different_leaf{/*depth=*/2, TAPROOT_LEAF_TAPSCRIPT, {OP_4}};
    auto different = CreateP2MR(*wallet, {a, b, different_leaf}, "different", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(different);
    BOOST_CHECK(different->id != first->id);
    BOOST_CHECK(different->script_pub_key != first->script_pub_key);
    BOOST_CHECK_EQUAL(ListP2MR(*wallet).size(), 2U);
}

BOOST_FIXTURE_TEST_CASE(create_does_not_reuse_same_leaves_in_another_shape, BasicTestingSetup)
{
    const P2MRTreeLeaf a{/*depth=*/2, TAPROOT_LEAF_TAPSCRIPT, {OP_1}};
    const P2MRTreeLeaf b{/*depth=*/2, TAPROOT_LEAF_TAPSCRIPT, {OP_2}};
    const P2MRTreeLeaf c{/*depth=*/2, TAPROOT_LEAF_TAPSCRIPT, {OP_3}};
    const P2MRTreeLeaf d{/*depth=*/2, TAPROOT_LEAF_TAPSCRIPT, {OP_4}};
    const std::vector<P2MRTreeLeaf> abcd{a, b, c, d};
    const std::vector<P2MRTreeLeaf> acbd{a, c, b, d};

    {
        // ((A,B),(C,D)) and ((A,C),(B,D)) have the same leaf multiset and different roots
        auto wallet = MakeP2MRTestWallet(*m_node.chain);
        LOCK(wallet->cs_wallet);
        auto first = CreateP2MR(*wallet, abcd, "first", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(first);
        auto swapped = CreateP2MR(*wallet, {b, a, d, c}, "swapped", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(swapped);
        BOOST_CHECK_EQUAL(swapped->id, first->id);
        auto other = CreateP2MR(*wallet, acbd, "other", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(other);
        BOOST_CHECK(other->id != first->id);
        BOOST_CHECK(other->script_pub_key != first->script_pub_key);
        BOOST_CHECK_EQUAL(ListP2MR(*wallet).size(), 2U);
    }

    {
        // A record under the ((A,B),(C,D)) output whose stored tree is ((A,C),(B,D))
        auto wallet = MakeP2MRTestWallet(*m_node.chain);
        LOCK(wallet->cs_wallet);
        auto builder = BuildP2MRTreeChecked(abcd);
        BOOST_REQUIRE(builder);
        const CTxDestination dest = builder->GetOutput();
        const CScript spk = GetScriptForDestination(dest);
        const std::string bad_id{"00000000000000aa"};
        UniValue meta(UniValue::VOBJ);
        meta.pushKV("id", bad_id);
        meta.pushKV("address", EncodeDestination(dest));
        meta.pushKV("scriptPubKey", HexStr(spk));
        const WitnessV2P2MR& root = std::get<WitnessV2P2MR>(dest);
        meta.pushKV("merkle_root", HexStr(Span<const unsigned char>{root.begin(), root.end()}));
        meta.pushKV("created_at", int64_t{1});
        meta.pushKV("label", "bad");
        meta.pushKV("state", "created");
        meta.pushKV("tree", P2MRTreeToUniValue(acbd));
        WalletBatch batch(wallet->GetDatabase(), /*fFlushOnClose=*/false);
        BOOST_REQUIRE(wallet->SetP2MRMetadata(batch, dest, bad_id, meta.write()));

        auto created = CreateP2MR(*wallet, abcd, "good", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(created);
        BOOST_CHECK(created->id != bad_id);
        BOOST_CHECK_EQUAL(HexStr(created->script_pub_key), HexStr(spk));
    }
}

BOOST_FIXTURE_TEST_CASE(p2mr_metadata_not_in_receive_requests, BasicTestingSetup)
{
    // F2.17: P2MR metadata has its own record type; the GUI-facing receive
    // request list must never see it, including rows written by old wallets.
    const auto leaves = MakeOpTrueTree();
    std::string address;
    std::string legacy_json;
    MockableData records;
    // The downgrade guard must sit in the mandatory flag range, where
    // binaries that do not know it refuse to load the wallet.
    static_assert((WALLET_FLAG_P2MR_METADATA >> 32) != 0);

    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
        wallet->LoadWallet();
        LOCK(wallet->cs_wallet);
        BOOST_CHECK(!wallet->IsWalletFlagSet(WALLET_FLAG_P2MR_METADATA));
        auto created = CreateP2MR(*wallet, leaves, "meta", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(created);
        address = created->address;

        // The first p2mrmeta write marks the wallet so older binaries,
        // which would silently hide the P2MR balance, refuse to open it.
        BOOST_CHECK(wallet->IsWalletFlagSet(WALLET_FLAG_P2MR_METADATA));

        std::string value;
        BOOST_CHECK(wallet->GetP2MRMetadata(created->dest, created->id, value));
        BOOST_CHECK(wallet->GetAddressReceiveRequests().empty());

        // Erasing a receive request for an unknown destination must not
        // plant a ghost address-book entry (reload would not have one).
        const CTxDestination unknown{PKHash{uint160{}}};
        BOOST_CHECK(wallet->m_address_book.find(unknown) == wallet->m_address_book.end());
        WalletBatch ghost_batch(wallet->GetDatabase(), /*fFlushOnClose=*/false);
        wallet->EraseAddressReceiveRequest(ghost_batch, unknown, "nope");
        BOOST_CHECK(wallet->m_address_book.find(unknown) == wallet->m_address_book.end());

        // Simulate a pre-F2.17 wallet: metadata stored as a receive request.
        legacy_json = value;
        WalletBatch batch(wallet->GetDatabase(), /*fFlushOnClose=*/false);
        BOOST_REQUIRE(wallet->SetAddressReceiveRequest(batch, created->dest, "rrp2mr:legacy-id", legacy_json));
        records = GetMockableDatabase(*wallet).m_records;
    }

    {
        auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(records));
        BOOST_CHECK_EQUAL(wallet->LoadWallet(), DBErrors::LOAD_OK);
        LOCK(wallet->cs_wallet);
        const CTxDestination dest = DecodeDestination(address);
        std::string value;
        // The legacy row is routed into the metadata map on load...
        BOOST_CHECK(wallet->GetP2MRMetadata(dest, "legacy-id", value));
        BOOST_CHECK_EQUAL(value, legacy_json);
        // ...together with the natively stored entry...
        BOOST_CHECK_EQUAL(wallet->ListP2MRMetadata().size(), 2U);
        // ...and receive requests stay empty.
        BOOST_CHECK(wallet->GetAddressReceiveRequests().empty());
        // The downgrade guard survives the reload.
        BOOST_CHECK(wallet->IsWalletFlagSet(WALLET_FLAG_P2MR_METADATA));
    }
}

BOOST_FIXTURE_TEST_CASE(fund_p2mr_rolls_back_on_funding_failure, BasicTestingSetup)
{
    const auto leaves = MakeOpTrueTree();
    CCoinControl coin_control;
    auto builder = BuildP2MRTreeChecked(leaves);
    BOOST_REQUIRE(builder);
    const CTxDestination dest = builder->GetOutput();

    auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
    wallet->LoadWallet();

    // Reopens the wallet from its current records, so every assertion can
    // also be checked against what a restart would load.
    const auto reload = [&] {
        MockableData records = GetMockableDatabase(*wallet).m_records;
        wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase(records));
        BOOST_CHECK_EQUAL(wallet->LoadWallet(), DBErrors::LOAD_OK);
    };

    // The wallet has no coins, so CreateTransaction fails. The entry this
    // call created must be rolled back (Quarks INFO-15).
    {
        LOCK(wallet->cs_wallet);
        auto funded = FundP2MR(*wallet, leaves, CENT, "fund-fail", /*subtract_fee_from_amount=*/false, coin_control, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(!funded);
    }
    reload();
    {
        LOCK(wallet->cs_wallet);
        BOOST_CHECK(ListP2MR(*wallet).empty());
        BOOST_CHECK(wallet->ListP2MRMetadata().empty());
        BOOST_CHECK(!wallet->FindAddressBookEntry(dest));
    }

    // A pre-existing address-book entry (labelled, but not P2MR-tracked)
    // must survive the rollback with its label and purpose intact, even
    // though CreateP2MR overwrote them.
    {
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet->SetAddressBook(dest, "pre-existing", AddressPurpose::SEND));
        auto funded = FundP2MR(*wallet, leaves, CENT, "overwrite", /*subtract_fee_from_amount=*/false, coin_control, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(!funded);
    }
    reload();
    {
        LOCK(wallet->cs_wallet);
        BOOST_CHECK(wallet->ListP2MRMetadata().empty());
        const auto* entry = wallet->FindAddressBookEntry(dest);
        BOOST_REQUIRE(entry);
        BOOST_CHECK_EQUAL(entry->GetLabel(), "pre-existing");
        BOOST_CHECK(entry->purpose == AddressPurpose::SEND);
        BOOST_REQUIRE(wallet->DelAddressBook(dest));
    }

    // A pre-existing change entry (no label, no database rows) must come
    // back in memory, and the rollback must remove the name/purpose rows
    // CreateP2MR's SetAddressBook added.
    {
        LOCK(wallet->cs_wallet);
        wallet->m_address_book[dest]; // plant an in-memory change entry
        auto funded = FundP2MR(*wallet, leaves, CENT, "overwrite", /*subtract_fee_from_amount=*/false, coin_control, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(!funded);
        const auto* entry = wallet->FindAddressBookEntry(dest, /*allow_change=*/true);
        BOOST_REQUIRE(entry);
        BOOST_CHECK(entry->IsChange());
        BOOST_CHECK(!entry->purpose);
    }
    reload();
    {
        LOCK(wallet->cs_wallet);
        // The change entry had no rows of its own, so after a reload the
        // destination must be absent entirely: leaked rows would make it
        // reappear as a labelled entry.
        BOOST_CHECK(!wallet->FindAddressBookEntry(dest, /*allow_change=*/true));
        BOOST_CHECK(wallet->ListP2MRMetadata().empty());
    }

    // A pre-existing tree that the failing call reused stays.
    {
        LOCK(wallet->cs_wallet);
        auto created = CreateP2MR(*wallet, leaves, "keep", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(created);
        BOOST_CHECK(!created->reused);
        auto funded = FundP2MR(*wallet, leaves, CENT, "keep", /*subtract_fee_from_amount=*/false, coin_control, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(!funded);
    }
    reload();
    {
        LOCK(wallet->cs_wallet);
        BOOST_CHECK_EQUAL(ListP2MR(*wallet).size(), 1U);
        BOOST_CHECK(wallet->FindAddressBookEntry(dest));
        const auto metadata = wallet->ListP2MRMetadata();
        BOOST_REQUIRE_EQUAL(metadata.size(), 1U);
        std::string value;
        BOOST_CHECK(wallet->GetP2MRMetadata(dest, std::get<1>(metadata.front()), value));

BOOST_FIXTURE_TEST_CASE(wallet_is_mine_recognizes_valid_p2mr_metadata, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const auto leaves = MakeOpTrueTree();

    auto builder = BuildP2MRTreeChecked(leaves);
    BOOST_REQUIRE(builder);
    const CScript tracked_script = GetScriptForDestination(builder->GetOutput());

    BOOST_CHECK_EQUAL(wallet->IsMine(tracked_script), ISMINE_NO);

    auto created = CreateP2MR(*wallet, leaves, "tracked", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
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

BOOST_FIXTURE_TEST_CASE(wallet_is_mine_tracks_unowned_p2mr_metadata_as_watchonly, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);

    CKey external_key;
    external_key.MakeNewKey(/*fCompressedIn=*/true);
    const auto leaves = MakeXOnlyChecksigTree(XOnlyPubKey{external_key.GetPubKey()});

    LOCK(wallet->cs_wallet);
    auto created = CreateP2MR(*wallet, leaves, "external", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(created);

    BOOST_CHECK(IsTrackedP2MRScript(*wallet, created->script_pub_key));
    BOOST_CHECK_EQUAL(wallet->IsMine(created->script_pub_key), ISMINE_WATCH_ONLY);

    CKey recipient_key;
    recipient_key.MakeNewKey(/*fCompressedIn=*/true);
    const auto spend = CreateP2MRSpend(*wallet, created->id, PKHash(recipient_key.GetPubKey()), CENT, 1000);
    BOOST_CHECK(!spend);
}

// Tracked P2MR metadata is looked up by the script's destination. Scripts that
// only resemble the tracked one must not match it.
BOOST_FIXTURE_TEST_CASE(tracked_p2mr_lookup_matches_only_its_script, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);

    CKey external_key;
    external_key.MakeNewKey(/*fCompressedIn=*/true);
    const auto leaves = MakeXOnlyChecksigTree(XOnlyPubKey{external_key.GetPubKey()});

    LOCK(wallet->cs_wallet);
    auto created = CreateP2MR(*wallet, leaves, "external", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(created);
    const CScript& tracked = created->script_pub_key;
    int version;
    std::vector<unsigned char> program;
    BOOST_REQUIRE(tracked.IsWitnessProgram(version, program));
    BOOST_REQUIRE_EQUAL(version, 2);
    BOOST_REQUIRE_EQUAL(program.size(), 32U);

    BOOST_CHECK(IsTrackedP2MRScript(*wallet, tracked));
    BOOST_CHECK_EQUAL(wallet->IsMine(tracked), ISMINE_WATCH_ONLY);

    std::vector<unsigned char> other{program};
    other[0] ^= 1;
    const CScript untracked = CScript() << OP_2 << other;
    const CScript short_program = CScript() << OP_2 << std::vector<unsigned char>(program.begin(), program.end() - 1);
    const CScript v1_program = CScript() << OP_1 << program;
    for (const CScript& script : {untracked, short_program, v1_program}) {
        BOOST_CHECK(!IsTrackedP2MRScript(*wallet, script));
        BOOST_CHECK_EQUAL(wallet->IsMine(script), ISMINE_NO);
    }
}

// GetSolvingProvider and FillPSBT look up the entry for one coin's script and
// build a provider from the entries under that script's destination only.
BOOST_FIXTURE_TEST_CASE(p2mr_script_lookup_and_provider_read_one_destination, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);

    std::vector<P2MRCreated> created;
    for (int i = 0; i < 3; ++i) {
        CKey key;
        key.MakeNewKey(/*fCompressedIn=*/true);
        auto entry = CreateP2MR(*wallet, MakeXOnlyChecksigTree(XOnlyPubKey{key.GetPubKey()}), "external", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(entry);
        created.push_back(*entry);
    }

    for (const P2MRCreated& entry : created) {
        const auto found = GetP2MRByScript(*wallet, entry.script_pub_key);
        BOOST_REQUIRE(found);
        BOOST_CHECK_EQUAL(found->id, entry.id);
        BOOST_CHECK(found->dest == entry.dest);

        const FlatSigningProvider provider{BuildP2MRSigningProviderForDestination(*wallet, found->dest)};
        BOOST_CHECK_EQUAL(provider.p2mr_trees.size(), 1U);
        BOOST_CHECK_EQUAL(provider.p2mr_trees.count(std::get<WitnessV2P2MR>(entry.dest)), 1U);

        // The wallet has no ScriptPubKeyMan, so this is the P2MR fallback.
        const auto solving = wallet->GetSolvingProvider(entry.script_pub_key);
        BOOST_REQUIRE(solving);
        P2MRBuilder builder;
        BOOST_CHECK(solving->GetP2MRBuilder(std::get<WitnessV2P2MR>(entry.dest), builder));
    }

    int version;
    std::vector<unsigned char> program;
    BOOST_REQUIRE(created[0].script_pub_key.IsWitnessProgram(version, program));
    program[0] ^= 1;
    for (const CScript& script : {CScript() << OP_2 << program, CScript() << OP_1 << program, CScript{}}) {
        BOOST_CHECK(!GetP2MRByScript(*wallet, script));
        BOOST_CHECK(!wallet->GetSolvingProvider(script));
    }

    // CreateP2MR stores metadata only under a WitnessV2P2MR destination.
    // Metadata under any other destination is not a P2MR entry for its script.
    CKey other_key;
    other_key.MakeNewKey(/*fCompressedIn=*/true);
    const CTxDestination pkh_dest{PKHash{other_key.GetPubKey()}};
    UniValue pkh_meta(UniValue::VOBJ);
    pkh_meta.pushKV("id", "under-pkh");
    pkh_meta.pushKV("tree", P2MRTreeToUniValue(MakeOpTrueTree()));
    WalletBatch batch(wallet->GetDatabase(), /*fFlushOnClose=*/false);
    BOOST_REQUIRE(wallet->SetP2MRMetadata(batch, pkh_dest, "under-pkh", pkh_meta.write()));
    const CScript pkh_script{GetScriptForDestination(pkh_dest)};
    BOOST_CHECK(!GetP2MRByScript(*wallet, pkh_script));
    BOOST_CHECK(!wallet->GetSolvingProvider(pkh_script));
}

// Three records under one destination: first a tree that does not build, then
// the owned tree, then a tree that builds to another root. The lookup returns
// the first record, and the provider has the owned tree.
BOOST_FIXTURE_TEST_CASE(p2mr_provider_skips_bad_records_under_the_same_destination, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    wallet->SetupLegacyScriptPubKeyMan();

    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    auto created = ImportDilithiumKeyAsP2MR(*wallet, key, "owned");
    BOOST_REQUIRE(created);

    UniValue corrupt_meta(UniValue::VOBJ);
    corrupt_meta.pushKV("id", "corrupt-first");
    corrupt_meta.pushKV("address", created->address);
    corrupt_meta.pushKV("created_at", int64_t{1});
    corrupt_meta.pushKV("state", "created");
    corrupt_meta.pushKV("tree", UniValue(UniValue::VARR));
    WalletBatch batch(wallet->GetDatabase(), /*fFlushOnClose=*/false);
    // "!" sorts before the hex digits of a generated id, and "~" after them.
    BOOST_REQUIRE(wallet->SetP2MRMetadata(batch, created->dest, "!corrupt", corrupt_meta.write()));
    UniValue other_root_meta{corrupt_meta};
    other_root_meta.pushKV("id", "other-root-last");
    other_root_meta.pushKV("tree", P2MRTreeToUniValue(MakeOpTrueTree()));
    BOOST_REQUIRE(wallet->SetP2MRMetadata(batch, created->dest, "~other-root", other_root_meta.write()));
    BOOST_REQUIRE_EQUAL(ListP2MR(*wallet).size(), 3U);

    const auto found = GetP2MRByScript(*wallet, created->script_pub_key);
    BOOST_REQUIRE(found);
    BOOST_CHECK_EQUAL(found->id, "corrupt-first");
    BOOST_CHECK(found->tree.empty());

    FlatSigningProvider provider{BuildP2MRSigningProviderForDestination(*wallet, found->dest)};
    const WitnessV2P2MR& output{std::get<WitnessV2P2MR>(created->dest)};
    BOOST_REQUIRE_EQUAL(provider.p2mr_trees.count(output), 1U);
    BOOST_CHECK(provider.p2mr_trees.at(output).GetOutput() == output);
    BOOST_CHECK_EQUAL(provider.dilithium_keys.size(), 1U);

    // The coin selection path.
    const auto solving = wallet->GetSolvingProvider(created->script_pub_key);
    BOOST_REQUIRE(solving);
    P2MRBuilder builder;
    BOOST_REQUIRE(solving->GetP2MRBuilder(output, builder));
    BOOST_CHECK(builder.GetOutput() == output);
}

// CWallet::SignTransaction builds its P2MR provider from the destinations of
// the coins being spent, not from every entry.
BOOST_FIXTURE_TEST_CASE(sign_transaction_reads_the_spent_p2mr_entry, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    wallet->SetupLegacyScriptPubKeyMan();

    std::vector<P2MRCreated> created;
    for (int i = 0; i < 3; ++i) {
        CDilithiumKey key;
        BOOST_REQUIRE(key.MakeNewKey());
        auto entry = ImportDilithiumKeyAsP2MR(*wallet, key, "owned");
        BOOST_REQUIRE(entry);
        created.push_back(*entry);
    }

    // Two inputs at one destination and one at another; created[0] is not spent.
    std::map<COutPoint, Coin> coins;
    CMutableTransaction tx;
    for (const auto& [n, index] : std::vector<std::pair<uint32_t, size_t>>{{0, 1}, {1, 1}, {2, 2}}) {
        const COutPoint prevout{uint256::ONE, n};
        coins[prevout] = Coin(CTxOut{COIN, created[index].script_pub_key}, /*nHeightIn=*/1, /*fCoinBaseIn=*/false);
        tx.vin.emplace_back(prevout);
    }
    tx.vout.emplace_back(COIN, created[0].script_pub_key);

    std::map<int, bilingual_str> input_errors;
    BOOST_CHECK(wallet->SignTransaction(tx, coins, SIGHASH_DEFAULT, input_errors));
    BOOST_CHECK(input_errors.empty());
    for (const CTxIn& txin : tx.vin) BOOST_CHECK(!txin.scriptWitness.IsNull());
}

// importdilithiumkey stores the key and then CreateP2MR finds the existing
// tree and returns before SetP2MRMetadata. A cached watch-only hit would
// still be there, and coin selection would skip the output.
BOOST_FIXTURE_TEST_CASE(imported_dilithium_key_upgrades_cached_watchonly_p2mr, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    wallet->SetupLegacyScriptPubKeyMan();

    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    const CScript leaf_script = CScript() << ToByteVector(key.GetPubKey()) << OP_CHECKSIGDILITHIUM;
    const std::vector<P2MRTreeLeaf> leaves{{
        /*depth=*/0,
        TAPROOT_LEAF_TAPSCRIPT,
        {leaf_script.begin(), leaf_script.end()},
    }};

    auto created = CreateP2MR(*wallet, leaves, "watch", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(created);
    BOOST_CHECK_EQUAL(wallet->IsMine(created->script_pub_key), ISMINE_WATCH_ONLY);

    auto imported = ImportDilithiumKeyAsP2MR(*wallet, key, "imported");
    BOOST_REQUIRE(imported);
    BOOST_CHECK_EQUAL(imported->id, created->id);
    BOOST_CHECK_EQUAL(wallet->IsMine(created->script_pub_key), ISMINE_SPENDABLE);
}

BOOST_FIXTURE_TEST_CASE(tracked_balance_deduplicates_legacy_duplicate_metadata, BasicTestingSetup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const auto leaves = MakeOpTrueTree();

    auto created = CreateP2MR(*wallet, leaves, "original", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
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

BOOST_FIXTURE_TEST_CASE(create_p2mr_spend_aggregates_inputs_and_reports_effective_fee, TestChain100Setup)
{
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    const auto leaves = MakeOpTrueTree();

    auto created = CreateP2MR(*wallet, leaves, "aggregate", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
    BOOST_REQUIRE(created);

    const CBlockIndex* tip = WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain().Tip());
    BOOST_REQUIRE(tip);
    wallet->SetLastBlockProcessed(tip->nHeight, tip->GetBlockHash());
    auto add_confirmed_utxo = [&](CAmount amount, uint32_t lock_time) {
        CMutableTransaction tx;
        tx.nVersion = 2;
        tx.nLockTime = lock_time;
        tx.vout.emplace_back(amount, created->script_pub_key);
        const auto added = wallet->AddToWallet(
            MakeTransactionRef(std::move(tx)),
            TxStateConfirmed{tip->GetBlockHash(), tip->nHeight, /*index=*/0});
        BOOST_REQUIRE(added);
    };
    add_confirmed_utxo(20 * CENT, 1);
    add_confirmed_utxo(30 * CENT + 1100, 2);

    CKey recipient_key;
    recipient_key.MakeNewKey(/*fCompressedIn=*/true);
    auto spend = CreateP2MRSpend(*wallet, created->id, PKHash(recipient_key.GetPubKey()), 50 * CENT, 1000);
    BOOST_REQUIRE(spend);

    BOOST_CHECK_EQUAL(spend->inputs.size(), 2U);
    BOOST_CHECK_EQUAL(spend->tx.vin.size(), 2U);
    BOOST_CHECK_EQUAL(spend->input_amount, 50 * CENT + 1100);
    BOOST_CHECK_EQUAL(spend->effective_fee, 1100);
    BOOST_CHECK(!spend->has_change);
    BOOST_REQUIRE_EQUAL(spend->tx.vout.size(), 1U);
}

BOOST_FIXTURE_TEST_CASE(create_p2mr_spend_change_stays_p2mr, TestChain100Setup)
{
    const CBlockIndex* tip = WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain().Tip());
    BOOST_REQUIRE(tip);

    CKey recipient_key;
    recipient_key.MakeNewKey(/*fCompressedIn=*/true);
    const CTxDestination recipient{PKHash(recipient_key.GetPubKey())};
    const CAmount send{CENT};
    const CAmount fee{1000};
    const CAmount dust_gate{546};

    // One confirmed output on a fresh OP_TRUE P2MR entry.
    const auto fund = [&](CWallet& wallet, CAmount amount) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet) {
        wallet.SetLastBlockProcessed(tip->nHeight, tip->GetBlockHash());
        auto created = CreateP2MR(wallet, MakeOpTrueTree(), "change-source", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(created);
        CMutableTransaction funding;
        funding.nVersion = 2;
        funding.nLockTime = 1;
        funding.vout.emplace_back(amount, created->script_pub_key);
        BOOST_REQUIRE(wallet.AddToWallet(MakeTransactionRef(std::move(funding)), TxStateConfirmed{tip->GetBlockHash(), tip->nHeight, /*index=*/0}));
        return *created;
    };

    // Every case gets its own wallet: the OP_TRUE entries share one script, so
    // a second funding in the same wallet would change which coin is picked.
    const auto make_wallet = [&](bool descriptors) {
        auto wallet = MakeP2MRTestWallet(*m_node.chain);
        LOCK(wallet->cs_wallet);
        if (descriptors) {
            wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
            wallet->SetupDescriptorScriptPubKeyMans();
        } else {
            wallet->SetupLegacyScriptPubKeyMan();
            LegacyScriptPubKeyMan* spk_man = wallet->GetLegacyScriptPubKeyMan();
            BOOST_REQUIRE(spk_man);
            BOOST_REQUIRE(WITH_LOCK(spk_man->cs_KeyStore, return spk_man->SetupGeneration(true)));
        }
        return wallet;
    };

    for (const bool descriptors : {true, false}) {
        BOOST_TEST_MESSAGE(strprintf("descriptors=%d", descriptors));
        {
            auto wallet = make_wallet(descriptors);
            LOCK(wallet->cs_wallet);
            const auto at_gate = fund(*wallet, send + fee + dust_gate);
            auto no_change = CreateP2MRSpend(*wallet, at_gate.id, recipient, send, fee);
            BOOST_REQUIRE_MESSAGE(no_change, util::ErrorString(no_change).original);
            BOOST_CHECK(!no_change->has_change);
            BOOST_CHECK_EQUAL(no_change->tx.vout.size(), 1U);
            BOOST_CHECK_EQUAL(no_change->effective_fee, fee + dust_gate);
        }

        auto wallet = make_wallet(descriptors);
        LOCK(wallet->cs_wallet);
        const auto over_gate = fund(*wallet, send + fee + dust_gate + 1);
        std::vector<CScript> change_scripts;
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto spend = CreateP2MRSpend(*wallet, over_gate.id, recipient, send, fee);
            BOOST_REQUIRE_MESSAGE(spend, util::ErrorString(spend).original);
            BOOST_REQUIRE(spend->has_change);
            BOOST_CHECK_EQUAL(spend->change_amount, dust_gate + 1);
            BOOST_REQUIRE_EQUAL(spend->tx.vout.size(), 2U);
            const CTxOut& change_out = spend->tx.vout[1];
            BOOST_CHECK_EQUAL(change_out.nValue, dust_gate + 1);

            std::vector<std::vector<unsigned char>> solutions;
            const TxoutType change_type{Solver(change_out.scriptPubKey, solutions)};
            BOOST_CHECK_MESSAGE(change_type == TxoutType::WITNESS_V2_P2MR,
                                strprintf("change output is %s", GetTxnOutputType(change_type)));
            BOOST_CHECK(change_out.scriptPubKey != over_gate.script_pub_key);
            BOOST_CHECK(wallet->IsMine(change_out.scriptPubKey) == ISMINE_SPENDABLE);
            BOOST_CHECK(OutputIsChange(*wallet, change_out));
            change_scripts.push_back(change_out.scriptPubKey);
        }
        // Each spend gets a fresh change key.
        BOOST_CHECK(change_scripts[0] != change_scripts[1]);
    }

    for (const bool descriptors : {true, false}) {
        BOOST_TEST_MESSAGE(strprintf("locked, descriptors=%d", descriptors));
        // A locked wallet cannot derive the change key, and says so instead of
        // failing inside key derivation.
        auto wallet = make_wallet(descriptors);
        const auto over_gate = WITH_LOCK(wallet->cs_wallet, return fund(*wallet, send + fee + dust_gate + 1));
        BOOST_REQUIRE(wallet->EncryptWallet("pass"));
        BOOST_REQUIRE(wallet->IsLocked());
        {
            LOCK(wallet->cs_wallet);
            auto refused = CreateP2MRSpend(*wallet, over_gate.id, recipient, send, fee);
            BOOST_REQUIRE(!refused);
            BOOST_CHECK(util::ErrorString(refused).original.find("walletpassphrase") != std::string::npos);
        }
        BOOST_REQUIRE(wallet->Unlock("pass"));
        LOCK(wallet->cs_wallet);
        auto spend = CreateP2MRSpend(*wallet, over_gate.id, recipient, send, fee);
        BOOST_REQUIRE_MESSAGE(spend, util::ErrorString(spend).original);
        BOOST_REQUIRE_EQUAL(spend->tx.vout.size(), 2U);
        std::vector<std::vector<unsigned char>> solutions;
        BOOST_CHECK(Solver(spend->tx.vout[1].scriptPubKey, solutions) == TxoutType::WITNESS_V2_P2MR);
    }
}

BOOST_FIXTURE_TEST_CASE(create_p2mr_spend_pays_given_change_address, TestChain100Setup)
{
    const CBlockIndex* tip = WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain().Tip());
    BOOST_REQUIRE(tip);
    CKey recipient_key;
    recipient_key.MakeNewKey(/*fCompressedIn=*/true);
    const CTxDestination recipient{PKHash(recipient_key.GetPubKey())};
    const CAmount send{CENT};
    const CAmount fee{1000};
    const CTxDestination change_to{WitnessV2P2MR{std::vector<unsigned char>(WitnessV2P2MR::SIZE, 0x42)}};

    for (const bool watch_only : {true, false}) {
        BOOST_TEST_MESSAGE(strprintf("watch_only=%d", watch_only));
        auto wallet = MakeP2MRTestWallet(*m_node.chain);
        LOCK(wallet->cs_wallet);
        wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
        if (watch_only) {
            wallet->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
        } else {
            wallet->SetupDescriptorScriptPubKeyMans();
        }
        wallet->SetLastBlockProcessed(tip->nHeight, tip->GetBlockHash());
        auto created = CreateP2MR(*wallet, MakeOpTrueTree(), "source", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(created);
        CMutableTransaction funding;
        funding.nVersion = 2;
        funding.nLockTime = 1;
        funding.vout.emplace_back(20 * CENT, created->script_pub_key);
        BOOST_REQUIRE(wallet->AddToWallet(MakeTransactionRef(std::move(funding)), TxStateConfirmed{tip->GetBlockHash(), tip->nHeight, /*index=*/0}));

        if (watch_only) {
            // Without private keys there is no Dilithium key to derive change
            // from, and the change is not quietly moved to witness v0.
            auto refused = CreateP2MRSpend(*wallet, created->id, recipient, send, fee);
            BOOST_REQUIRE(!refused);
            BOOST_CHECK(util::ErrorString(refused).original.find("Pass change_address") != std::string::npos);
        }

        auto spend = CreateP2MRSpend(*wallet, created->id, recipient, send, fee, change_to);
        BOOST_REQUIRE_MESSAGE(spend, util::ErrorString(spend).original);
        BOOST_REQUIRE(spend->has_change);
        BOOST_CHECK(spend->change_dest == change_to);
        BOOST_REQUIRE_EQUAL(spend->tx.vout.size(), 2U);
        BOOST_CHECK(spend->tx.vout[1].scriptPubKey == GetScriptForDestination(change_to));
        BOOST_CHECK_EQUAL(spend->tx.vout[1].nValue, 20 * CENT - send - fee);

        auto wrong = CreateP2MRSpend(*wallet, created->id, recipient, send, fee, recipient);
        BOOST_REQUIRE(!wrong);
        BOOST_CHECK(util::ErrorString(wrong).original.find("change_address must be a P2MR address") != std::string::npos);
    }

namespace {
// Every shipped chain, including testnet, is P2MR-only from height 1.
// Unschedule it in this fixture so the key-hash match, which applies only
// where those payments can still be spent, has a chain to run on.
struct LegacyDilithiumPaymentsSetup : public BasicTestingSetup {
    LegacyDilithiumPaymentsSetup()
        : BasicTestingSetup{ChainType::BTQTEST, /*extra_args=*/{"-testnetdilithiump2mrheight=2147483647"}} {}
};
} // namespace

BOOST_FIXTURE_TEST_CASE(dilithium_key_hash_matches_where_still_spendable, LegacyDilithiumPaymentsSetup)
{
    BOOST_REQUIRE(LegacyDilithiumBase58PaymentsAllowed());
    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    const CDilithiumPubKey pubkey{key.GetPubKey()};
    CDilithiumKey other;
    BOOST_REQUIRE(other.MakeNewKey());

    BOOST_CHECK(DilithiumPubKeyMatchesDestination(pubkey, DilithiumPKHash{pubkey}));
    BOOST_CHECK(DilithiumPubKeyMatchesDestination(pubkey, DilithiumWitnessV0KeyHash{pubkey}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(other.GetPubKey(), DilithiumPKHash{pubkey}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(other.GetPubKey(), DilithiumWitnessV0KeyHash{pubkey}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, PKHash{uint160{pubkey.GetID()}}));
}

BOOST_FIXTURE_TEST_CASE(dilithium_pubkey_matches_only_its_own_destinations, TestingSetup)
{
    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    const CDilithiumPubKey pubkey{key.GetPubKey()};
    CDilithiumKey other;
    BOOST_REQUIRE(other.MakeNewKey());
    const CDilithiumPubKey other_pubkey{other.GetPubKey()};

    // The one leaf is <OP_PUSHDATA2 1312-byte key> OP_CHECKSIGDILITHIUM, tapscript
    // version, depth 0, and the address commits to that leaf's hash alone.
    const auto leaves = SingleKeyDilithiumLeaves(pubkey);
    BOOST_REQUIRE_EQUAL(leaves.size(), 1U);
    BOOST_CHECK_EQUAL(leaves[0].depth, 0);
    BOOST_CHECK_EQUAL(leaves[0].leaf_version, 0xc0);
    std::vector<unsigned char> expected_leaf{0x4d, 0x20, 0x05};
    expected_leaf.insert(expected_leaf.end(), pubkey.begin(), pubkey.end());
    expected_leaf.push_back(0xbb);
    BOOST_CHECK(leaves[0].script == expected_leaf);
    const WitnessV2P2MR single{ComputeTapleafHash(0xc0, expected_leaf)};
    auto single_tree = BuildP2MRTreeChecked(leaves);
    BOOST_REQUIRE(single_tree);
    BOOST_CHECK(single_tree->GetOutput() == single);

    BOOST_CHECK(DilithiumPubKeyMatchesDestination(pubkey, single));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(other_pubkey, single));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(CDilithiumPubKey{}, single));

    // P2MR-only from height 1 on this chain: nobody can spend coins sent to a
    // Dilithium key hash, so a signature cannot prove control of one.
    BOOST_REQUIRE(!LegacyDilithiumBase58PaymentsAllowed());
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, DilithiumPKHash{pubkey}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, DilithiumWitnessV0KeyHash{pubkey}));

    // The key's own hash under a type that is not a Dilithium key hash.
    const uint160 key_hash{pubkey.GetID()};
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, PKHash{key_hash}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, WitnessV0KeyHash{key_hash}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, ScriptHash{key_hash}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, DilithiumScriptHash{key_hash}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, WitnessV1Taproot{XOnlyPubKey{Span<const unsigned char>{single.m_merkle_root, WitnessV2P2MR::SIZE}}}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, WitnessUnknown{3, std::vector<unsigned char>(single.m_merkle_root, single.m_merkle_root + 32)}));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, CNoDestination{}));

    // The key's leaf beside a second spend path is not the key's address.
    std::vector<P2MRTreeLeaf> with_escape{SingleKeyDilithiumLeaves(pubkey)};
    with_escape[0].depth = 1;
    with_escape.push_back(P2MRTreeLeaf{/*depth=*/1, TAPROOT_LEAF_TAPSCRIPT, {static_cast<unsigned char>(OP_TRUE)}});
    auto escape_tree = BuildP2MRTreeChecked(with_escape);
    BOOST_REQUIRE(escape_tree);
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, escape_tree->GetOutput()));

    // Nor is the same leaf script under another leaf version.
    std::vector<P2MRTreeLeaf> other_version{SingleKeyDilithiumLeaves(pubkey)};
    other_version[0].leaf_version = 0xc2;
    auto version_tree = BuildP2MRTreeChecked(other_version);
    BOOST_REQUIRE(version_tree);
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, version_tree->GetOutput()));

    // Receive destinations the wallet creates match the key in their leaf.
    auto wallet = MakeP2MRTestWallet(*m_node.chain);
    LOCK(wallet->cs_wallet);
    wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    wallet->SetupDescriptorScriptPubKeyMans();

    const auto received = CreateDilithiumP2MRReceive(*wallet, "receive");
    BOOST_REQUIRE_MESSAGE(received, util::ErrorString(received).original);
    const auto entry = GetP2MR(*wallet, received->id);
    BOOST_REQUIRE(entry);
    BOOST_REQUIRE_EQUAL(entry->tree.size(), 1U);
    const auto policy = ParseP2MRDilithiumLeaf(CScript{entry->tree[0].script.begin(), entry->tree[0].script.end()});
    BOOST_REQUIRE_EQUAL(policy.pubkeys.size(), 1U);
    BOOST_CHECK(DilithiumPubKeyMatchesDestination(policy.pubkeys[0], received->dest));
    BOOST_CHECK(!DilithiumPubKeyMatchesDestination(pubkey, received->dest));

    const auto imported = ImportDilithiumKeyAsP2MR(*wallet, key, "import");
    BOOST_REQUIRE_MESSAGE(imported, util::ErrorString(imported).original);
    BOOST_CHECK(imported->dest == CTxDestination{single});
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

// A leaf of OP_2 <32-byte-push> is itself a witness v2 program. Signing it
// used to re-enter SignP2MR over the same p2mr_spenddata.scripts map and
// crash from stack exhaustion. It must fail to sign instead.
BOOST_FIXTURE_TEST_CASE(produce_signature_rejects_p2mr_program_leaf, BasicTestingSetup)
{
    const std::vector<unsigned char> inner_program(32, 0xab);
    const CScript leaf_script = CScript() << OP_2 << inner_program;
    const std::vector<unsigned char> leaf_bytes{leaf_script.begin(), leaf_script.end()};

    P2MRBuilder builder;
    builder.Add(/*depth=*/0, leaf_bytes, TAPROOT_LEAF_TAPSCRIPT).Finalize();
    BOOST_REQUIRE(builder.IsValid());
    BOOST_REQUIRE(builder.IsComplete());

    const WitnessV2P2MR output = builder.GetOutput();
    const CScript script_pubkey = GetScriptForDestination(output);
    const CAmount amount = COIN;

    FlatSigningProvider provider;
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
    BOOST_CHECK(!ProduceSignature(provider, creator, script_pubkey, sigdata));
    BOOST_CHECK(!sigdata.complete);
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

    // Ask for the key rather than for a legacy Dilithium destination: the
    // destination is refused on this (P2MR-only) chain, and this case is about
    // the leaf built from the key, not about base58 Dilithium addresses.
    const util::Result<CDilithiumPubKey> pubkey = keyman->GenerateNewDilithiumKey();
    BOOST_REQUIRE(pubkey);
    const DilithiumPKHash keyhash{*pubkey};
    const CScript leaf_script = GetScriptForDestination(CTxDestination{keyhash});

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

BOOST_FIXTURE_TEST_CASE(build_signing_provider_exports_descriptor_xonly_p2mr_leaf, BasicTestingSetup)
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

    const util::Result<CTxDestination> dest = keyman->GetNewDestination(OutputType::LEGACY);
    BOOST_REQUIRE(dest);
    BOOST_REQUIRE(std::holds_alternative<PKHash>(*dest));
    const PKHash keyhash = std::get<PKHash>(*dest);
    const CScript script_pub_key = GetScriptForDestination(*dest);
    std::unique_ptr<SigningProvider> solving_provider = keyman->GetSolvingProvider(script_pub_key);
    BOOST_REQUIRE(solving_provider);
    CPubKey pubkey;
    BOOST_REQUIRE(solving_provider->GetPubKey(ToKeyID(keyhash), pubkey));
    const XOnlyPubKey xonly_pubkey{pubkey};

    const std::vector<P2MRTreeLeaf> leaves = MakeXOnlyChecksigTree(xonly_pubkey);
    const CScript leaf_script{leaves[0].script.begin(), leaves[0].script.end()};

    P2MRCreated created;
    FlatSigningProvider provider;
    {
        LOCK(wallet.cs_wallet);
        auto created_res = CreateP2MR(wallet, leaves, "descriptor-xonly", /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
        BOOST_REQUIRE(created_res);
        created = std::move(*created_res);
        BOOST_CHECK_EQUAL(wallet.IsMine(created.script_pub_key), ISMINE_SPENDABLE);
        provider = BuildP2MRSigningProvider(wallet, created.id);
    }

    CKey provider_key;
    BOOST_REQUIRE(provider.GetKeyByXOnly(xonly_pubkey, provider_key));
    BOOST_CHECK(XOnlyPubKey{provider_key.GetPubKey()} == xonly_pubkey);

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
    BOOST_REQUIRE_EQUAL(sigdata.scriptWitness.stack.size(), 3U);
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[0].size(), 64U);
    BOOST_CHECK_EQUAL(HexStr(sigdata.scriptWitness.stack[1]), HexStr(leaf_script));

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
