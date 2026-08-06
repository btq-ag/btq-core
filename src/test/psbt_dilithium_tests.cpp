// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <core_io.h>
#include <crypto/dilithium_key.h>
#include <key_io.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <psbt_dilithium.h>
#include <script/dilithium_leaf.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(psbt_dilithium_tests, BasicTestingSetup)

namespace {

struct Signer {
    CDilithiumKey key;
    CDilithiumPubKey pubkey;
};

Signer MakeSigner()
{
    Signer s;
    s.key.MakeNewKey();
    s.pubkey = s.key.GetPubKey();
    return s;
}

/** A funded 1-in 1-out spend of a single-leaf P2MR output, as a PSBT. */
struct Fixture {
    std::vector<Signer> signers;
    CScript leaf_script;
    P2MRBuilder builder;
    WitnessV2P2MR output;
    CTxOut prevout;
    PartiallySignedTransaction psbt;
    FlatSigningProvider full_provider; //!< holds every private key

    FlatSigningProvider ProviderFor(const std::vector<size_t>& key_indexes) const
    {
        FlatSigningProvider provider;
        provider.p2mr_trees = full_provider.p2mr_trees;
        for (size_t i : key_indexes) {
            const DilithiumPKHash id(signers[i].pubkey);
            provider.dilithium_pubkeys.emplace(id, signers[i].pubkey);
            provider.dilithium_keys.emplace(id, signers[i].key);
        }
        return provider;
    }
};

Fixture MakeFixture(const CScript& leaf_script, std::vector<Signer> signers)
{
    Fixture f;
    f.signers = std::move(signers);
    f.leaf_script = leaf_script;

    f.builder.Add(0, leaf_script, TAPROOT_LEAF_TAPSCRIPT);
    f.builder.Finalize();
    BOOST_REQUIRE(f.builder.IsComplete());
    f.output = f.builder.GetOutput();

    f.prevout.nValue = 100000;
    f.prevout.scriptPubKey = GetScriptForDestination(f.output);

    CMutableTransaction tx;
    tx.nVersion = 2;
    tx.vin.emplace_back(COutPoint{uint256{1}, 0});
    tx.vout.emplace_back(90000, CScript() << OP_TRUE);

    f.psbt = PartiallySignedTransaction{tx};
    f.psbt.inputs[0].witness_utxo = f.prevout;

    f.full_provider.p2mr_trees[f.output] = f.builder;
    for (const Signer& s : f.signers) {
        const DilithiumPKHash id(s.pubkey);
        f.full_provider.dilithium_pubkeys.emplace(id, s.pubkey);
        f.full_provider.dilithium_keys.emplace(id, s.key);
    }
    return f;
}

std::string Roundtrip(PartiallySignedTransaction& psbt)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << psbt;
    const std::string encoded = EncodeBase64(MakeUCharSpan(ss));
    PartiallySignedTransaction decoded;
    std::string error;
    BOOST_REQUIRE_MESSAGE(DecodeBase64PSBT(decoded, encoded, error), error);
    psbt = decoded;
    return encoded;
}

bool WitnessVerifies(const Fixture& f, const PartiallySignedTransaction& psbt)
{
    PartiallySignedTransaction copy = psbt;
    CMutableTransaction result;
    if (!FinalizeAndExtractPSBT(copy, result)) return false;

    PrecomputedTransactionData txdata;
    txdata.Init(result, {f.prevout}, /*force=*/true);
    const CTransaction tx{result};
    TransactionSignatureChecker checker(&tx, 0, f.prevout.nValue, txdata, MissingDataBehavior::FAIL);
    return VerifyScript(result.vin[0].scriptSig, f.prevout.scriptPubKey, &result.vin[0].scriptWitness,
                        STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_DILITHIUM, checker);
}

} // namespace

