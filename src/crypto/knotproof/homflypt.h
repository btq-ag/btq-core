// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// HOMFLY-PT polynomial P(α,z) — two-variable knot invariant.
// GENSYS OPTX v2.2.2 Level 14.
//
// Skein relation: α·P(L+) - α⁻¹·P(L-) = z·P(L0)
//
// Specializations:
//   P(q, q^{1/2} - q^{-1/2}) = V(q)     (Jones)
//   P(1, t^{1/2} - t^{-1/2}) = Δ(t)     (Alexander)
//
// The two-variable structure provides O(λ²) bits of key material
// vs. O(λ) for single-variable invariants.

#ifndef BTQ_CRYPTO_KNOTPROOF_HOMFLYPT_H
#define BTQ_CRYPTO_KNOTPROOF_HOMFLYPT_H

#include <crypto/knotproof/braid.h>
#include <crypto/knotproof/laurentpoly.h>

#include <cstdint>
#include <map>
#include <vector>

namespace knotproof {

/**
 * Two-variable Laurent polynomial in Z[α^±1, z^±1].
 *
 * Stored as a map from (α-degree, z-degree) pairs to coefficients.
 * This is a sparse representation suitable for the HOMFLY-PT polynomial
 * which typically has O(crossing_number²) terms.
 */
struct BivariatePoly {
    std::map<std::pair<int, int>, int64_t> terms; // (α_deg, z_deg) → coefficient

    BivariatePoly() = default;

    /** Create monomial c · α^a · z^b. */
    static BivariatePoly Monomial(int64_t coeff, int alpha_deg, int z_deg);

    /** The zero polynomial. */
    static BivariatePoly Zero() { return BivariatePoly(); }

    /** The constant 1. */
    static BivariatePoly One();

    bool IsZero() const { return terms.empty(); }

    BivariatePoly operator+(const BivariatePoly& other) const;
    BivariatePoly operator-(const BivariatePoly& other) const;
    BivariatePoly operator*(const BivariatePoly& other) const;
    BivariatePoly operator*(int64_t scalar) const;

    /** Remove zero terms. */
    void Normalize();

    /** Evaluate at (α, z) = (a_val, z_val). */
    int64_t Eval(int64_t a_val, int64_t z_val) const;

    /** Specialize to Jones: P(q, q^{1/2} - q^{-1/2}). Returns Z[q^±1]. */
    LaurentPolynomial SpecializeJones() const;

    /** Specialize to Alexander: P(1, t^{1/2} - t^{-1/2}). Returns Z[t^±1]. */
    LaurentPolynomial SpecializeAlexander() const;

    /** Serialize to bytes. */
    std::vector<uint8_t> ToBytes() const;

    /** Deserialize from bytes. Returns false on malformed input. */
    bool FromBytes(const std::vector<uint8_t>& data);

    /** Human-readable string. */
    std::string ToString() const;
};

/**
 * HOMFLY-PT polynomial engine.
 *
 * Computes P(α,z) via the skein relation applied recursively
 * to crossings of the braid closure, with memoization.
 *
 * Complexity: O(2^n) in crossing number (same as Jones).
 * MAX_CROSSINGS bounds the computation.
 */
class HOMFLYPT {
public:
    static constexpr int MAX_CROSSINGS = 24;

    /**
     * Compute HOMFLY-PT from braid word via Ocneanu trace.
     * Uses the Hecke algebra representation of Bₙ.
     */
    static BivariatePoly Compute(const BraidWord& braid);

    /**
     * Compute from PD code crossings via skein tree.
     */
    static BivariatePoly ComputeFromPD(const std::vector<Crossing>& crossings, int writhe);

    /**
     * Verify HOMFLY-PT constraints:
     *   - P(1,0) = 1 for knots
     *   - Specializations must match Jones and Alexander
     */
    static bool Verify(const BivariatePoly& poly, int crossing_number);

    /**
     * Known HOMFLY-PT polynomials for test knots.
     */
    static BivariatePoly Trefoil();
    static BivariatePoly FigureEight();
};

} // namespace knotproof

#endif // BTQ_CRYPTO_KNOTPROOF_HOMFLYPT_H
