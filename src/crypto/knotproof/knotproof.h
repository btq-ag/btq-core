// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// KnotProof: Dual-layer spatial signature combining knot polynomial
// invariants with Dilithium post-quantum signatures.
//
// Implements the GENSYS OPTX v2.2.2 22-level proof chain:
//   Layer 1: Dilithium2 (lattice-based, NIST FIPS 204) — resists Shor
//   Layer 2: Knot invariant proof (topological) — Jones is #P-hard
//   Layer 3: AGT biometric binding — gaze tensor on Δ² simplex
//
// Full invariant suite I(K) per GENSYS Levels 3,11,12,14:
//   Jones V(q), Alexander Δ(t), Conway ∇(z), HOMFLY-PT P(α,z)
//
// Security theorem (GENSYS Level 20):
//   Adv[A] ≤ ε_mut(λ) + ε_fact(λ) + ε_edge(λ)
//
// Per Google Quantum AI (March 2026): ECDSA breakable with ~1,200 logical
// qubits in ~9 minutes. This module provides defense-in-depth beyond PQC.
//
// US Patent Application Serial No. 19/243,050

#ifndef BTQ_CRYPTO_KNOTPROOF_KNOTPROOF_H
#define BTQ_CRYPTO_KNOTPROOF_KNOTPROOF_H

#include <crypto/knotproof/agtbind.h>
#include <crypto/knotproof/alexander.h>
#include <crypto/knotproof/braid.h>
#include <crypto/knotproof/conway.h>
#include <crypto/knotproof/homflypt.h>
#include <crypto/knotproof/jones.h>
#include <crypto/knotproof/laurentpoly.h>

#include <cstdint>
#include <vector>

namespace knotproof {

/**
 * Complete knot invariant suite I(K).
 * Per GENSYS OPTX v2.2.2, each proof contains all four polynomial invariants.
 */
struct InvariantSuite {
    LaurentPolynomial alexander;  // Δ(t) ∈ Z[t, t⁻¹]  (Level 11)
    LaurentPolynomial jones;      // V(q) ∈ Z[q, q⁻¹]  (Level 3)
    LaurentPolynomial conway;     // ∇(z) ∈ Z[z]        (Level 12)
    BivariatePoly homflypt;       // P(α,z) ∈ Z[α±¹,z±¹] (Level 14)

    /** Serialize all invariants to bytes. */
    std::vector<uint8_t> ToBytes() const;

    /** Deserialize from bytes. */
    bool FromBytes(const std::vector<uint8_t>& data);

    /** Verify internal consistency: HOMFLY-PT specializations must match. */
    bool IsConsistent(int crossing_number) const;
};

/**
 * Spatial signature: the output of a KnotProof signing operation.
 *
 * Contains the full knot invariant proof from the GENSYS pipeline:
 *   - Braid word (from AGT Markov chain on Δ²)
 *   - Complete invariant suite I(K)
 *   - AGT tensor state (final Markov chain position)
 *   - JOULE temporal binding
 *   - SHA256 commitment
 *
 * Total overhead: ~300-600 bytes (on top of Dilithium2's 2420-byte sig).
 */
struct SpatialSignature {
    // Knot proof data
    std::vector<uint8_t> braid_bytes;       // Serialized braid word
    std::vector<uint8_t> invariant_bytes;   // Serialized InvariantSuite
    std::vector<uint8_t> agt_tensor_bytes;  // Serialized AGTTensor (Markov state)
    std::vector<uint8_t> agt_commitment;    // 32-byte binding commitment
    int64_t timestamp{0};                    // JOULE temporal binding
    int32_t modular_depth{3};               // Quantization depth m

    // Dilithium signature stored separately in btq-core's existing sig field.

    /** Serialize to bytes. */
    std::vector<uint8_t> ToBytes() const;

    /** Deserialize from bytes. Returns false on malformed input. */
    bool FromBytes(const std::vector<uint8_t>& data);

    /** Total serialized size in bytes. */
    size_t Size() const;

    /** Check if signature data is well-formed. */
    bool IsValid() const;
};

/**
 * KnotProof: the main API for spatial signing and verification.
 *
 * Signing flow (GENSYS pipeline):
 *   1. Gaze samples → Markov chain on Δ² → AGTTensor
 *   2. Quantized simplex positions → braid word β
 *   3. Mix with message hash for uniqueness
 *   4. Compute full invariant suite I(K = β̂)
 *   5. Create SHA256 commitment binding all components
 *   6. Package as SpatialSignature
 *   7. (Caller applies Dilithium signature separately)
 *
 * Verification flow:
 *   1. Deserialize SpatialSignature
 *   2. Recover braid from AGT tensor + message hash
 *   3. Verify all 4 polynomial invariants match
 *   4. Check polynomial constraints (Δ(1)=±1, V(1)=1, ∇(0)=1)
 *   5. Verify AGT commitment
 *   6. Check complexity score ≥ minimum
 *   7. (Caller verifies Dilithium signature separately)
 */
class KnotProof {
public:
    static constexpr int MAX_CROSSINGS = 24;
    static constexpr int MAX_STRANDS = 8;
    static constexpr int MIN_CROSSINGS = 6;
    static constexpr int DEFAULT_CROSSINGS = 12;

    /**
     * Create a spatial signature from gaze samples + message hash.
     * Full pipeline per GENSYS OPTX v2.2.2.
     *
     * @param msg_hash  32-byte message hash (transaction sighash)
     * @param samples   Gaze sample sequence from JETT OE capture
     * @param alpha     Markov chain learning rate (default 0.20)
     * @param m         Modular depth for quantization (default 3)
     */
    static SpatialSignature Sign(const uint8_t* msg_hash,
                                  const std::vector<GazeSample>& samples,
                                  double alpha = MarkovAGT::DEFAULT_ALPHA,
                                  int m = 3);

    /**
     * Create a spatial signature from pre-built AGT tensor.
     * For when the Markov chain ran on-device (MediaPipe/ARKit).
     */
    static SpatialSignature SignFromTensor(const uint8_t* msg_hash,
                                            const AGTTensor& tensor,
                                            int m = 3);

    /**
     * Verify a spatial signature against message + gaze samples.
     */
    static bool Verify(const SpatialSignature& sig,
                       const uint8_t* msg_hash,
                       const std::vector<GazeSample>& samples,
                       double alpha = MarkovAGT::DEFAULT_ALPHA);

    /**
     * Verify from pre-built tensor (when Markov chain ran externally).
     */
    static bool VerifyFromTensor(const SpatialSignature& sig,
                                  const uint8_t* msg_hash,
                                  const AGTTensor& tensor);

    /**
     * Non-biometric verification (no AGT data).
     * Only checks polynomial consistency from message hash alone.
     */
    static bool VerifyNonBiometric(const SpatialSignature& sig,
                                    const uint8_t* msg_hash);

    /**
     * Complexity score: crossing count + polynomial degree spans + writhe.
     * Must exceed MIN_MAINNET_COMPLEXITY for mainnet transactions.
     */
    static int ComplexityScore(const SpatialSignature& sig);

    /**
     * Compute the full invariant suite I(K) for a braid word.
     * Returns Jones, Alexander, Conway, and HOMFLY-PT.
     */
    static InvariantSuite ComputeInvariants(const BraidWord& braid);
};

} // namespace knotproof

#endif // BTQ_CRYPTO_KNOTPROOF_KNOTPROOF_H
