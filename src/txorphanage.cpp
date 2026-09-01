// Copyright (c) 2021-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txorphanage.h>

#include <consensus/validation.h>
#include <logging.h>
#include <policy/policy.h>
#include <random.h>
#include <util/check.h>
#include <util/time.h>

#include <algorithm>
#include <cassert>
#include <iterator>

uint256 TxOrphanage::ResolveWtxid(const uint256& hash) const
{
    if (m_orphans.count(hash)) return hash;
    auto it = m_txid_to_wtxid.find(hash);
    if (it != m_txid_to_wtxid.end()) return it->second;
    return {};
}

bool TxOrphanage::AddTx(const CTransactionRef& tx, NodeId peer)
{
    LOCK(m_mutex);

    const uint256& hash = tx->GetHash();
    const uint256& wtxid = tx->GetWitnessHash();
    if (m_orphans.count(wtxid)) {
        // Existing orphan: just record this announcer.
        const bool added = [&] {
            const auto it = m_orphans.find(wtxid);
            const auto ret = it->second.announcers.insert(peer);
            if (!ret.second) return false;
            auto& peer_info = m_peer_orphanage_info.try_emplace(peer).first->second;
            peer_info.m_total_usage += it->second.GetUsage();
            m_total_announcements += 1;
            LogPrint(BCLog::TXPACKAGES, "added peer=%d as announcer of orphan tx %s\n", peer, wtxid.ToString());
            return true;
        }();
        (void)added;
        return false;
    }

    unsigned int sz = GetTransactionWeight(*tx);
    if (sz > MAX_STANDARD_TX_WEIGHT) {
        LogPrint(BCLog::TXPACKAGES, "ignoring large orphan tx (size: %u, txid: %s, wtxid: %s)\n", sz, hash.ToString(), wtxid.ToString());
        return false;
    }

    auto ret = m_orphans.emplace(wtxid, OrphanTx{{tx, {peer}, Now<NodeSeconds>() + ORPHAN_TX_EXPIRE_TIME}, m_orphan_list.size()});
    assert(ret.second);
    m_orphan_list.push_back(ret.first);
    m_txid_to_wtxid.emplace(hash, wtxid);
    for (const CTxIn& txin : tx->vin) {
        m_outpoint_to_orphan_it[txin.prevout].insert(ret.first);
    }
    m_total_orphan_usage += sz;
    m_total_latency_score += ret.first->second.GetLatencyScore();
    m_total_announcements += 1;
    auto& peer_info = m_peer_orphanage_info.try_emplace(peer).first->second;
    peer_info.m_total_usage += sz;

    LogPrint(BCLog::TXPACKAGES, "stored orphan tx %s (wtxid=%s), weight: %u (mapsz %u outsz %u)\n", hash.ToString(), wtxid.ToString(), sz,
             m_orphans.size(), m_outpoint_to_orphan_it.size());
    return true;
}

bool TxOrphanage::AddAnnouncer(const uint256& wtxid, NodeId peer)
{
    LOCK(m_mutex);
    const auto it = m_orphans.find(wtxid);
    if (it == m_orphans.end()) return false;
    Assume(!it->second.announcers.empty());
    const auto ret = it->second.announcers.insert(peer);
    if (!ret.second) return false;
    auto& peer_info = m_peer_orphanage_info.try_emplace(peer).first->second;
    peer_info.m_total_usage += it->second.GetUsage();
    m_total_announcements += 1;
    LogPrint(BCLog::TXPACKAGES, "added peer=%d as announcer of orphan tx %s\n", peer, wtxid.ToString());
    return true;
}

int TxOrphanage::EraseTx(const uint256& hash)
{
    LOCK(m_mutex);
    const uint256 wtxid = ResolveWtxid(hash);
    if (wtxid.IsNull()) return 0;
    return EraseTxNoLock(wtxid);
}

