// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/p2mr.h>

#include <core_io.h>
#include <crypto/dilithium_key.h>
#include <interfaces/chain.h>
#include <key.h>
#include <key_io.h>
#include <policy/policy.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/util.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/sign.h>
#include <script/solver.h>
#include <span.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <algorithm>
#include <set>

namespace wallet {

namespace {
constexpr const char* P2MR_STATE_CREATED{"created"};
constexpr CAmount DEFAULT_P2MR_DUST_THRESHOLD{546};

struct P2MRKeyRequirements {
    std::set<CKeyID> dilithium_key_ids;
    std::set<XOnlyPubKey> xonly_pubkeys;
};

std::string NewP2MRId()
{
    return GetRandHash().GetHex().substr(0, 16);
}

UniValue LeafToUniValue(const P2MRTreeLeaf& leaf)
{
    UniValue out(UniValue::VOBJ);
    out.pushKV("depth", leaf.depth);
    out.pushKV("leaf_version", leaf.leaf_version);
    out.pushKV("script", HexStr(leaf.script));
    return out;
}

UniValue BuildMetadataJSON(const std::string& id,
                           const std::string& address,
                           const CScript& script_pub_key,
                           const uint256& merkle_root,
                           const std::string& label,
                           const std::vector<P2MRTreeLeaf>& leaves)
{
    UniValue meta(UniValue::VOBJ);
    meta.pushKV("id", id);
    meta.pushKV("address", address);
    meta.pushKV("scriptPubKey", HexStr(script_pub_key));
    meta.pushKV("merkle_root", HexStr(merkle_root));
    meta.pushKV("created_at", GetTime());
    meta.pushKV("label", label);
    meta.pushKV("state", P2MR_STATE_CREATED);
    meta.pushKV("tree", P2MRTreeToUniValue(leaves));
    return meta;
}

bool DecodeMetadata(const std::string& raw, UniValue& out)
{
    return out.read(raw) && out.isObject();
}

bool SameP2MRTree(const std::vector<P2MRTreeLeaf>& a, const std::vector<P2MRTreeLeaf>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].depth != b[i].depth) return false;
        if (a[i].leaf_version != b[i].leaf_version) return false;
        if (a[i].script != b[i].script) return false;
    }
    return true;
}

void AddDilithiumKeyIDFromHash(Span<const unsigned char> bytes, std::set<CKeyID>& out)
{
    if (bytes.size() != uint160::size()) return;
    CKeyID keyid;
    std::copy(bytes.begin(), bytes.end(), keyid.begin());
    out.insert(keyid);
}

void AddDilithiumKeyIDFromPubKey(const CDilithiumPubKey& pubkey, std::set<CKeyID>& out)
{
    if (!pubkey.IsValid()) return;
    const uint160 id = pubkey.GetID();
    CKeyID keyid;
    std::copy(id.begin(), id.end(), keyid.begin());
    out.insert(keyid);
}

std::set<CKeyID> GetP2MRDilithiumKeyIDs(const std::vector<P2MRTreeLeaf>& leaves)
{
    std::set<CKeyID> key_ids;
    for (const P2MRTreeLeaf& leaf : leaves) {
        CScript script{leaf.script.begin(), leaf.script.end()};
        std::vector<std::vector<unsigned char>> solutions;
        const TxoutType which_type = Solver(script, solutions);
        switch (which_type) {
        case TxoutType::DILITHIUM_PUBKEY: {
            if (solutions.empty()) break;
            const CDilithiumPubKey pubkey{solutions[0]};
            AddDilithiumKeyIDFromPubKey(pubkey, key_ids);
            break;
        }
        case TxoutType::DILITHIUM_PUBKEYHASH:
        case TxoutType::DILITHIUM_WITNESS_V0_KEYHASH:
            if (!solutions.empty()) AddDilithiumKeyIDFromHash(solutions[0], key_ids);
            break;
        case TxoutType::DILITHIUM_MULTISIG:
            for (size_t i = 1; i + 1 < solutions.size(); ++i) {
                const CDilithiumPubKey pubkey{solutions[i]};
                AddDilithiumKeyIDFromPubKey(pubkey, key_ids);
            }
            break;
        default:
            break;
        }
    }
    return key_ids;
}

