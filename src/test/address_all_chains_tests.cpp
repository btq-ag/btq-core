// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Multi-chain address encode/decode simulation.
//
// For every chain (mainnet, testnet, signet, regtest) and every address
// family (classical Base58 P2PKH/P2SH, SegWit v0 P2WPKH/P2WSH, Taproot
// v1, Dilithium Base58 P2DPKH/P2DSH, Dilithium SegWit v0 P2DWPKH/P2DWSH),
// this test:
//
//   1. Builds a canonical destination with deterministic payload.
//   2. Encodes it under that chain's params and checks the address prefix
//      matches the expected bech32/base58 metadata.
//   3. Round-trips EncodeDestination -> DecodeDestination -> same variant.
//   4. Cross-chain rejection: under every OTHER chain's params, the same
//      address must fail to decode (IsValidDestination == false) and
//      DecodeDestination's error_str must be non-empty and descriptive.
//
// This test would have caught the "tbtq addresses not working for
// transfers" regression: it deterministically exercises testnet HRPs
// (tbtq / tdbt) end-to-end without requiring a running node.
//
// Narrative: --log_level=message, for example:
//   ./test/test_btq --run_test=address_all_chains_tests --log_level=message

#include <addresstype.h>
#include <chainparams.h>
#include <crypto/dilithium_key.h>
#include <key_io.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <array>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(address_all_chains_tests, BasicTestingSetup)

namespace {

struct ChainInfo {
    ChainType chain;
    const char* name;
    const char* bech32_hrp;
    const char* dilithium_bech32_hrp;
    unsigned char base58_pubkey;
    unsigned char base58_script;
    unsigned char base58_dilithium_pubkey;
    unsigned char base58_dilithium_script;
};

constexpr std::array<ChainInfo, 4> kAllChains{{
    {ChainType::BTQMAIN,    "main",    "qbtc", "dbtc", 75,  135, 76,  136},
    {ChainType::BTQTEST,    "test",    "tbtq", "tdbt", 111, 196, 112, 197},
    {ChainType::BTQSIGNET,  "signet",  "qtb",  "sdbt", 111, 196, 112, 197},
    {ChainType::BTQREGTEST, "regtest", "qcrt", "rdbt", 111, 196, 112, 197},
}};

// Deterministic payloads so encoded addresses are stable across runs.
uint160 MakeHash160(uint8_t seed)
{
    uint160 h;
    for (size_t i = 0; i < h.size(); ++i) h.data()[i] = static_cast<uint8_t>(seed + i);
    return h;
}

uint256 MakeHash256(uint8_t seed)
{
    uint256 h;
    for (size_t i = 0; i < h.size(); ++i) h.data()[i] = static_cast<uint8_t>(seed + i);
    return h;
}

// Build one destination of every supported kind for the given chain.
std::vector<std::pair<std::string, CTxDestination>> BuildAllDestinations()
{
    std::vector<std::pair<std::string, CTxDestination>> out;

    out.emplace_back("P2PKH",     PKHash(MakeHash160(0x10)));
    out.emplace_back("P2SH",      ScriptHash(MakeHash160(0x20)));
    out.emplace_back("P2WPKH",    WitnessV0KeyHash(MakeHash160(0x30)));
    out.emplace_back("P2WSH",     WitnessV0ScriptHash(MakeHash256(0x40)));

    // Deterministic 32-byte x-only pubkey for the Taproot case.
    std::vector<unsigned char> xonly(32);
    for (size_t i = 0; i < xonly.size(); ++i) xonly[i] = static_cast<uint8_t>(0x50 + i);
    out.emplace_back("P2TR",      WitnessV1Taproot(XOnlyPubKey(xonly)));

    out.emplace_back("P2DPKH",    DilithiumPKHash(MakeHash160(0x60)));
    out.emplace_back("P2DSH",     DilithiumScriptHash(MakeHash160(0x70)));
    out.emplace_back("P2DWPKH",   DilithiumWitnessV0KeyHash(MakeHash160(0x80)));
    out.emplace_back("P2DWSH",    DilithiumWitnessV0ScriptHash(MakeHash256(0x90)));

    return out;
}

bool StartsWithCaseInsensitive(const std::string& s, const std::string& prefix)
{
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (ToLower(s[i]) != ToLower(prefix[i])) return false;
    }
    return true;
}

