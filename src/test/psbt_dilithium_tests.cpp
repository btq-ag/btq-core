// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <core_io.h>
#include <crypto/dilithium_key.h>
#include <key.h>
#include <key_io.h>
#include <pubkey.h>
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

// The PSBT parse cap on control blocks must be the consensus cap, not an
// independent value. Anything smaller would leave outputs that are spendable
// on-chain but unspendable through a PSBT.
BOOST_AUTO_TEST_CASE(control_blocks_up_to_the_consensus_limit_round_trip)
{
    for (const int depth : {1, 16, 17, 32, (int)P2MR_CONTROL_MAX_NODE_COUNT}) {
        // A degenerate tree, so the first leaf sits at `depth` and therefore
        // carries a merkle path of `depth` nodes.
        P2MRBuilder builder;
        builder.Add(depth, CScript() << OP_1, TAPROOT_LEAF_TAPSCRIPT);
        builder.Add(depth, CScript() << OP_2, TAPROOT_LEAF_TAPSCRIPT);
        for (int d = depth - 1; d >= 1; --d) {
            builder.Add(d, CScript() << OP_1 << d << OP_EQUAL, TAPROOT_LEAF_TAPSCRIPT);
        }
        builder.Finalize();
        BOOST_REQUIRE_MESSAGE(builder.IsComplete(), "tree incomplete at depth " << depth);

        const P2MRSpendData spenddata = builder.GetSpendData();
        size_t widest = 0;
        for (const auto& [leaf, controls] : spenddata.scripts) {
            for (const auto& control : controls) widest = std::max(widest, control.size());
        }
        BOOST_CHECK_EQUAL(widest, P2MR_CONTROL_BASE_SIZE + P2MR_CONTROL_NODE_SIZE * depth);

        CMutableTransaction tx;
        tx.nVersion = 2;
        tx.vin.emplace_back(COutPoint{uint256{1}, 0});
        tx.vout.emplace_back(90000, CScript() << OP_TRUE);

        PartiallySignedTransaction psbt{tx};
        CTxOut prevout;
        prevout.nValue = 100000;
        prevout.scriptPubKey = GetScriptForDestination(builder.GetOutput());
        psbt.inputs[0].witness_utxo = prevout;
        psbt.inputs[0].m_p2mr_merkle_root = spenddata.merkle_root;
        for (const auto& [leaf, controls] : spenddata.scripts) {
            psbt.inputs[0].m_p2mr_scripts[leaf] = controls;
        }

        const PartiallySignedTransaction before = psbt;
        Roundtrip(psbt);
        BOOST_CHECK_MESSAGE(psbt.inputs[0].m_p2mr_scripts == before.inputs[0].m_p2mr_scripts,
                            "control blocks lost at depth " << depth);
    }
}

BOOST_AUTO_TEST_CASE(a_leaf_with_no_control_block_is_rejected)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    // No control block means nothing proves the leaf belongs to the output. The
    // wire cannot carry this, because a leaf with no control block serializes no
    // record, but a merge or an in-process build can still produce it.
    f.psbt.inputs[0].m_p2mr_scripts[{std::vector<unsigned char>(leaf.begin(), leaf.end()), TAPROOT_LEAF_TAPSCRIPT}] = {};

    const PrecomputedTransactionData txdata = PrecomputePSBTData(f.psbt);
    std::string error;
    BOOST_CHECK(!ValidateP2MRDilithiumInput(f.psbt, 0, &txdata, error));
    BOOST_CHECK_MESSAGE(error.find("no control block") != std::string::npos, error);

    // A signer that holds the key but not the tree takes the leaf straight from
    // the PSBT, so nothing fills the empty set in. Signing must decline the leaf
    // instead of reading the first control block of an empty set.
    FlatSigningProvider keys_only;
    const DilithiumPKHash id(signer.pubkey);
    keys_only.dilithium_pubkeys.emplace(id, signer.pubkey);
    keys_only.dilithium_keys.emplace(id, signer.key);
    BOOST_CHECK(!SignPSBTInput(keys_only, f.psbt, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/true));
    BOOST_CHECK(f.psbt.inputs[0].final_script_witness.IsNull());
    BOOST_CHECK(f.psbt.inputs[0].m_p2mr_dilithium_script_sigs.empty());
    BOOST_CHECK(InspectP2MRInput(f.psbt, 0).status == P2MRInputStatus::UNKNOWN_LEAF);
}