void AddXOnlyKeyIfValid(Span<const unsigned char> bytes, std::set<XOnlyPubKey>& out)
{
    if (bytes.size() != XOnlyPubKey::size()) return;
    const XOnlyPubKey pubkey{bytes};
    if (pubkey.IsFullyValid()) out.insert(pubkey);
}

void AddP2MRXOnlyKeys(const CScript& script, std::set<XOnlyPubKey>& out)
{
    if (script.size() == 34 && script[0] == XOnlyPubKey::size() && script[33] == OP_CHECKSIG) {
        AddXOnlyKeyIfValid(Span<const unsigned char>{script.data() + 1, XOnlyPubKey::size()}, out);
    }

    const auto multi_a = MatchMultiA(script);
    if (!multi_a) return;
    for (Span<const unsigned char> keyspan : multi_a->second) {
        AddXOnlyKeyIfValid(keyspan, out);
    }
}

P2MRKeyRequirements GetP2MRKeyRequirements(const std::vector<P2MRTreeLeaf>& leaves)
{
    P2MRKeyRequirements out;
    out.dilithium_key_ids = GetP2MRDilithiumKeyIDs(leaves);
    for (const P2MRTreeLeaf& leaf : leaves) {
        if (leaf.leaf_version != TAPROOT_LEAF_TAPSCRIPT) continue;
        AddP2MRXOnlyKeys(CScript{leaf.script.begin(), leaf.script.end()}, out.xonly_pubkeys);
    }
    return out;
}

bool WalletHaveDilithiumKey(const CWallet& wallet, const CKeyID& keyid)
{
    for (ScriptPubKeyMan* spk_man : wallet.GetAllScriptPubKeyMans()) {
        if (auto desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man)) {
            LOCK(desc_spk_man->cs_desc_man);
            if (desc_spk_man->HaveDilithiumKey(keyid)) return true;
        } else if (auto legacy_spk_man = dynamic_cast<LegacyScriptPubKeyMan*>(spk_man)) {
            if (legacy_spk_man->HaveDilithiumKey(keyid)) return true;
        }
    }
    return false;
}

bool WalletHaveXOnlyKey(const CWallet& wallet, const XOnlyPubKey& pubkey)
{
    for (ScriptPubKeyMan* spk_man : wallet.GetAllScriptPubKeyMans()) {
        if (auto desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man)) {
            LOCK(desc_spk_man->cs_desc_man);
            if (desc_spk_man->HaveKeyByXOnly(pubkey)) return true;
        } else if (auto legacy_spk_man = dynamic_cast<LegacyScriptPubKeyMan*>(spk_man)) {
            for (const CKeyID& keyid : pubkey.GetKeyIDs()) {
                if (legacy_spk_man->HaveKey(keyid)) return true;
            }
        }
    }
    return false;
}

bool IsOpTrueLeaf(const P2MRTreeLeaf& leaf)
{
    return leaf.leaf_version == TAPROOT_LEAF_TAPSCRIPT &&
           leaf.script.size() == 1 &&
           leaf.script[0] == OP_TRUE;
}

bool IsDilithiumLeafSpendable(const CWallet& wallet, const CScript& script)
{
    std::vector<std::vector<unsigned char>> solutions;
    const TxoutType which_type = Solver(script, solutions);
    switch (which_type) {
    case TxoutType::DILITHIUM_PUBKEY: {
        if (solutions.empty()) return false;
        const CDilithiumPubKey pubkey{solutions[0]};
        if (!pubkey.IsValid()) return false;
        CKeyID keyid;
        const uint160 id = pubkey.GetID();
        std::copy(id.begin(), id.end(), keyid.begin());
        return WalletHaveDilithiumKey(wallet, keyid);
    }
    case TxoutType::DILITHIUM_PUBKEYHASH:
    case TxoutType::DILITHIUM_WITNESS_V0_KEYHASH: {
        if (solutions.empty() || solutions[0].size() != uint160::size()) return false;
        CKeyID keyid;
        std::copy(solutions[0].begin(), solutions[0].end(), keyid.begin());
        return WalletHaveDilithiumKey(wallet, keyid);
    }
    case TxoutType::DILITHIUM_MULTISIG: {
        if (solutions.size() < 3 || solutions.front().empty()) return false;
        const int required = solutions.front()[0];
        int available = 0;
        for (size_t i = 1; i + 1 < solutions.size(); ++i) {
            const CDilithiumPubKey pubkey{solutions[i]};
            if (!pubkey.IsValid()) continue;
            CKeyID keyid;
            const uint160 id = pubkey.GetID();
            std::copy(id.begin(), id.end(), keyid.begin());
            if (WalletHaveDilithiumKey(wallet, keyid) && ++available >= required) return true;
        }
        return false;
    }
    default:
        return false;
    }
}

