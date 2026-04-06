// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// Braid word representation and planar diagram (PD) notation for knots.
// Converts between braid group generators and crossing diagrams.

#ifndef BTQ_CRYPTO_KNOTPROOF_BRAID_H
#define BTQ_CRYPTO_KNOTPROOF_BRAID_H

#include <cstdint>
#include <string>
#include <vector>

namespace knotproof {

/**
 * A crossing in planar diagram notation.
 * Each crossing has 4 arc indices (i, j, k, l) ordered counterclockwise
 * from the incoming under-strand, plus a sign (+1 or -1).
 */
struct Crossing {
    int arcs[4];   // Arc indices (counterclockwise from under-in)
    int sign;      // +1 = positive (right-hand), -1 = negative (left-hand)
    int64_t weight; // AGT tensor weight bound to this crossing (default 1)

    Crossing() : arcs{0, 0, 0, 0}, sign(1), weight(1) {}
    Crossing(int a, int b, int c, int d, int s, int64_t w = 1)
        : arcs{a, b, c, d}, sign(s), weight(w) {}
};

/**
 * BraidWord represents an element of the braid group B_n.
 *
 * Generators: sigma_i (positive crossing of strand i over i+1)
 *             sigma_i^{-1} (negative crossing)
 *
 * Stored as a sequence of signed generator indices:
 *   +i means sigma_i, -i means sigma_i^{-1} (1-indexed)
 *
 * The braid index n is the number of strands.
 */
class BraidWord {
public:
    std::vector<int> generators;  // Signed generator indices
    int num_strands{2};           // Braid index n (>= 2)

    BraidWord() = default;
    BraidWord(std::vector<int> gens, int n);

    /** Number of crossings (word length). */
    int Length() const { return static_cast<int>(generators.size()); }

    /** Writhe = sum of signs of all crossings. */
    int Writhe() const;

    /** Append another braid word (concatenation = composition). */
    void Append(const BraidWord& other);

    /** Conjugate: b * this * b^{-1}. */
    BraidWord Conjugate(const BraidWord& b) const;

    /** Invert: reverse order, negate all generators. */
    BraidWord Inverse() const;

    /** Apply Markov move type I (conjugation) or II (stabilization). */
    BraidWord MarkovStabilize() const;

    /** Convert braid closure to planar diagram crossings. */
    std::vector<Crossing> ToPDCode() const;

    /** Serialize to bytes for hashing. */
    std::vector<uint8_t> ToBytes() const;

    /** Deserialize from bytes. Returns false on malformed input. */
    bool FromBytes(const std::vector<uint8_t>& data);

    /** Human-readable string (e.g., "s1 s2^{-1} s1"). */
    std::string ToString() const;

    /**
     * Create a braid from a deterministic hash.
     * Maps 32 bytes -> braid word with controlled complexity.
     * Used to generate knot diagrams from transaction data.
     */
    static BraidWord FromHash(const uint8_t* hash32, int target_strands, int target_crossings);

    /**
     * Create trefoil braid (sigma_1^3 in B_2).
     * Simplest nontrivial knot for testing.
     */
    static BraidWord Trefoil();

    /**
     * Create figure-eight braid (sigma_1 sigma_2^{-1} sigma_1 sigma_2^{-1} in B_3).
     */
    static BraidWord FigureEight();
};

} // namespace knotproof

#endif // BTQ_CRYPTO_KNOTPROOF_BRAID_H
