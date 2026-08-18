// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_PSBT_DILITHIUM_H
#define BTQ_PSBT_DILITHIUM_H

#include <psbt.h>
#include <script/dilithium_leaf.h>
#include <uint256.h>

#include <string>
#include <vector>

/** Where an input stands in the Dilithium P2MR signing flow. */
enum class P2MRInputStatus {
    //! Not a P2MR input and carries no P2MR fields.
    NOT_P2MR,
    //! final_script_witness is set.
    FINALIZED,
    //! P2MR input whose spend path we cannot reason about (leaf missing, ambiguous, or unrecognised).
    UNKNOWN_LEAF,
    UNSIGNED,
    PARTIALLY_SIGNED,
    //! Enough authorised signatures are present to build a witness.
    FINALIZABLE,
};

std::string P2MRInputStatusName(P2MRInputStatus status);

/** What we can say about an input's Dilithium P2MR spend path without verifying signatures. */
struct P2MRInputInfo {
    P2MRInputStatus status{P2MRInputStatus::NOT_P2MR};
    P2MRDilithiumLeafPolicy policy;
    CScript leaf_script;
    int leaf_version{0};
    std::vector<unsigned char> control_block;
    uint256 leaf_hash;
    //! Signatures present from keys the leaf authorises.
    int sigs_present{0};
    //! Signatures the leaf requires.
    int sigs_required{0};
};

/**
 * Describe an input's Dilithium P2MR spend path.
 *
 * This is a structural inspection: it selects the leaf being spent and counts
 * signatures, but does not verify them. Use ValidateP2MRDilithiumInput for that.
 */
P2MRInputInfo InspectP2MRInput(const PartiallySignedTransaction& psbt, unsigned int index);

/**
 * Fail-closed check of one input's Dilithium/P2MR fields.
 *
 * Verifies that every advertised leaf commits to the witness program and that
 * every partial signature is from an authorised key and cryptographically valid
 * for the recomputed BIP341 sighash. Inputs with no Dilithium/P2MR fields pass
 * trivially.
 *
 * `txdata` may be null only when the input carries no partial signatures;
 * otherwise verification cannot proceed and the input is rejected.
 */
bool ValidateP2MRDilithiumInput(const PartiallySignedTransaction& psbt, unsigned int index,
                                const PrecomputedTransactionData* txdata, std::string& error);

/** Run ValidateP2MRDilithiumInput over every input. */
bool ValidateP2MRDilithiumPSBT(const PartiallySignedTransaction& psbt, std::string& error);

#endif // BTQ_PSBT_DILITHIUM_H
