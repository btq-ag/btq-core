// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.

#include <crypto/knotproof/conway.h>
#include <crypto/knotproof/alexander.h>

#include <cmath>

namespace knotproof {

LaurentPolynomial Conway::FromAlexander(const LaurentPolynomial& alex)
{
    if (alex.IsZero()) return LaurentPolynomial::Zero();

    // The Conway polynomial ∇(z) relates to Alexander Δ(t) via:
    //   Δ(t) = ∇(t^{1/2} - t^{-1/2})
    //
    // Inversion: given Δ(t) = Σ aₖ t^k, compute ∇(z) such that
    // substituting z = t^{1/2} - t^{-1/2} recovers Δ.
    //
    // For symmetric Alexander polynomials (Δ(t) = Δ(t⁻¹)),
    // the Conway polynomial has only even powers of z.
    //
    // Algorithm: use the recursion based on degree.
    // For Δ(t) = a₀ + Σ_{k>0} aₖ(t^k + t^{-k}), express
    // t^k + t^{-k} in terms of z² using Chebyshev-like relations:
    //   t + t⁻¹ = z² + 2
    //   t² + t⁻² = (z² + 2)² - 2 = z⁴ + 4z² + 2
    //   t^k + t^{-k} = T_k(z² + 2) where T_k is Chebyshev polynomial

    // Find the symmetric part of Alexander
    int max_deg = alex.MaxDegree();
    int min_deg = alex.min_degree;
    int half_span = std::max(std::abs(max_deg), std::abs(min_deg));

    // Build powers of (z² + 2) iteratively
    // p_k represents t^k + t^{-k} in terms of z
    // p_0 = 2, p_1 = z² + 2, p_k = (z²+2)·p_{k-1} - p_{k-2}
    std::vector<LaurentPolynomial> chebyshev;

    // p_0 = 2 (constant)
    chebyshev.push_back(LaurentPolynomial({2}, 0));

    if (half_span >= 1) {
        // p_1 = z² + 2 (degree 2 in z)
        chebyshev.push_back(LaurentPolynomial({2, 0, 1}, 0));
    }

    // z² + 2 as a polynomial for multiplication
    LaurentPolynomial z2plus2({2, 0, 1}, 0);

    for (int k = 2; k <= half_span; k++) {
        // p_k = (z²+2) · p_{k-1} - p_{k-2}
        LaurentPolynomial pk = (z2plus2 * chebyshev[k - 1]) - chebyshev[k - 2];
        chebyshev.push_back(pk);
    }

    // Now reconstruct ∇(z):
    // Δ(t) = a₀ + Σ_{k>0} aₖ(t^k + t^{-k})
    // where aₖ = Coeff(k) for the symmetric Alexander polynomial.
    // Note: our Alexander is normalized so Δ(t) = Δ(t⁻¹) up to sign.

    LaurentPolynomial conway = LaurentPolynomial::Zero();

    // Constant term
    int64_t a0 = alex.Coeff(0);
    if (a0 != 0) {
        conway = conway + LaurentPolynomial({a0}, 0);
    }

    // Symmetric terms
    for (int k = 1; k <= half_span; k++) {
        int64_t ak = alex.Coeff(k);
        // For symmetric polynomials, Coeff(k) = Coeff(-k)
        // The Chebyshev expansion gives t^k + t^{-k}
        if (ak != 0 && k < static_cast<int>(chebyshev.size())) {
            conway = conway + (chebyshev[k] * ak);
        }
    }

    // Subtract the constant overcounting: each p_k already includes
    // the constant 2 for k=0 case. Adjust:
    // Actually, ∇(z) should satisfy ∇(0) = Δ(1) = ±1.
    // The direct Chebyshev expansion naturally handles this.

    conway.Normalize();
    return conway;
}

LaurentPolynomial Conway::Compute(const BraidWord& braid)
{
    LaurentPolynomial alex = Alexander::Compute(braid);
    return FromAlexander(alex);
}

bool Conway::Verify(const LaurentPolynomial& poly, int crossing_number)
{
    // ∇(0) = ±1 for knots (evaluating at z=0 gives the Alexander normalization)
    int64_t val_at_0 = poly.Eval(0);
    if (val_at_0 != 1 && val_at_0 != -1) return false;

    // Degree in z should be ≤ 2 * genus ≤ crossing number
    if (!poly.IsZero()) {
        if (poly.MaxDegree() > crossing_number) return false;
    }

    return true;
}

LaurentPolynomial Conway::Trefoil()
{
    // ∇_{3₁}(z) = 1 + z²
    return LaurentPolynomial({1, 0, 1}, 0);
}

LaurentPolynomial Conway::FigureEight()
{
    // ∇_{4₁}(z) = 1 - z²
    return LaurentPolynomial({1, 0, -1}, 0);
}

} // namespace knotproof
