// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.

#include <crypto/knotproof/laurentpoly.h>

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstring>
#include <sstream>

namespace knotproof {

LaurentPolynomial::LaurentPolynomial(std::vector<int64_t> c, int min_deg)
    : coeffs(std::move(c)), min_degree(min_deg)
{
    Normalize();
}

void LaurentPolynomial::Normalize()
{
    // Strip trailing zeros
    while (!coeffs.empty() && coeffs.back() == 0) {
        coeffs.pop_back();
    }
    // Strip leading zeros (adjust min_degree)
    while (!coeffs.empty() && coeffs.front() == 0) {
        coeffs.erase(coeffs.begin());
        min_degree++;
    }
    if (coeffs.empty()) {
        min_degree = 0;
        return;
    }
    // Sign convention: lowest non-zero coefficient positive
    if (coeffs.front() < 0) {
        for (auto& c : coeffs) c = -c;
    }
}

int LaurentPolynomial::MaxDegree() const
{
    if (coeffs.empty()) return INT_MIN;
    return min_degree + static_cast<int>(coeffs.size()) - 1;
}

bool LaurentPolynomial::IsZero() const
{
    return coeffs.empty();
}

int64_t LaurentPolynomial::Coeff(int k) const
{
    int idx = k - min_degree;
    if (idx < 0 || idx >= static_cast<int>(coeffs.size())) return 0;
    return coeffs[idx];
}

void LaurentPolynomial::Shift(int k)
{
    min_degree += k;
}

int64_t LaurentPolynomial::Eval(int64_t t) const
{
    if (coeffs.empty()) return 0;
    // For negative min_degree, requires t != 0 and integer division
    // This is primarily for testing with small values
    int64_t result = 0;
    for (int i = 0; i < static_cast<int>(coeffs.size()); i++) {
        int deg = min_degree + i;
        int64_t term = coeffs[i];
        if (deg > 0) {
            for (int j = 0; j < deg; j++) term *= t;
        } else if (deg < 0) {
            // Integer division — only exact for t = ±1
            int64_t denom = 1;
            for (int j = 0; j < -deg; j++) denom *= t;
            if (denom != 0) term /= denom;
        }
        result += term;
    }
    return result;
}

LaurentPolynomial LaurentPolynomial::operator+(const LaurentPolynomial& other) const
{
    if (IsZero()) return other;
    if (other.IsZero()) return *this;

    int lo = std::min(min_degree, other.min_degree);
    int hi = std::max(MaxDegree(), other.MaxDegree());
    std::vector<int64_t> result(hi - lo + 1, 0);

    for (int k = lo; k <= hi; k++) {
        result[k - lo] = Coeff(k) + other.Coeff(k);
    }
    return LaurentPolynomial(std::move(result), lo);
}

LaurentPolynomial LaurentPolynomial::operator-(const LaurentPolynomial& other) const
{
    if (other.IsZero()) return *this;

    int lo = std::min(min_degree, other.min_degree);
    int hi = std::max(MaxDegree(), other.MaxDegree());
    std::vector<int64_t> result(hi - lo + 1, 0);

    for (int k = lo; k <= hi; k++) {
        result[k - lo] = Coeff(k) - other.Coeff(k);
    }
    return LaurentPolynomial(std::move(result), lo);
}

LaurentPolynomial LaurentPolynomial::operator*(const LaurentPolynomial& other) const
{
    if (IsZero() || other.IsZero()) return Zero();

    int new_min = min_degree + other.min_degree;
    int new_size = static_cast<int>(coeffs.size()) + static_cast<int>(other.coeffs.size()) - 1;
    std::vector<int64_t> result(new_size, 0);

    for (int i = 0; i < static_cast<int>(coeffs.size()); i++) {
        for (int j = 0; j < static_cast<int>(other.coeffs.size()); j++) {
            result[i + j] += coeffs[i] * other.coeffs[j];
        }
    }
    return LaurentPolynomial(std::move(result), new_min);
}

LaurentPolynomial LaurentPolynomial::operator*(int64_t scalar) const
{
    if (scalar == 0) return Zero();
    std::vector<int64_t> result(coeffs.size());
    for (size_t i = 0; i < coeffs.size(); i++) {
        result[i] = coeffs[i] * scalar;
    }
    return LaurentPolynomial(std::move(result), min_degree);
}

bool LaurentPolynomial::operator==(const LaurentPolynomial& other) const
{
    // Both must be normalized for this to work
    LaurentPolynomial a = *this;
    LaurentPolynomial b = other;
    a.Normalize();
    b.Normalize();
    return a.min_degree == b.min_degree && a.coeffs == b.coeffs;
}

bool LaurentPolynomial::operator!=(const LaurentPolynomial& other) const
{
    return !(*this == other);
}

std::vector<uint8_t> LaurentPolynomial::ToBytes() const
{
    // Format: [4 bytes min_degree (signed, little-endian)]
    //         [4 bytes num_coeffs]
    //         [8 bytes per coefficient (signed, little-endian)]
    std::vector<uint8_t> out;
    out.resize(4 + 4 + coeffs.size() * 8);

    int32_t md = static_cast<int32_t>(min_degree);
    uint32_t nc = static_cast<uint32_t>(coeffs.size());
    std::memcpy(out.data(), &md, 4);
    std::memcpy(out.data() + 4, &nc, 4);
    for (size_t i = 0; i < coeffs.size(); i++) {
        int64_t c = coeffs[i];
        std::memcpy(out.data() + 8 + i * 8, &c, 8);
    }
    return out;
}

bool LaurentPolynomial::FromBytes(const std::vector<uint8_t>& data)
{
    if (data.size() < 8) return false;

    int32_t md;
    uint32_t nc;
    std::memcpy(&md, data.data(), 4);
    std::memcpy(&nc, data.data() + 4, 4);

    // Sanity limits: max 256 coefficients
    if (nc > 256) return false;
    if (data.size() != 8 + nc * 8) return false;

    min_degree = md;
    coeffs.resize(nc);
    for (uint32_t i = 0; i < nc; i++) {
        std::memcpy(&coeffs[i], data.data() + 8 + i * 8, 8);
    }
    Normalize();
    return true;
}

std::string LaurentPolynomial::ToString() const
{
    if (IsZero()) return "0";
    std::ostringstream ss;
    bool first = true;
    for (int i = 0; i < static_cast<int>(coeffs.size()); i++) {
        int64_t c = coeffs[i];
        if (c == 0) continue;
        int deg = min_degree + i;

        if (!first && c > 0) ss << " + ";
        else if (!first && c < 0) ss << " - ";
        else if (c < 0) ss << "-";

        int64_t ac = std::abs(c);
        if (deg == 0) {
            ss << ac;
        } else if (ac != 1) {
            ss << ac;
        }
        if (deg == 1) ss << "t";
        else if (deg == -1) ss << "t^{-1}";
        else if (deg != 0) ss << "t^{" << deg << "}";

        first = false;
    }
    return ss.str();
}

LaurentPolynomial LaurentPolynomial::Monomial(int64_t coeff, int degree)
{
    if (coeff == 0) return Zero();
    return LaurentPolynomial({coeff}, degree);
}

LaurentPolynomial LaurentPolynomial::Zero()
{
    return LaurentPolynomial();
}

LaurentPolynomial LaurentPolynomial::One()
{
    return LaurentPolynomial({1}, 0);
}

} // namespace knotproof
