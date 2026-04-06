// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / Jett Optx (jettoptics.ai)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// Topological Key Derivation Function (TKDF) — GENSYS OPTX btQ v1.0
//
// Implements eq. (4) from the GENSYS whitepaper:
//   OPTX-KeyGen(K, s, n) = ML-DSA.KeyGen(H(α_K ∥ ν_K ∥ ω_K ∥ s), n)
//
// where:
//   α_K = Alexander(K, e^{2πi/n})  ∈ ℂ  (16 bytes IEEE 754)
//   ν_K = Jones(K, e^{2πi/n})      ∈ ℂ  (16 bytes IEEE 754)
//   ω_K = writhe(K)                 ∈ ℤ  (4 bytes int32 BE)
//   s   = entropy seed              ∈ {0,1}^256
//   H   = SHAKE-256 with 512-bit output
//
// Hybrid design per Grok 4.20 security review:
//   knot invariants → structured seed → SHAKE-256 → ML-DSA KeyGen
//   This preserves collision resistance of SHAKE-256 while adding
//   topological entropy that is orthogonal to lattice assumptions.
//
// US Patent Application Serial No. 19/243,050

#ifndef BTQ_CRYPTO_KNOT_INVARIANT_H
#define BTQ_CRYPTO_KNOT_INVARIANT_H

#include <cstdint>
#include <vector>

namespace optx {

// Size constants matching GENSYS BIP-360 witness extension spec
static constexpr size_t TKDF_ALPHA_SIZE = 16;   // IEEE 754 complex128
static constexpr size_t TKDF_NU_SIZE = 16;      // IEEE 754 complex128
static constexpr size_t TKDF_WRITHE_SIZE = 4;   // int32 big-endian
static constexpr size_t TKDF_SEED_SIZE = 32;    // 256-bit entropy
static constexpr size_t TKDF_OUTPUT_SIZE = 64;  // SHAKE-256 512-bit output
static constexpr size_t TKDF_INVARIANT_SIZE = TKDF_ALPHA_SIZE + TKDF_NU_SIZE + TKDF_WRITHE_SIZE; // 36 bytes

// Maximum DT code size (bounds crossing count to MAX_OPTX_CROSSINGS=16)
static constexpr size_t MAX_DT_CODE_SIZE = 64;

/**
 * Parsed knot data from OP_OPTX_KNOT witness stack.
 *
 * Wire format (big-endian):
 *   [dt_code_len:2] [dt_code:var] [alpha_K:16] [nu_K:16] [writhe:4]
 */
struct KnotWitnessData {
    std::vector<uint8_t> dt_code;        // Dowker-Thistlethwaite code
    uint8_t alpha_k[TKDF_ALPHA_SIZE];    // Alexander eval at root of unity
    uint8_t nu_k[TKDF_NU_SIZE];          // Jones eval at root of unity
    int32_t writhe;                       // Writhe number w(K)

    /** Deserialize from witness stack element. */
    bool FromBytes(const std::vector<uint8_t>& data);

    /** Serialize to witness stack element. */
    std::vector<uint8_t> ToBytes() const;

    /** Total serialized size. */
    size_t Size() const;
};

/**
 * TKDF: Topological Key Derivation Function.
 *
 * Core computation: SHAKE-256(α_K ∥ ν_K ∥ ω_K ∥ seed) → 512 bits
 *
 * The output is used as the seed for ML-DSA.KeyGen, replacing
 * raw entropy with topologically-augmented entropy.
 */
class TKDF {
public:
    /**
     * Derive TKDF output from knot invariants and entropy seed.
     *
     * @param alpha_k   Alexander polynomial evaluation (16 bytes, IEEE 754 complex128 BE)
     * @param nu_k      Jones polynomial evaluation (16 bytes, IEEE 754 complex128 BE)
     * @param writhe    Writhe number w(K)
     * @param seed      Entropy seed (32 bytes)
     * @param out       Output buffer (64 bytes, SHAKE-256 output)
     * @return true on success
     */
    static bool Derive(const uint8_t* alpha_k,
                       const uint8_t* nu_k,
                       int32_t writhe,
                       const uint8_t* seed,
                       uint8_t* out);

    /**
     * Derive from parsed KnotWitnessData + seed.
     */
    static bool DeriveFromWitness(const KnotWitnessData& knot,
                                   const uint8_t* seed,
                                   uint8_t* out);

    /**
     * Verify that knot invariants in witness match the DT code.
     * Recomputes Alexander, Jones, and writhe from the DT code
     * and compares against the claimed values.
     *
     * @param knot  Parsed witness data with DT code and claimed invariants
     * @param n     ML-DSA security level (2, 3, or 5) for root of unity
     * @return true if all invariants match
     */
    static bool VerifyInvariants(const KnotWitnessData& knot, int n = 2);
};

/**
 * Validate knot witness data bounds (DoS protection).
 * Enforces MAX_OPTX_CROSSINGS, MAX_OPTX_STRANDS, MAX_OPTX_WORD_LENGTH.
 *
 * @param data Raw witness stack element
 * @return true if within bounds
 */
bool CheckKnotDataBounds(const std::vector<uint8_t>& data);

} // namespace optx

#endif // BTQ_CRYPTO_KNOT_INVARIANT_H
