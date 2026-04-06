// Copyright (c) 2026 The BTQ Core developers
// Copyright (c) 2026 OPTX / jOSH-Spatial (AstroJOE)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/MIT.
//
// AGT binding: Markov chain on Δ² → quantized braid word → knot invariants.
// Implements the pipeline from US Patent App 19/243,050 and GENSYS OPTX v2.2.2.

#include <crypto/knotproof/agtbind.h>
#include <crypto/knotproof/alexander.h>
#include <crypto/knotproof/jones.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

// Use btq-core's SHA256 if available
#ifdef HAVE_CONFIG_H
#include <crypto/sha256.h>
#else
namespace {
void SHA256Hash(const uint8_t* data, size_t len, uint8_t* out32)
{
    // FNV-based mixing — NOT cryptographic, only for standalone tests.
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
// AGTTensor
// ============================================================

void AGTTensor::Reset()
{
    w[0] = w[1] = 0.333;
    w[2] = 0.334;
    std::memset(T, 0, sizeof(T));
    timestamp = 0;
    std::memset(session_id, 0, 32);
    num_samples = 0;
}

std::vector<uint8_t> AGTTensor::ToBytes() const
{
    // Format: [3*8 w][9*8 T][8 timestamp][32 session_id][4 num_samples]
    // Total: 24 + 72 + 8 + 32 + 4 = 140 bytes
    std::vector<uint8_t> out(140);
    size_t off = 0;

    for (int i = 0; i < 3; i++) {
        std::memcpy(out.data() + off, &w[i], 8);
        off += 8;
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            std::memcpy(out.data() + off, &T[i][j], 8);
            off += 8;
        }
    }
    std::memcpy(out.data() + off, &timestamp, 8);
    off += 8;
    std::memcpy(out.data() + off, session_id, 32);
    off += 32;
    int32_t ns = static_cast<int32_t>(num_samples);
    std::memcpy(out.data() + off, &ns, 4);

    return out;
}

bool AGTTensor::FromBytes(const std::vector<uint8_t>& data)
{
    if (data.size() != 140) return false;
    size_t off = 0;

    for (int i = 0; i < 3; i++) {
        std::memcpy(&w[i], data.data() + off, 8);
        off += 8;
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            std::memcpy(&T[i][j], data.data() + off, 8);
            off += 8;
        }
    }
    std::memcpy(&timestamp, data.data() + off, 8);
    off += 8;
    std::memcpy(session_id, data.data() + off, 32);
    off += 32;
    int32_t ns;
    std::memcpy(&ns, data.data() + off, 4);
    num_samples = ns;

    return true;
}

bool AGTTensor::IsValid() const
{
    // Simplex constraint: Σwᵢ ≈ 1, wᵢ ≥ 0
    double sum = w[0] + w[1] + w[2];
    if (std::fabs(sum - 1.0) > 1e-6) return false;
    for (int i = 0; i < 3; i++) {
        if (w[i] < -1e-9) return false;
    }
    return num_samples > 0;
}

// ============================================================
// MarkovAGT — Markov chain on Δ²
// ============================================================

MarkovAGT::MarkovAGT() : alpha_(DEFAULT_ALPHA)
{
    tensor_.Reset();
    prev_w_[0] = prev_w_[1] = 0.333;
    prev_w_[2] = 0.334;
}

MarkovAGT::MarkovAGT(double alpha) : alpha_(alpha)
{
    assert(alpha > 0.0 && alpha <= 1.0);
    tensor_.Reset();
    prev_w_[0] = prev_w_[1] = 0.333;
    prev_w_[2] = 0.334;
}

void MarkovAGT::SimplexProject(double w[3])
{
    // Euclidean projection onto Δ²:
    // 1. Clamp negatives to zero
    // 2. Normalize to sum = 1
    // Per JETT OE Whitepaper Section 2.3: Π operator.

    for (int i = 0; i < 3; i++) {
        if (w[i] < 0.0) w[i] = 0.0;
    }

    double sum = w[0] + w[1] + w[2];
    if (sum < 1e-12) {
        // Degenerate: reset to uniform
        w[0] = w[1] = w[2] = 1.0 / 3.0;
        return;
    }

    w[0] /= sum;
    w[1] /= sum;
    w[2] /= sum;
}

void MarkovAGT::Quantize(const double w[3], int m, int e[3])
{
    // Per GENSYS OPTX v2.2.2 Level 19:
    //   eᵢ = round(wᵢ · (2m+1)) - m
    // Clamped to [-m, m]. Zero exponents are preserved (identity generator).
    int range = 2 * m + 1;
    for (int i = 0; i < 3; i++) {
        e[i] = static_cast<int>(std::round(w[i] * range)) - m;
        if (e[i] > m) e[i] = m;
        if (e[i] < -m) e[i] = -m;
    }
}