BOOST_AUTO_TEST_CASE(threshold_leaf_roundtrips_through_the_parser)
{
    std::vector<CDilithiumPubKey> pubkeys;
    for (int i = 0; i < 3; ++i) pubkeys.push_back(MakeSigner().pubkey);

    const CScript script = GetScriptForDilithiumThreshold(2, pubkeys);
    const P2MRDilithiumLeafPolicy policy = ParseP2MRDilithiumLeaf(script);

    BOOST_CHECK(policy.type == P2MRLeafTemplate::THRESHOLD_ACCUMULATOR);
    BOOST_CHECK_EQUAL(policy.m, 2);
    BOOST_CHECK_EQUAL(policy.n(), 3);
    BOOST_CHECK(policy.pubkeys == pubkeys);
    for (size_t i = 0; i < pubkeys.size(); ++i) {
        BOOST_CHECK_EQUAL(FindPolicyKeyIndex(policy, pubkeys[i]).value(), i);
    }
    BOOST_CHECK(!FindPolicyKeyIndex(policy, MakeSigner().pubkey).has_value());
}

BOOST_AUTO_TEST_CASE(single_key_leaf_is_recognised)
{
    const CDilithiumPubKey pubkey = MakeSigner().pubkey;
    CScript script;
    script << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM;

    const P2MRDilithiumLeafPolicy policy = ParseP2MRDilithiumLeaf(script);
    BOOST_CHECK(policy.type == P2MRLeafTemplate::SINGLE_CHECKSIGDILITHIUM);
    BOOST_CHECK_EQUAL(policy.m, 1);
    BOOST_CHECK_EQUAL(policy.n(), 1);
}

BOOST_AUTO_TEST_CASE(unknown_leaves_are_not_misparsed)
{
    BOOST_CHECK(!ParseP2MRDilithiumLeaf(CScript() << OP_TRUE).IsValid());
    // A truncated accumulator (missing the final threshold comparison).
    const CDilithiumPubKey pubkey = MakeSigner().pubkey;
    CScript truncated;
    truncated << OP_0 << OP_TOALTSTACK << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM << OP_FROMALTSTACK << OP_ADD;
    BOOST_CHECK(!ParseP2MRDilithiumLeaf(truncated).IsValid());
}

BOOST_AUTO_TEST_CASE(threshold_witness_puts_the_first_key_on_top)
{
    std::vector<CDilithiumPubKey> pubkeys;
    for (int i = 0; i < 3; ++i) pubkeys.push_back(MakeSigner().pubkey);
    const P2MRDilithiumLeafPolicy policy = ParseP2MRDilithiumLeaf(GetScriptForDilithiumThreshold(2, pubkeys));

    // Keys 0 and 2 signed; key 1 did not.
    const std::vector<std::vector<unsigned char>> sigs{{0xaa}, {}, {0xcc}};
    std::vector<std::vector<unsigned char>> stack;
    BOOST_REQUIRE(BuildDilithiumLeafWitness(policy, sigs, stack));

    // The leaf checks key 0 first and OP_CHECKSIGDILITHIUM consumes from the
    // top, so the stack is pushed in reverse key order.
    BOOST_REQUIRE_EQUAL(stack.size(), 3U);
    BOOST_CHECK(stack[0] == std::vector<unsigned char>{0xcc});
    BOOST_CHECK(stack[1].empty());
    BOOST_CHECK(stack[2] == std::vector<unsigned char>{0xaa});

    // One signature is not enough for a 2-of-3.
    std::vector<std::vector<unsigned char>> too_few_stack;
    BOOST_CHECK(!BuildDilithiumLeafWitness(policy, {{0xaa}, {}, {}}, too_few_stack));
    BOOST_CHECK(too_few_stack.empty());
}