int TxOrphanage::EraseTxNoLock(const uint256& wtxid)
{
    AssertLockHeld(m_mutex);
    auto it = m_orphans.find(wtxid);
    if (it == m_orphans.end()) return 0;
    for (const CTxIn& txin : it->second.tx->vin) {
        auto itPrev = m_outpoint_to_orphan_it.find(txin.prevout);
        if (itPrev == m_outpoint_to_orphan_it.end()) continue;
        itPrev->second.erase(it);
        if (itPrev->second.empty()) m_outpoint_to_orphan_it.erase(itPrev);
    }

    const auto tx_size{it->second.GetUsage()};
    const auto latency{it->second.GetLatencyScore()};
    m_total_orphan_usage -= tx_size;
    m_total_latency_score -= latency;
    m_total_announcements -= it->second.announcers.size();
    for (const auto& peer : it->second.announcers) {
        auto peer_it = m_peer_orphanage_info.find(peer);
        if (Assume(peer_it != m_peer_orphanage_info.end())) {
            peer_it->second.m_total_usage -= tx_size;
        }
    }

    size_t old_pos = it->second.list_pos;
    assert(m_orphan_list[old_pos] == it);
    if (old_pos + 1 != m_orphan_list.size()) {
        auto it_last = m_orphan_list.back();
        m_orphan_list[old_pos] = it_last;
        it_last->second.list_pos = old_pos;
    }
    const auto& txid = it->second.tx->GetHash();
    LogPrint(BCLog::TXPACKAGES, "   removed orphan tx %s (wtxid=%s)\n", txid.ToString(), wtxid.ToString());
    m_orphan_list.pop_back();
    m_txid_to_wtxid.erase(txid);
    m_orphans.erase(it);
    return 1;
}

void TxOrphanage::EraseForPeer(NodeId peer)
{
    LOCK(m_mutex);

    m_peer_orphanage_info.erase(peer);

    int nErased = 0;
    auto iter = m_orphans.begin();
    while (iter != m_orphans.end()) {
        auto& [wtxid, orphan] = *iter++;
        auto orphan_it = orphan.announcers.find(peer);
        if (orphan_it == orphan.announcers.end()) continue;
        orphan.announcers.erase(peer);
        m_total_announcements -= 1;
        if (orphan.announcers.empty()) {
            nErased += EraseTxNoLock(wtxid);
        }
    }
    if (nErased > 0) LogPrint(BCLog::TXPACKAGES, "Erased %d orphan tx from peer=%d\n", nErased, peer);
}

bool TxOrphanage::NeedsTrim() const
{
    AssertLockHeld(m_mutex);
    if (m_orphans.empty()) return false;
    if (m_total_latency_score > m_max_latency_score) return true;

    unsigned int n_peers{0};
    for (const auto& [peer, info] : m_peer_orphanage_info) {
        if (info.m_total_usage > 0) ++n_peers;
    }
    const int64_t reserved = std::max<int64_t>(m_reserved_usage_per_peer, static_cast<int64_t>(n_peers) * m_reserved_usage_per_peer);
    const int64_t max_usage = std::min(m_max_global_usage, reserved);
    return m_total_orphan_usage > max_usage;
}