void MarkovAGT::Update(const GazeSample& sample)
{
    if (tensor_.num_samples >= MAX_SAMPLES) return;

    // Save previous state for transition matrix
    std::memcpy(prev_w_, tensor_.w, sizeof(prev_w_));

    // Construct gaze input vector g(t)
    // Per JETT OE: g(t) ∈ {e_cog, e_emo, e_env} (one-hot or soft)
    double g[3] = {sample.cog, sample.emo, sample.env};
    SimplexProject(g); // Ensure g is on Δ²

    // Core update rule: w(t+1) = Π[(1-α)w(t) + αg(t)]
    // Per JETT OE Whitepaper Section 2.3 and Astro Knots Section 3
    for (int i = 0; i < 3; i++) {
        tensor_.w[i] = (1.0 - alpha_) * tensor_.w[i] + alpha_ * g[i];
    }
    SimplexProject(tensor_.w);

    // Update Markov transition matrix T (online outer product accumulation)
    // T[i][j] ≈ P(state j at t+1 | state i at t)
    // Using running average: T = T + (w_new ⊗ w_old - T) / n
    tensor_.num_samples++;
    double n = static_cast<double>(tensor_.num_samples);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double outer = tensor_.w[j] * prev_w_[i];
            tensor_.T[i][j] += (outer - tensor_.T[i][j]) / n;
        }
    }

    // Update timestamp to latest sample
    tensor_.timestamp = sample.timestamp;

    // Quantize current state and accumulate exponents
    // These will be used by ToBraidWord()
    int e[3];
    Quantize(tensor_.w, 3, e); // Default m=3, overridable in ToBraidWord
    exponents_.push_back(e[0]);
    exponents_.push_back(e[1]);
    exponents_.push_back(e[2]);
}

BraidWord MarkovAGT::ToBraidWord(int modular_depth) const
{
    if (exponents_.empty()) {
        return BraidWord::Trefoil(); // Fallback to simplest nontrivial knot
    }

    // Per GENSYS Level 19: β = σ₁^{e_COG} · σ₂^{e_EMO} · σ₁^{e_ENV}
    // for each gaze sample in the temporal sequence.
    //
    // Each sample contributes up to 3 generator groups to the braid word.
    // σᵢ^{n} = σᵢ repeated n times (positive) or σᵢ^{-1} repeated |n| times.
    // σ��^{0} = identity (omitted).
    //
    // We use B₃ (3 strands) as the base braid group:
    //   σ₁ = COG strand crossing
    //   σ₂ = EMO strand crossing
    //   σ₁ again for ENV (interleaved)

    std::vector<int> generators;
    int num_triplets = static_cast<int>(exponents_.size()) / 3;

    for (int t = 0; t < num_triplets; t++) {
        int e_cog = exponents_[t * 3 + 0];
        int e_emo = exponents_[t * 3 + 1];
        int e_env = exponents_[t * 3 + 2];

        // Re-quantize if modular_depth differs from stored m=3
        // (exponents were stored at m=3 during Update; rescale if needed)
        if (modular_depth != 3) {
            double w_cog = static_cast<double>(e_cog + 3) / 7.0;
            double w_emo = static_cast<double>(e_emo + 3) / 7.0;
            double w_env = static_cast<double>(e_env + 3) / 7.0;
            int range = 2 * modular_depth + 1;
            e_cog = static_cast<int>(std::round(w_cog * range)) - modular_depth;
            e_emo = static_cast<int>(std::round(w_emo * range)) - modular_depth;
            e_env = static_cast<int>(std::round(w_env * range)) - modular_depth;
        }

        // σ₁^{e_COG}: append |e_cog| copies of σ₁ or σ₁⁻��
        for (int k = 0; k < std::abs(e_cog); k++) {
            generators.push_back(e_cog > 0 ? 1 : -1);
        }

        // σ₂^{e_EMO}: append |e_emo| copies of σ₂ or σ₂⁻¹
        for (int k = 0; k < std::abs(e_emo); k++) {
            generators.push_back(e_emo > 0 ? 2 : -2);
        }

        // σ₁^{e_ENV}: append |e_env| copies of σ₁ or σ₁⁻¹
        for (int k = 0; k < std::abs(e_env); k++) {
            generators.push_back(e_env > 0 ? 1 : -1);
        }
    }

    if (generators.empty()) {
        // All exponents were zero — degenerate to trefoil
        return BraidWord::Trefoil();
    }

    // Cap braid length to prevent DoS
    if (generators.size() > 1024) {
        generators.resize(1024);
    }

    return BraidWord(std::move(generators), 3); // B₃
}