// Expected human-readable prefix for each destination label on a given chain.
std::string ExpectedPrefix(const std::string& label, const ChainInfo& info)
{
    if (label == "P2WPKH" || label == "P2WSH" || label == "P2TR") {
        return std::string(info.bech32_hrp) + "1";
    }
    if (label == "P2DWPKH" || label == "P2DWSH") {
        return std::string(info.dilithium_bech32_hrp) + "1";
    }
    return {}; // Base58 prefixes vary per version byte; we'll skip prefix check.
}

std::string PreviewAddr(const std::string& addr, size_t head = 36, size_t tail = 14)
{
    if (addr.size() <= head + tail + 3) return addr;
    return addr.substr(0, head) + "…(" + std::to_string(addr.size()) + " chars)…" + addr.substr(addr.size() - tail);
}

std::string ErrSnippet(const std::string& err, size_t maxl = 160)
{
    if (err.size() <= maxl) return err;
    return err.substr(0, maxl) + "…";
}

/** Human-readable variant name for log output (matches CTxDestination alternatives). */
std::string VariantLabel(const CTxDestination& d)
{
    if (std::holds_alternative<CNoDestination>(d)) return "CNoDestination";
    if (std::holds_alternative<PubKeyDestination>(d)) return "PubKeyDestination";
    if (std::holds_alternative<PKHash>(d)) return "PKHash(P2PKH)";
    if (std::holds_alternative<ScriptHash>(d)) return "ScriptHash(P2SH)";
    if (std::holds_alternative<WitnessV0ScriptHash>(d)) return "WitnessV0ScriptHash(P2WSH)";
    if (std::holds_alternative<WitnessV0KeyHash>(d)) return "WitnessV0KeyHash(P2WPKH)";
    if (std::holds_alternative<WitnessV1Taproot>(d)) return "WitnessV1Taproot(P2TR)";
    if (std::holds_alternative<WitnessV2P2MR>(d)) return "WitnessV2P2MR";
    if (std::holds_alternative<WitnessUnknown>(d)) return "WitnessUnknown";
    if (std::holds_alternative<DilithiumPubKeyDestination>(d)) return "DilithiumPubKeyDestination";
    if (std::holds_alternative<DilithiumPKHash>(d)) return "DilithiumPKHash(P2DPKH)";
    if (std::holds_alternative<DilithiumScriptHash>(d)) return "DilithiumScriptHash(P2DSH)";
    if (std::holds_alternative<DilithiumWitnessV0KeyHash>(d)) return "DilithiumWitnessV0KeyHash(P2DWPKH)";
    if (std::holds_alternative<DilithiumWitnessV0ScriptHash>(d)) return "DilithiumWitnessV0ScriptHash(P2DWSH)";
    return "unknown_variant";
}

} // namespace

