// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// Alexander polynomial computation via the Burau representation.
// Delta(t) in Z[t, t^{-1}] — a classical knot invariant.

#ifndef BTQ_CRYPTO_KNOTPROOF_ALEXANDER_H
#define BTQ_CRYPTO_KNOTPROOF_ALEXANDER_H

#include <crypto/knotproof/braid.h>
#include <crypto/knotproof/laurentpoly.h>

#include <vector>

namespace knotproof {

/**
 * Alexander polynomial engine.
 *
 * Computes Delta(t) via the reduced Burau representation:
 *   sigma_i -> I with row i replaced by [-t, 1] block
 *   Delta(t) = det(I - reduced_Burau(braid)) * normalization
 *
 * The result is normalized to canonical form:
 *   - Multiply by t^k to clear negative powers
 *   - Leading coefficient positive
 *   - Symmetric: Delta(t) = Delta(t^{-1}) up to units
 */
class Alexander {
public:
    /**
     * Compute Alexander polynomial from a braid word.
     * Uses the reduced Burau matrix determinant method.
     */
    static LaurentPolynomial Compute(const BraidWord& braid);

    /**
     * Compute from PD code crossings directly.
     * Builds the Seifert matrix and computes det(V - tV^T).
     */
    static LaurentPolynomial ComputeFromPD(const std::vector<Crossing>& crossings);

    /**
     * Verify the Alexander polynomial satisfies known constraints:
     *   - Delta(1) = +/- 1 for knots
     *   - Symmetry: Delta(t) = Delta(t^{-1}) up to sign and t^k
     *   - Degree bounds from crossing number
     * Returns true if all checks pass.
     */
    static bool Verify(const LaurentPolynomial& poly, int crossing_number);

private:
    /**
     * Build the reduced Burau matrix for a braid word.
     * Returns (n-1) x (n-1) matrix of Laurent polynomials.
     */
    static std::vector<std::vector<LaurentPolynomial>> BurauMatrix(const BraidWord& braid);

    /**
     * Compute determinant of a matrix of Laurent polynomials.
     * Uses Bareiss algorithm adapted for polynomial entries.
     */
    static LaurentPolynomial PolyDeterminant(
        std::vector<std::vector<LaurentPolynomial>>& matrix);
};

} // namespace knotproof

#endif // BTQ_CRYPTO_KNOTPROOF_ALEXANDER_H
