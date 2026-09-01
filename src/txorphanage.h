// Copyright (c) 2021-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_TXORPHANAGE_H
#define BTQ_TXORPHANAGE_H

#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <net.h>
#include <policy/policy.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <random.h>
#include <sync.h>
#include <util/time.h>

#include <map>
#include <set>

/** Expiration time for orphan transactions */
static constexpr auto ORPHAN_TX_EXPIRE_TIME{20min};
/** Minimum time between orphan transactions expire time checks */
static constexpr auto ORPHAN_TX_EXPIRE_INTERVAL{5min};

/** One max-standard Dilithium tx of reserved unique weight per peer. */
static constexpr int64_t DEFAULT_RESERVED_ORPHAN_WEIGHT_PER_PEER{MAX_STANDARD_TX_WEIGHT};
/** Entry + inputs/10 budget. Caps EraseForBlock / LimitOrphans work. */
static constexpr unsigned int DEFAULT_MAX_ORPHANAGE_LATENCY_SCORE{3000};
/** Hard unique-weight ceiling so many peers cannot park two full blocks. */
static constexpr int64_t DEFAULT_MAX_ORPHANAGE_WEIGHT{2 * static_cast<int64_t>(MAX_BLOCK_WEIGHT)};

/** Track orphan transactions (TX_MISSING_INPUTS).
 * Multiple peers can announce the same wtxid. Eviction is by unique weight and
 * latency score, not by a 100-tx count.
 */
class TxOrphanage {
public:
    explicit TxOrphanage(int64_t max_global_usage = DEFAULT_MAX_ORPHANAGE_WEIGHT,
                         unsigned int max_latency_score = DEFAULT_MAX_ORPHANAGE_LATENCY_SCORE,
                         int64_t reserved_usage_per_peer = DEFAULT_RESERVED_ORPHAN_WEIGHT_PER_PEER)
        : m_max_global_usage{max_global_usage},
          m_max_latency_score{max_latency_score},
          m_reserved_usage_per_peer{reserved_usage_per_peer} {}

    /** Add a new orphan. Returns false if it already existed (announcer may still be added)
     * or the tx is over MAX_STANDARD_TX_WEIGHT. */
    bool AddTx(const CTransactionRef& tx, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Add another announcer to an existing orphan. */
    bool AddAnnouncer(const uint256& wtxid, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Check if we already have an orphan (txid or wtxid). */
    bool HaveTx(const GenTxid& gtxid) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    bool HaveTxFromPeer(const uint256& wtxid, NodeId peer) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    CTransactionRef GetTx(const uint256& wtxid) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    CTransactionRef GetTxToReconsider(NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Erase by txid or wtxid. Returns 1 if something was removed. */
    int EraseTx(const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    void EraseForPeer(NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    void EraseForBlock(const CBlock& block) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Expire old orphans, then evict until unique weight and latency are within caps. */
    void LimitOrphans(FastRandomContext& rng) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    void AddChildrenToWorkSet(const CTransaction& tx, FastRandomContext& rng) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    bool HaveTxToReconsider(NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    std::vector<CTransactionRef> GetChildrenFromSamePeer(const CTransactionRef& parent, NodeId nodeid) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    size_t Size() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        LOCK(m_mutex);
        return m_orphans.size();
    }

    unsigned int TotalOrphanUsage() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        LOCK(m_mutex);
        return m_total_orphan_usage;
    }

    unsigned int UsageByPeer(NodeId peer) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        LOCK(m_mutex);
        auto peer_it = m_peer_orphanage_info.find(peer);
        return peer_it == m_peer_orphanage_info.end() ? 0 : peer_it->second.m_total_usage;
    }

    void SanityCheck() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    struct OrphanTxBase {
        CTransactionRef tx;
        std::set<NodeId> announcers;
        NodeSeconds nTimeExpire;

        unsigned int GetUsage() const
        {
            return GetTransactionWeight(*tx);
        }
        unsigned int GetLatencyScore() const
        {
            return 1 + static_cast<unsigned int>(tx->vin.size() / 10);
        }
    };

    std::vector<OrphanTxBase> GetOrphanTransactions() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

protected:
    mutable Mutex m_mutex;

    struct OrphanTx : public OrphanTxBase {
        size_t list_pos;
    };

    const int64_t m_max_global_usage;
    const unsigned int m_max_latency_score;
    const int64_t m_reserved_usage_per_peer;

    unsigned int m_total_orphan_usage GUARDED_BY(m_mutex){0};
    unsigned int m_total_latency_score GUARDED_BY(m_mutex){0};
    unsigned int m_total_announcements GUARDED_BY(m_mutex){0};

    /** Primary map is wtxid -> orphan */
    std::map<uint256, OrphanTx> m_orphans GUARDED_BY(m_mutex);
    /** txid -> wtxid so HaveTx/EraseTx still work on txid */
    std::map<uint256, uint256> m_txid_to_wtxid GUARDED_BY(m_mutex);

    struct PeerOrphanInfo {
        std::set<uint256> m_work_set;
        unsigned int m_total_usage{0};
    };
    std::map<NodeId, PeerOrphanInfo> m_peer_orphanage_info GUARDED_BY(m_mutex);

    using OrphanMap = decltype(m_orphans);

    struct IteratorComparator {
        template <typename I>
        bool operator()(const I& a, const I& b) const
        {
            return a->first < b->first;
        }
    };

    std::map<COutPoint, std::set<OrphanMap::iterator, IteratorComparator>> m_outpoint_to_orphan_it GUARDED_BY(m_mutex);
    std::vector<OrphanMap::iterator> m_orphan_list GUARDED_BY(m_mutex);
    NodeSeconds m_next_sweep GUARDED_BY(m_mutex){0s};

    int EraseTxNoLock(const uint256& wtxid) EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
    bool NeedsTrim() const EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
    uint256 ResolveWtxid(const uint256& hash) const EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
};

#endif // BTQ_TXORPHANAGE_H
