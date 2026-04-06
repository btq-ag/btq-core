// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// Laurent polynomial over Z[t, t^-1] for knot invariant computation.
// Used by Alexander and Jones polynomial engines.

#ifndef BTQ_CRYPTO_KNOTPROOF_LAURENTPOLY_H
#define BTQ_CRYPTO_KNOTPROOF_LAURENTPOLY_H

#include <cstdint>
#include <string>
#include <vector>

namespace knotproof {

/**
 * LaurentPolynomial represents an element of Z[t, t^-1].
 *
 * Stored as a vector of coefficients with a min_degree offset.
 * coeffs[i] is the coefficient of t^(min_degree + i).
 *
 * Canonical form: no leading/trailing zero coefficients,
 * and the lowest non-zero coefficient is positive (sign convention).
 */
class LaurentPolynomial {
public:
    std::vector<int64_t> coeffs;
    int min_degree{0};

    LaurentPolynomial() = default;
    LaurentPolynomial(std::vector<int64_t> c, int min_deg);

    /** Remove leading/trailing zeros, enforce sign convention. */
    void Normalize();

    /** Return degree of highest non-zero term. -inf (INT_MIN) if zero polynomial. */
    int MaxDegree() const;

    /** Return true if this is the zero polynomial. */
    bool IsZero() const;

    /** Coefficient of t^k. Returns 0 if out of range. */
    int64_t Coeff(int k) const;

    /** Multiply by t^k (degree shift). */
    void Shift(int k);

    /** Evaluate at integer t (for testing). */
    int64_t Eval(int64_t t) const;

    LaurentPolynomial operator+(const LaurentPolynomial& other) const;
    LaurentPolynomial operator-(const LaurentPolynomial& other) const;
    LaurentPolynomial operator*(const LaurentPolynomial& other) const;
    LaurentPolynomial operator*(int64_t scalar) const;
    bool operator==(const LaurentPolynomial& other) const;
    bool operator!=(const LaurentPolynomial& other) const;

    /** Deterministic canonical byte serialization for hashing/signatures. */
    std::vector<uint8_t> ToBytes() const;

    /** Deserialize from canonical bytes. Returns false on malformed input. */
    bool FromBytes(const std::vector<uint8_t>& data);

    /** Human-readable string (e.g., "-t^{-1} + 1 - t"). */
    std::string ToString() const;

    /** Create monomial c * t^k. */
    static LaurentPolynomial Monomial(int64_t coeff, int degree);

    /** The zero polynomial. */
    static LaurentPolynomial Zero();

    /** The identity polynomial (1). */
    static LaurentPolynomial One();
};

} // namespace knotproof

#endif // BTQ_CRYPTO_KNOTPROOF_LAURENTPOLY_H
