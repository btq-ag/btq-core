// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_QT_P2MRCONTROLLER_H
#define BTQ_QT_P2MRCONTROLLER_H

#include <consensus/amount.h>
#include <interfaces/wallet.h>

#include <optional>
#include <vector>

#include <QObject>
#include <QString>

class WalletModel;

/**
 * Qt-side adapter around the interfaces::Wallet P2MR methods.
 *
 * Translates wallet results into QString errors, emits Qt signals when the
 * vault set or balance changes, and provides synchronous helpers suitable for
 * use directly from dialog code on the GUI thread.
 *
 * The controller does not own the wallet; it borrows a reference to
 * WalletModel and must not outlive it.
 */
class P2MRController : public QObject
{
    Q_OBJECT

public:
    /** Convenience template ids displayed in the GUI. */
    enum class TreeTemplate {
        OpTrue,   //!< Single OP_TRUE leaf (testing only).
        Custom,   //!< User-provided leaves.
    };

    /** GUI-level snapshot of a P2MR vault including its on-chain balance. */
    struct VaultRow {
        QString id;
        QString address;
        QString label;
        QString state;
        QString merkle_root_hex;
        CAmount balance{0};
        int64_t created_at{0};
        size_t leaf_count{0};
    };

    /** Outcome of a fully prepared spend (built + signed + dry-run). */
    struct PreparedSpend {
        interfaces::WalletP2MRSpend spend;
        QString signed_hex;
        QString txid;
    };

    explicit P2MRController(WalletModel* wallet_model, QObject* parent = nullptr);

    /** Build the leaf set for a named template. Custom requires `custom`. */
    static std::vector<interfaces::WalletP2MRTreeLeaf> BuildTreeForTemplate(
        TreeTemplate tpl,
        const std::vector<interfaces::WalletP2MRTreeLeaf>& custom = {});

    /** List vaults with current confirmed balance per entry. */
    std::vector<VaultRow> listVaults(int min_depth = 1);

    /** Fetch a single vault entry by id (no balance). */
    std::optional<interfaces::WalletP2MREntry> getVault(const QString& id);

    /** Total confirmed unspent balance across all tracked P2MR vaults. */
    CAmount totalBalance(int min_depth = 1);

    /** Create a new P2MR address. Returns descriptive error on failure. */
    bool createVault(const std::vector<interfaces::WalletP2MRTreeLeaf>& leaves,
                     const QString& label,
                     interfaces::WalletP2MRCreated& out,
                     QString& error);

    /** Create + fund a new P2MR address. */
    bool createAndFundVault(const std::vector<interfaces::WalletP2MRTreeLeaf>& leaves,
                            CAmount amount,
                            const QString& label,
                            bool subtract_fee,
                            interfaces::WalletP2MRFunded& out,
                            QString& error);

    /** Build + sign + dry-run a spend, but DO NOT broadcast. */
    bool prepareSpend(const QString& vault_id,
                      const QString& destination_address,
                      CAmount amount,
                      CAmount fee,
                      PreparedSpend& out,
                      QString& error);

    /** Broadcast a previously prepared spend transaction. */
    bool broadcastPreparedSpend(const PreparedSpend& spend,
                                QString& txid_hex_out,
                                QString& error);

Q_SIGNALS:
    /** Emitted when the controller has changed wallet state likely to require
     *  the GUI to refresh its vault view (creation, funding, or successful
     *  broadcast). */
    void vaultsChanged();

private:
    WalletModel* m_wallet_model;
};

#endif // BTQ_QT_P2MRCONTROLLER_H
