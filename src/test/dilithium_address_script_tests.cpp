// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <crypto/dilithium_key.h>
#include <key_io.h>
#include <outputtype.h>
#include <script/script.h>
#include <script/solver.h>
#include <script/interpreter.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_address_script_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(dilithium_destination_types)
{
    // Test Dilithium destination type creation
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Test DilithiumPubKeyDestination
    DilithiumPubKeyDestination pubkey_dest(pubkey);
    BOOST_CHECK(pubkey_dest.GetPubKey() == pubkey);
    
    // Test DilithiumPKHash
    DilithiumPKHash pk_hash(pubkey);
    BOOST_CHECK(pk_hash == DilithiumPKHash(pubkey.GetID()));
    
    // Test DilithiumScriptHash
    CScript script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    DilithiumScriptHash script_hash(script);
    BOOST_CHECK(script_hash == DilithiumScriptHash(Hash160(script)));
    
    // Test DilithiumWitnessV0KeyHash
    DilithiumWitnessV0KeyHash witness_key_hash(pubkey);
    BOOST_CHECK(witness_key_hash == DilithiumWitnessV0KeyHash(pubkey.GetID()));
    
    // Test DilithiumWitnessV0ScriptHash
    DilithiumWitnessV0ScriptHash witness_script_hash(script);
    uint256 script_hash_uint256;
    CSHA256().Write(script.data(), script.size()).Finalize(script_hash_uint256.begin());
    BOOST_CHECK(witness_script_hash == DilithiumWitnessV0ScriptHash(script_hash_uint256));
}

BOOST_AUTO_TEST_CASE(dilithium_address_encoding)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Test DilithiumPKHash address encoding
    DilithiumPKHash pk_hash(pubkey);
    std::string encoded_address = EncodeDestination(pk_hash);
    BOOST_CHECK(!encoded_address.empty());
    
    // Test address decoding
    CTxDestination decoded_dest = DecodeDestination(encoded_address);
    BOOST_CHECK(std::holds_alternative<DilithiumPKHash>(decoded_dest));
    BOOST_CHECK(std::get<DilithiumPKHash>(decoded_dest) == pk_hash);
    
    // Test DilithiumScriptHash address encoding
    CScript script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    DilithiumScriptHash script_hash(script);
    std::string encoded_script_address = EncodeDestination(script_hash);
    BOOST_CHECK(!encoded_script_address.empty());
    
    // Test script address decoding
    CTxDestination decoded_script_dest = DecodeDestination(encoded_script_address);
    BOOST_CHECK(std::holds_alternative<DilithiumScriptHash>(decoded_script_dest));
    BOOST_CHECK(std::get<DilithiumScriptHash>(decoded_script_dest) == script_hash);
}

BOOST_AUTO_TEST_CASE(dilithium_bech32_address_encoding)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Test DilithiumWitnessV0KeyHash Bech32 encoding
    DilithiumWitnessV0KeyHash witness_key_hash(pubkey);
    std::string encoded_witness_address = EncodeDestination(witness_key_hash);
    BOOST_CHECK(!encoded_witness_address.empty());
    BOOST_CHECK(encoded_witness_address.find("dbtc") == 0); // Should start with Dilithium Bech32 HRP
    
    // Test witness address decoding
    CTxDestination decoded_witness_dest = DecodeDestination(encoded_witness_address);
    BOOST_CHECK(std::holds_alternative<DilithiumWitnessV0KeyHash>(decoded_witness_dest));
    BOOST_CHECK(std::get<DilithiumWitnessV0KeyHash>(decoded_witness_dest) == witness_key_hash);
    
    // Test DilithiumWitnessV0ScriptHash Bech32 encoding
    CScript script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    DilithiumWitnessV0ScriptHash witness_script_hash(script);
    std::string encoded_witness_script_address = EncodeDestination(witness_script_hash);
    BOOST_CHECK(!encoded_witness_script_address.empty());
    BOOST_CHECK(encoded_witness_script_address.find("dbtc") == 0);
    
    // Test witness script address decoding
    CTxDestination decoded_witness_script_dest = DecodeDestination(encoded_witness_script_address);
    BOOST_CHECK(std::holds_alternative<DilithiumWitnessV0ScriptHash>(decoded_witness_script_dest));
    BOOST_CHECK(std::get<DilithiumWitnessV0ScriptHash>(decoded_witness_script_dest) == witness_script_hash);
}

