// Copyright (c) 2021 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <script/script.h>
#include <util/strencodings.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(script_parse_tests)
BOOST_AUTO_TEST_CASE(parse_script)
{
    const std::vector<std::pair<std::string,std::string>> IN_OUT{
        // {IN: script string , OUT: hex string }
        {"", ""},
        {"0", "00"},
        {"1", "51"},
        {"2", "52"},
        {"3", "53"},
        {"4", "54"},
        {"5", "55"},
        {"6", "56"},
        {"7", "57"},
        {"8", "58"},
        {"9", "59"},
        {"10", "5a"},
        {"11", "5b"},
        {"12", "5c"},
        {"13", "5d"},
        {"14", "5e"},
        {"15", "5f"},
        {"16", "60"},
        {"17", "0111"},
        {"-9", "0189"},
        {"0x17", "17"},
        {"'17'", "023137"},
        {"ELSE", "67"},
        {"NOP10", "b9"},
    };
    std::string all_in;
    std::string all_out;
    for (const auto& [in, out] : IN_OUT) {
        BOOST_CHECK_EQUAL(HexStr(ParseScript(in)), out);
        all_in += " " + in + " ";
        all_out += out;
    }
    BOOST_CHECK_EQUAL(HexStr(ParseScript(all_in)), all_out);

    BOOST_CHECK_EXCEPTION(ParseScript("11111111111111111111"), std::runtime_error, HasReason("script parse error: decimal numeric value only allowed in the range -0xFFFFFFFF...0xFFFFFFFF"));
    BOOST_CHECK_EXCEPTION(ParseScript("11111111111"), std::runtime_error, HasReason("script parse error: decimal numeric value only allowed in the range -0xFFFFFFFF...0xFFFFFFFF"));
    BOOST_CHECK_EXCEPTION(ParseScript("OP_NOT_AN_OPCODE"), std::runtime_error, HasReason("script parse error: unknown opcode"));

    // Upstream asserts OP_CHECKSIGADD is unknown here, because at Bitcoin's
    // MAX_OPCODE (OP_NOP10, 0xb9) it sits outside the table OpCodeParser builds.
    // BTQ added five Dilithium opcodes at 0xbb-0xbf and moved MAX_OPCODE to
    // OP_DILITHIUM_PUBKEY, which brings 0xba back into range. The same constant
    // bounds CScript::HasValidOps(), so this widening is not confined to parsing.
    BOOST_CHECK_EQUAL(HexStr(ParseScript("OP_CHECKSIGADD")), "ba");
    BOOST_CHECK_EQUAL(HexStr(ParseScript("OP_DILITHIUM_PUBKEY")), "bf");
}
BOOST_AUTO_TEST_SUITE_END()
