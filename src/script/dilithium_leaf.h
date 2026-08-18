// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_SCRIPT_DILITHIUM_LEAF_H
#define BTQ_SCRIPT_DILITHIUM_LEAF_H

#include <crypto/dilithium_key.h>
#include <script/script.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/**
 * Recognised shapes of a Dilithium tapscript leaf inside a P2MR tree.
 *
 * Dilithium opcodes are consensus-valid only under SigVersion::P2MR_TAPSCRIPT,
 * so these templates describe leaves of a BIP360 P2MR Merkle tree, never a
 * bare scriptPubKey.
 */
enum class P2MRLeafTemplate {
    //! <pubkey> OP_CHECKSIGDILITHIUM
    SINGLE_CHECKSIGDILITHIUM,
    //! <m> <pubkey>... <n> OP_CHECKMULTISIGDILITHIUM
    CHECKMULTISIGDILITHIUM,
    //! Accumulator m-of-n, see GetScriptForDilithiumThreshold
    THRESHOLD_ACCUMULATOR,
    UNKNOWN,
};

/** Spending policy expressed by a Dilithium P2MR leaf. */
struct P2MRDilithiumLeafPolicy {
    P2MRLeafTemplate type{P2MRLeafTemplate::UNKNOWN};
    //! Signatures required. 1 for a single-key leaf.
    int m{0};
    //! Public keys in script order; index 0 is evaluated first.
    std::vector<CDilithiumPubKey> pubkeys;

    int n() const { return static_cast<int>(pubkeys.size()); }
    bool IsValid() const { return type != P2MRLeafTemplate::UNKNOWN; }
};

/**
 * Build an m-of-n Dilithium threshold leaf.
 *
 * Unlike OP_CHECKMULTISIGDILITHIUM, this accumulator form lets a key that did
 * not sign contribute an empty signature slot, so any m-sized subset of the n
 * signers produces a valid witness. This is the form btq-multisig uses and the
 * only one that supports partial signing across independent wallets.
 *
 *   OP_0
 *   (OP_TOALTSTACK <pubkey> OP_CHECKSIGDILITHIUM OP_FROMALTSTACK OP_ADD) x n
 *   <m> OP_GREATERTHANOREQUAL
 */
CScript GetScriptForDilithiumThreshold(int m, const std::vector<CDilithiumPubKey>& pubkeys);

/** Classify a leaf script. Returns type UNKNOWN when nothing matches. */
P2MRDilithiumLeafPolicy ParseP2MRDilithiumLeaf(const CScript& script);

/** Stable RPC-facing name for a leaf template. */
std::string P2MRLeafTemplateName(P2MRLeafTemplate type);

/** Position of `pubkey` in the policy's script order, if it participates. */
std::optional<size_t> FindPolicyKeyIndex(const P2MRDilithiumLeafPolicy& policy, const CDilithiumPubKey& pubkey);

/**
 * Assemble the signature portion of the witness stack for a leaf.
 *
 * `sigs_by_key_index` is parallel to policy.pubkeys; an empty entry means that
 * key did not sign. On success `stack_out` holds the elements that go below the
 * leaf script and control block, in push order (stack bottom first).
 *
 * Returns false when the available signatures cannot satisfy the policy.
 */
bool BuildDilithiumLeafWitness(const P2MRDilithiumLeafPolicy& policy,
                               const std::vector<std::vector<unsigned char>>& sigs_by_key_index,
                               std::vector<std::vector<unsigned char>>& stack_out);

#endif // BTQ_SCRIPT_DILITHIUM_LEAF_H