void MarkovAGT::StationaryDistribution(double pi[3]) const
{
    // Power iteration on T to find stationary distribution.
    // π = πT, normalized to sum = 1.
    pi[0] = pi[1] = pi[2] = 1.0 / 3.0;

    for (int iter = 0; iter < 100; iter++) {
        double next[3] = {0, 0, 0};
        for (int j = 0; j < 3; j++) {
            for (int i = 0; i < 3; i++) {
                next[j] += pi[i] * tensor_.T[i][j];
            }
        }
        double sum = next[0] + next[1] + next[2];
        if (sum < 1e-12) break;
        for (int j = 0; j < 3; j++) pi[j] = next[j] / sum;
    }
}

// ============================================================
// AGTBind — Pipeline orchestration
// ============================================================

void AGTBind::TensorHash(const AGTTensor& tensor, uint8_t* out32)
{
    auto bytes = tensor.ToBytes();
#ifdef HAVE_CONFIG_H
    CSHA256 hasher;
    hasher.Write(bytes.data(), bytes.size());
    hasher.Finalize(out32);
#else
    SHA256Hash(bytes.data(), bytes.size(), out32);
#endif
}

BraidWord AGTBind::GenerateBraid(const std::vector<GazeSample>& samples,
                                  double alpha,
                                  int modular_depth)
{
    MarkovAGT markov(alpha);
    for (const auto& s : samples) {
        markov.Update(s);
    }
    return markov.ToBraidWord(modular_depth);
}

BraidWord AGTBind::GenerateBraidFromTensor(const AGTTensor& tensor,
                                            int modular_depth)
{
    // When the Markov chain was run externally (e.g., on-device MediaPipe),
    // we receive the final tensor state. Generate a braid from the
    // simplex position and transition matrix eigenstructure.
    //
    // Use the transition matrix rows as pseudo-gaze samples to reconstruct
    // the braid word pattern.
    double pi[3];
    // Estimate stationary distribution from T
    pi[0] = pi[1] = pi[2] = 1.0 / 3.0;
    for (int iter = 0; iter < 50; iter++) {
        double next[3] = {0, 0, 0};
        for (int j = 0; j < 3; j++) {
            for (int i = 0; i < 3; i++) {
                next[j] += pi[i] * tensor.T[i][j];
            }
        }
        double sum = next[0] + next[1] + next[2];
        if (sum < 1e-12) break;
        for (int j = 0; j < 3; j++) pi[j] = next[j] / sum;
    }

    // Quantize final state + stationary distribution for braid
    int e_state[3], e_pi[3];
    MarkovAGT::Quantize(tensor.w, modular_depth, e_state);
    MarkovAGT::Quantize(pi, modular_depth, e_pi);

    std::vector<int> generators;

    // First triplet from final state
    for (int k = 0; k < std::abs(e_state[0]); k++)
        generators.push_back(e_state[0] > 0 ? 1 : -1);
    for (int k = 0; k < std::abs(e_state[1]); k++)
        generators.push_back(e_state[1] > 0 ? 2 : -2);
    for (int k = 0; k < std::abs(e_state[2]); k++)
        generators.push_back(e_state[2] > 0 ? 1 : -1);

    // Second triplet from stationary distribution
    for (int k = 0; k < std::abs(e_pi[0]); k++)
        generators.push_back(e_pi[0] > 0 ? 1 : -1);
    for (int k = 0; k < std::abs(e_pi[1]); k++)
        generators.push_back(e_pi[1] > 0 ? 2 : -2);
    for (int k = 0; k < std::abs(e_pi[2]); k++)
        generators.push_back(e_pi[2] > 0 ? 1 : -1);

    if (generators.empty()) {
        return BraidWord::Trefoil();
    }

    return BraidWord(std::move(generators), 3);
}

void AGTBind::BindWeights(std::vector<Crossing>& crossings,
                           const AGTTensor& tensor)
{
    int n = static_cast<int>(crossings.size());
    if (n == 0) return;

    // Distribute simplex weights across crossings.
    // Each crossing gets weight from the corresponding AGT dimension.
    for (int i = 0; i < n; i++) {
        int channel = i % 3;
        double raw = tensor.w[channel];

        // Scale to integer range [1, 127] for crossing weight
        int64_t weight = static_cast<int64_t>(std::round(raw * 126.0)) + 1;
        if (weight < 1) weight = 1;
        if (weight > 127) weight = 127;

        crossings[i].weight = weight;
    }
}