BOOST_AUTO_TEST_CASE(merging_does_not_let_an_empty_set_discard_a_control_block)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    const auto spenddata = f.builder.GetSpendData();
    BOOST_REQUIRE_EQUAL(spenddata.scripts.size(), 1U);
    const auto& [leaf_key, real_controls] = *spenddata.scripts.begin();
    BOOST_REQUIRE(!real_controls.empty());

    PSBTInput empty_leaf;
    empty_leaf.m_p2mr_scripts[leaf_key] = {};

    PSBTInput real_leaf;
    real_leaf.m_p2mr_scripts[leaf_key] = real_controls;

    // Empty destination must pick up the real control block.
    PSBTInput merged = empty_leaf;
    merged.Merge(real_leaf);
    BOOST_CHECK(merged.m_p2mr_scripts[leaf_key] == real_controls);

    // Real destination must keep its control block when the incoming set is empty.
    merged = real_leaf;
    merged.Merge(empty_leaf);
    BOOST_CHECK(merged.m_p2mr_scripts[leaf_key] == real_controls);

    SignatureData sigdata;
    sigdata.p2mr_spenddata.scripts[leaf_key] = real_controls;
    PSBTInput from_sig = empty_leaf;
    from_sig.FromSignatureData(sigdata);
    BOOST_CHECK(from_sig.m_p2mr_scripts[leaf_key] == real_controls);
}

// Quarks F2.10: outputs carry the P2MR tree and merkle root so a receiving
// wallet can store the metadata it needs to later spend the output.
BOOST_AUTO_TEST_CASE(output_p2mr_tree_is_filled_and_roundtrips)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    // Pay the P2MR output itself, so UpdatePSBTOutput sees a P2MR destination.
    CMutableTransaction tx;
    tx.nVersion = 2;
    tx.vin.emplace_back(COutPoint{uint256{1}, 0});
    tx.vout.emplace_back(90000, f.prevout.scriptPubKey);
    PartiallySignedTransaction psbt{tx};

    UpdatePSBTOutput(f.full_provider, psbt, 0);
    BOOST_REQUIRE_EQUAL(psbt.outputs[0].m_p2mr_tree.size(), 1U);
    BOOST_CHECK(psbt.outputs[0].m_p2mr_tree == f.builder.GetTreeTuples());
    BOOST_CHECK(psbt.outputs[0].m_p2mr_merkle_root ==
                uint256{std::vector<unsigned char>(f.output.begin(), f.output.end())});

    // An unknown output field must survive alongside the new ones.
    const std::vector<unsigned char> unknown_key{0x20, 0xab};
    const std::vector<unsigned char> unknown_val{0x01, 0x02, 0x03};
    psbt.outputs[0].unknown[unknown_key] = unknown_val;

    PartiallySignedTransaction before = psbt;
    Roundtrip(psbt);
    BOOST_CHECK(psbt.outputs[0].m_p2mr_tree == before.outputs[0].m_p2mr_tree);
    BOOST_CHECK(psbt.outputs[0].m_p2mr_merkle_root == before.outputs[0].m_p2mr_merkle_root);
    BOOST_CHECK(psbt.outputs[0].unknown.at(unknown_key) == unknown_val);

    // A provider that does not know the tree leaves the output untouched.
    PartiallySignedTransaction bare{tx};
    FlatSigningProvider empty_provider;
    UpdatePSBTOutput(empty_provider, bare, 0);
    BOOST_CHECK(bare.outputs[0].m_p2mr_tree.empty());
    BOOST_CHECK(bare.outputs[0].m_p2mr_merkle_root.IsNull());
}

