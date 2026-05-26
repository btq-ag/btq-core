// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>
#include <util/strencodings.h>
#include <wallet/p2mr.h>

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

BOOST_AUTO_TEST_CASE(build_rejects_empty_leaves_vector)
{
    std::vector<P2MRTreeLeaf> empty;
    auto built = BuildP2MRTreeChecked(empty);
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

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
