// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// AGT (Agentive Gaze Tensor) binding for knot-theoretic proofs.
//
// Implements the patented pipeline from US Patent App 19/243,050:
//   Gaze capture → Markov chain on Δ² simplex → Quantized braid word → Knot invariants
//
// The three AGT dimensions (COG/EMO/ENV) + JOULE timestamp create
// a 3-dimensional security space. The Markov chain prediction
// probabilities from JETT (Joule Encryption Temporal Template) RAG
// are converted into braid group generators via modular depth quantization.
//
// Reference: GENSYS OPTX v2.2.2, Levels 8 and 19.

#ifndef BTQ_CRYPTO_KNOTPROOF_AGTBIND_H
#define BTQ_CRYPTO_KNOTPROOF_AGTBIND_H

#include <crypto/knotproof/braid.h>
#include <crypto/knotproof/laurentpoly.h>

#include <array>
#include <cstdint>
#include <vector>

namespace knotproof {

/**
 * A single gaze sample classified into COG/EMO/ENV.
 * Raw input to the Markov chain update.
 */
struct GazeSample {
    double cog{0.0};    // Cognitive focus weight [0,1]
    double emo{0.0};    // Emotional valence weight [0,1]
    double env{0.0};    // Environmental context weight [0,1]
    int64_t timestamp{0}; // JOULE temporal binding (unix ms)
};

/**
 * AGT tensor on the 2-simplex Δ².
 *
 * w(t) = (w_cog, w_emo, w_env) ∈ Δ² where:
 *   Σwᵢ = 1, wᵢ ≥ 0
 *
 * This is a probability distribution over the three gaze categories,
 * evolving as a discrete-time Markov chain via the update rule:
 *   w(t+1) = Π[(1-α)w(t) + αg(t)]
 *
 * where Π is orthogonal projection onto Δ² and α ∈ (0,1] is the
 * learning rate (adjusted by cognitive load / emotional state).
 *
 * Per JETT OE Whitepaper Section 2.2: "w(t) represents a probability
 * distribution over the gaze categories, with w_cog, w_emo, w_env
 * being the weights for cognitive, emotional, and environmental
 * categories respectively at time t."
 */
struct AGTTensor {
    double w[3]{0.333, 0.333, 0.334}; // Current simplex state [cog, emo, env]
    double T[3][3]{};                   // Markov transition matrix (accumulated)
    int64_t timestamp{0};               // JOULE temporal binding (unix ms)
    uint8_t session_id[32]{};           // Session hash for replay prevention
    int num_samples{0};                 // Number of gaze samples accumulated

    /** Initialize to uniform distribution on Δ². */
    void Reset();

    /** Serialize to deterministic bytes. */
    std::vector<uint8_t> ToBytes() const;

    /** Deserialize from bytes. Returns false on malformed input. */
    bool FromBytes(const std::vector<uint8_t>& data);

    /** Check if tensor has valid (non-zero) data. */
    bool IsValid() const;
};

/**
 * Markov chain state for AGT evolution on Δ².
 *
 * Accumulates gaze samples over time, maintaining:
 *   - Current simplex position w(t)
 *   - Transition matrix T (online estimate)
 *   - Sequence of quantized exponents for braid generation
 *   - JOULE temporal binding
 *
 * The temporal sequence of w(t) values forms the Markov chain whose
 * transition probabilities get quantized into Artin braid generators.
 */
class MarkovAGT {
public:
    /** Default learning rate α (EMDR/REM adjustable). Range: 0.15-0.30 recommended. */
    static constexpr double DEFAULT_ALPHA = 0.20;

    /** Maximum gaze samples per proof (bounds braid word length). */
    static constexpr int MAX_SAMPLES = 256;

    MarkovAGT();
    explicit MarkovAGT(double alpha);

    /**
     * Project vector onto the 2-simplex Δ².
     * Ensures Σwᵢ = 1, wᵢ ≥ 0 via Euclidean projection.
     * Per JETT OE Whitepaper Section 2.3.
     */
    static void SimplexProject(double w[3]);

    /**
     * Update the AGT tensor with a new gaze sample.
     * Implements: w(t+1) = Π[(1-α)w(t) + αg(t)]
     *
     * Also updates the transition matrix T and accumulates
     * quantized exponents for braid word generation.
     *
     * @param sample  Gaze sample (COG/EMO/ENV weights + timestamp)
     */
    void Update(const GazeSample& sample);

    /**
     * Get the current AGT tensor state.
     */
    const AGTTensor& GetTensor() const { return tensor_; }

    /**
     * Get the accumulated quantized exponent sequence.
     * Each triplet (e_cog, e_emo, e_env) was derived via:
     *   eᵢ = vᵢ · (2m+1) - m
     * per GENSYS OPTX v2.2.2 Level 19.
     */
    const std::vector<int>& GetExponents() const { return exponents_; }