BOOST_AUTO_TEST_CASE(output_p2mr_merge_fills_missing_fields_in_both_orders)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    CMutableTransaction tx;
    tx.nVersion = 2;
    tx.vin.emplace_back(COutPoint{uint256{1}, 0});
    tx.vout.emplace_back(90000, f.prevout.scriptPubKey);

    const uint256 root{std::vector<unsigned char>(f.output.begin(), f.output.end())};

    PartiallySignedTransaction root_only{tx};
    root_only.outputs[0].m_p2mr_merkle_root = root;
    PartiallySignedTransaction tree_and_root{tx};
    tree_and_root.outputs[0].m_p2mr_tree = f.builder.GetTreeTuples();
    tree_and_root.outputs[0].m_p2mr_merkle_root = root;

    // combinepsbt([root_only, tree_and_root]) must not drop the tree.
    PartiallySignedTransaction merged = root_only;
    BOOST_REQUIRE(merged.Merge(tree_and_root));
    BOOST_CHECK(merged.outputs[0].m_p2mr_tree == f.builder.GetTreeTuples());
    BOOST_CHECK(merged.outputs[0].m_p2mr_merkle_root == root);

    // The reverse order keeps both fields too.
    PartiallySignedTransaction merged_rev = tree_and_root;
    BOOST_REQUIRE(merged_rev.Merge(root_only));
    BOOST_CHECK(merged_rev.outputs[0].m_p2mr_tree == f.builder.GetTreeTuples());
    BOOST_CHECK(merged_rev.outputs[0].m_p2mr_merkle_root == root);

    // The fields merge independently: a tree-only side completes a
    // root-only side.
    PartiallySignedTransaction tree_only{tx};
    tree_only.outputs[0].m_p2mr_tree = f.builder.GetTreeTuples();
    PartiallySignedTransaction cross = root_only;
    BOOST_REQUIRE(cross.Merge(tree_only));
    BOOST_CHECK(cross.outputs[0].m_p2mr_tree == f.builder.GetTreeTuples());
    BOOST_CHECK(cross.outputs[0].m_p2mr_merkle_root == root);
}

BOOST_AUTO_TEST_CASE(output_p2mr_tree_disagreeing_with_the_root_is_rejected)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    CMutableTransaction tx;
    tx.nVersion = 2;
    tx.vin.emplace_back(COutPoint{uint256{1}, 0});
    tx.vout.emplace_back(90000, f.prevout.scriptPubKey);
    PartiallySignedTransaction psbt{tx};
    psbt.outputs[0].m_p2mr_tree = f.builder.GetTreeTuples();
    psbt.outputs[0].m_p2mr_merkle_root = uint256::ONE; // not the tree's root

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << psbt;
    const std::string encoded = EncodeBase64(MakeUCharSpan(ss));
    PartiallySignedTransaction decoded;
    std::string error;
    BOOST_CHECK(!DecodeBase64PSBT(decoded, encoded, error));
}

BOOST_AUTO_TEST_CASE(output_p2mr_metadata_must_match_the_outputs_program)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    // A self-consistent tree+root pair whose root is NOT this output's witness
    // program must be rejected: it would poison the receiver's spend metadata.
    const Signer other = MakeSigner();
    CScript other_leaf;
    other_leaf << ToByteVector(other.pubkey) << OP_CHECKSIGDILITHIUM;
    P2MRBuilder other_builder;
    other_builder.Add(0, other_leaf, TAPROOT_LEAF_TAPSCRIPT);
    other_builder.Finalize();
    const WitnessV2P2MR other_output = other_builder.GetOutput();

    CMutableTransaction tx;
    tx.nVersion = 2;
    tx.vin.emplace_back(COutPoint{uint256{1}, 0});
    tx.vout.emplace_back(90000, f.prevout.scriptPubKey); // pays f.output, not other_output
    PartiallySignedTransaction psbt{tx};
    psbt.outputs[0].m_p2mr_tree = other_builder.GetTreeTuples();
    psbt.outputs[0].m_p2mr_merkle_root =
        uint256{std::vector<unsigned char>(other_output.begin(), other_output.end())};

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << psbt;
    std::string error;
    {
        PartiallySignedTransaction decoded;
        BOOST_CHECK(!DecodeBase64PSBT(decoded, EncodeBase64(MakeUCharSpan(ss)), error));
    }

    // P2MR metadata on a non-P2MR output is rejected as well.
    CMutableTransaction classical_tx;
    classical_tx.nVersion = 2;
    classical_tx.vin.emplace_back(COutPoint{uint256{1}, 0});
    classical_tx.vout.emplace_back(90000, CScript() << OP_TRUE);
    PartiallySignedTransaction classical{classical_tx};
    classical.outputs[0].m_p2mr_tree = f.builder.GetTreeTuples();

    CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
    ss2 << classical;
    {
        PartiallySignedTransaction decoded;
        BOOST_CHECK(!DecodeBase64PSBT(decoded, EncodeBase64(MakeUCharSpan(ss2)), error));
    }

    // And the honest case still parses: metadata matching the paid program.
    PartiallySignedTransaction honest{tx};
    honest.outputs[0].m_p2mr_tree = f.builder.GetTreeTuples();
    honest.outputs[0].m_p2mr_merkle_root =
        uint256{std::vector<unsigned char>(f.output.begin(), f.output.end())};
    CDataStream ss3(SER_NETWORK, PROTOCOL_VERSION);
    ss3 << honest;
    {
        PartiallySignedTransaction decoded;
        BOOST_CHECK_MESSAGE(DecodeBase64PSBT(decoded, EncodeBase64(MakeUCharSpan(ss3)), error), error);
        BOOST_CHECK(decoded.outputs[0].m_p2mr_tree == f.builder.GetTreeTuples());
    }
}

