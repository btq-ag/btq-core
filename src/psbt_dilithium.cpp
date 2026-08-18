// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <psbt_dilithium.h>

#include <script/interpreter.h>
#include <span.h>
#include <tinyformat.h>

#include <map>
#include <set>

namespace {

//! Extract the 32-byte Merkle root from a P2MR scriptPubKey.
bool GetP2MRProgram(const CTxOut& utxo, std::vector<unsigned char>& program)
{
    int witversion;
    std::vector<unsigned char> prog;
    if (!utxo.scriptPubKey.IsWitnessProgram(witversion, prog)) return false;
    if (witversion != 2 || prog.size() != WITNESS_V2_P2MR_SIZE) return false;
    program = std::move(prog);
    return true;
}

//! Recompute the BIP341 sighash a Dilithium leaf signature commits to.
bool ComputeLeafSighash(const CMutableTransaction& tx, unsigned int index,
                        const PrecomputedTransactionData& txdata, const uint256& leaf_hash,
                        uint8_t hash_type, uint256& sighash_out)
{
    if (!txdata.m_bip341_taproot_ready || !txdata.m_spent_outputs_ready) return false;
    ScriptExecutionData execdata;
    execdata.m_annex_init = true;
    execdata.m_annex_present = false;
    execdata.m_codeseparator_pos_init = true;
    execdata.m_codeseparator_pos = 0xFFFFFFFF;
    execdata.m_tapleaf_hash_init = true;
    execdata.m_tapleaf_hash = leaf_hash;
    return SignatureHashSchnorr(sighash_out, execdata, tx, index, hash_type,
                                SigVersion::P2MR_TAPSCRIPT, txdata, MissingDataBehavior::FAIL);
}

bool HasP2MRFields(const PSBTInput& input)
{
    return !input.m_p2mr_scripts.empty() || !input.m_p2mr_dilithium_script_sigs.empty() ||
           !input.m_p2mr_merkle_root.IsNull();
}

} // namespace