bool IsXOnlyLeafSpendable(const CWallet& wallet, const CScript& script)
{
    if (script.size() == 34 && script[0] == XOnlyPubKey::size() && script[33] == OP_CHECKSIG) {
        const XOnlyPubKey pubkey{Span<const unsigned char>{script.data() + 1, XOnlyPubKey::size()}};
        return pubkey.IsFullyValid() && WalletHaveXOnlyKey(wallet, pubkey);
    }

    const auto multi_a = MatchMultiA(script);
    if (!multi_a) return false;
    const int required = multi_a->first;
    int available = 0;
    for (Span<const unsigned char> keyspan : multi_a->second) {
        const XOnlyPubKey pubkey{keyspan};
        if (pubkey.IsFullyValid() && WalletHaveXOnlyKey(wallet, pubkey) && ++available >= required) {
            return true;
        }
    }
    return false;
}

bool IsP2MRLeafSpendable(const CWallet& wallet, const P2MRTreeLeaf& leaf)
{
    if (IsOpTrueLeaf(leaf)) return true;
    if (leaf.leaf_version != TAPROOT_LEAF_TAPSCRIPT) return false;
    const CScript script{leaf.script.begin(), leaf.script.end()};
    return IsXOnlyLeafSpendable(wallet, script) || IsDilithiumLeafSpendable(wallet, script);
}

bool IsP2MREntryValid(const P2MREntry& entry)
{
    return std::holds_alternative<WitnessV2P2MR>(entry.dest) && BuildP2MRTreeChecked(entry.tree).has_value();
}

bool IsP2MREntrySpendable(const CWallet& wallet, const P2MREntry& entry)
{
    if (!IsP2MREntryValid(entry)) return false;
    return std::any_of(entry.tree.begin(), entry.tree.end(), [&](const P2MRTreeLeaf& leaf) {
        return IsP2MRLeafSpendable(wallet, leaf);
    });
}

P2MREntry MetadataToEntry(const CTxDestination& dest, const UniValue& meta, const std::string& fallback_id)
{
    P2MREntry entry;
    entry.dest = dest;
    entry.script_pub_key = GetScriptForDestination(dest);

    auto safe_get_str = [&meta](const std::string& key, const std::string& fallback) -> std::string {
        if (!meta.exists(key)) return fallback;
        try { return meta[key].get_str(); } catch (...) { return fallback; }
    };

    entry.id = safe_get_str("id", fallback_id);
    entry.address = safe_get_str("address", EncodeDestination(dest));
    entry.label = safe_get_str("label", "");
    entry.state = safe_get_str("state", std::string{P2MR_STATE_CREATED});

    if (meta.exists("created_at")) {
        try { entry.created_at = meta["created_at"].getInt<int64_t>(); }
        catch (...) { entry.created_at = 0; }
    }

    if (std::holds_alternative<WitnessV2P2MR>(dest)) {
        const WitnessV2P2MR& w = std::get<WitnessV2P2MR>(dest);
        std::copy(w.begin(), w.end(), entry.merkle_root.begin());
    }

    if (meta.exists("tree") && meta["tree"].isArray()) {
        // ParseP2MRTreeChecked may also throw on UniValue type access; guard it.
        try {
            auto parsed = ParseP2MRTreeChecked(meta["tree"]);
            if (parsed) entry.tree = std::move(*parsed);
        } catch (...) {
            // Leave tree empty on corrupt metadata. BuildP2MRSigningProvider
            // skips entries whose tree fails to build, so spends from a
            // corrupt entry safely error out instead of producing wrong wit-
            // nesses.
        }
    }
    return entry;
}

} // namespace