BOOST_AUTO_TEST_CASE(dilithium_script_generation)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Test DilithiumPubKeyDestination script generation
    DilithiumPubKeyDestination pubkey_dest(pubkey);
    CScript pubkey_script = GetScriptForDestination(pubkey_dest);
    
    // For large data (>75 bytes), Bitcoin uses OP_PUSHDATA1 or OP_PUSHDATA2
    // CDilithiumPubKey::SIZE is 1312 bytes, so we need OP_PUSHDATA2 (0x4D) + 2 length bytes
    BOOST_CHECK(pubkey_script.size() == CDilithiumPubKey::SIZE + 4); // OP_PUSHDATA2 + 2 length bytes + data + opcode
    BOOST_CHECK(pubkey_script[0] == OP_PUSHDATA2); // 0x4D = 77
    BOOST_CHECK(pubkey_script.back() == OP_CHECKSIGDILITHIUM);
    
    // Test DilithiumPKHash script generation
    DilithiumPKHash pk_hash(pubkey);
    CScript pkh_script = GetScriptForDestination(pk_hash);
    BOOST_CHECK(pkh_script.size() == 25);
    BOOST_CHECK(pkh_script[0] == OP_DUP);
    BOOST_CHECK(pkh_script[1] == OP_HASH160);
    BOOST_CHECK(pkh_script[2] == 20);
    BOOST_CHECK(pkh_script[23] == OP_EQUALVERIFY);
    BOOST_CHECK(pkh_script[24] == OP_CHECKSIGDILITHIUM);
    
    // Test DilithiumScriptHash script generation
    CScript redeem_script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    DilithiumScriptHash script_hash(redeem_script);
    CScript p2sh_script = GetScriptForDestination(script_hash);
    BOOST_CHECK(p2sh_script.size() == 23);
    BOOST_CHECK(p2sh_script[0] == OP_HASH160);
    BOOST_CHECK(p2sh_script[1] == 20);
    BOOST_CHECK(p2sh_script[22] == OP_EQUAL);
    
    // Test DilithiumWitnessV0KeyHash script generation
    DilithiumWitnessV0KeyHash witness_key_hash(pubkey);
    CScript witness_script = GetScriptForDestination(witness_key_hash);
    BOOST_CHECK(witness_script.size() == 22);
    BOOST_CHECK(witness_script[0] == OP_0);
    BOOST_CHECK(witness_script[1] == 20);
    
    // Test DilithiumWitnessV0ScriptHash script generation
    DilithiumWitnessV0ScriptHash witness_script_hash(redeem_script);
    CScript witness_p2sh_script = GetScriptForDestination(witness_script_hash);
    BOOST_CHECK(witness_p2sh_script.size() == 34);
    BOOST_CHECK(witness_p2sh_script[0] == OP_0);
    BOOST_CHECK(witness_p2sh_script[1] == 32);
}

BOOST_AUTO_TEST_CASE(dilithium_script_solving)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Test Dilithium P2PK script solving
    CScript p2pk_script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(p2pk_script, solutions);
    BOOST_CHECK(type == TxoutType::DILITHIUM_PUBKEY);
    BOOST_CHECK(solutions.size() == 1);
    BOOST_CHECK(solutions[0].size() == CDilithiumPubKey::SIZE);
    BOOST_CHECK(std::equal(solutions[0].begin(), solutions[0].end(), pubkey.begin()));
    
    // Test Dilithium P2PKH script solving
    DilithiumPKHash pk_hash(pubkey);
    CScript p2pkh_script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pk_hash) << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;
    solutions.clear();
    type = Solver(p2pkh_script, solutions);
    BOOST_CHECK(type == TxoutType::DILITHIUM_PUBKEYHASH);
    BOOST_CHECK(solutions.size() == 1);
    BOOST_CHECK(solutions[0].size() == 20);
    BOOST_CHECK(std::equal(solutions[0].begin(), solutions[0].end(), pk_hash.begin()));
    
    // Test Dilithium P2SH script solving
    CScript redeem_script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    DilithiumScriptHash script_hash(redeem_script);
    CScript p2sh_script = CScript() << OP_HASH160 << ToByteVector(script_hash) << OP_EQUAL;
    
    solutions.clear();
    type = Solver(p2sh_script, solutions);
    BOOST_CHECK(type == TxoutType::DILITHIUM_SCRIPTHASH);
    BOOST_CHECK(solutions.size() == 1);
    BOOST_CHECK(solutions[0].size() == 20);
    BOOST_CHECK(std::equal(solutions[0].begin(), solutions[0].end(), script_hash.begin()));
}

