// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// Jones polynomial computation via the Kauffman bracket.
// V(t) in Z[t^{1/2}, t^{-1/2}] — a stronger knot invariant than Alexander.

#ifndef BTQ_CRYPTO_KNOTPROOF_JONES_H
#define BTQ_CRYPTO_KNOTPROOF_JONES_H

#include <crypto/knotproof/braid.h>
#include <crypto/knotproof/laurentpoly.h>

#include <vector>

namespace knotproof {

/**
 * Jones polynomial engine.
 *
 * Computes V(t) via the Kauffman bracket <K>:
 *   - Each crossing is resolved in two ways (A and B smoothing)
 *   - <K> = A * <K_0> + A^{-1} * <K_inf> for each crossing
 *   - V(t) = (-A^3)^{-writhe} * <K> with A = t^{-1/4}
 *
 * We work in Z[A, A^{-1}] internally, then substitute A^4 = t^{-1}
 * to express the result in Z[t^{1/2}, t^{-1/2}].
 *
 * For integer coefficient output, we use the variable substitution
 * A = q (so the result is in Z[q, q^{-1}]) and note the relationship.
 *
 * Complexity: O(2^n) in crossing number n. For btq-core, we cap
 * crossing number at MAX_CROSSINGS to bound computation.
 */
class Jones {
public:
    static constexpr int MAX_CROSSINGS = 24;

    /**
     * Compute Jones polynomial from PD code crossings.
     * Returns polynomial in Z[q, q^{-1}] where q = A (Kauffman variable).
     * Convert to standard V(t) via q = t^{-1/4}.
     */
    static LaurentPolynomial Compute(const std::vector<Crossing>& crossings, int writhe);

    /**
     * Compute from braid word (converts to PD code first).
     */
    static LaurentPolynomial ComputeFromBraid(const BraidWord& braid);

    /**
     * Verify Jones polynomial constraints:
     *   - V(1) = 1 for knots
     *   - Degree span bounded by crossing number
     * Returns true if all checks pass.
     */
    static bool Verify(const LaurentPolynomial& poly, int crossing_number);

private:
    /**
     * Compute Kauffman bracket recursively with memoization.
     * state: bitmask of crossing resolutions (0 = A-smoothing, 1 = B-smoothing)
     */
    static LaurentPolynomial KauffmanBracket(
        const std::vector<Crossing>& crossings,
        int num_crossings);

    /**
     * Count connected components (loops) after all crossings are resolved.
     * Each loop contributes a factor of (-A^2 - A^{-2}).
     */
    static int CountLoops(const std::vector<Crossing>& crossings, uint32_t state);
};

} // namespace knotproof

#endif // BTQ_CRYPTO_KNOTPROOF_JONES_H