// --- Tree parsing ----------------------------------------------------------

util::Result<std::vector<P2MRTreeLeaf>> ParseP2MRTreeChecked(const UniValue& tree)
{
    if (!tree.isArray() || tree.empty()) {
        return util::Error{Untranslated("tree must be a non-empty array")};
    }
    std::vector<P2MRTreeLeaf> out;
    out.reserve(tree.size());
    for (size_t i = 0; i < tree.size(); ++i) {
        const UniValue& leaf = tree[i];
        if (!leaf.isObject() || !leaf.exists("depth") || !leaf.exists("leaf_version") || !leaf.exists("script")) {
            return util::Error{Untranslated("each tree entry must contain depth, leaf_version, script")};
        }
        // UniValue's typed getters (getInt, get_str) throw on type mismatch.
        // Convert those to clean Result errors so callers (RPC and GUI) get
        // structured error messages rather than uncaught exceptions.
        int depth, leaf_version;
        std::string script_hex;
        try {
            depth = leaf["depth"].getInt<int>();
            leaf_version = leaf["leaf_version"].getInt<int>();
            script_hex = leaf["script"].get_str();
        } catch (const std::exception& e) {
            return util::Error{Untranslated(std::string("leaf field type error: ") + e.what())};
        }
        if (depth < 0 || depth > 128) return util::Error{Untranslated("depth out of range")};
        if (leaf_version < 0 || leaf_version > 255) return util::Error{Untranslated("leaf_version out of range")};
        if ((leaf_version & ~TAPROOT_LEAF_MASK) != 0) return util::Error{Untranslated("leaf_version parity bit must be unset")};
        auto script = TryParseHex<unsigned char>(script_hex);
        if (!script) return util::Error{Untranslated("script must be valid hex")};

        P2MRTreeLeaf l;
        l.depth = static_cast<uint8_t>(depth);
        l.leaf_version = static_cast<uint8_t>(leaf_version);
        l.script = std::move(*script);
        out.push_back(std::move(l));
    }
    return out;
}

std::vector<P2MRTreeLeaf> ParseP2MRTreeFromUniValue(const UniValue& tree)
{
    auto res = ParseP2MRTreeChecked(tree);
    if (!res) throw JSONRPCError(RPC_INVALID_PARAMETER, util::ErrorString(res).original);
    return std::move(*res);
}

util::Result<P2MRBuilder> BuildP2MRTreeChecked(const std::vector<P2MRTreeLeaf>& leaves)
{
    if (leaves.empty()) {
        return util::Error{Untranslated("tree must contain at least one leaf")};
    }
    P2MRBuilder builder;
    for (const auto& leaf : leaves) {
        if (leaf.depth > P2MR_CONTROL_MAX_NODE_COUNT) {
            return util::Error{Untranslated("depth out of range")};
        }
        if ((leaf.leaf_version & ~TAPROOT_LEAF_MASK) != 0) {
            return util::Error{Untranslated("leaf_version parity bit must be unset")};
        }
        builder.Add(leaf.depth, leaf.script, leaf.leaf_version);
    }
    if (!builder.IsValid() || !builder.IsComplete()) {
        return util::Error{Untranslated("invalid P2MR tree, verify DFS order and depths")};
    }
    builder.Finalize();
    return builder;
}

UniValue P2MRTreeToUniValue(const std::vector<P2MRTreeLeaf>& leaves)
{
    UniValue out(UniValue::VARR);
    for (const auto& leaf : leaves) out.push_back(LeafToUniValue(leaf));
    return out;
}

// --- Storage / lookup ------------------------------------------------------

std::vector<P2MREntry> ListP2MR(const CWallet& wallet)
{
    AssertLockHeld(wallet.cs_wallet);
    std::vector<P2MREntry> out;
    for (const auto& [dest, rid, raw] : wallet.ListP2MRMetadata()) {
        UniValue meta;
        if (!DecodeMetadata(raw, meta)) continue;
        out.push_back(MetadataToEntry(dest, meta, rid));
    }
    return out;
}