BOOST_AUTO_TEST_CASE(single_key_psbt_survives_serialization)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    // Sign without finalizing, so the PSBT has to carry the Dilithium material.
    const PrecomputedTransactionData txdata = PrecomputePSBTData(f.psbt);
    BOOST_CHECK(!SignPSBTInput(f.ProviderFor({}), f.psbt, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/false));
    BOOST_CHECK(SignPSBTInput(f.full_provider, f.psbt, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/false));

    BOOST_CHECK_EQUAL(f.psbt.inputs[0].m_p2mr_scripts.size(), 1U);
    BOOST_CHECK_EQUAL(f.psbt.inputs[0].m_p2mr_dilithium_script_sigs.size(), 1U);
    BOOST_CHECK(f.psbt.inputs[0].m_p2mr_merkle_root == f.builder.GetSpendData().merkle_root);

    PartiallySignedTransaction before = f.psbt;
    Roundtrip(f.psbt);
    BOOST_CHECK(f.psbt.inputs[0].m_p2mr_scripts == before.inputs[0].m_p2mr_scripts);
    BOOST_CHECK(f.psbt.inputs[0].m_p2mr_dilithium_script_sigs == before.inputs[0].m_p2mr_dilithium_script_sigs);
    BOOST_CHECK(f.psbt.inputs[0].m_p2mr_merkle_root == before.inputs[0].m_p2mr_merkle_root);

    // A wallet with no keys at all can finalize what the PSBT already carries.
    BOOST_CHECK(WitnessVerifies(f, f.psbt));
}

BOOST_AUTO_TEST_CASE(threshold_psbt_accumulates_signatures_across_providers)
{
    std::vector<Signer> signers{MakeSigner(), MakeSigner(), MakeSigner()};
    std::vector<CDilithiumPubKey> pubkeys;
    for (const Signer& s : signers) pubkeys.push_back(s.pubkey);
    Fixture f = MakeFixture(GetScriptForDilithiumThreshold(2, pubkeys), signers);

    const PrecomputedTransactionData txdata = PrecomputePSBTData(f.psbt);

    // Signer 0 alone cannot satisfy a 2-of-3, but must still leave its
    // signature behind for the next signer.
    PartiallySignedTransaction first = f.psbt;
    BOOST_CHECK(!SignPSBTInput(f.ProviderFor({0}), first, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/false));
    BOOST_CHECK_EQUAL(first.inputs[0].m_p2mr_dilithium_script_sigs.size(), 1U);
    BOOST_CHECK_EQUAL(InspectP2MRInput(first, 0).sigs_present, 1);
    BOOST_CHECK(InspectP2MRInput(first, 0).status == P2MRInputStatus::PARTIALLY_SIGNED);
    Roundtrip(first);

    // Signer 2 works from the serialized PSBT and never sees signer 0's key.
    PartiallySignedTransaction second = f.psbt;
    BOOST_CHECK(!SignPSBTInput(f.ProviderFor({2}), second, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/false));
    Roundtrip(second);

    PartiallySignedTransaction combined = first;
    BOOST_REQUIRE(combined.Merge(second));
    BOOST_CHECK_EQUAL(combined.inputs[0].m_p2mr_dilithium_script_sigs.size(), 2U);

    const P2MRInputInfo info = InspectP2MRInput(combined, 0);
    BOOST_CHECK(info.status == P2MRInputStatus::FINALIZABLE);
    BOOST_CHECK_EQUAL(info.sigs_present, 2);
    BOOST_CHECK_EQUAL(info.sigs_required, 2);

    BOOST_CHECK(WitnessVerifies(f, combined));
}

BOOST_AUTO_TEST_CASE(finalized_input_drops_the_bulky_signing_material)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    const PrecomputedTransactionData txdata = PrecomputePSBTData(f.psbt);
    BOOST_REQUIRE(SignPSBTInput(f.full_provider, f.psbt, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/true));

    BOOST_CHECK(!f.psbt.inputs[0].final_script_witness.IsNull());
    BOOST_CHECK(f.psbt.inputs[0].m_p2mr_scripts.empty());
    BOOST_CHECK(f.psbt.inputs[0].m_p2mr_dilithium_script_sigs.empty());
    BOOST_CHECK(f.psbt.inputs[0].m_p2mr_merkle_root.IsNull());
    BOOST_CHECK(InspectP2MRInput(f.psbt, 0).status == P2MRInputStatus::FINALIZED);
}