void TxOrphanage::LimitOrphans(FastRandomContext& rng)
{
    LOCK(m_mutex);

    unsigned int nEvicted = 0;
    auto nNow{Now<NodeSeconds>()};
    if (m_next_sweep <= nNow) {
        int nErased = 0;
        auto nMinExpTime{nNow + ORPHAN_TX_EXPIRE_TIME - ORPHAN_TX_EXPIRE_INTERVAL};
        auto iter = m_orphans.begin();
        while (iter != m_orphans.end()) {
            auto maybeErase = iter++;
            if (maybeErase->second.nTimeExpire <= nNow) {
                nErased += EraseTxNoLock(maybeErase->first);
            } else {
                nMinExpTime = std::min(maybeErase->second.nTimeExpire, nMinExpTime);
            }
        }
        m_next_sweep = nMinExpTime + ORPHAN_TX_EXPIRE_INTERVAL;
        if (nErased > 0) LogPrint(BCLog::TXPACKAGES, "Erased %d orphan tx due to expiration\n", nErased);
    }

    while (NeedsTrim() && !m_orphan_list.empty()) {
        NodeId worst = -1;
        unsigned int worst_usage = 0;
        for (const auto& [peer, info] : m_peer_orphanage_info) {
            if (info.m_total_usage > worst_usage) {
                worst = peer;
                worst_usage = info.m_total_usage;
            }
        }
        if (worst < 0 || worst_usage == 0) {
            // Fall back to a random unique orphan if peer accounting is empty.
            size_t randompos = rng.randrange(m_orphan_list.size());
            EraseTxNoLock(m_orphan_list[randompos]->first);
            ++nEvicted;
            continue;
        }

        // Prefer evicting an orphan this peer uniquely announced.
        const uint256* unique_wtxid = nullptr;
        const uint256* shared_wtxid = nullptr;
        for (const auto& [wtxid, orphan] : m_orphans) {
            if (!orphan.announcers.count(worst)) continue;
            if (orphan.announcers.size() == 1) {
                unique_wtxid = &wtxid;
                break;
            }
            if (!shared_wtxid) shared_wtxid = &wtxid;
        }
        if (unique_wtxid) {
            EraseTxNoLock(*unique_wtxid);
            ++nEvicted;
        } else if (shared_wtxid) {
            auto& orphan = m_orphans[*shared_wtxid];
            orphan.announcers.erase(worst);
            m_total_announcements -= 1;
            auto peer_it = m_peer_orphanage_info.find(worst);
            if (peer_it != m_peer_orphanage_info.end()) {
                peer_it->second.m_total_usage -= orphan.GetUsage();
            }
            ++nEvicted;
        } else {
            break;
        }
    }
    if (nEvicted > 0) LogPrint(BCLog::TXPACKAGES, "orphanage overflow, removed %u announcements\n", nEvicted);
}

void TxOrphanage::AddChildrenToWorkSet(const CTransaction& tx, FastRandomContext& rng)
{
    LOCK(m_mutex);

    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const auto it_by_prev = m_outpoint_to_orphan_it.find(COutPoint(tx.GetHash(), i));
        if (it_by_prev == m_outpoint_to_orphan_it.end()) continue;
        for (const auto& elem : it_by_prev->second) {
            if (!Assume(!elem->second.announcers.empty())) continue;
            auto announcer_iter = std::begin(elem->second.announcers);
            std::advance(announcer_iter, rng.randrange(elem->second.announcers.size()));
            auto announcer = *announcer_iter;
            std::set<uint256>& orphan_work_set = m_peer_orphanage_info.try_emplace(announcer).first->second.m_work_set;
            orphan_work_set.insert(elem->first);
            LogPrint(BCLog::TXPACKAGES, "added %s (wtxid=%s) to peer %d workset\n",
                     tx.GetHash().ToString(), tx.GetWitnessHash().ToString(), announcer);
        }
    }
}

bool TxOrphanage::HaveTx(const GenTxid& gtxid) const
{
    LOCK(m_mutex);
    const uint256& hash = gtxid.GetHash();
    if (gtxid.IsWtxid()) {
        return m_orphans.count(hash);
    }
    return m_txid_to_wtxid.count(hash) || m_orphans.count(hash);
}

CTransactionRef TxOrphanage::GetTx(const uint256& wtxid) const
{
    LOCK(m_mutex);
    auto it = m_orphans.find(wtxid);
    return it != m_orphans.end() ? it->second.tx : nullptr;
}

bool TxOrphanage::HaveTxFromPeer(const uint256& wtxid, NodeId peer) const
{
    LOCK(m_mutex);
    auto it = m_orphans.find(wtxid);
    return it != m_orphans.end() && it->second.announcers.count(peer);
}

CTransactionRef TxOrphanage::GetTxToReconsider(NodeId peer)
{
    LOCK(m_mutex);

    auto peer_it = m_peer_orphanage_info.find(peer);
    if (peer_it == m_peer_orphanage_info.end()) return nullptr;

    auto& work_set = peer_it->second.m_work_set;
    while (!work_set.empty()) {
        uint256 wtxid = *work_set.begin();
        work_set.erase(work_set.begin());
        const auto orphan_it = m_orphans.find(wtxid);
        if (orphan_it != m_orphans.end()) {
            return orphan_it->second.tx;
        }
    }
    return nullptr;
}

bool TxOrphanage::HaveTxToReconsider(NodeId peer)
{
    LOCK(m_mutex);

    auto work_set_it = m_peer_orphanage_info.find(peer);
    if (work_set_it == m_peer_orphanage_info.end()) return false;
    return !work_set_it->second.m_work_set.empty();
}