// Per-chain round-trip: encode under chain X, decode under chain X, require
// success and variant equality.
BOOST_AUTO_TEST_CASE(encode_decode_roundtrip_all_chains)
{
    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("===================================================================");
    BOOST_TEST_MESSAGE(" encode_decode_roundtrip_all_chains");
    BOOST_TEST_MESSAGE("-------------------------------------------------------------------");
    BOOST_TEST_MESSAGE(" For main / test / signet / regtest: SelectParams, encode each");
    BOOST_TEST_MESSAGE(" destination family, check Bech32 HRP prefix where applicable,");
    BOOST_TEST_MESSAGE(" DecodeDestination on the same chain, assert variant + re-encode.");
    BOOST_TEST_MESSAGE("===================================================================");

    for (const auto& info : kAllChains) {
        BOOST_TEST_MESSAGE("");
        BOOST_TEST_MESSAGE("--- Chain: " << info.name << " | bech32 HRP: " << info.bech32_hrp
            << " | dilithium HRP: " << info.dilithium_bech32_hrp << " ---");
        SelectParams(info.chain);
        const auto dests = BuildAllDestinations();

        for (const auto& [label, dest] : dests) {
            const std::string encoded = EncodeDestination(dest);
            BOOST_REQUIRE_MESSAGE(!encoded.empty(),
                "EncodeDestination returned empty for " << label << " on " << info.name);

            const std::string expected_prefix = ExpectedPrefix(label, info);
            if (!expected_prefix.empty()) {
                BOOST_CHECK_MESSAGE(StartsWithCaseInsensitive(encoded, expected_prefix),
                    "Expected " << label << " on " << info.name << " to start with '"
                                << expected_prefix << "', got " << encoded);
            }

            std::string err;
            CTxDestination decoded = DecodeDestination(encoded, err);
            const bool legacy_dilithium =
                label == "P2DPKH" || label == "P2DSH" || label == "P2DWPKH" || label == "P2DWSH";
            // Base58 Dilithium P2PKH/P2SH stay valid payment destinations on
            // testnet while P2MR-only is unscheduled; witness-v0 Dilithium and
            // post-activation Base58 Dilithium do not.
            const bool legacy_base58_payments_allowed =
                (label == "P2DPKH" || label == "P2DSH") && info.chain == ChainType::BTQTEST;
            if (legacy_dilithium && !legacy_base58_payments_allowed) {
                // Historical Dilithium destinations remain encodable/decodable for
                // display, but are no longer valid payment destinations.
                BOOST_CHECK_MESSAGE(decoded.index() == dest.index(),
                    "Decoded variant index mismatch for " << label << " on " << info.name);
                BOOST_CHECK_MESSAGE(!IsValidDestination(decoded),
                    "Legacy Dilithium destination unexpectedly valid for payments: " << label);
            } else {
                BOOST_CHECK_MESSAGE(IsValidDestination(decoded),
                    "Same-chain decode failed for " << label << " on " << info.name
                    << " addr=" << encoded << " err=" << err);
            }
            BOOST_CHECK_MESSAGE(err.empty(),
                "Same-chain decode produced error for " << label << " on " << info.name
                << " addr=" << encoded << " err=" << err);
            BOOST_CHECK_MESSAGE(decoded.index() == dest.index(),
                "Decoded variant index mismatch for " << label << " on " << info.name);
            BOOST_CHECK_MESSAGE(EncodeDestination(decoded) == encoded,
                "Re-encoded address mismatch for " << label << " on " << info.name);
            BOOST_TEST_MESSAGE("      PASS " << label << ": " << PreviewAddr(encoded));
            BOOST_TEST_MESSAGE("             decode ok, variant " << VariantLabel(decoded) << ", err empty, re-encode identical"
                << (expected_prefix.empty() ? "" : std::string("; prefix starts with ") + expected_prefix));
        }
    }

    // Restore mainnet for subsequent tests.
    SelectParams(ChainType::BTQMAIN);
    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE(" ... Restored chain params to mainnet.");
}