std::optional<P2MREntry> GetP2MR(const CWallet& wallet, const std::string& id)
{
    AssertLockHeld(wallet.cs_wallet);
    for (const auto& [dest, rid, raw] : wallet.ListP2MRMetadata()) {
        if (rid != id) continue;
        UniValue meta;
        if (!DecodeMetadata(raw, meta)) continue;
        return MetadataToEntry(dest, meta, rid);
    }
    return std::nullopt;
}

// --- Signing provider ------------------------------------------------------

FlatSigningProvider BuildP2MRSigningProvider(const CWallet& wallet, const std::optional<std::string>& only_id)
{
    AssertLockHeld(wallet.cs_wallet);
    FlatSigningProvider provider;
    P2MRKeyRequirements requirements;
    for (const auto& entry : ListP2MR(wallet)) {
        if (only_id && entry.id != *only_id) continue;
        if (!std::holds_alternative<WitnessV2P2MR>(entry.dest)) continue;
        auto builder_res = BuildP2MRTreeChecked(entry.tree);
        if (!builder_res) continue;
        provider.p2mr_trees[std::get<WitnessV2P2MR>(entry.dest)] = std::move(*builder_res);
        const auto entry_requirements = GetP2MRKeyRequirements(entry.tree);
        requirements.dilithium_key_ids.insert(entry_requirements.dilithium_key_ids.begin(), entry_requirements.dilithium_key_ids.end());
        requirements.xonly_pubkeys.insert(entry_requirements.xonly_pubkeys.begin(), entry_requirements.xonly_pubkeys.end());
    }

    for (ScriptPubKeyMan* spk_man : wallet.GetAllScriptPubKeyMans()) {
        for (const XOnlyPubKey& xonly_pubkey : requirements.xonly_pubkeys) {
            CKey key;
            if (auto desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man)) {
                LOCK(desc_spk_man->cs_desc_man);
                if (!desc_spk_man->GetKeyByXOnly(xonly_pubkey, key)) continue;
            } else if (auto legacy_spk_man = dynamic_cast<LegacyScriptPubKeyMan*>(spk_man)) {
                if (!legacy_spk_man->GetKeyByXOnly(xonly_pubkey, key)) continue;
            } else {
                continue;
            }
            const CPubKey pubkey = key.GetPubKey();
            const CKeyID keyid = pubkey.GetID();
            provider.pubkeys.emplace(keyid, pubkey);
            provider.keys.emplace(keyid, std::move(key));
        }

        for (const CKeyID& keyid : requirements.dilithium_key_ids) {
            CDilithiumKey key;
            if (auto desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man)) {
                LOCK(desc_spk_man->cs_desc_man);
                if (!desc_spk_man->GetDilithiumKey(keyid, key)) continue;
            } else if (auto legacy_spk_man = dynamic_cast<LegacyScriptPubKeyMan*>(spk_man)) {
                if (!legacy_spk_man->GetDilithiumKey(keyid, key)) continue;
            } else {
                continue;
            }
            const CDilithiumPubKey pubkey = key.GetPubKey();
            const DilithiumPKHash provider_keyid(pubkey);
            provider.dilithium_pubkeys.emplace(provider_keyid, pubkey);
            provider.dilithium_keys.emplace(provider_keyid, std::move(key));
        }
    }
    return provider;
}

// --- Balance helpers -------------------------------------------------------

bool IsTrackedP2MRScript(const CWallet& wallet, const CScript& script)
{
    AssertLockHeld(wallet.cs_wallet);
    for (const auto& entry : ListP2MR(wallet)) {
        if (entry.script_pub_key != script) continue;
        if (IsP2MREntryValid(entry)) return true;
    }
    return false;
}

isminetype GetTrackedP2MRScriptIsMine(const CWallet& wallet, const CScript& script)
{
    AssertLockHeld(wallet.cs_wallet);
    for (const auto& entry : ListP2MR(wallet)) {
        if (entry.script_pub_key != script) continue;
        if (!IsP2MREntryValid(entry)) continue;
        return IsP2MREntrySpendable(wallet, entry) ? ISMINE_SPENDABLE : ISMINE_WATCH_ONLY;
    }
    return ISMINE_NO;
}