BOOST_AUTO_TEST_CASE(output_p2mr_malformed_tree_is_rejected)
{
    const Signer signer = MakeSigner();
    CScript leaf;
    leaf << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM;
    Fixture f = MakeFixture(leaf, {signer});

    CMutableTransaction tx;
    tx.nVersion = 2;
    tx.vin.emplace_back(COutPoint{uint256{1}, 0});
    tx.vout.emplace_back(90000, f.prevout.scriptPubKey);
    PartiallySignedTransaction psbt{tx};
    // Two leaves both at depth 0 cannot complete a tree.
    const std::vector<unsigned char> script_v{leaf.begin(), leaf.end()};
    psbt.outputs[0].m_p2mr_tree.emplace_back(0, TAPROOT_LEAF_TAPSCRIPT, script_v);
    psbt.outputs[0].m_p2mr_tree.emplace_back(0, TAPROOT_LEAF_TAPSCRIPT, script_v);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << psbt;
    const std::string encoded = EncodeBase64(MakeUCharSpan(ss));
    PartiallySignedTransaction decoded;
    std::string error;
    BOOST_CHECK(!DecodeBase64PSBT(decoded, encoded, error));
}

BOOST_AUTO_TEST_CASE(hybrid_leaf_roundtrips_through_the_parser)
{
    const Signer signer = MakeSigner();
    CKey schnorr_key;
    schnorr_key.MakeNewKey(/*fCompressed=*/true);
    const XOnlyPubKey xonly{schnorr_key.GetPubKey()};

    const CScript script = GetScriptForHybridDilithiumLeaf(signer.pubkey, xonly);
    const P2MRDilithiumLeafPolicy policy = ParseP2MRDilithiumLeaf(script);

    BOOST_CHECK(policy.type == P2MRLeafTemplate::HYBRID_DILITHIUM_SCHNORR);
    BOOST_CHECK_EQUAL(policy.m, 1);
    BOOST_CHECK_EQUAL(policy.n(), 1);
    BOOST_REQUIRE_EQUAL(policy.pubkeys.size(), 1U);
    BOOST_CHECK(policy.pubkeys[0] == signer.pubkey);
    BOOST_REQUIRE_EQUAL(policy.schnorr_pubkeys.size(), 1U);
    BOOST_CHECK(policy.schnorr_pubkeys[0] == xonly);

    // Near misses must stay unrecognised.
    CScript no_checksig;
    no_checksig << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUMVERIFY << ToByteVector(xonly);
    BOOST_CHECK(!ParseP2MRDilithiumLeaf(no_checksig).IsValid());

    CScript non_verify;
    non_verify << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUM << ToByteVector(xonly) << OP_CHECKSIG;
    BOOST_CHECK(!ParseP2MRDilithiumLeaf(non_verify).IsValid());

    CScript trailing = script;
    trailing << OP_NOP;
    BOOST_CHECK(!ParseP2MRDilithiumLeaf(trailing).IsValid());

    CScript short_key;
    short_key << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUMVERIFY
              << std::vector<unsigned char>(31, 0x02) << OP_CHECKSIG;
    BOOST_CHECK(!ParseP2MRDilithiumLeaf(short_key).IsValid());

    // A 32-byte push above the field prime is not a BIP340 x coordinate.
    // OP_CHECKSIG fails on any non-empty signature for it, so the leaf is
    // unspendable and must not read as signable.
    CScript bad_x;
    bad_x << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUMVERIFY
          << std::vector<unsigned char>(32, 0xff) << OP_CHECKSIG;
    BOOST_CHECK(!ParseP2MRDilithiumLeaf(bad_x).IsValid());

    // A non-minimal push of the x-only key parses with GetOp but fails
    // MINIMALDATA at spend time, so the parser must not claim it.
    CScript nonminimal;
    nonminimal << ToByteVector(signer.pubkey) << OP_CHECKSIGDILITHIUMVERIFY;
    nonminimal.push_back(OP_PUSHDATA1);
    nonminimal.push_back(32);
    nonminimal.insert(nonminimal.end(), xonly.begin(), xonly.end());
    nonminimal.push_back(OP_CHECKSIG);
    BOOST_CHECK(!ParseP2MRDilithiumLeaf(nonminimal).IsValid());
}

