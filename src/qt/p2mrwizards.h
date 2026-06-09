// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_QT_P2MRWIZARDS_H
#define BTQ_QT_P2MRWIZARDS_H

#include <consensus/amount.h>
#include <interfaces/wallet.h>
#include <qt/p2mrcontroller.h>

#include <vector>

#include <QDialog>
#include <QString>

class BTQAmountField;
class PlatformStyle;
class WalletModel;

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QCheckBox;
QT_END_NAMESPACE

/**
 * Dialog to create a new P2MR vault. Production chains require a custom JSON
 * tree; regtest also exposes an OP_TRUE template for feature testing.
 *
 * On success the controller has already persisted the new entry; the dialog
 * exposes the resulting address through createdAddress() / createdId().
 */
class P2MRNewVaultDialog : public QDialog
{
    Q_OBJECT

public:
    P2MRNewVaultDialog(P2MRController* controller,
                       const PlatformStyle* platform_style,
                       QWidget* parent = nullptr);

    /** Whether to also fund the new vault from the wallet's main balance. */
    void setOfferFunding(bool offer);
    void setInitialLabel(const QString& label);

    QString createdAddress() const { return m_created_address; }
    QString createdId() const { return m_created_id; }

public Q_SLOTS:
    void accept() override;

private Q_SLOTS:
    void onTemplateChanged(int index);

private:
    P2MRController* m_controller;
    const PlatformStyle* m_platform_style;
    QComboBox* m_template_combo{nullptr};
    QLineEdit* m_label_edit{nullptr};
    QPlainTextEdit* m_custom_tree_edit{nullptr};
    QLabel* m_warning_label{nullptr};
    QCheckBox* m_fund_checkbox{nullptr};
    BTQAmountField* m_amount_field{nullptr};
    QString m_created_address;
    QString m_created_id;

    std::vector<interfaces::WalletP2MRTreeLeaf> currentLeaves(QString& error) const;
    void buildLayout();
};

/**
 * Wizard-style spend dialog: builds, signs, and dry-runs the spend, asks the
 * user to confirm, then broadcasts.
 */
class P2MRSpendDialog : public QDialog
{
    Q_OBJECT

public:
    P2MRSpendDialog(P2MRController* controller,
                    WalletModel* wallet_model,
                    QString vault_id,
                    const PlatformStyle* platform_style,
                    QWidget* parent = nullptr);

    QString broadcastTxId() const { return m_broadcast_txid; }

public Q_SLOTS:
    void accept() override;

private Q_SLOTS:
    void onPrepare();
    void onBroadcast();

private:
    P2MRController* m_controller;
    WalletModel* m_wallet_model;
    QString m_vault_id;

    QLineEdit* m_dest_edit{nullptr};
    BTQAmountField* m_amount_field{nullptr};
    BTQAmountField* m_fee_field{nullptr};
    QLabel* m_preview_label{nullptr};
    QPushButton* m_prepare_button{nullptr};
    QPushButton* m_broadcast_button{nullptr};

    std::optional<P2MRController::PreparedSpend> m_prepared;
    QString m_broadcast_txid;

    void buildLayout(const PlatformStyle* platform_style);
};

/** Read-only dialog showing the full tree, scriptPubKey, and merkle root. */
class P2MRDetailsDialog : public QDialog
{
    Q_OBJECT

public:
    P2MRDetailsDialog(const interfaces::WalletP2MREntry& entry,
                      QWidget* parent = nullptr);
};

#endif // BTQ_QT_P2MRWIZARDS_H