// Cross-chain rejection: an address encoded under chain X must NOT decode
// successfully under any other chain Y. This is the direct coverage for
// "tbtq addresses not working for transfers" when the node is on the wrong
// network.
BOOST_AUTO_TEST_CASE(cross_chain_rejection_all_chains)
{
    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("===================================================================");
    BOOST_TEST_MESSAGE(" cross_chain_rejection_all_chains");
    BOOST_TEST_MESSAGE("-------------------------------------------------------------------");
    BOOST_TEST_MESSAGE(" Build the full address grid (all chains x all destination kinds).");
    BOOST_TEST_MESSAGE(" For each TARGET chain, decode every address: same source must pass;");
    BOOST_TEST_MESSAGE(" cross-chain Bech32 must fail with a useful error; Base58 may still");
    BOOST_TEST_MESSAGE(" decode across test/signet/regtest when version bytes match (documented).");
    BOOST_TEST_MESSAGE("===================================================================");

    // First, collect every chain's encoded addresses under its own params.
    struct Row {
        ChainType source_chain;
        const char* source_name;
        std::string label;
        std::string address;
    };
    std::vector<Row> rows;

    for (const auto& info : kAllChains) {
        SelectParams(info.chain);
        BOOST_TEST_MESSAGE(" Capturing addresses under native params: " << info.name);
        for (const auto& [label, dest] : BuildAllDestinations()) {
            const std::string encoded = EncodeDestination(dest);
            BOOST_REQUIRE(!encoded.empty());
            rows.push_back({info.chain, info.name, label, encoded});
        }
    }
    BOOST_TEST_MESSAGE(" Collected " << rows.size() << " encoded addresses total.");

    // Now walk every (target chain, source row) pair. Same-chain must accept,
    // cross-chain bech32 types must reject with a useful error.
    for (const auto& target : kAllChains) {
        BOOST_TEST_MESSAGE("");
        BOOST_TEST_MESSAGE("--- Decode matrix with SelectParams(" << target.name << ") ---");
        SelectParams(target.chain);

        size_t count_same_chain = 0;
        size_t count_base58_cross_skipped = 0;
        size_t count_cross_invalid = 0;
        size_t count_bech32_cross_with_err = 0;
        std::string sample_bech32_err;

        for (const auto& row : rows) {
            std::string err;
            CTxDestination decoded = DecodeDestination(row.address, err);
            const bool valid = IsValidDestination(decoded);

            if (row.source_chain == target.chain) {
                ++count_same_chain;
                // Mirror encode_decode_roundtrip_all_chains: witness-v0 Dilithium
                // is never a payment destination; Base58 Dilithium only while
                // testnet keeps P2MR-only unscheduled.
                const bool legacy_dilithium =
                    row.label == "P2DPKH" || row.label == "P2DSH" ||
                    row.label == "P2DWPKH" || row.label == "P2DWSH";
                const bool legacy_base58_payments_allowed =
                    (row.label == "P2DPKH" || row.label == "P2DSH") &&
                    target.chain == ChainType::BTQTEST;
                if (legacy_dilithium && !legacy_base58_payments_allowed) {
                    BOOST_CHECK_MESSAGE(!valid,
                        "Legacy Dilithium destination unexpectedly valid for payments: "
                            << row.label << " on " << target.name
                            << " addr=" << row.address);
                } else {
                    BOOST_CHECK_MESSAGE(valid,
                        "Expected " << row.label << " from " << row.source_name
                                    << " to decode on target " << target.name
                                    << " addr=" << row.address << " err=" << err);
                }
                continue;
            }

            // Base58-family destinations share version bytes between testnet,
            // signet and regtest (all use 111/196/112/197). On those chain
            // combinations a cross-chain Base58 address *will* still decode.
            // That is a property of the protocol, not a bug: only bech32
            // family addresses carry an HRP that nails the network.
            const bool base58_family =
                row.label == "P2PKH" || row.label == "P2SH" ||
                row.label == "P2DPKH" || row.label == "P2DSH";

            const bool base58_prefixes_match =
                row.source_chain != ChainType::BTQMAIN &&
                target.chain     != ChainType::BTQMAIN;

            if (base58_family && base58_prefixes_match) {
                ++count_base58_cross_skipped;
                // Tolerated: document but don't fail.
                continue;
            }

            ++count_cross_invalid;
            BOOST_CHECK_MESSAGE(!valid,
                "Cross-chain decode unexpectedly succeeded: " << row.label
                << " from " << row.source_name << " on target " << target.name
                << " addr=" << row.address);

            // For bech32 families the decoder should name the HRP mismatch.
            const bool bech32_family =
                row.label == "P2WPKH" || row.label == "P2WSH" || row.label == "P2TR" ||
                row.label == "P2DWPKH" || row.label == "P2DWSH";

            if (bech32_family) {
                ++count_bech32_cross_with_err;
                if (sample_bech32_err.empty() && !err.empty()) sample_bech32_err = err;
                BOOST_CHECK_MESSAGE(!err.empty(),
                    "Expected non-empty cross-chain error for " << row.label
                    << " from " << row.source_name << " on target " << target.name
                    << " addr=" << row.address);
            }
        }
        BOOST_TEST_MESSAGE("    Summary: same-chain decodes exercised=" << count_same_chain
            << " (expect 9); base58 cross skipped=" << count_base58_cross_skipped
            << "; strict cross-chain rejects=" << count_cross_invalid
            << "; bech32 cross checks (non-empty err)=" << count_bech32_cross_with_err);
        if (!sample_bech32_err.empty()) {
            BOOST_TEST_MESSAGE("    Example cross-chain decoder message: " << ErrSnippet(sample_bech32_err));
        }
    }

    SelectParams(ChainType::BTQMAIN);
    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE(" ... Cross-chain matrix complete; mainnet params restored.");
}