static CAmount SumUnspentForScript(const CWallet& wallet, const CScript& script, int min_depth)
{
    CAmount total{0};
    for (const auto& [txid, wtx] : wallet.mapWallet) {
        if (!wtx.tx) continue;
        if (wallet.GetTxDepthInMainChain(wtx) < min_depth) continue;
        for (uint32_t n = 0; n < wtx.tx->vout.size(); ++n) {
            const CTxOut& txout = wtx.tx->vout[n];
            if (txout.scriptPubKey != script) continue;
            if (wallet.IsSpent(COutPoint(txid, n))) continue;
            total += txout.nValue;
        }
    }
    return total;
}

CAmount GetP2MREntryBalance(const CWallet& wallet, const P2MREntry& entry, int min_depth)
{
    AssertLockHeld(wallet.cs_wallet);
    return SumUnspentForScript(wallet, entry.script_pub_key, min_depth);
}

CAmount GetTrackedP2MRBalance(const CWallet& wallet, int min_depth)
{
    AssertLockHeld(wallet.cs_wallet);
    CAmount total{0};
    std::set<CScript> counted_scripts;
    for (const auto& entry : ListP2MR(wallet)) {
        if (!counted_scripts.insert(entry.script_pub_key).second) continue;
        total += SumUnspentForScript(wallet, entry.script_pub_key, min_depth);
    }
    return total;
}

// --- Create / Fund ---------------------------------------------------------

util::Result<P2MRCreated> CreateP2MR(CWallet& wallet,
                                     const std::vector<P2MRTreeLeaf>& leaves,
                                     const std::string& label)
{
    AssertLockHeld(wallet.cs_wallet);
    auto builder_res = BuildP2MRTreeChecked(leaves);
    if (!builder_res) return util::Error{util::ErrorString(builder_res)};
    P2MRBuilder builder = std::move(*builder_res);

    P2MRCreated out;
    out.dest = builder.GetOutput();
    out.script_pub_key = GetScriptForDestination(out.dest);
    out.address = EncodeDestination(out.dest);
    const WitnessV2P2MR& w = std::get<WitnessV2P2MR>(out.dest);
    std::copy(w.begin(), w.end(), out.merkle_root.begin());

    for (const auto& entry : ListP2MR(wallet)) {
        if (entry.script_pub_key == out.script_pub_key && SameP2MRTree(entry.tree, leaves)) {
            out.id = entry.id;
            out.address = entry.address;
            out.merkle_root = entry.merkle_root;
            out.dest = entry.dest;
            return out;
        }
    }

    out.id = NewP2MRId();
    const UniValue meta = BuildMetadataJSON(out.id, out.address, out.script_pub_key, out.merkle_root, label, leaves);

    WalletBatch batch(wallet.GetDatabase(), /*fFlushOnClose=*/false);
    if (!wallet.SetAddressBook(out.dest, label, AddressPurpose::RECEIVE)) {
        return util::Error{Untranslated("failed to set P2MR address book entry")};
    }
    if (!wallet.SetP2MRMetadata(batch, out.dest, out.id, meta.write())) {
        return util::Error{Untranslated("failed to persist P2MR metadata")};
    }
    return out;
}

util::Result<P2MRFunded> FundP2MR(CWallet& wallet,
                                  const std::vector<P2MRTreeLeaf>& leaves,
                                  CAmount amount,
                                  const std::string& label,
                                  bool subtract_fee_from_amount,
                                  const CCoinControl& coin_control)
{
    AssertLockHeld(wallet.cs_wallet);
    if (wallet.IsLocked()) {
        return util::Error{_("Wallet is locked")};
    }

    auto created_res = CreateP2MR(wallet, leaves, label);
    if (!created_res) return util::Error{util::ErrorString(created_res)};
    P2MRCreated created = std::move(*created_res);

    std::vector<CRecipient> recipients{{created.dest, amount, subtract_fee_from_amount}};
    auto tx_res = CreateTransaction(wallet, recipients, /*change_pos=*/-1, coin_control, /*sign=*/true);
    if (!tx_res) {
        return util::Error{util::ErrorString(tx_res)};
    }

    wallet.CommitTransaction(tx_res->tx, /*value_map=*/{}, /*orderForm=*/{});

    P2MRFunded out;
    out.created = std::move(created);
    out.txid = tx_res->tx->GetHash();
    out.fee = tx_res->fee;
    return out;
}

