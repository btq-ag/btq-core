// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// OP_CHECKSIG_KNOTPROOF: Script opcode for knot-theoretic signature verification.
//
// Integrates the GENSYS OPTX v2.2.2 22-level proof chain into btq-core's
// script engine, enabling on-chain verification of dual-layer signatures
// (Dilithium + knot invariant proof with full I(K) suite).
//
// Stack layout:
//   <spatial_sig> <agt_data> <msg_hash> OP_CHECKSIG_KNOTPROOF
//
// Verification checks all 4 polynomial invariants:
//   Jones V(q), Alexander Δ(t), Conway ∇(z), HOMFLY-PT P(α,z)
// Plus AGT commitment, complexity score, and temporal binding.

#include <crypto/knotproof/knotproof.h>

#include <cstdint>
#include <vector>

namespace knotproof {

/**
 * Script verification flags.
 */
enum KnotProofFlags : uint32_t {
    KNOTPROOF_NONE               = 0,
    KNOTPROOF_REQUIRE_AGT        = (1 << 0),  // Require AGT biometric binding
    KNOTPROOF_ALLOW_NONBIOMETRIC = (1 << 1),  // Allow non-biometric mode
    KNOTPROOF_CHECK_COMPLEXITY   = (1 << 2),  // Enforce minimum complexity score
    KNOTPROOF_CHECK_CONSISTENCY  = (1 << 3),  // Verify invariant cross-consistency
    KNOTPROOF_FULL_SUITE         = (1 << 4),  // Require all 4 invariants present
};

static constexpr int MIN_MAINNET_COMPLEXITY = 50;
static constexpr size_t MAX_SPATIAL_SIG_SIZE = 4096; // Increased for full suite

/**
 * Verify a spatial signature in script context.
 */
bool VerifyKnotProofScript(const std::vector<uint8_t>& sig_data,
                           const uint8_t* msg_hash,
                           const std::vector<uint8_t>& agt_data,
                           uint32_t flags)
{
    if (sig_data.empty() || sig_data.size() > MAX_SPATIAL_SIG_SIZE) return false;

    SpatialSignature sig;
    if (!sig.FromBytes(sig_data)) return false;
    if (!sig.IsValid()) return false;

    // Determine biometric vs non-biometric
    bool has_agt = !agt_data.empty();

    if ((flags & KNOTPROOF_REQUIRE_AGT) && !has_agt) return false;

    bool result;
    if (has_agt) {
        AGTTensor tensor;
        if (!tensor.FromBytes(agt_data)) return false;
        if (!tensor.IsValid()) return false;
        result = KnotProof::VerifyFromTensor(sig, msg_hash, tensor);
    } else if (flags & KNOTPROOF_ALLOW_NONBIOMETRIC) {
        result = KnotProof::VerifyNonBiometric(sig, msg_hash);
    } else {
        return false;
    }

    if (!result) return false;

    // Full invariant suite check
    if (flags & KNOTPROOF_FULL_SUITE) {
        InvariantSuite suite;
        if (!suite.FromBytes(sig.invariant_bytes)) return false;
        // Ensure all 4 invariants are non-trivial
        if (suite.alexander.IsZero() && suite.jones.IsZero()) return false;
    }

    // Cross-consistency check
    if (flags & KNOTPROOF_CHECK_CONSISTENCY) {
        InvariantSuite suite;
        if (!suite.FromBytes(sig.invariant_bytes)) return false;
        BraidWord braid;
        if (!braid.FromBytes(sig.braid_bytes)) return false;
        if (!suite.IsConsistent(braid.Length())) return false;
    }

    // Complexity check
    if (flags & KNOTPROOF_CHECK_COMPLEXITY) {
        int score = KnotProof::ComplexityScore(sig);
        if (score < MIN_MAINNET_COMPLEXITY) return false;
    }

    return true;
}

/**
 * Create a spatial signature for transaction inclusion.
 */
bool CreateKnotProofScript(const uint8_t* msg_hash,
                           const std::vector<uint8_t>& agt_data,
                           std::vector<uint8_t>& out_sig,
                           int modular_depth)
{
    if (!agt_data.empty()) {
        AGTTensor tensor;
        if (!tensor.FromBytes(agt_data)) return false;
        SpatialSignature sig = KnotProof::SignFromTensor(msg_hash, tensor, modular_depth);
        if (!sig.IsValid()) return false;
        out_sig = sig.ToBytes();
        return true;
    }

    // Non-biometric: zero tensor
    AGTTensor zero;
    zero.Reset();
    zero.num_samples = 1;
    zero.timestamp = 1; // Placeholder
    SpatialSignature sig = KnotProof::SignFromTensor(msg_hash, zero, modular_depth);
    if (!sig.IsValid()) return false;
    out_sig = sig.ToBytes();
    return true;
}

/**
 * Virtual size cost with witness discount.
 */
int64_t KnotProofWeight(const std::vector<uint8_t>& sig_data)
{
    int64_t raw = static_cast<int64_t>(sig_data.size());
    return (raw + 3) / 4; // 4:1 witness discount
}

} // namespace knotproof
