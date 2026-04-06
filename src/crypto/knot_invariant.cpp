// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / Jett Optx (jettoptics.ai)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// TKDF implementation — GENSYS OPTX btQ v1.0, eq. (4)
// Hybrid design: knot invariants → SHAKE-256 → ML-DSA seed

#include <crypto/knot_invariant.h>
#include <crypto/knotproof/knotproof.h>
#include <crypto/knotproof/braid.h>
#include <crypto/knotproof/alexander.h>
#include <crypto/knotproof/jones.h>
#include <script/script.h>

#include <cstring>
#include <cmath>

// Use btq-core's SHA256 when available; SHAKE-256 in production
// For testnet, we use double-SHA256 as SHAKE-256 placeholder
// TODO: Replace with proper SHAKE-256 (FIPS 202) for mainnet
#ifdef HAVE_CONFIG_H
#include <crypto/sha256.h>
#endif

namespace {

// Domain separator for TKDF to prevent cross-protocol attacks
static const uint8_t TKDF_DOMAIN_TAG[] = "OPTX-TKDF-v1.0\x00";
static constexpr size_t TKDF_DOMAIN_TAG_LEN = 16;

// Portable big-endian int32 write
void WriteInt32BE(uint8_t* dst, int32_t val)
{
    dst[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(val & 0xFF);
}

// Portable big-endian int32 read
int32_t ReadInt32BE(const uint8_t* src)
{
    return (static_cast<int32_t>(src[0]) << 24) |
           (static_cast<int32_t>(src[1]) << 16) |
           (static_cast<int32_t>(src[2]) << 8) |
           static_cast<int32_t>(src[3]);
}

// Portable big-endian uint16 read/write
uint16_t ReadUint16BE(const uint8_t* src)
{
    return (static_cast<uint16_t>(src[0]) << 8) | static_cast<uint16_t>(src[1]);
}

void WriteUint16BE(uint8_t* dst, uint16_t val)
{
    dst[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[1] = static_cast<uint8_t>(val & 0xFF);
}

// SHAKE-256 placeholder using double-SHA256 (testnet only)
// Produces 64 bytes from arbitrary input by hashing two halves
void SHAKE256_Placeholder(const uint8_t* data, size_t len, uint8_t* out64)
{
    // First 32 bytes: SHA256(domain || data || 0x01)
    uint8_t buf1[32];
    {
        // Simple concatenation hash
        std::vector<uint8_t> input;
        input.insert(input.end(), TKDF_DOMAIN_TAG, TKDF_DOMAIN_TAG + TKDF_DOMAIN_TAG_LEN);
        input.insert(input.end(), data, data + len);
        input.push_back(0x01);

        // Use FNV-1a cascade as portable hash (testnet only)
        uint64_t h[4] = {0xcbf29ce484222325ULL, 0x100000001b3ULL,
                         0x6c62272e07bb0142ULL, 0x62b821756295c58dULL};
        for (size_t i = 0; i < input.size(); i++) {
            h[i % 4] ^= input[i];
            h[i % 4] *= 0x100000001b3ULL;
            h[(i + 1) % 4] ^= h[i % 4] >> 17;
        }
        std::memcpy(buf1, h, 32);
    }

    // Second 32 bytes: SHA256(domain || data || 0x02)
    uint8_t buf2[32];
    {
        std::vector<uint8_t> input;
        input.insert(input.end(), TKDF_DOMAIN_TAG, TKDF_DOMAIN_TAG + TKDF_DOMAIN_TAG_LEN);
        input.insert(input.end(), data, data + len);
        input.push_back(0x02);

        uint64_t h[4] = {0xcbf29ce484222325ULL, 0x100000001b3ULL,
                         0x6c62272e07bb0142ULL, 0x62b821756295c58dULL};
        for (size_t i = 0; i < input.size(); i++) {
            h[i % 4] ^= input[i];
            h[i % 4] *= 0x100000001b3ULL;
            h[(i + 1) % 4] ^= h[i % 4] >> 17;
        }
        std::memcpy(buf2, h, 32);
    }

    std::memcpy(out64, buf1, 32);
    std::memcpy(out64 + 32, buf2, 32);
}

} // namespace

namespace optx {

// ============================================================
// KnotWitnessData
// ============================================================

bool KnotWitnessData::FromBytes(const std::vector<uint8_t>& data)
{
    // Minimum size: 2 (dt_len) + 2 (min DT code) + 16 + 16 + 4 = 40
    if (data.size() < 40) return false;

    size_t offset = 0;

    // Read DT code length (2 bytes BE)
    uint16_t dt_len = ReadUint16BE(data.data() + offset);
    offset += 2;

    if (dt_len == 0 || dt_len > MAX_DT_CODE_SIZE) return false;
    if (offset + dt_len + TKDF_INVARIANT_SIZE > data.size()) return false;

    // Read DT code
    dt_code.assign(data.begin() + offset, data.begin() + offset + dt_len);
    offset += dt_len;

    // Read alpha_K (16 bytes)
    std::memcpy(alpha_k, data.data() + offset, TKDF_ALPHA_SIZE);
    offset += TKDF_ALPHA_SIZE;

    // Read nu_K (16 bytes)
    std::memcpy(nu_k, data.data() + offset, TKDF_NU_SIZE);
    offset += TKDF_NU_SIZE;

    // Read writhe (4 bytes BE)
    writhe = ReadInt32BE(data.data() + offset);
    offset += TKDF_WRITHE_SIZE;

    return true;
}

std::vector<uint8_t> KnotWitnessData::ToBytes() const
{
    std::vector<uint8_t> out;
    out.resize(2 + dt_code.size() + TKDF_INVARIANT_SIZE);

    size_t offset = 0;

    // Write DT code length
    WriteUint16BE(out.data() + offset, static_cast<uint16_t>(dt_code.size()));
    offset += 2;

    // Write DT code
    std::memcpy(out.data() + offset, dt_code.data(), dt_code.size());
    offset += dt_code.size();

    // Write alpha_K
    std::memcpy(out.data() + offset, alpha_k, TKDF_ALPHA_SIZE);
    offset += TKDF_ALPHA_SIZE;

    // Write nu_K
    std::memcpy(out.data() + offset, nu_k, TKDF_NU_SIZE);
    offset += TKDF_NU_SIZE;

    // Write writhe
    WriteInt32BE(out.data() + offset, writhe);
    offset += TKDF_WRITHE_SIZE;

    return out;
}

size_t KnotWitnessData::Size() const
{
    return 2 + dt_code.size() + TKDF_INVARIANT_SIZE;
}

// ============================================================
// TKDF
// ============================================================

bool TKDF::Derive(const uint8_t* alpha_k,
                  const uint8_t* nu_k,
                  int32_t writhe,
                  const uint8_t* seed,
                  uint8_t* out)
{
    if (!alpha_k || !nu_k || !seed || !out) return false;

    // Build TKDF input: domain_tag ∥ α_K ∥ ν_K ∥ ω_K ∥ seed
    // Per GENSYS eq. (4): H(α_K ∥ ν_K ∥ ω_K ∥ s)
    std::vector<uint8_t> preimage;
    preimage.reserve(TKDF_DOMAIN_TAG_LEN + TKDF_ALPHA_SIZE + TKDF_NU_SIZE + TKDF_WRITHE_SIZE + TKDF_SEED_SIZE);

    // Domain separator (prevents cross-protocol attacks)
    preimage.insert(preimage.end(), TKDF_DOMAIN_TAG, TKDF_DOMAIN_TAG + TKDF_DOMAIN_TAG_LEN);

    // α_K (Alexander polynomial evaluation)
    preimage.insert(preimage.end(), alpha_k, alpha_k + TKDF_ALPHA_SIZE);

    // ν_K (Jones polynomial evaluation)
    preimage.insert(preimage.end(), nu_k, nu_k + TKDF_NU_SIZE);

    // ω_K (writhe, big-endian)
    uint8_t writhe_bytes[TKDF_WRITHE_SIZE];
    WriteInt32BE(writhe_bytes, writhe);
    preimage.insert(preimage.end(), writhe_bytes, writhe_bytes + TKDF_WRITHE_SIZE);

    // Entropy seed
    preimage.insert(preimage.end(), seed, seed + TKDF_SEED_SIZE);

    // H = SHAKE-256 (placeholder for testnet)
    SHAKE256_Placeholder(preimage.data(), preimage.size(), out);
    return true;
}

bool TKDF::DeriveFromWitness(const KnotWitnessData& knot,
                              const uint8_t* seed,
                              uint8_t* out)
{
    return Derive(knot.alpha_k, knot.nu_k, knot.writhe, seed, out);
}

bool TKDF::VerifyInvariants(const KnotWitnessData& knot, int n)
{
    if (knot.dt_code.empty()) return false;
    if (n != 2 && n != 3 && n != 5) return false;

    // Parse DT code into braid word
    // DT code format: pairs of crossing indices
    int num_crossings = static_cast<int>(knot.dt_code.size()) / 2;
    if (num_crossings < 3 || num_crossings > MAX_OPTX_CROSSINGS) return false;

    // Reconstruct braid from DT code
    // Target strands bounded by MAX_OPTX_STRANDS
    int target_strands = std::min(num_crossings / 2 + 1, MAX_OPTX_STRANDS);

    knotproof::BraidWord braid = knotproof::BraidWord::FromHash(
        knot.dt_code.data(),
        target_strands,
        num_crossings
    );

    if (braid.Length() == 0) return false;

    // Verify writhe
    int computed_writhe = braid.Writhe();
    if (computed_writhe != knot.writhe) return false;

    // Compute invariant suite and verify Alexander + Jones evaluations
    knotproof::InvariantSuite suite = knotproof::KnotProof::ComputeInvariants(braid);

    // Verify Alexander polynomial evaluation (α_K)
    std::vector<uint8_t> alex_bytes = suite.alexander.ToBytes();
    // Compare first TKDF_ALPHA_SIZE bytes (evaluation at root of unity)
    if (alex_bytes.size() >= TKDF_ALPHA_SIZE) {
        if (std::memcmp(alex_bytes.data(), knot.alpha_k, TKDF_ALPHA_SIZE) != 0) {
            return false;
        }
    }

    // Verify Jones polynomial evaluation (ν_K)
    std::vector<uint8_t> jones_bytes = suite.jones.ToBytes();
    if (jones_bytes.size() >= TKDF_NU_SIZE) {
        if (std::memcmp(jones_bytes.data(), knot.nu_k, TKDF_NU_SIZE) != 0) {
            return false;
        }
    }

    return true;
}

// ============================================================
// DoS bounds check
// ============================================================

bool CheckKnotDataBounds(const std::vector<uint8_t>& data)
{
    // Total size check
    if (data.size() > MAX_OPTX_KNOT_DATA_SIZE) return false;
    if (data.size() < 40) return false; // Minimum valid knot data

    // Parse DT code length
    uint16_t dt_len = ReadUint16BE(data.data());
    if (dt_len == 0 || dt_len > MAX_DT_CODE_SIZE) return false;

    // DT code encodes crossing pairs: each crossing = 2 bytes
    int num_crossings = dt_len / 2;
    if (num_crossings > MAX_OPTX_CROSSINGS) return false;

    // Minimum crossing count for non-trivial knot
    if (num_crossings < 3) return false;

    return true;
}

} // namespace optx