// --- Spend / sign / test ---------------------------------------------------

util::Result<P2MRSpendUnsigned> CreateP2MRSpend(CWallet& wallet,
                                                const std::string& p2mr_id,
                                                const CTxDestination& to_dest,
                                                CAmount send_amount,
                                                CAmount fee)
{
    AssertLockHeld(wallet.cs_wallet);
    if (!IsValidDestination(to_dest)) {
        return util::Error{Untranslated("invalid destination address")};
    }
    if (send_amount <= 0) {
        return util::Error{Untranslated("send amount must be positive")};
    }
    if (fee < 0) {
        return util::Error{Untranslated("fee must be non-negative")};
    }

    auto entry = GetP2MR(wallet, p2mr_id);
    if (!entry) return util::Error{Untranslated("unknown p2mr_id")};

    const CScript& target_spk = entry->script_pub_key;
    if (!MoneyRange(send_amount) || !MoneyRange(fee) || send_amount > MAX_MONEY - fee) {
        return util::Error{Untranslated("amount out of range")};
    }
    const CAmount target_amount = send_amount + fee;

    std::vector<std::pair<COutPoint, CAmount>> selected;
    CAmount input_amount{0};
    for (const auto& [txid, wtx] : wallet.mapWallet) {
        if (!wtx.tx) continue;
        if (wallet.GetTxDepthInMainChain(wtx) <= 0) continue;
        for (uint32_t n = 0; n < wtx.tx->vout.size(); ++n) {
            const CTxOut& txout = wtx.tx->vout[n];
            if (txout.scriptPubKey != target_spk) continue;
            const COutPoint outpoint{txid, n};
            if (wallet.IsSpent(outpoint)) continue;
            selected.emplace_back(outpoint, txout.nValue);
            input_amount += txout.nValue;
            if (input_amount >= target_amount) break;
        }
        if (input_amount >= target_amount) break;
    }
    if (selected.empty()) return util::Error{Untranslated("no spendable P2MR UTXO found")};

    const CAmount change = input_amount - target_amount;
    if (change < 0) return util::Error{Untranslated("insufficient P2MR UTXO amount")};

    P2MRSpendUnsigned out;
    out.input = selected.front().first;
    out.inputs.reserve(selected.size());
    for (const auto& selected_input : selected) {
        out.tx.vin.emplace_back(selected_input.first);
        out.inputs.push_back(selected_input.first);
    }
    out.tx.vout.emplace_back(send_amount, GetScriptForDestination(to_dest));

    if (change > DEFAULT_P2MR_DUST_THRESHOLD) {
        auto change_dest = wallet.GetNewChangeDestination(OutputType::BECH32);
        if (!change_dest) return util::Error{util::ErrorString(change_dest)};
        out.tx.vout.emplace_back(change, GetScriptForDestination(*change_dest));
        out.has_change = true;
        out.change_amount = change;
    }

    out.p2mr_id = p2mr_id;
    out.input_amount = input_amount;
    out.effective_fee = input_amount - send_amount - out.change_amount;
    return out;
}