LaurentPolynomial AGTBind::WeightedAlexander(const BraidWord& braid,
                                              const AGTTensor& tensor)
{
    auto crossings = braid.ToPDCode();
    BindWeights(crossings, tensor);

    LaurentPolynomial base = Alexander::Compute(braid);

    // Weight factor from simplex state — encodes biometric identity
    // into the polynomial coefficient scaling
    int64_t weight_factor = 1;
    for (int i = 0; i < 3; i++) {
        int64_t wi = static_cast<int64_t>(std::round(tensor.w[i] * 100.0));
        if (wi == 0) wi = 1;
        weight_factor *= wi;
    }
    if (weight_factor == 0) weight_factor = 1;
    // Clamp to prevent overflow
    if (weight_factor > 1000000) weight_factor = weight_factor % 1000000 + 1;

    return base * weight_factor;
}

LaurentPolynomial AGTBind::WeightedJones(const BraidWord& braid,
                                          const AGTTensor& tensor)
{
    auto crossings = braid.ToPDCode();
    BindWeights(crossings, tensor);
    int writhe = braid.Writhe();
    return Jones::Compute(crossings, writhe);
}

AGTBind::BindResult AGTBind::FullBind(const std::vector<GazeSample>& samples,
                                       double alpha,
                                       int modular_depth)
{
    BindResult result;

    // 1. Run Markov chain
    MarkovAGT markov(alpha);
    for (const auto& s : samples) {
        markov.Update(s);
    }

    // 2. Build braid from trajectory
    result.braid = markov.ToBraidWord(modular_depth);
    result.final_tensor = markov.GetTensor();

    // 3. Compute weighted invariants
    result.alexander = WeightedAlexander(result.braid, result.final_tensor);
    result.jones = WeightedJones(result.braid, result.final_tensor);

    // 4. Commitment = SHA256(alexander || jones || tensor || timestamp)
    auto alex_bytes = result.alexander.ToBytes();
    auto jones_bytes = result.jones.ToBytes();
    auto tensor_bytes = result.final_tensor.ToBytes();

    std::vector<uint8_t> preimage;
    preimage.insert(preimage.end(), alex_bytes.begin(), alex_bytes.end());
    preimage.insert(preimage.end(), jones_bytes.begin(), jones_bytes.end());
    preimage.insert(preimage.end(), tensor_bytes.begin(), tensor_bytes.end());

    // Include JOULE timestamp for temporal binding
    uint8_t ts_bytes[8];
    std::memcpy(ts_bytes, &result.final_tensor.timestamp, 8);
    preimage.insert(preimage.end(), ts_bytes, ts_bytes + 8);

    result.commitment.resize(32);
#ifdef HAVE_CONFIG_H
    CSHA256 hasher;
    hasher.Write(preimage.data(), preimage.size());
    hasher.Finalize(result.commitment.data());
#else
    SHA256Hash(preimage.data(), preimage.size(), result.commitment.data());
#endif

    return result;
}

AGTBind::BindResult AGTBind::FullBindFromTensor(const AGTTensor& tensor,
                                                  int modular_depth)
{
    BindResult result;

    result.braid = GenerateBraidFromTensor(tensor, modular_depth);
    result.final_tensor = tensor;
    result.alexander = WeightedAlexander(result.braid, tensor);
    result.jones = WeightedJones(result.braid, tensor);

    auto alex_bytes = result.alexander.ToBytes();
    auto jones_bytes = result.jones.ToBytes();
    auto tensor_bytes = tensor.ToBytes();

    std::vector<uint8_t> preimage;
    preimage.insert(preimage.end(), alex_bytes.begin(), alex_bytes.end());
    preimage.insert(preimage.end(), jones_bytes.begin(), jones_bytes.end());
    preimage.insert(preimage.end(), tensor_bytes.begin(), tensor_bytes.end());

    uint8_t ts_bytes[8];
    std::memcpy(ts_bytes, &tensor.timestamp, 8);
    preimage.insert(preimage.end(), ts_bytes, ts_bytes + 8);

    result.commitment.resize(32);
#ifdef HAVE_CONFIG_H
    CSHA256 hasher;
    hasher.Write(preimage.data(), preimage.size());
    hasher.Finalize(result.commitment.data());
#else
    SHA256Hash(preimage.data(), preimage.size(), result.commitment.data());
#endif

    return result;
}

bool AGTBind::VerifyBinding(const BindResult& result,
                             const std::vector<GazeSample>& samples,
                             double alpha,
                             int modular_depth)
{
    BindResult recomputed = FullBind(samples, alpha, modular_depth);

    if (recomputed.braid.generators != result.braid.generators) return false;
    if (recomputed.alexander != result.alexander) return false;
    if (recomputed.jones != result.jones) return false;
    if (recomputed.commitment != result.commitment) return false;

    return true;
}

} // namespace knotproof