void TxOrphanage::EraseForBlock(const CBlock& block)
{
    LOCK(m_mutex);

    std::vector<uint256> vOrphanErase;
    for (const CTransactionRef& ptx : block.vtx) {
        const CTransaction& tx = *ptx;
        for (const auto& txin : tx.vin) {
            auto itByPrev = m_outpoint_to_orphan_it.find(txin.prevout);
            if (itByPrev == m_outpoint_to_orphan_it.end()) continue;
            for (auto mi = itByPrev->second.begin(); mi != itByPrev->second.end(); ++mi) {
                vOrphanErase.push_back((*mi)->first);
            }
        }
    }

    if (!vOrphanErase.empty()) {
        int nErased = 0;
        for (const uint256& orphanHash : vOrphanErase) {
            nErased += EraseTxNoLock(orphanHash);
        }
        LogPrint(BCLog::TXPACKAGES, "Erased %d orphan tx included or conflicted by block\n", nErased);
    }
}

std::vector<CTransactionRef> TxOrphanage::GetChildrenFromSamePeer(const CTransactionRef& parent, NodeId nodeid) const
{
    LOCK(m_mutex);

    std::vector<OrphanMap::iterator> iters;
    for (unsigned int i = 0; i < parent->vout.size(); i++) {
        const auto it_by_prev = m_outpoint_to_orphan_it.find(COutPoint(parent->GetHash(), i));
        if (it_by_prev == m_outpoint_to_orphan_it.end()) continue;
        for (const auto& elem : it_by_prev->second) {
            if (elem->second.announcers.count(nodeid)) {
                iters.emplace_back(elem);
            }
        }
    }

    std::sort(iters.begin(), iters.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs->second.nTimeExpire == rhs->second.nTimeExpire) {
            return &(*lhs) < &(*rhs);
        }
        return lhs->second.nTimeExpire > rhs->second.nTimeExpire;
    });
    iters.erase(std::unique(iters.begin(), iters.end()), iters.end());

    std::vector<CTransactionRef> children_found;
    children_found.reserve(iters.size());
    for (const auto& child_iter : iters) {
        children_found.emplace_back(child_iter->second.tx);
    }
    return children_found;
}

std::vector<TxOrphanage::OrphanTxBase> TxOrphanage::GetOrphanTransactions() const
{
    LOCK(m_mutex);
    std::vector<OrphanTxBase> ret;
    ret.reserve(m_orphans.size());
    for (const auto& o : m_orphans) {
        ret.push_back({o.second.tx, o.second.announcers, o.second.nTimeExpire});
    }
    return ret;
}

void TxOrphanage::SanityCheck() const
{
    LOCK(m_mutex);
    unsigned int counted_total_announcements{0};
    unsigned int counted_total_usage{0};
    unsigned int counted_latency{0};
    std::map<NodeId, unsigned int> counted_size_per_peer;

    for (const auto& [wtxid, orphan] : m_orphans) {
        counted_total_announcements += orphan.announcers.size();
        counted_total_usage += orphan.GetUsage();
        counted_latency += orphan.GetLatencyScore();
        Assume(!orphan.announcers.empty());
        Assume(m_txid_to_wtxid.count(orphan.tx->GetHash()));
        for (const auto& peer : orphan.announcers) {
            counted_size_per_peer.try_emplace(peer).first->second += orphan.GetUsage();
        }
    }

    Assume(m_total_announcements >= m_orphans.size());
    Assume(counted_total_announcements == m_total_announcements);
    Assume(counted_total_usage == m_total_orphan_usage);
    Assume(counted_latency == m_total_latency_score);
    Assume(m_txid_to_wtxid.size() == m_orphans.size());
    Assume(counted_size_per_peer.size() <= m_peer_orphanage_info.size());

    for (const auto& [peerid, info] : m_peer_orphanage_info) {
        auto it_counted = counted_size_per_peer.find(peerid);
        if (it_counted == counted_size_per_peer.end()) {
            Assume(info.m_total_usage == 0);
        } else {
            Assume(it_counted->second == info.m_total_usage);
        }
    }
}