BOOST_AUTO_TEST_CASE(schnorr_only_signature_pins_the_leaf_choice)
{
    // Two-leaf tree: a plain Dilithium leaf and a hybrid leaf. With only the
    // schnorr half of the hybrid leaf signed, inspection must still narrow
    // the spend to the hybrid leaf and report a partial signature instead of
    // unknown_leaf.
    const Signer a = MakeSigner();
    const Signer b = MakeSigner();
    CKey schnorr_key;
    schnorr_key.MakeNewKey(/*fCompressed=*/true);
    const XOnlyPubKey xonly{schnorr_key.GetPubKey()};

    CScript plain;
    plain << ToByteVector(a.pubkey) << OP_CHECKSIGDILITHIUM;
    const CScript hybrid = GetScriptForHybridDilithiumLeaf(b.pubkey, xonly);

    P2MRBuilder builder;
    builder.Add(1, plain, TAPROOT_LEAF_TAPSCRIPT);
    builder.Add(1, hybrid, TAPROOT_LEAF_TAPSCRIPT);
    builder.Finalize();
    BOOST_REQUIRE(builder.IsComplete());

    CMutableTransaction tx;
    tx.nVersion = 2;
    tx.vin.emplace_back(COutPoint{uint256{1}, 0});
    tx.vout.emplace_back(90000, CScript() << OP_TRUE);
    PartiallySignedTransaction psbt{tx};
    psbt.inputs[0].witness_utxo = CTxOut{100000, GetScriptForDestination(builder.GetOutput())};

    const P2MRSpendData spenddata = builder.GetSpendData();
    psbt.inputs[0].m_p2mr_merkle_root = spenddata.merkle_root;
    for (const auto& [leaf, control_blocks] : spenddata.scripts) {
        psbt.inputs[0].m_p2mr_scripts[leaf].insert(control_blocks.begin(), control_blocks.end());
    }

    const uint256 hybrid_hash = ComputeTapleafHash(TAPROOT_LEAF_TAPSCRIPT, std::vector<unsigned char>(hybrid.begin(), hybrid.end()));
    // 64 zero bytes plus SIGHASH_ALL: a well-formed signature that does not verify.
    std::vector<unsigned char> bogus_sig(64, 0x00);
    bogus_sig.push_back(SIGHASH_ALL);
    psbt.inputs[0].m_tap_script_sigs[{xonly, hybrid_hash}] = bogus_sig;

    const P2MRInputInfo info = InspectP2MRInput(psbt, 0);
    BOOST_CHECK(info.status == P2MRInputStatus::PARTIALLY_SIGNED);
    BOOST_CHECK(info.leaf_hash == hybrid_hash);

    // A tap signature for a leaf this tree does not contain must not hide
    // the hybrid leaf the real signature pinned.
    CKey stray_key;
    stray_key.MakeNewKey(/*fCompressed=*/true);
    psbt.inputs[0].m_tap_script_sigs[{XOnlyPubKey{stray_key.GetPubKey()}, uint256::ONE}] =
        std::vector<unsigned char>(64, 0x11);
    const P2MRInputInfo with_stray = InspectP2MRInput(psbt, 0);
    BOOST_CHECK(with_stray.status == P2MRInputStatus::PARTIALLY_SIGNED);
    BOOST_CHECK(with_stray.leaf_hash == hybrid_hash);

    // The cached schnorr half does not verify, so decode must reject the
    // PSBT instead of handing signers a signature they will trust forever.
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << psbt;
    PartiallySignedTransaction decoded;
    std::string error;
    BOOST_CHECK(!DecodeBase64PSBT(decoded, EncodeBase64(MakeUCharSpan(ss)), error));
    BOOST_CHECK(error.find("does not verify") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(hybrid_witness_requires_both_signatures)
{
    const Signer signer = MakeSigner();
    CKey schnorr_key;
    schnorr_key.MakeNewKey(/*fCompressed=*/true);
    const XOnlyPubKey xonly{schnorr_key.GetPubKey()};
    const P2MRDilithiumLeafPolicy policy = ParseP2MRDilithiumLeaf(GetScriptForHybridDilithiumLeaf(signer.pubkey, xonly));

    // Both present: schnorr signature at the stack bottom, Dilithium on top.
    std::vector<std::vector<unsigned char>> stack;
    BOOST_REQUIRE(BuildDilithiumLeafWitness(policy, {{0xdd}}, stack, {{0xee}}));
    BOOST_REQUIRE_EQUAL(stack.size(), 2U);
    BOOST_CHECK(stack[0] == std::vector<unsigned char>{0xee});
    BOOST_CHECK(stack[1] == std::vector<unsigned char>{0xdd});

    // Missing either signature cannot satisfy the leaf.
    BOOST_CHECK(!BuildDilithiumLeafWitness(policy, {{}}, stack, {{0xee}}));
    BOOST_CHECK(!BuildDilithiumLeafWitness(policy, {{0xdd}}, stack, {{}}));
    BOOST_CHECK(!BuildDilithiumLeafWitness(policy, {{0xdd}}, stack, {}));
}

BOOST_AUTO_TEST_CASE(hybrid_psbt_needs_both_signatures_to_spend)
{
    const Signer signer = MakeSigner();
    CKey schnorr_key;
    schnorr_key.MakeNewKey(/*fCompressed=*/true);
    const XOnlyPubKey xonly{schnorr_key.GetPubKey()};
    const CScript leaf = GetScriptForHybridDilithiumLeaf(signer.pubkey, xonly);

    Fixture f = MakeFixture(leaf, {signer});
    const CPubKey schnorr_pubkey = schnorr_key.GetPubKey();
    f.full_provider.pubkeys.emplace(schnorr_pubkey.GetID(), schnorr_pubkey);
    f.full_provider.keys.emplace(schnorr_pubkey.GetID(), schnorr_key);

    const PrecomputedTransactionData txdata = PrecomputePSBTData(f.psbt);

    // The Dilithium key alone cannot complete the input.
    {
        PartiallySignedTransaction psbt = f.psbt;
        BOOST_CHECK(!SignPSBTInput(f.ProviderFor({0}), psbt, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/true));
    }
    // The schnorr key alone cannot either.
    {
        PartiallySignedTransaction psbt = f.psbt;
        FlatSigningProvider schnorr_only;
        schnorr_only.p2mr_trees = f.full_provider.p2mr_trees;
        schnorr_only.pubkeys.emplace(schnorr_pubkey.GetID(), schnorr_pubkey);
        schnorr_only.keys.emplace(schnorr_pubkey.GetID(), schnorr_key);
        BOOST_CHECK(!SignPSBTInput(schnorr_only, psbt, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/true));
    }

    // Both keys together produce a witness that verifies under consensus rules.
    BOOST_CHECK(SignPSBTInput(f.full_provider, f.psbt, 0, &txdata, SIGHASH_ALL, nullptr, /*finalize=*/false));
    BOOST_CHECK(WitnessVerifies(f, f.psbt));

    // Consensus check: blanking either signature in the final witness fails.
    PartiallySignedTransaction finalized = f.psbt;
    CMutableTransaction spend;
    BOOST_REQUIRE(FinalizeAndExtractPSBT(finalized, spend));
    BOOST_REQUIRE_EQUAL(spend.vin[0].scriptWitness.stack.size(), 4U); // schnorr, dilithium, leaf, control

    PrecomputedTransactionData spend_txdata;
    spend_txdata.Init(spend, {f.prevout}, /*force=*/true);
    const CTransaction spend_tx{spend};
    TransactionSignatureChecker checker(&spend_tx, 0, f.prevout.nValue, spend_txdata, MissingDataBehavior::FAIL);
    const auto verify_with = [&](const std::vector<std::vector<unsigned char>>& stack) {
        CScriptWitness witness;
        witness.stack = stack;
        return VerifyScript(spend.vin[0].scriptSig, f.prevout.scriptPubKey, &witness,
                            STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_DILITHIUM, checker);
    };

    BOOST_CHECK(verify_with(spend.vin[0].scriptWitness.stack));

    auto no_schnorr = spend.vin[0].scriptWitness.stack;
    no_schnorr[0].clear();
    BOOST_CHECK(!verify_with(no_schnorr));

    auto no_dilithium = spend.vin[0].scriptWitness.stack;
    no_dilithium[1].clear();
    BOOST_CHECK(!verify_with(no_dilithium));
}

BOOST_AUTO_TEST_SUITE_END()