// Regression vector: a literal `tbtq1...` SegWit v0 address must decode on
// testnet, and must be rejected with a descriptive error on every other
// chain. This is the exact shape of the user-reported failure.
BOOST_AUTO_TEST_CASE(tbtq_regression_vector)
{
    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("===================================================================");
    BOOST_TEST_MESSAGE(" tbtq_regression_vector");
    BOOST_TEST_MESSAGE("-------------------------------------------------------------------");
    BOOST_TEST_MESSAGE(" User-style regression: fixed payload -> tbtq1... and tdbt1... strings,");
    BOOST_TEST_MESSAGE(" must decode on testnet only; other chains reject with non-empty err.");
    BOOST_TEST_MESSAGE("===================================================================");

    // Build a deterministic testnet P2WPKH address.
    SelectParams(ChainType::BTQTEST);
    BOOST_TEST_MESSAGE(" Step 1: SelectParams(test); encode P2WPKH -> expect tbtq1 prefix.");
    const std::string tbtq_addr = EncodeDestination(WitnessV0KeyHash(MakeHash160(0xAB)));
    BOOST_REQUIRE_MESSAGE(!tbtq_addr.empty(), "Failed to encode tbtq P2WPKH on testnet");
    BOOST_CHECK_MESSAGE(StartsWithCaseInsensitive(tbtq_addr, "tbtq1"),
        "Expected tbtq1... prefix, got " << tbtq_addr);
    BOOST_TEST_MESSAGE("       Encoded: " << PreviewAddr(tbtq_addr));

    BOOST_TEST_MESSAGE(" Step 2: encode Dilithium P2DWPKH -> expect tdbt1 prefix.");
    // And a testnet Dilithium P2DWPKH address.
    const std::string tdbt_addr = EncodeDestination(DilithiumWitnessV0KeyHash(MakeHash160(0xCD)));
    BOOST_REQUIRE_MESSAGE(!tdbt_addr.empty(), "Failed to encode tdbt P2DWPKH on testnet");
    BOOST_CHECK_MESSAGE(StartsWithCaseInsensitive(tdbt_addr, "tdbt1"),
        "Expected tdbt1... prefix, got " << tdbt_addr);
    BOOST_TEST_MESSAGE("       Encoded: " << PreviewAddr(tdbt_addr));

    BOOST_TEST_MESSAGE(" Step 3: decode both on testnet (must succeed, empty err).");
    // Same chain: must succeed.
    {
        std::string err;
        CTxDestination d = DecodeDestination(tbtq_addr, err);
        BOOST_CHECK_MESSAGE(IsValidDestination(d),
            "tbtq P2WPKH failed to decode on testnet: " << err);
        BOOST_TEST_MESSAGE("       On testnet: tbtq addr valid; variant=" << VariantLabel(d) << "; err=\"" << err << "\".");
    }
    {
        std::string err;
        CTxDestination d = DecodeDestination(tdbt_addr, err);
        // Historical Dilithium witness-v0 addresses still decode, but are not
        // valid payment destinations under P2MR-only Dilithium consensus.
        BOOST_CHECK_MESSAGE(std::holds_alternative<DilithiumWitnessV0KeyHash>(d),
            "tdbt P2DWPKH failed to decode on testnet: " << err);
        BOOST_CHECK_MESSAGE(!IsValidDestination(d),
            "tdbt P2DWPKH unexpectedly remains a valid payment destination");
        BOOST_TEST_MESSAGE("       On testnet: tdbt addr decoded; variant=" << VariantLabel(d) << "; err=\"" << err << "\".");
    }

    BOOST_TEST_MESSAGE(" Step 4: switch to each non-testnet chain; both addresses must fail");
    BOOST_TEST_MESSAGE("         DecodeDestination with IsValidDestination == false and err non-empty.");
    // Every other chain must reject with a non-empty diagnostic.
    for (const auto& info : kAllChains) {
        if (info.chain == ChainType::BTQTEST) continue;
        SelectParams(info.chain);
        BOOST_TEST_MESSAGE("    Trying wrong chain: " << info.name);

        {
            std::string err;
            CTxDestination d = DecodeDestination(tbtq_addr, err);
            BOOST_CHECK_MESSAGE(!IsValidDestination(d),
                "tbtq address unexpectedly valid on " << info.name);
            BOOST_CHECK_MESSAGE(!err.empty(),
                "Expected diagnostic for tbtq on " << info.name << " but got empty error. "
                "This is the 'silent invalid BTQ address' UX bug.");
            BOOST_TEST_MESSAGE("       tbtq on " << info.name << ": IsValid=" << IsValidDestination(d)
                << " err=\"" << ErrSnippet(err) << "\"");
        }
        {
            std::string err;
            CTxDestination d = DecodeDestination(tdbt_addr, err);
            BOOST_CHECK_MESSAGE(!IsValidDestination(d),
                "tdbt address unexpectedly valid on " << info.name);
            BOOST_CHECK_MESSAGE(!err.empty(),
                "Expected diagnostic for tdbt on " << info.name << " but got empty error.");
            BOOST_TEST_MESSAGE("       tdbt on " << info.name << ": IsValid=" << IsValidDestination(d)
                << " err=\"" << ErrSnippet(err) << "\"");
        }
    }

    SelectParams(ChainType::BTQMAIN);
    BOOST_TEST_MESSAGE(" ... Regression vector complete; mainnet params restored.");
}

BOOST_AUTO_TEST_SUITE_END()