    /**
     * Build the braid word from accumulated Markov chain state.
     *
     * Per GENSYS Level 19: β = σ₁^{e_COG} · σ₂^{e_EMO} · σ₁^{e_ENV}
     * for each gaze sample in the sequence.
     *
     * Longer gaze sequences produce longer braid words with more
     * crossings, increasing knot complexity and security.
     *
     * @param modular_depth  Quantization depth m (default from security parameter)
     * @return  BraidWord representing the full gaze trajectory
     */
    BraidWord ToBraidWord(int modular_depth = 3) const;

    /**
     * Compute the Markov chain's stationary distribution π.
     * The eigenvector of T corresponding to eigenvalue 1.
     */
    void StationaryDistribution(double pi[3]) const;

    /** Get the learning rate. */
    double Alpha() const { return alpha_; }

    /** Number of gaze samples processed. */
    int NumSamples() const { return tensor_.num_samples; }

private:
    AGTTensor tensor_;
    double alpha_;
    double prev_w_[3]; // Previous state for transition matrix update
    std::vector<int> exponents_; // Accumulated quantized exponents

    /**
     * Quantize simplex position to integer exponents.
     * eᵢ = round(wᵢ · (2m+1)) - m, clamped to [-m, m]
     */
    static void Quantize(const double w[3], int m, int e[3]);
};

/**
 * AGT binding engine.
 *
 * Maps the Markov chain AGT trajectory into knot invariants,
 * creating a biometric-bound topological signature.
 *
 * Full pipeline (GENSYS Levels 8→19→3/11/12/14):
 *   1. Gaze samples → Markov chain on Δ² (MarkovAGT)
 *   2. Quantized simplex positions → braid word β
 *   3. Braid closure K = β̂ (Alexander's theorem)
 *   4. Compute invariant suite I(K)
 *   5. SHA256 commitment binding tensor + invariants + timestamp
 */
class AGTBind {
public:
    /**
     * Generate a braid word from a sequence of gaze samples.
     * This is the correct pipeline per the JETT OE patent.
     */
    static BraidWord GenerateBraid(const std::vector<GazeSample>& samples,
                                    double alpha = MarkovAGT::DEFAULT_ALPHA,
                                    int modular_depth = 3);

    /**
     * Generate a braid word from a pre-built AGT tensor.
     * For cases where the Markov chain was run externally
     * (e.g., on-device gaze processing via MediaPipe).
     */
    static BraidWord GenerateBraidFromTensor(const AGTTensor& tensor,
                                              int modular_depth = 3);

    /**
     * Bind AGT tensor weights to crossing diagram.
     * Modifies crossing weights in-place based on simplex state.
     */
    static void BindWeights(std::vector<Crossing>& crossings,
                            const AGTTensor& tensor);

    /**
     * Compute the AGT-weighted Alexander polynomial.
     */
    static LaurentPolynomial WeightedAlexander(const BraidWord& braid,
                                                const AGTTensor& tensor);

    /**
     * Compute the AGT-weighted Jones polynomial.
     */
    static LaurentPolynomial WeightedJones(const BraidWord& braid,
                                            const AGTTensor& tensor);

    /**
     * Full binding result: braid + invariant suite + commitment.
     */
    struct BindResult {
        BraidWord braid;
        LaurentPolynomial alexander;
        LaurentPolynomial jones;
        AGTTensor final_tensor;          // Final Markov chain state
        std::vector<uint8_t> commitment; // SHA256(all invariants || tensor || timestamp)
    };

    /**
     * Full binding from gaze sample sequence.
     * Runs the complete patented pipeline.
     */
    static BindResult FullBind(const std::vector<GazeSample>& samples,
                                double alpha = MarkovAGT::DEFAULT_ALPHA,
                                int modular_depth = 3);

    /**
     * Full binding from pre-built tensor (external Markov chain).
     */
    static BindResult FullBindFromTensor(const AGTTensor& tensor,
                                          int modular_depth = 3);

    /**
     * Verify that a commitment matches the given gaze samples.
     * Recomputes the full pipeline and checks equality.
     */
    static bool VerifyBinding(const BindResult& result,
                               const std::vector<GazeSample>& samples,
                               double alpha = MarkovAGT::DEFAULT_ALPHA,
                               int modular_depth = 3);

    /**
     * Hash AGT tensor to 32-byte digest for commitments.
     */
    static void TensorHash(const AGTTensor& tensor, uint8_t* out32);
};

} // namespace knotproof

#endif // BTQ_CRYPTO_KNOTPROOF_AGTBIND_H
