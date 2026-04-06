// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.

#include <crypto/knotproof/braid.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <sstream>

namespace knotproof {

BraidWord::BraidWord(std::vector<int> gens, int n)
    : generators(std::move(gens)), num_strands(n)
{
    assert(n >= 2);
    // Validate all generators are in range [-(n-1), -1] or [1, n-1]
    for (int g : generators) {
        assert(g != 0);
        assert(std::abs(g) < n);
    }
}

int BraidWord::Writhe() const
{
    int w = 0;
    for (int g : generators) {
        w += (g > 0) ? 1 : -1;
    }
    return w;
}

void BraidWord::Append(const BraidWord& other)
{
    assert(num_strands == other.num_strands);
    generators.insert(generators.end(),
                      other.generators.begin(),
                      other.generators.end());
}

BraidWord BraidWord::Inverse() const
{
    std::vector<int> inv(generators.rbegin(), generators.rend());
    for (auto& g : inv) g = -g;
    return BraidWord(std::move(inv), num_strands);
}

BraidWord BraidWord::Conjugate(const BraidWord& b) const
{
    // b * this * b^{-1}
    BraidWord result = b;
    result.Append(*this);
    result.Append(b.Inverse());
    return result;
}

BraidWord BraidWord::MarkovStabilize() const
{
    // Type II: add one strand, append sigma_n or sigma_n^{-1}
    std::vector<int> gens = generators;
    gens.push_back(num_strands); // sigma_n (positive)
    return BraidWord(std::move(gens), num_strands + 1);
}

std::vector<Crossing> BraidWord::ToPDCode() const
{
    // Convert braid closure to PD notation.
    // Each generator sigma_i (or sigma_i^{-1}) produces one crossing.
    // Arc numbering follows the strands through the braid.
    int n = num_strands;
    int num_crossings = static_cast<int>(generators.size());
    if (num_crossings == 0) return {};

    // Allocate arcs: each crossing has 4 arc endpoints.
    // Total arcs = 2 * num_crossings (each crossing creates 2 new arcs).
    int arc_counter = 0;

    // Track which arc is currently at each strand position.
    // strand_arc[i] = current arc index on strand i (0-indexed).
    std::vector<int> strand_arc(n);
    for (int i = 0; i < n; i++) {
        strand_arc[i] = arc_counter++;
    }

    std::vector<Crossing> crossings;
    crossings.reserve(num_crossings);

    for (int ci = 0; ci < num_crossings; ci++) {
        int g = generators[ci];
        int strand = std::abs(g) - 1; // 0-indexed strand
        int sign = (g > 0) ? 1 : -1;

        // Incoming arcs
        int in_under = strand_arc[strand];
        int in_over = strand_arc[strand + 1];

        // Outgoing arcs (new)
        int out_under = arc_counter++;
        int out_over = arc_counter++;

        // PD notation: [in_under, in_over, out_under, out_over]
        // For positive crossing: strand goes over strand+1
        // For negative crossing: strand goes under strand+1
        Crossing c;
        if (sign > 0) {
            c.arcs[0] = in_under;
            c.arcs[1] = in_over;
            c.arcs[2] = out_under;
            c.arcs[3] = out_over;
        } else {
            c.arcs[0] = in_over;
            c.arcs[1] = in_under;
            c.arcs[2] = out_over;
            c.arcs[3] = out_under;
        }
        c.sign = sign;
        c.weight = 1;
        crossings.push_back(c);

        // Update strand tracking
        strand_arc[strand] = out_under;
        strand_arc[strand + 1] = out_over;
    }

    return crossings;
}

std::vector<uint8_t> BraidWord::ToBytes() const
{
    // Format: [4 bytes num_strands] [4 bytes num_generators]
    //         [4 bytes per generator (signed, little-endian)]
    std::vector<uint8_t> out;
    uint32_t ns = static_cast<uint32_t>(num_strands);
    uint32_t ng = static_cast<uint32_t>(generators.size());
    out.resize(4 + 4 + ng * 4);

    std::memcpy(out.data(), &ns, 4);
    std::memcpy(out.data() + 4, &ng, 4);
    for (uint32_t i = 0; i < ng; i++) {
        int32_t g = static_cast<int32_t>(generators[i]);
        std::memcpy(out.data() + 8 + i * 4, &g, 4);
    }
    return out;
}

bool BraidWord::FromBytes(const std::vector<uint8_t>& data)
{
    if (data.size() < 8) return false;

    uint32_t ns, ng;
    std::memcpy(&ns, data.data(), 4);
    std::memcpy(&ng, data.data() + 4, 4);

    if (ns < 2 || ns > 64) return false;
    if (ng > 1024) return false;
    if (data.size() != 8 + ng * 4) return false;

    num_strands = static_cast<int>(ns);
    generators.resize(ng);
    for (uint32_t i = 0; i < ng; i++) {
        int32_t g;
        std::memcpy(&g, data.data() + 8 + i * 4, 4);
        if (g == 0 || std::abs(g) >= num_strands) return false;
        generators[i] = g;
    }
    return true;
}

std::string BraidWord::ToString() const
{
    if (generators.empty()) return "e"; // identity
    std::ostringstream ss;
    for (size_t i = 0; i < generators.size(); i++) {
        if (i > 0) ss << " ";
        int g = generators[i];
        if (g > 0) {
            ss << "s" << g;
        } else {
            ss << "s" << (-g) << "^{-1}";
        }
    }
    return ss.str();
}

BraidWord BraidWord::FromHash(const uint8_t* hash32, int target_strands, int target_crossings)
{
    assert(target_strands >= 2 && target_strands <= 64);
    assert(target_crossings >= 1 && target_crossings <= 256);

    std::vector<int> gens;
    gens.reserve(target_crossings);

    // Use hash bytes to deterministically generate braid generators.
    // Each byte encodes: which strand (modulo n-1) and sign (high bit).
    for (int i = 0; i < target_crossings; i++) {
        uint8_t b = hash32[i % 32];
        // Mix with position to avoid repetition when target_crossings > 32
        if (i >= 32) {
            b ^= hash32[(i * 7 + 3) % 32];
            b ^= static_cast<uint8_t>(i & 0xFF);
        }

        int strand_idx = (b & 0x7F) % (target_strands - 1) + 1; // 1-indexed
        int sign = (b & 0x80) ? -1 : 1;
        gens.push_back(sign * strand_idx);
    }

    return BraidWord(std::move(gens), target_strands);
}

BraidWord BraidWord::Trefoil()
{
    // Trefoil = closure of sigma_1^3 in B_2
    return BraidWord({1, 1, 1}, 2);
}

BraidWord BraidWord::FigureEight()
{
    // Figure-eight = closure of sigma_1 sigma_2^{-1} sigma_1 sigma_2^{-1} in B_3
    return BraidWord({1, -2, 1, -2}, 3);
}

} // namespace knotproof