BOOST_AUTO_TEST_CASE(a_forged_signature_is_rejected_on_decode)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    const PrecomputedTransactionData txdata = PrecomputePSBTData(f.psbt);
    BOOST_REQUIRE(SignPSBTInput(f.full_provider, f.psbt, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/false));

    auto& entry = *f.psbt.inputs[0].m_p2mr_dilithium_script_sigs.begin();
    std::vector<unsigned char> sig = entry.second.second;
    sig[100] ^= 0xff;
    f.psbt.inputs[0].m_p2mr_dilithium_script_sigs[entry.first].second = sig;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << f.psbt;
    PartiallySignedTransaction decoded;
    std::string error;
    BOOST_CHECK(!DecodeRawPSBT(decoded, MakeByteSpan(ss), error));
    BOOST_CHECK_MESSAGE(error.find("signature") != std::string::npos, error);
}

BOOST_AUTO_TEST_CASE(a_leaf_not_committed_to_by_the_output_is_rejected)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    // Advertise a leaf that is not in the tree the output commits to.
    CScript foreign_leaf;
    foreign_leaf << ToByteVector(MakeSigner().pubkey) << OP_CHECKSIGDILITHIUM;
    const auto spenddata = f.builder.GetSpendData();
    const auto& control_blocks = spenddata.scripts.begin()->second;
    f.psbt.inputs[0].m_p2mr_scripts[{std::vector<unsigned char>(foreign_leaf.begin(), foreign_leaf.end()), TAPROOT_LEAF_TAPSCRIPT}] = control_blocks;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << f.psbt;
    PartiallySignedTransaction decoded;
    std::string error;
    BOOST_CHECK(!DecodeRawPSBT(decoded, MakeByteSpan(ss), error));
    BOOST_CHECK_MESSAGE(error.find("commit") != std::string::npos, error);
}

BOOST_AUTO_TEST_CASE(a_merkle_root_disagreeing_with_the_output_is_rejected)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    f.psbt.inputs[0].m_p2mr_merkle_root = uint256{42};

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << f.psbt;
    PartiallySignedTransaction decoded;
    std::string error;
    BOOST_CHECK(!DecodeRawPSBT(decoded, MakeByteSpan(ss), error));
    BOOST_CHECK_MESSAGE(error.find("merkle root") != std::string::npos, error);
}

BOOST_AUTO_TEST_CASE(duplicate_wire_keys_are_rejected)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    const PrecomputedTransactionData txdata = PrecomputePSBTData(f.psbt);
    BOOST_REQUIRE(SignPSBTInput(f.full_provider, f.psbt, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/false));

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << f.psbt;
    std::vector<unsigned char> bytes{MakeUCharSpan(ss).begin(), MakeUCharSpan(ss).end()};

    // Locate the merkle root record (0x01 0x1a, then a 32-byte value) and
    // duplicate it in place.
    const std::vector<unsigned char> record{0x01, PSBT_IN_P2MR_MERKLE_ROOT, 0x20};
    const auto it = std::search(bytes.begin(), bytes.end(), record.begin(), record.end());
    BOOST_REQUIRE(it != bytes.end());
    bytes.insert(it + record.size() + 32, it, it + record.size() + 32);

    PartiallySignedTransaction decoded;
    std::string error;
    BOOST_CHECK(!DecodeRawPSBT(decoded, MakeByteSpan(bytes), error));
    BOOST_CHECK_MESSAGE(error.find("Duplicate Key") != std::string::npos, error);
}

BOOST_AUTO_TEST_SUITE_END()
