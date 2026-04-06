// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.

#include <crypto/knotproof/jones.h>

#include <cassert>
#include <cmath>
#include <unordered_map>

namespace knotproof {

int Jones::CountLoops(const std::vector<Crossing>& crossings, uint32_t state)
{
    int n = static_cast<int>(crossings.size());
    if (n == 0) return 1; // No crossings = one trivial loop

    // Find max arc index
    int max_arc = 0;
    for (const auto& c : crossings) {
        for (int k = 0; k < 4; k++) {
            max_arc = std::max(max_arc, c.arcs[k]);
        }
    }

    // Union-Find to track connected arc components
    std::vector<int> parent(max_arc + 1);
    for (int i = 0; i <= max_arc; i++) parent[i] = i;

    // Find with path compression
    auto find = [&](int x) -> int {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    auto unite = [&](int x, int y) {
        x = find(x);
        y = find(y);
        if (x != y) parent[x] = y;
    };

    // For each crossing, resolve according to state bitmask
    for (int i = 0; i < n; i++) {
        const auto& c = crossings[i];
        bool b_smoothing = (state >> i) & 1;

        if (!b_smoothing) {
            // A-smoothing: connect arcs[0]-arcs[3] and arcs[1]-arcs[2]
            unite(c.arcs[0], c.arcs[3]);
            unite(c.arcs[1], c.arcs[2]);
        } else {
            // B-smoothing: connect arcs[0]-arcs[1] and arcs[2]-arcs[3]
            unite(c.arcs[0], c.arcs[1]);
            unite(c.arcs[2], c.arcs[3]);
        }
    }

    // Count distinct components among arcs used in crossings
    std::vector<bool> seen(max_arc + 1, false);
    std::vector<bool> used(max_arc + 1, false);
    for (const auto& c : crossings) {
        for (int k = 0; k < 4; k++) used[c.arcs[k]] = true;
    }

    int loops = 0;
    for (int i = 0; i <= max_arc; i++) {
        if (!used[i]) continue;
        int root = find(i);
        if (!seen[root]) {
            seen[root] = true;
            loops++;
        }
    }
    return loops;
}

LaurentPolynomial Jones::KauffmanBracket(
    const std::vector<Crossing>& crossings,
    int num_crossings)
{
    assert(num_crossings <= MAX_CROSSINGS);

    // <K> = sum over all states s of A^{a(s)-b(s)} * (-A^2 - A^{-2})^{loops(s)-1}
    // where a(s) = number of A-smoothings, b(s) = number of B-smoothings

    // Loop factor: d = -A^2 - A^{-2}
    LaurentPolynomial d = LaurentPolynomial::Monomial(-1, 2) +
                           LaurentPolynomial::Monomial(-1, -2);

    LaurentPolynomial bracket = LaurentPolynomial::Zero();

    uint32_t num_states = 1u << num_crossings;
    for (uint32_t state = 0; state < num_states; state++) {
        // Count A and B smoothings
        int a_count = 0, b_count = 0;
        for (int i = 0; i < num_crossings; i++) {
            if ((state >> i) & 1) {
                b_count++;
            } else {
                a_count++;
            }
        }

        // A^{a-b} factor (accounting for crossing weights)
        int a_power = 0;
        for (int i = 0; i < num_crossings; i++) {
            bool is_b = (state >> i) & 1;
            int64_t w = crossings[i].weight;
            // Weighted: A-smoothing contributes +w, B-smoothing contributes -w
            // For unit weights, this reduces to a_count - b_count
            if (!is_b) {
                a_power += static_cast<int>(w);
            } else {
                a_power -= static_cast<int>(w);
            }
        }
        LaurentPolynomial a_factor = LaurentPolynomial::Monomial(1, a_power);

        // Loop factor: d^{loops-1}
        int loops = CountLoops(crossings, state);
        LaurentPolynomial loop_factor = LaurentPolynomial::One();
        for (int i = 1; i < loops; i++) {
            loop_factor = loop_factor * d;
        }

        bracket = bracket + (a_factor * loop_factor);
    }

    return bracket;
}

LaurentPolynomial Jones::Compute(const std::vector<Crossing>& crossings, int writhe)
{
    int n = static_cast<int>(crossings.size());
    if (n == 0) return LaurentPolynomial::One();
    if (n > MAX_CROSSINGS) {
        // Too complex — return zero to signal failure
        return LaurentPolynomial::Zero();
    }

    LaurentPolynomial bracket = KauffmanBracket(crossings, n);

    // Jones polynomial: V(q) = (-A^3)^{-writhe} * <K>
    // = (-1)^{-writhe} * A^{-3*writhe} * <K>
    int sign = (writhe % 2 == 0) ? 1 : -1;
    int a_shift = -3 * writhe;

    LaurentPolynomial normalization = LaurentPolynomial::Monomial(sign, a_shift);
    LaurentPolynomial jones = normalization * bracket;
    jones.Normalize();

    return jones;
}

LaurentPolynomial Jones::ComputeFromBraid(const BraidWord& braid)
{
    auto crossings = braid.ToPDCode();
    int writhe = braid.Writhe();
    return Compute(crossings, writhe);
}

bool Jones::Verify(const LaurentPolynomial& poly, int crossing_number)
{
    if (poly.IsZero()) return false;

    // Check 1: V(1) should be +-1 for knots
    // (In the A variable, evaluate at A = -1 for the unknot normalization)
    // For the standard substitution, V(1) = 1
    int64_t val_at_1 = poly.Eval(1);
    if (val_at_1 != 1 && val_at_1 != -1) return false;

    // Check 2: Degree span bounded by crossing number
    if (!poly.IsZero()) {
        int span = poly.MaxDegree() - poly.min_degree;
        // Jones polynomial degree span <= 4 * crossing_number (in A variable)
        if (span > 4 * crossing_number) return false;
    }

    return true;
}

} // namespace knotproof