BOOST_AUTO_TEST_CASE(dilithium_multisig_script)
{
    // Generate multiple Dilithium keys
    std::vector<CDilithiumKey> keys(3);
    std::vector<CDilithiumPubKey> pubkeys(3);
    
    for (int i = 0; i < 3; i++) {
        keys[i].MakeNewKey();
        BOOST_REQUIRE(keys[i].IsValid());
        pubkeys[i] = keys[i].GetPubKey();
        BOOST_REQUIRE(pubkeys[i].IsValid());
    }
    
    // Test Dilithium multisig script generation
    CScript multisig_script = GetScriptForDilithiumMultisig(2, pubkeys);
    BOOST_CHECK(multisig_script.size() > 0);
    BOOST_CHECK(multisig_script.back() == OP_CHECKMULTISIGDILITHIUM);
    
    // Test multisig script solving
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(multisig_script, solutions);
    BOOST_CHECK(type == TxoutType::DILITHIUM_MULTISIG);
    BOOST_CHECK(solutions.size() == 5); // 2 (required) + 3 (pubkeys) + 3 (total)
    BOOST_CHECK(solutions[0].size() == 1);
    BOOST_CHECK(solutions[0][0] == 2); // Required signatures
    BOOST_CHECK(solutions[4].size() == 1);
    BOOST_CHECK(solutions[4][0] == 3); // Total pubkeys
}

BOOST_AUTO_TEST_CASE(dilithium_destination_extraction)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Test Dilithium P2PK destination extraction
    CScript p2pk_script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    CTxDestination dest;
    bool has_address = ExtractDestination(p2pk_script, dest);
    BOOST_CHECK(!has_address); // P2PK doesn't have an address
    BOOST_CHECK(std::holds_alternative<DilithiumPubKeyDestination>(dest));
    
    // Test Dilithium P2PKH destination extraction
    DilithiumPKHash pk_hash(pubkey);
    CScript p2pkh_script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pk_hash) << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;
    dest = CTxDestination{};
    has_address = ExtractDestination(p2pkh_script, dest);
    BOOST_CHECK(has_address);
    BOOST_CHECK(std::holds_alternative<DilithiumPKHash>(dest));
    BOOST_CHECK(std::get<DilithiumPKHash>(dest) == pk_hash);
    
    // Test Dilithium P2SH destination extraction
    CScript redeem_script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    DilithiumScriptHash script_hash(redeem_script);
    CScript p2sh_script = CScript() << OP_HASH160 << ToByteVector(script_hash) << OP_EQUAL;
    dest = CTxDestination{};
    has_address = ExtractDestination(p2sh_script, dest);
    BOOST_CHECK(has_address);
    BOOST_CHECK(std::holds_alternative<DilithiumScriptHash>(dest));
    BOOST_CHECK(std::get<DilithiumScriptHash>(dest) == script_hash);
}

BOOST_AUTO_TEST_CASE(dilithium_output_types)
{
    // Test Dilithium output type parsing
    std::optional<OutputType> type;
    
    type = ParseOutputType("dilithium-legacy");
    BOOST_CHECK(type.has_value());
    BOOST_CHECK(type.value() == OutputType::DILITHIUM_LEGACY);
    
    type = ParseOutputType("dilithium-bech32");
    BOOST_CHECK(type.has_value());
    BOOST_CHECK(type.value() == OutputType::DILITHIUM_BECH32);
    
    // Test Dilithium output type formatting
    std::string formatted = FormatOutputType(OutputType::DILITHIUM_LEGACY);
    BOOST_CHECK(formatted == "dilithium-legacy");
    
    formatted = FormatOutputType(OutputType::DILITHIUM_BECH32);
    BOOST_CHECK(formatted == "dilithium-bech32");
}

BOOST_AUTO_TEST_CASE(dilithium_valid_destination)
{
    CDilithiumKey key;
    key.MakeNewKey();
    BOOST_REQUIRE(key.IsValid());
    
    CDilithiumPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    
    // Test valid destinations
    DilithiumPubKeyDestination pubkey_dest(pubkey);
    BOOST_CHECK(!IsValidDestination(pubkey_dest)); // P2PK doesn't have address
    
    DilithiumPKHash pk_hash(pubkey);
    BOOST_CHECK(IsValidDestination(pk_hash));
    
    CScript script = CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;
    DilithiumScriptHash script_hash(script);
    BOOST_CHECK(IsValidDestination(script_hash));
    
    DilithiumWitnessV0KeyHash witness_key_hash(pubkey);
    BOOST_CHECK(IsValidDestination(witness_key_hash));
    
    DilithiumWitnessV0ScriptHash witness_script_hash(script);
    BOOST_CHECK(IsValidDestination(witness_script_hash));
}

BOOST_AUTO_TEST_SUITE_END()
