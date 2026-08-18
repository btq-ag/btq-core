// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/dilithium_leaf.h>

#include <script/script.h>
#include <script/solver.h>
#include <span.h>

#include <algorithm>

CScript GetScriptForDilithiumThreshold(int m, const std::vector<CDilithiumPubKey>& pubkeys)
{
    CScript script;
    script << OP_0; // accumulator, parked on the alt stack between checks
    for (const CDilithiumPubKey& pubkey : pubkeys) {
        script << OP_TOALTSTACK;
        script << ToByteVector(pubkey);
        script << OP_CHECKSIGDILITHIUM;
        script << OP_FROMALTSTACK << OP_ADD;
    }
    script << m << OP_GREATERTHANOREQUAL;
    return script;
}

namespace {
//! Decode a script number pushed by any valid encoding, minimal or not.
std::optional<int> DecodePushedNumber(opcodetype opcode, const std::vector<unsigned char>& data)
{
    if (opcode == OP_0) return 0;
    if (opcode >= OP_1 && opcode <= OP_16) return CScript::DecodeOP_N(opcode);
    if (data.empty() || data.size() > 4) return std::nullopt;
    try {
        return static_cast<int>(CScriptNum(data, /*fRequireMinimal=*/false, 4).getint());
    } catch (const scriptnum_error&) {
        return std::nullopt;
    }
}

bool ParseThresholdAccumulator(const CScript& script, P2MRDilithiumLeafPolicy& out)
{
    CScript::const_iterator it = script.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;

    if (!script.GetOp(it, opcode, data) || opcode != OP_0) return false;

    std::vector<CDilithiumPubKey> pubkeys;
    while (true) {
        if (!script.GetOp(it, opcode, data)) return false;
        // Anything other than another OP_TOALTSTACK ends the key loop; this
        // opcode is then the threshold push.
        if (opcode != OP_TOALTSTACK) break;

        if (!script.GetOp(it, opcode, data) || data.size() != CDilithiumPubKey::SIZE) return false;
        const CDilithiumPubKey pubkey{Span<const unsigned char>{data}};
        if (!pubkey.IsFullyValid()) return false;
        pubkeys.push_back(pubkey);
        if (pubkeys.size() > MAX_PUBKEYS_PER_MULTISIG) return false;

        if (!script.GetOp(it, opcode, data) || opcode != OP_CHECKSIGDILITHIUM) return false;
        if (!script.GetOp(it, opcode, data) || opcode != OP_FROMALTSTACK) return false;
        if (!script.GetOp(it, opcode, data) || opcode != OP_ADD) return false;
    }

    const std::optional<int> m = DecodePushedNumber(opcode, data);
    if (!m) return false;
    if (!script.GetOp(it, opcode, data) || opcode != OP_GREATERTHANOREQUAL) return false;
    if (it != script.end()) return false;
    if (pubkeys.empty() || *m < 1 || *m > static_cast<int>(pubkeys.size())) return false;

    out.type = P2MRLeafTemplate::THRESHOLD_ACCUMULATOR;
    out.m = *m;
    out.pubkeys = std::move(pubkeys);
    return true;
}
} // namespace

P2MRDilithiumLeafPolicy ParseP2MRDilithiumLeaf(const CScript& script)
{
    P2MRDilithiumLeafPolicy policy;

    std::vector<std::vector<unsigned char>> solutions;
    switch (Solver(script, solutions)) {
    case TxoutType::DILITHIUM_PUBKEY: {
        const CDilithiumPubKey pubkey{Span<const unsigned char>{solutions[0]}};
        if (!pubkey.IsFullyValid()) return policy;
        policy.type = P2MRLeafTemplate::SINGLE_CHECKSIGDILITHIUM;
        policy.m = 1;
        policy.pubkeys = {pubkey};
        return policy;
    }
    case TxoutType::DILITHIUM_MULTISIG: {
        const int m = solutions.front()[0];
        const int n = solutions.back()[0];
        if (n < 1 || m < 1 || m > n || solutions.size() != static_cast<size_t>(n) + 2) return policy;
        std::vector<CDilithiumPubKey> pubkeys;
        for (int i = 1; i <= n; ++i) {
            const CDilithiumPubKey pubkey{Span<const unsigned char>{solutions[i]}};
            if (!pubkey.IsFullyValid()) return policy;
            pubkeys.push_back(pubkey);
        }
        policy.type = P2MRLeafTemplate::CHECKMULTISIGDILITHIUM;
        policy.m = m;
        policy.pubkeys = std::move(pubkeys);
        return policy;
    }
    default:
        break;
    }

    ParseThresholdAccumulator(script, policy);
    return policy;
}

std::string P2MRLeafTemplateName(P2MRLeafTemplate type)
{
    switch (type) {
    case P2MRLeafTemplate::SINGLE_CHECKSIGDILITHIUM: return "dilithium_single";
    case P2MRLeafTemplate::CHECKMULTISIGDILITHIUM: return "dilithium_checkmultisig";
    case P2MRLeafTemplate::THRESHOLD_ACCUMULATOR: return "dilithium_threshold";
    case P2MRLeafTemplate::UNKNOWN: return "unknown";
    } // no default case, so the compiler can warn about missing cases
    return "unknown";
}

std::optional<size_t> FindPolicyKeyIndex(const P2MRDilithiumLeafPolicy& policy, const CDilithiumPubKey& pubkey)
{
    const auto it = std::find(policy.pubkeys.begin(), policy.pubkeys.end(), pubkey);
    if (it == policy.pubkeys.end()) return std::nullopt;
    return static_cast<size_t>(std::distance(policy.pubkeys.begin(), it));
}

bool BuildDilithiumLeafWitness(const P2MRDilithiumLeafPolicy& policy,
                               const std::vector<std::vector<unsigned char>>& sigs_by_key_index,
                               std::vector<std::vector<unsigned char>>& stack_out)
{
    stack_out.clear();
    if (!policy.IsValid()) return false;
    if (sigs_by_key_index.size() != policy.pubkeys.size()) return false;

    switch (policy.type) {
    case P2MRLeafTemplate::SINGLE_CHECKSIGDILITHIUM:
        if (sigs_by_key_index[0].empty()) return false;
        stack_out.push_back(sigs_by_key_index[0]);
        return true;

    case P2MRLeafTemplate::CHECKMULTISIGDILITHIUM:
        // OP_CHECKMULTISIGDILITHIUM keeps the CHECKMULTISIG off-by-one dummy and
        // consumes exactly m signatures, in pubkey order.
        stack_out.emplace_back();
        for (const auto& sig : sigs_by_key_index) {
            if (static_cast<int>(stack_out.size()) == policy.m + 1) break;
            if (!sig.empty()) stack_out.push_back(sig);
        }
        if (static_cast<int>(stack_out.size()) != policy.m + 1) {
            stack_out.clear();
            return false;
        }
        return true;

    case P2MRLeafTemplate::THRESHOLD_ACCUMULATOR: {
        const auto signed_count = std::count_if(sigs_by_key_index.begin(), sigs_by_key_index.end(),
                                                [](const auto& sig) { return !sig.empty(); });
        if (signed_count < policy.m) return false;
        // The leaf checks key 0 first, so slot 0 has to be on top of the stack.
        for (size_t k = sigs_by_key_index.size(); k-- > 0;) {
            stack_out.push_back(sigs_by_key_index[k]);
        }
        return true;
    }

    case P2MRLeafTemplate::UNKNOWN:
        return false;
    } // no default case, so the compiler can warn about missing cases

    return false;
}