util::Result<P2MRSpendSigned> SignP2MRTransaction(const CWallet& wallet,
                                                  const CMutableTransaction& tx_in,
                                                  const std::optional<std::string>& only_id)
{
    AssertLockHeld(wallet.cs_wallet);
    P2MRSpendSigned out;
    out.tx = tx_in;

    std::map<COutPoint, Coin> coins;
    for (const CTxIn& txin : out.tx.vin) coins[txin.prevout];
    wallet.chain().findCoins(coins);

    FlatSigningProvider p2mr_provider = BuildP2MRSigningProvider(wallet, only_id);

    // First let wallet sign any non-P2MR inputs (e.g. legacy/segwit change being consolidated).
    std::map<int, bilingual_str> ignored_errors;
    wallet.SignTransaction(out.tx, coins, SIGHASH_DEFAULT, ignored_errors);

    std::vector<CTxOut> spent_outputs;
    spent_outputs.reserve(out.tx.vin.size());
    for (const CTxIn& txin : out.tx.vin) {
        const auto coin_it = coins.find(txin.prevout);
        spent_outputs.push_back(coin_it != coins.end() && !coin_it->second.IsSpent() ? coin_it->second.out : CTxOut{});
    }
    PrecomputedTransactionData txdata;
    txdata.Init(out.tx, std::move(spent_outputs), /*force=*/true);

    bool complete = true;
    for (unsigned int i = 0; i < out.tx.vin.size(); ++i) {
        auto it = coins.find(out.tx.vin[i].prevout);
        if (it == coins.end() || it->second.IsSpent()) continue;
        std::vector<std::vector<unsigned char>> solutions;
        if (Solver(it->second.out.scriptPubKey, solutions) != TxoutType::WITNESS_V2_P2MR) continue;

        SignatureData sigdata = DataFromTransaction(out.tx, i, it->second.out);
        MutableTransactionSignatureCreator creator(out.tx, i, it->second.out.nValue, &txdata, SIGHASH_DEFAULT);
        if (ProduceSignature(p2mr_provider, creator, it->second.out.scriptPubKey, sigdata)) {
            UpdateInput(out.tx.vin[i], sigdata);
            continue;
        }

        // Fallback for no-argument scripts such as OP_TRUE. Only report completion
        // after verifying the assembled witness against the actual transaction.
        bool fallback_ok = false;
        if (!solutions.empty() && solutions[0].size() == WitnessV2P2MR::SIZE) {
            const WitnessV2P2MR p2mr_output{solutions[0]};
            P2MRSpendData spenddata;
            if (p2mr_provider.GetP2MRSpendData(p2mr_output, spenddata) && !spenddata.scripts.empty()) {
                const auto& [script_key, control_blocks] = *spenddata.scripts.begin();
                const auto& [script, leaf_version] = script_key;
                if (leaf_version == TAPROOT_LEAF_TAPSCRIPT && !control_blocks.empty()) {
                    out.tx.vin[i].scriptSig.clear();
                    out.tx.vin[i].scriptWitness.stack.clear();
                    out.tx.vin[i].scriptWitness.stack.emplace_back(script.begin(), script.end());
                    out.tx.vin[i].scriptWitness.stack.push_back(*control_blocks.begin());
                    std::vector<CTxOut> spent_outputs;
                    spent_outputs.reserve(out.tx.vin.size());
                    for (const CTxIn& txin : out.tx.vin) {
                        const auto coin_it = coins.find(txin.prevout);
                        spent_outputs.push_back(coin_it != coins.end() && !coin_it->second.IsSpent() ? coin_it->second.out : CTxOut{});
                    }
                    PrecomputedTransactionData txdata;
                    txdata.Init(out.tx, std::move(spent_outputs), /*force=*/true);
                    const CTransaction tx_const{out.tx};
                    TransactionSignatureChecker checker(&tx_const, i, it->second.out.nValue, txdata, MissingDataBehavior::FAIL);
                    fallback_ok = VerifyScript(out.tx.vin[i].scriptSig, it->second.out.scriptPubKey, &out.tx.vin[i].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, checker);
                    if (!fallback_ok) out.tx.vin[i].scriptWitness.stack.clear();
                }
            }
        }
        if (!fallback_ok) complete = false;
    }

    out.complete = complete;
    return out;
}

P2MRMempoolAccept TestP2MRTransaction(CWallet& wallet, const CMutableTransaction& tx)
{
    CTransactionRef tx_ref = MakeTransactionRef(tx);
    std::string err_string;
    const bool allowed = wallet.chain().broadcastTransaction(tx_ref, /*max_tx_fee=*/MAX_MONEY,
                                                             /*relay=*/false, err_string);

    P2MRMempoolAccept out;
    out.txid = tx_ref->GetHash();
    out.allowed = allowed;
    if (!allowed) out.reject_reason = err_string;
    return out;
}

} // namespace wallet
