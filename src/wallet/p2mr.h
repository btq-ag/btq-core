// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_WALLET_P2MR_H
#define BTQ_WALLET_P2MR_H

#include <addresstype.h>
#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <uint256.h>
#include <util/result.h>
#include <wallet/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

class CCoinControl;
class UniValue;

namespace wallet {
class CWallet;

/**
 * Internal representation of a BIP360 P2MR script tree leaf.
 *
 * A tree is given in DFS order. Each leaf carries its depth in the binary
 * Merkle tree, a leaf version (typically TAPROOT_LEAF_TAPSCRIPT = 0xc0),
 * and the raw tapscript bytes.
 */
struct P2MRTreeLeaf {
    uint8_t depth{0};
    uint8_t leaf_version{0xc0};
    std::vector<unsigned char> script;
};

/** Metadata describing a wallet-tracked P2MR destination. */
struct P2MREntry {
    std::string id;                 //!< wallet-local 16-char hex id
    std::string address;            //!< encoded address (bech32m, witness v2)
    CScript script_pub_key;         //!< 34-byte scriptPubKey
    uint256 merkle_root;            //!< raw 32-byte merkle root
    int64_t created_at{0};          //!< wallet-local creation time
    std::string label;
    std::string state;              //!< e.g. "created"
    std::vector<P2MRTreeLeaf> tree;
    CTxDestination dest;            //!< parsed CTxDestination (WitnessV2P2MR)
};

/** Result of creating and storing a new P2MR destination. */
struct P2MRCreated {
    std::string id;
    std::string address;
    CScript script_pub_key;
    uint256 merkle_root;
    CTxDestination dest;
};

/** Result of funding a P2MR destination (createstored + send). */
struct P2MRFunded {
    P2MRCreated created;
    uint256 txid;
    CAmount fee{0};
};

/** Result of building an unsigned P2MR spend transaction. */
struct P2MRSpendUnsigned {
    CMutableTransaction tx;
    std::string p2mr_id;
    COutPoint input;
    std::vector<COutPoint> inputs;
    CAmount input_amount{0};
    CAmount effective_fee{0};
    CAmount change_amount{0};
    bool has_change{false};
};

/** Result of signing a P2MR transaction. */
struct P2MRSpendSigned {
    CMutableTransaction tx;
    bool complete{false};
};

/** Result of a mempool-accept dry run. */
struct P2MRMempoolAccept {
    uint256 txid;
    bool allowed{false};
    std::string reject_reason;
};

// --- Tree parsing helpers ----------------------------------------------------

/**
 * Convert tree leaves from a UniValue array (used by RPC layer) into the
 * canonical internal representation.
 * Throws JSONRPCError on malformed input when called from RPC context, or
 * returns Result error when called via the API form (see ParseP2MRTreeChecked).
 */
std::vector<P2MRTreeLeaf> ParseP2MRTreeFromUniValue(const UniValue& tree);

/** Non-throwing version returning a Result. */
util::Result<std::vector<P2MRTreeLeaf>> ParseP2MRTreeChecked(const UniValue& tree);

/** Build and finalize a P2MRBuilder from leaves. */
util::Result<P2MRBuilder> BuildP2MRTreeChecked(const std::vector<P2MRTreeLeaf>& leaves);

/** Encode leaves back as a UniValue array (DFS order preserved). */
UniValue P2MRTreeToUniValue(const std::vector<P2MRTreeLeaf>& leaves);

// --- Wallet operations (GUI-friendly, non-throwing) --------------------------

/** List all P2MR metadata entries stored in the wallet. */
std::vector<P2MREntry> ListP2MR(const CWallet& wallet);

/** Lookup a single entry by id. */
std::optional<P2MREntry> GetP2MR(const CWallet& wallet, const std::string& id);

/** Lookup a tracked P2MR entry by scriptPubKey. */
std::optional<P2MREntry> GetP2MRByScript(const CWallet& wallet, const CScript& script);

/** Lookup a tracked P2MR entry by destination / address encoding. */
std::optional<P2MREntry> GetP2MRByDestination(const CWallet& wallet, const CTxDestination& dest);

/**
 * Resolve the Dilithium key id used by a single-key Dilithium P2MR receive
 * destination. Returns nullopt if the address is not a tracked P2MR or the
 * tree does not contain exactly one Dilithium key.
 */
std::optional<CKeyID> GetSingleDilithiumKeyIDForP2MR(const CWallet& wallet, const CTxDestination& dest);

/** Create and persist a new P2MR destination. */
util::Result<P2MRCreated> CreateP2MR(CWallet& wallet,
                                     const std::vector<P2MRTreeLeaf>& leaves,
                                     const std::string& label);

/**
 * Generate (or reuse) a wallet Dilithium key and create a single-leaf P2MR
 * receive destination whose leaf is `<pubkey> OP_CHECKSIGDILITHIUM`.
 * This is the only consensus-valid Dilithium receive path.
 */
util::Result<P2MRCreated> CreateDilithiumP2MRReceive(CWallet& wallet, const std::string& label);

/**
 * Import an existing Dilithium key and create a matching single-leaf P2MR
 * receive destination for it.
 */
util::Result<P2MRCreated> ImportDilithiumKeyAsP2MR(CWallet& wallet,
                                                   const CDilithiumKey& key,
                                                   const std::string& label);

/** Create + persist + fund a P2MR destination in one call. */
util::Result<P2MRFunded> FundP2MR(CWallet& wallet,
                                  const std::vector<P2MRTreeLeaf>& leaves,
                                  CAmount amount,
                                  const std::string& label,
                                  bool subtract_fee_from_amount,
                                  const CCoinControl& coin_control);

/** Build an unsigned spend of a tracked P2MR UTXO to a destination.
 *  Non-const because change address generation may extend the keypool. */
util::Result<P2MRSpendUnsigned> CreateP2MRSpend(CWallet& wallet,
                                                const std::string& p2mr_id,
                                                const CTxDestination& to_dest,
                                                CAmount send_amount,
                                                CAmount fee);

/** Sign P2MR inputs using stored metadata. Leaves unrelated inputs untouched. */
util::Result<P2MRSpendSigned> SignP2MRTransaction(const CWallet& wallet,
                                                  const CMutableTransaction& tx_in,
                                                  const std::optional<std::string>& only_id);

/** Dry-run mempool accept (no broadcast/relay). */
P2MRMempoolAccept TestP2MRTransaction(CWallet& wallet, const CMutableTransaction& tx);

/**
 * Build a FlatSigningProvider populated with the P2MR builders for the
 * wallet's stored metadata, optionally restricted to a single id.
 */
FlatSigningProvider BuildP2MRSigningProvider(const CWallet& wallet,
                                             const std::optional<std::string>& only_id);

/** Return true if the script matches any wallet-tracked P2MR scriptPubKey. */
bool IsTrackedP2MRScript(const CWallet& wallet, const CScript& script);

/** Return spendable/watch-only ownership for a wallet-tracked P2MR scriptPubKey. */
isminetype GetTrackedP2MRScriptIsMine(const CWallet& wallet, const CScript& script);

/** Sum the values of confirmed unspent outputs whose script matches a tracked P2MR. */
CAmount GetTrackedP2MRBalance(const CWallet& wallet, int min_depth = 1);

/** Per-entry confirmed unspent balance. */
CAmount GetP2MREntryBalance(const CWallet& wallet, const P2MREntry& entry, int min_depth = 1);

} // namespace wallet

#endif // BTQ_WALLET_P2MR_H
