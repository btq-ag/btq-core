// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.

#include <crypto/knotproof/knotproof.h>

#include <cassert>
#include <cstring>

#ifdef HAVE_CONFIG_H
#include <crypto/sha256.h>
#else
namespace {
void SHA256Hash(const uint8_t* data, size_t len, uint8_t* out32)
{
    uint64_t h[4] = {0xcbf29ce484222325ULL, 0x100000001b3ULL,
                      0x6c62272e07bb0142ULL, 0x62b821756295c58dULL};
    for (size_t i = 0; i < len; i++) {
        h[i % 4] ^= data[i];
        h[i % 4] *= 0x100000001b3ULL;
        h[(i + 1) % 4] ^= h[i % 4] >> 17;
    }
    std::memcpy(out32, h, 32);
}
} // namespace
#endif

namespace knotproof {

// ============================================================
// InvariantSuite
// ============================================================

std::vector<uint8_t> InvariantSuite::ToBytes() const
{
    auto write_section = [](std::vector<uint8_t>& out, const std::vector<uint8_t>& data) {
        uint32_t len = static_cast<uint32_t>(data.size());
        size_t pos = out.size();
        out.resize(pos + 4 + len);
        std::memcpy(out.data() + pos, &len, 4);
        if (len > 0) std::memcpy(out.data() + pos + 4, data.data(), len);
    };

    std::vector<uint8_t> out;
    write_section(out, alexander.ToBytes());
    write_section(out, jones.ToBytes());
    write_section(out, conway.ToBytes());
    write_section(out, homflypt.ToBytes());
    return out;
}

bool InvariantSuite::FromBytes(const std::vector<uint8_t>& data)
{
    size_t offset = 0;
    auto read_section = [&](std::vector<uint8_t>& target) -> bool {
        if (offset + 4 > data.size()) return false;
        uint32_t len;
        std::memcpy(&len, data.data() + offset, 4);
        offset += 4;
        if (len > 8192) return false;
        if (offset + len > data.size()) return false;
        target.assign(data.data() + offset, data.data() + offset + len);
        offset += len;
        return true;
    };

    std::vector<uint8_t> alex_b, jones_b, conway_b, homfly_b;
    if (!read_section(alex_b)) return false;
    if (!read_section(jones_b)) return false;
    if (!read_section(conway_b)) return false;
    if (!read_section(homfly_b)) return false;

    if (!alexander.FromBytes(alex_b)) return false;
    if (!jones.FromBytes(jones_b)) return false;
    if (!conway.FromBytes(conway_b)) return false;
    if (!homflypt.FromBytes(homfly_b)) return false;

    return true;
}

bool InvariantSuite::IsConsistent(int crossing_number) const
{
    if (!Alexander::Verify(alexander, crossing_number)) return false;
    if (!Jones::Verify(jones, crossing_number)) return false;
    if (!Conway::Verify(conway, crossing_number)) return false;
    if (!HOMFLYPT::Verify(homflypt, crossing_number)) return false;
    return true;
}

// ============================================================
// SpatialSignature
// ============================================================

std::vector<uint8_t> SpatialSignature::ToBytes() const
{
    auto write_section = [](std::vector<uint8_t>& out, const std::vector<uint8_t>& data) {
        uint32_t len = static_cast<uint32_t>(data.size());
        size_t pos = out.size();
        out.resize(pos + 4 + len);
        std::memcpy(out.data() + pos, &len, 4);
        if (len > 0) std::memcpy(out.data() + pos + 4, data.data(), len);
    };

    std::vector<uint8_t> out;
    write_section(out, braid_bytes);
    write_section(out, invariant_bytes);
    write_section(out, agt_tensor_bytes);
    write_section(out, agt_commitment);

    size_t pos = out.size();
    out.resize(pos + 8 + 4);
    std::memcpy(out.data() + pos, &timestamp, 8);
    std::memcpy(out.data() + pos + 8, &modular_depth, 4);

    return out;
}

bool SpatialSignature::FromBytes(const std::vector<uint8_t>& data)
{
    size_t offset = 0;
    auto read_section = [&](std::vector<uint8_t>& target) -> bool {
        if (offset + 4 > data.size()) return false;
        uint32_t len;
        std::memcpy(&len, data.data() + offset, 4);
        offset += 4;
        if (len > 8192) return false;
        if (offset + len > data.size()) return false;
        target.assign(data.data() + offset, data.data() + offset + len);
        offset += len;
        return true;
    };

    if (!read_section(braid_bytes)) return false;
    if (!read_section(invariant_bytes)) return false;
    if (!read_section(agt_tensor_bytes)) return false;
    if (!read_section(agt_commitment)) return false;

    if (offset + 12 > data.size()) return false;
    std::memcpy(&timestamp, data.data() + offset, 8);
    std::memcpy(&modular_depth, data.data() + offset + 8, 4);

    return true;
}

size_t SpatialSignature::Size() const
{
    return 4 + braid_bytes.size() +
           4 + invariant_bytes.size() +
           4 + agt_tensor_bytes.size() +
           4 + agt_commitment.size() +
           8 + 4; // timestamp + modular_depth
}

bool SpatialSignature::IsValid() const
{
    if (braid_bytes.empty()) return false;
    if (invariant_bytes.empty()) return false;
    if (agt_commitment.size() != 32) return false;
    if (timestamp <= 0) return false;
    if (modular_depth < 1 || modular_depth > 32) return false;
    return true;
}

// ============================================================
// KnotProof
// ============================================================

InvariantSuite KnotProof::ComputeInvariants(const BraidWord& braid)
{
    InvariantSuite suite;
    suite.alexander = Alexander::Compute(braid);
    suite.jones = Jones::ComputeFromBraid(braid);
    suite.conway = Conway::Compute(braid);
    suite.homflypt = HOMFLYPT::Compute(braid);
    return suite;
}

SpatialSignature KnotProof::Sign(const uint8_t* msg_hash,
                                  const std::vector<GazeSample>& samples,
                                  double alpha,
                                  int m)
{
    SpatialSignature sig;
    sig.modular_depth = m;

    // 1. Run Markov chain on Δ² to build braid
    AGTBind::BindResult bind = AGTBind::FullBind(samples, alpha, m);

    // 2. Mix braid with message hash for transaction-specific uniqueness
    // XOR the first 32 bytes of braid serialization with msg_hash
    auto braid_raw = bind.braid.ToBytes();
    for (size_t i = 0; i < std::min(braid_raw.size(), size_t(32)); i++) {
        braid_raw[i] ^= msg_hash[i];
    }
    // Re-parse to get message-bound braid (may slightly alter generators)
    // Actually, keep the original braid for invariant consistency —
    // the message binding comes through the commitment hash instead.

    // 3. Compute full invariant suite I(K = β̂)
    InvariantSuite suite = ComputeInvariants(bind.braid);

    // 4. Package
    sig.braid_bytes = bind.braid.ToBytes();
    sig.invariant_bytes = suite.ToBytes();
    sig.agt_tensor_bytes = bind.final_tensor.ToBytes();
    sig.timestamp = bind.final_tensor.timestamp;

    // 5. Commitment = SHA256(invariants || tensor || msg_hash || timestamp)
    auto inv_bytes = suite.ToBytes();
    auto tensor_bytes = bind.final_tensor.ToBytes();

    std::vector<uint8_t> preimage;
    preimage.insert(preimage.end(), inv_bytes.begin(), inv_bytes.end());
    preimage.insert(preimage.end(), tensor_bytes.begin(), tensor_bytes.end());
    preimage.insert(preimage.end(), msg_hash, msg_hash + 32);
    uint8_t ts_bytes[8];
    std::memcpy(ts_bytes, &sig.timestamp, 8);
    preimage.insert(preimage.end(), ts_bytes, ts_bytes + 8);

    sig.agt_commitment.resize(32);
#ifdef HAVE_CONFIG_H
    CSHA256 hasher;
    hasher.Write(preimage.data(), preimage.size());
    hasher.Finalize(sig.agt_commitment.data());
#else
    SHA256Hash(preimage.data(), preimage.size(), sig.agt_commitment.data());
#endif

    return sig;
}

SpatialSignature KnotProof::SignFromTensor(const uint8_t* msg_hash,
                                            const AGTTensor& tensor,
                                            int m)
{
    SpatialSignature sig;
    sig.modular_depth = m;

    AGTBind::BindResult bind = AGTBind::FullBindFromTensor(tensor, m);
    InvariantSuite suite = ComputeInvariants(bind.braid);

    sig.braid_bytes = bind.braid.ToBytes();
    sig.invariant_bytes = suite.ToBytes();
    sig.agt_tensor_bytes = tensor.ToBytes();
    sig.timestamp = tensor.timestamp;

    auto inv_bytes = suite.ToBytes();
    auto tensor_bytes = tensor.ToBytes();

    std::vector<uint8_t> preimage;
    preimage.insert(preimage.end(), inv_bytes.begin(), inv_bytes.end());
    preimage.insert(preimage.end(), tensor_bytes.begin(), tensor_bytes.end());
    preimage.insert(preimage.end(), msg_hash, msg_hash + 32);
    uint8_t ts_bytes[8];
    std::memcpy(ts_bytes, &sig.timestamp, 8);
    preimage.insert(preimage.end(), ts_bytes, ts_bytes + 8);

    sig.agt_commitment.resize(32);
#ifdef HAVE_CONFIG_H
    CSHA256 hasher;
    hasher.Write(preimage.data(), preimage.size());
    hasher.Finalize(sig.agt_commitment.data());
#else
    SHA256Hash(preimage.data(), preimage.size(), sig.agt_commitment.data());
#endif

    return sig;
}

bool KnotProof::Verify(const SpatialSignature& sig,
                       const uint8_t* msg_hash,
                       const std::vector<GazeSample>& samples,
                       double alpha)
{
    if (!sig.IsValid()) return false;

    // Deserialize stored data
    BraidWord stored_braid;
    if (!stored_braid.FromBytes(sig.braid_bytes)) return false;

    InvariantSuite stored_suite;
    if (!stored_suite.FromBytes(sig.invariant_bytes)) return false;

    AGTTensor stored_tensor;
    if (!sig.agt_tensor_bytes.empty()) {
        if (!stored_tensor.FromBytes(sig.agt_tensor_bytes)) return false;
    }

    // Recompute from gaze samples
    AGTBind::BindResult recomputed = AGTBind::FullBind(samples, alpha, sig.modular_depth);

    // Verify braid matches
    if (recomputed.braid.generators != stored_braid.generators) return false;
    if (recomputed.braid.num_strands != stored_braid.num_strands) return false;

    // Verify invariant suite
    InvariantSuite expected_suite = ComputeInvariants(recomputed.braid);
    if (expected_suite.alexander != stored_suite.alexander) return false;
    if (expected_suite.jones != stored_suite.jones) return false;
    if (expected_suite.conway != stored_suite.conway) return false;

    // Check polynomial constraints
    int cn = stored_braid.Length();
    if (!stored_suite.IsConsistent(cn)) return false;

    // Verify commitment
    auto inv_bytes = stored_suite.ToBytes();
    auto tensor_bytes = stored_tensor.ToBytes();

    std::vector<uint8_t> preimage;
    preimage.insert(preimage.end(), inv_bytes.begin(), inv_bytes.end());
    preimage.insert(preimage.end(), tensor_bytes.begin(), tensor_bytes.end());
    preimage.insert(preimage.end(), msg_hash, msg_hash + 32);
    uint8_t ts_bytes[8];
    int64_t ts = sig.timestamp;
    std::memcpy(ts_bytes, &ts, 8);
    preimage.insert(preimage.end(), ts_bytes, ts_bytes + 8);

    uint8_t expected_commitment[32];
#ifdef HAVE_CONFIG_H
    CSHA256 hasher;
    hasher.Write(preimage.data(), preimage.size());
    hasher.Finalize(expected_commitment);
#else
    SHA256Hash(preimage.data(), preimage.size(), expected_commitment);
#endif

    if (sig.agt_commitment.size() != 32) return false;
    if (std::memcmp(expected_commitment, sig.agt_commitment.data(), 32) != 0) return false;

    return true;
}

bool KnotProof::VerifyFromTensor(const SpatialSignature& sig,
                                  const uint8_t* msg_hash,
                                  const AGTTensor& tensor)
{
    if (!sig.IsValid()) return false;

    BraidWord stored_braid;
    if (!stored_braid.FromBytes(sig.braid_bytes)) return false;

    InvariantSuite stored_suite;
    if (!stored_suite.FromBytes(sig.invariant_bytes)) return false;

    // Recompute from tensor
    BraidWord expected_braid = AGTBind::GenerateBraidFromTensor(tensor, sig.modular_depth);
    if (expected_braid.generators != stored_braid.generators) return false;

    InvariantSuite expected_suite = ComputeInvariants(expected_braid);
    if (expected_suite.alexander != stored_suite.alexander) return false;
    if (expected_suite.jones != stored_suite.jones) return false;

    int cn = stored_braid.Length();
    if (!stored_suite.IsConsistent(cn)) return false;

    return true;
}

bool KnotProof::VerifyNonBiometric(const SpatialSignature& sig,
                                    const uint8_t* msg_hash)
{
    if (!sig.IsValid()) return false;

    BraidWord stored_braid;
    if (!stored_braid.FromBytes(sig.braid_bytes)) return false;

    InvariantSuite stored_suite;
    if (!stored_suite.FromBytes(sig.invariant_bytes)) return false;

    // Non-biometric: zero tensor
    AGTTensor zero_tensor;
    zero_tensor.Reset();
    zero_tensor.num_samples = 1; // Mark as valid
    zero_tensor.timestamp = sig.timestamp;

    BraidWord expected = AGTBind::GenerateBraidFromTensor(zero_tensor, sig.modular_depth);
    if (expected.generators != stored_braid.generators) return false;

    InvariantSuite expected_suite = ComputeInvariants(expected);
    if (expected_suite.alexander != stored_suite.alexander) return false;
    if (expected_suite.jones != stored_suite.jones) return false;

    return true;
}

int KnotProof::ComplexityScore(const SpatialSignature& sig)
{
    BraidWord braid;
    if (!braid.FromBytes(sig.braid_bytes)) return 0;

    InvariantSuite suite;
    if (!suite.FromBytes(sig.invariant_bytes)) return 0;

    int score = 0;
    score += braid.Length() * 10;
    score += braid.num_strands * 5;
    score += std::abs(braid.Writhe()) * 3;

    if (!suite.alexander.IsZero()) {
        score += (suite.alexander.MaxDegree() - suite.alexander.min_degree) * 4;
        score += static_cast<int>(suite.alexander.coeffs.size()) * 2;
    }
    if (!suite.jones.IsZero()) {
        score += (suite.jones.MaxDegree() - suite.jones.min_degree) * 3;
        score += static_cast<int>(suite.jones.coeffs.size()) * 2;
    }
    if (!suite.conway.IsZero()) {
        score += (suite.conway.MaxDegree() - suite.conway.min_degree) * 2;
    }
    score += static_cast<int>(suite.homflypt.terms.size()) * 3;

    return score;
}

} // namespace knotproof
