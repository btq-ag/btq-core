// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_QT_P2MRVAULTDIALOG_H
#define BTQ_QT_P2MRVAULTDIALOG_H

#include <qt/p2mrcontroller.h>

#include <vector>

#include <QDialog>

class PlatformStyle;
class WalletModel;

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

/**
 * Top-level dialog for managing BIP360 P2MR vaults.
 *
 * Displays the list of wallet-tracked P2MR destinations with their balances
 * and provides buttons to create new vaults, fund vaults, spend from a
 * vault, and view the full tree of a selected vault.
 *
 * Modeled loosely on AddressBookPage but using QTableWidget (no Qt Designer
 * .ui form) to keep the GUI integration self-contained.
 */
class P2MRVaultDialog : public QDialog
{
    Q_OBJECT

public:
    explicit P2MRVaultDialog(const PlatformStyle* platform_style, QWidget* parent = nullptr);
    ~P2MRVaultDialog() override = default;

    void setModel(WalletModel* model);

public Q_SLOTS:
    void refresh();

private Q_SLOTS:
    void onNewVault();
    void onFundVault();
    void onSpend();
    void onDetails();
    void onCopyAddress();
    void onSelectionChanged();

private:
    const PlatformStyle* m_platform_style;
    WalletModel* m_wallet_model{nullptr};
    P2MRController* m_controller{nullptr};

    QTableWidget* m_table{nullptr};
    QLabel* m_total_balance_label{nullptr};
    QPushButton* m_new_button{nullptr};
    QPushButton* m_fund_button{nullptr};
    QPushButton* m_spend_button{nullptr};
    QPushButton* m_details_button{nullptr};
    QPushButton* m_copy_button{nullptr};
    QPushButton* m_refresh_button{nullptr};

    QString selectedVaultId() const;
    QString selectedAddress() const;

    void buildLayout();
    void updateButtonState();
};

#endif // BTQ_QT_P2MRVAULTDIALOG_H
