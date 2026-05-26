// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/p2mrcontroller.h>

#include <core_io.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <qt/walletmodel.h>
#include <util/strencodings.h>
#include <util/translation.h>

#include <QString>

namespace {
QString FromBilingual(const bilingual_str& s)
{
    return QString::fromStdString(s.original);
}
} // namespace

P2MRController::P2MRController(WalletModel* wallet_model, QObject* parent)
    : QObject(parent), m_wallet_model(wallet_model)
{
}

std::vector<interfaces::WalletP2MRTreeLeaf> P2MRController::BuildTreeForTemplate(
    TreeTemplate tpl,
    const std::vector<interfaces::WalletP2MRTreeLeaf>& custom)
{
    switch (tpl) {
    case TreeTemplate::OpTrue: {
        interfaces::WalletP2MRTreeLeaf leaf;
        leaf.depth = 0;
        leaf.leaf_version = 0xc0; // TAPROOT_LEAF_TAPSCRIPT
        leaf.script = {0x51};      // OP_TRUE
        return {leaf};
    }
    case TreeTemplate::Custom:
    default:
        return custom;
    }
}

std::vector<P2MRController::VaultRow> P2MRController::listVaults(int min_depth)
{
    std::vector<VaultRow> rows;
    if (!m_wallet_model) return rows;

    auto& wallet = m_wallet_model->wallet();
    auto entries = wallet.listP2MR();
    rows.reserve(entries.size());
    for (const auto& e : entries) {
        VaultRow row;
        row.id = QString::fromStdString(e.id);
        row.address = QString::fromStdString(e.address);
        row.label = QString::fromStdString(e.label);
        row.state = QString::fromStdString(e.state);
        row.merkle_root_hex = QString::fromStdString(HexStr(e.merkle_root));
        row.created_at = e.created_at;
        row.leaf_count = e.tree.size();
        row.balance = wallet.getP2MREntryBalance(e.id, min_depth);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::optional<interfaces::WalletP2MREntry> P2MRController::getVault(const QString& id)
{
    if (!m_wallet_model) return std::nullopt;
    return m_wallet_model->wallet().getP2MR(id.toStdString());
}

CAmount P2MRController::totalBalance(int min_depth)
{
    if (!m_wallet_model) return 0;
    return m_wallet_model->wallet().getP2MRBalance(min_depth);
}

bool P2MRController::createVault(const std::vector<interfaces::WalletP2MRTreeLeaf>& leaves,
                                 const QString& label,
                                 interfaces::WalletP2MRCreated& out,
                                 QString& error)
{
    if (!m_wallet_model) {
        error = tr("Wallet model unavailable");
        return false;
    }
    auto res = m_wallet_model->wallet().createP2MR(leaves, label.toStdString());
    if (!res) {
        error = FromBilingual(util::ErrorString(res));
        return false;
    }
    out = *res;
    Q_EMIT vaultsChanged();
    return true;
}

bool P2MRController::createAndFundVault(const std::vector<interfaces::WalletP2MRTreeLeaf>& leaves,
                                        CAmount amount,
                                        const QString& label,
                                        bool subtract_fee,
                                        interfaces::WalletP2MRFunded& out,
                                        QString& error)
{
    if (!m_wallet_model) {
        error = tr("Wallet model unavailable");
        return false;
    }
    auto res = m_wallet_model->wallet().fundP2MR(leaves, amount, label.toStdString(), subtract_fee);
    if (!res) {
        error = FromBilingual(util::ErrorString(res));
        return false;
    }
    out = *res;
    Q_EMIT vaultsChanged();
    return true;
}

bool P2MRController::prepareSpend(const QString& vault_id,
                                  const QString& destination_address,
                                  CAmount amount,
                                  CAmount fee,
                                  PreparedSpend& out,
                                  QString& error)
{
    if (!m_wallet_model) {
        error = tr("Wallet model unavailable");
        return false;
    }
    if (vault_id.isEmpty()) {
        error = tr("No P2MR vault selected");
        return false;
    }

    CTxDestination dest = DecodeDestination(destination_address.toStdString());
    if (!IsValidDestination(dest)) {
        error = tr("Invalid destination address");
        return false;
    }
    if (amount <= 0) {
        error = tr("Amount must be positive");
        return false;
    }
    if (fee < 0) {
        error = tr("Fee must be non-negative");
        return false;
    }

    auto res = m_wallet_model->wallet().prepareP2MRSpend(vault_id.toStdString(), dest, amount, fee);
    if (!res) {
        error = FromBilingual(util::ErrorString(res));
        return false;
    }
    if (!res->sign_complete) {
        error = tr("Failed to sign all P2MR inputs. The stored tree may not match this UTXO.");
        return false;
    }
    if (!res->mempool_allowed) {
        error = tr("Mempool rejected the transaction: %1")
                    .arg(QString::fromStdString(res->reject_reason));
        return false;
    }

    out.spend = *res;
    out.signed_hex = QString::fromStdString(EncodeHexTx(*res->tx));
    out.txid = QString::fromStdString(res->tx->GetHash().GetHex());
    return true;
}

bool P2MRController::broadcastPreparedSpend(const PreparedSpend& spend,
                                            QString& txid_hex_out,
                                            QString& error)
{
    if (!m_wallet_model) {
        error = tr("Wallet model unavailable");
        return false;
    }
    if (!spend.spend.tx) {
        error = tr("No prepared transaction to broadcast");
        return false;
    }
    auto res = m_wallet_model->wallet().broadcastP2MRSpend(spend.spend.tx);
    if (!res) {
        error = FromBilingual(util::ErrorString(res));
        return false;
    }
    txid_hex_out = QString::fromStdString(res->GetHex());
    Q_EMIT vaultsChanged();
    return true;
}