std::string P2MRInputStatusName(P2MRInputStatus status)
{
    switch (status) {
    case P2MRInputStatus::NOT_P2MR: return "not_p2mr";
    case P2MRInputStatus::FINALIZED: return "finalized";
    case P2MRInputStatus::UNKNOWN_LEAF: return "unknown_leaf";
    case P2MRInputStatus::UNSIGNED: return "unsigned";
    case P2MRInputStatus::PARTIALLY_SIGNED: return "partially_signed";
    case P2MRInputStatus::FINALIZABLE: return "finalizable";
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

P2MRInputInfo InspectP2MRInput(const PartiallySignedTransaction& psbt, unsigned int index)
{
    P2MRInputInfo info;
    const PSBTInput& input = psbt.inputs.at(index);

    CTxOut utxo;
    std::vector<unsigned char> program;
    const bool is_p2mr = psbt.GetInputUTXO(utxo, index) && GetP2MRProgram(utxo, program);
    if (!is_p2mr && !HasP2MRFields(input)) return info;

    if (PSBTInputSigned(input)) {
        info.status = P2MRInputStatus::FINALIZED;
        return info;
    }

    // Narrow the advertised leaves down to the one being spent. Partial
    // signatures pin the choice; without them a single leaf is unambiguous.
    std::set<uint256> sig_leaf_hashes;
    for (const auto& [keyid_leaf, _] : input.m_p2mr_dilithium_script_sigs) {
        sig_leaf_hashes.insert(keyid_leaf.second);
    }

    info.status = P2MRInputStatus::UNKNOWN_LEAF;
    if (sig_leaf_hashes.size() > 1) return info;

    int candidates = 0;
    for (const auto& [leaf, control_blocks] : input.m_p2mr_scripts) {
        const auto& [script, leaf_ver] = leaf;
        if (control_blocks.empty()) continue;
        const uint256 leaf_hash = ComputeTapleafHash(static_cast<uint8_t>(leaf_ver), script);
        if (!sig_leaf_hashes.empty() && sig_leaf_hashes.count(leaf_hash) == 0) continue;
        ++candidates;
        info.leaf_script = CScript(script.begin(), script.end());
        info.leaf_version = leaf_ver;
        info.leaf_hash = leaf_hash;
        if (!control_blocks.empty()) info.control_block = *control_blocks.begin();
    }
    if (candidates != 1) {
        info = P2MRInputInfo{};
        info.status = P2MRInputStatus::UNKNOWN_LEAF;
        return info;
    }

    info.policy = ParseP2MRDilithiumLeaf(info.leaf_script);
    if (!info.policy.IsValid()) return info;

    info.sigs_required = info.policy.m;
    for (const auto& [keyid_leaf, pubkey_sig] : input.m_p2mr_dilithium_script_sigs) {
        if (keyid_leaf.second != info.leaf_hash) continue;
        if (!FindPolicyKeyIndex(info.policy, pubkey_sig.first)) continue;
        ++info.sigs_present;
    }

    if (info.sigs_present == 0) {
        info.status = P2MRInputStatus::UNSIGNED;
    } else if (info.sigs_present >= info.sigs_required) {
        info.status = P2MRInputStatus::FINALIZABLE;
    } else {
        info.status = P2MRInputStatus::PARTIALLY_SIGNED;
    }
    return info;
}

bool ValidateP2MRDilithiumInput(const PartiallySignedTransaction& psbt, unsigned int index,
                                const PrecomputedTransactionData* txdata, std::string& error)
{
    const PSBTInput& input = psbt.inputs.at(index);
    if (!HasP2MRFields(input)) return true;

    const auto fail = [&](const std::string& reason) {
        error = strprintf("Input %u: %s", index, reason);
        return false;
    };

    CTxOut utxo;
    if (!psbt.GetInputUTXO(utxo, index)) {
        return fail("has Dilithium P2MR fields but no UTXO to check them against");
    }
    std::vector<unsigned char> program;
    if (!GetP2MRProgram(utxo, program)) {
        return fail("has Dilithium P2MR fields but does not spend a P2MR output");
    }
    if (!input.m_p2mr_merkle_root.IsNull() && input.m_p2mr_merkle_root != uint256(program)) {
        return fail("P2MR merkle root does not match the witness program");
    }
    // Consensus rejects SIGHASH_DEFAULT for P2MR tapscript, so SIGHASH_ALL is
    // the only type a Dilithium leaf signature can usefully commit to.
    if (input.sighash_type && *input.sighash_type != SIGHASH_ALL) {
        return fail("Dilithium P2MR inputs only support SIGHASH_ALL");
    }

    // Every advertised leaf must genuinely commit to the witness program,
    // otherwise a signer could be tricked into signing an unrelated script.
    std::map<uint256, CScript> leaves;
    for (const auto& [leaf, control_blocks] : input.m_p2mr_scripts) {
        const auto& [script, leaf_ver] = leaf;
        // A leaf with no control block proves nothing, because the commitment
        // check below runs per control block and would not run at all. Reject
        // it here so the leaf never reaches the signer as an accepted leaf.
        if (control_blocks.empty()) {
            return fail("P2MR leaf script has no control block");
        }
        const uint256 leaf_hash = ComputeTapleafHash(static_cast<uint8_t>(leaf_ver), script);
        for (const auto& control : control_blocks) {
            if (control.size() < P2MR_CONTROL_BASE_SIZE || control.size() > P2MR_CONTROL_MAX_SIZE ||
                (control.size() - P2MR_CONTROL_BASE_SIZE) % P2MR_CONTROL_NODE_SIZE != 0) {
                return fail("P2MR control block has an invalid size");
            }
            if ((control[0] & 1) != 1) {
                return fail("P2MR control block does not have the parity bit set");
            }
            if ((control[0] & TAPROOT_LEAF_MASK) != leaf_ver) {
                return fail("P2MR control block leaf version does not match the leaf script");
            }
            if (ComputeP2MRMerkleRoot(control, leaf_hash) != uint256(program)) {
                return fail("P2MR leaf script does not commit to the witness program");
            }
        }
        if (!leaves.emplace(leaf_hash, CScript(script.begin(), script.end())).second) {
            return fail("two P2MR leaf scripts collide on the same leaf hash");
        }
    }

    std::set<uint256> sig_leaf_hashes;
    for (const auto& [keyid_leaf, pubkey_sig] : input.m_p2mr_dilithium_script_sigs) {
        const uint256& leaf_hash = keyid_leaf.second;
        const auto& [pubkey, sig] = pubkey_sig;

        const auto leaf_it = leaves.find(leaf_hash);
        if (leaf_it == leaves.end()) {
            return fail("Dilithium partial signature refers to a leaf the PSBT does not contain");
        }
        sig_leaf_hashes.insert(leaf_hash);
        if (sig_leaf_hashes.size() > 1) {
            return fail("Dilithium partial signatures refer to more than one leaf");
        }

        const P2MRDilithiumLeafPolicy policy = ParseP2MRDilithiumLeaf(leaf_it->second);
        if (!policy.IsValid()) {
            return fail("Dilithium partial signature is attached to an unrecognised leaf script");
        }
        if (!FindPolicyKeyIndex(policy, pubkey)) {
            return fail("Dilithium partial signature is from a key the leaf does not authorise");
        }
        if (sig.back() != SIGHASH_ALL) {
            return fail("Dilithium partial signature uses an unsupported sighash type");
        }

        if (!txdata) {
            return fail("cannot verify Dilithium partial signatures without every input amount");
        }
        uint256 sighash;
        if (!ComputeLeafSighash(*psbt.tx, index, *txdata, leaf_hash, sig.back(), sighash)) {
            return fail("cannot compute the P2MR sighash needed to verify Dilithium partial signatures");
        }
        if (!pubkey.Verify(sighash, std::vector<unsigned char>(sig.begin(), sig.end() - 1))) {
            return fail("Dilithium partial signature does not verify");
        }
    }

    return true;
}

bool ValidateP2MRDilithiumPSBT(const PartiallySignedTransaction& psbt, std::string& error)
{
    if (!psbt.tx) return true;

    bool any = false;
    for (const PSBTInput& input : psbt.inputs) {
        if (HasP2MRFields(input)) {
            any = true;
            break;
        }
    }
    if (!any) return true;

    const PrecomputedTransactionData txdata = PrecomputePSBTData(psbt);
    const PrecomputedTransactionData* txdata_ptr =
        (txdata.m_bip341_taproot_ready && txdata.m_spent_outputs_ready) ? &txdata : nullptr;

    for (unsigned int i = 0; i < psbt.inputs.size(); ++i) {
        if (!ValidateP2MRDilithiumInput(psbt, i, txdata_ptr, error)) return false;
    }
    return true;
}
