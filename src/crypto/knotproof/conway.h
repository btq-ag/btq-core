// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// Conway polynomial ∇(z) ∈ Z[z] — reparametrization of Alexander.
// GENSYS OPTX v2.2.2 Level 12.
//
// Skein relation: ∇(L+) - ∇(L-) = z · ∇(L0)
// Relationship to Alexander: Δ_K(t) = ∇_K(t^{1/2} - t^{-1/2})
//
// Conway polynomial has improved algebraic properties:
//   - Additivity under connected sum: ∇_{K1#K2}(z) = ∇_{K1}(z) · ∇_{K2}(z)
//   - Integer coefficients (no Laurent variable)
//   - Sign distinguishes knots: trefoil ∇ = 1+z², figure-eight ∇ = 1-z²

#ifndef BTQ_CRYPTO_KNOTPROOF_CONWAY_H
#define BTQ_CRYPTO_KNOTPROOF_CONWAY_H

#include <crypto/knotproof/braid.h>
#include <crypto/knotproof/laurentpoly.h>

#include <cstdint>
#include <vector>

namespace knotproof {

/**
 * Conway polynomial engine.
 *
 * The Conway polynomial ∇_K(z) is derived from the Alexander polynomial
 * via the substitution z = t^{1/2} - t^{-1/2}.
 *
 * Since we work with integer polynomials in z (not Laurent),
 * the result is stored as a LaurentPolynomial with min_degree ≥ 0
 * (effectively a standard polynomial in z).
 */
class Conway {
public:
    /**
     * Compute Conway polynomial from a braid word.
     * First computes Alexander Δ(t), then reparametrizes.
     */
    static LaurentPolynomial Compute(const BraidWord& braid);

    /**
     * Convert Alexander polynomial Δ(t) to Conway polynomial ∇(z).
     * Substitutes t = ((z + √(z²+4))/2)² and simplifies.
     *
     * In practice, we use the inverse relationship:
     *   ∇(z) has the same coefficients as Δ(t) after the variable
     *   change, with integer output guaranteed.
     */
    static LaurentPolynomial FromAlexander(const LaurentPolynomial& alex);

    /**
     * Verify Conway polynomial constraints:
     *   - ∇(0) = 1 (unknot normalization)
     *   - Connected sum multiplicativity (if K = K1#K2, ∇_K = ∇_{K1} · ∇_{K2})
     *   - Degree bounds from crossing number
     */
    static bool Verify(const LaurentPolynomial& poly, int crossing_number);

    /**
     * Known Conway polynomials for test knots.
     */
    static LaurentPolynomial Trefoil();     // 1 + z²
    static LaurentPolynomial FigureEight(); // 1 - z²
};

} // namespace knotproof

#endif // BTQ_CRYPTO_KNOTPROOF_CONWAY_H
