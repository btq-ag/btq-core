// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/p2mrvaultdialog.h>

#include <qt/btqunits.h>
#include <qt/optionsmodel.h>
#include <qt/p2mrwizards.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
constexpr int COL_LABEL = 0;
constexpr int COL_ADDRESS = 1;
constexpr int COL_BALANCE = 2;
constexpr int COL_LEAVES = 3;
constexpr int COL_STATE = 4;
constexpr int COL_CREATED = 5;
constexpr int COL_ID = 6;
constexpr int COL_COUNT = 7;
} // namespace

P2MRVaultDialog::P2MRVaultDialog(const PlatformStyle* platform_style, QWidget* parent)
    : QDialog(parent), m_platform_style(platform_style)
{
    setWindowTitle(tr("P2MR Vaults"));
    resize(900, 480);
    buildLayout();
}

void P2MRVaultDialog::buildLayout()
{
    auto* main_layout = new QVBoxLayout(this);

    auto* header_label = new QLabel(
        tr("BIP360 Pay-to-Merkle-Root vaults tracked by this wallet. "
           "Vaults are stored locally; loss of wallet metadata makes funds "
           "unspendable. Always back up your wallet."), this);
    header_label->setWordWrap(true);
    main_layout->addWidget(header_label);

    m_table = new QTableWidget(0, COL_COUNT, this);
    QStringList headers{tr("Label"), tr("Address"), tr("Balance"), tr("Leaves"),
                        tr("State"), tr("Created"), tr("Id")};
    m_table->setHorizontalHeaderLabels(headers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ADDRESS, QHeaderView::Stretch);
    m_table->horizontalHeader()->setStretchLastSection(false);
    main_layout->addWidget(m_table, /*stretch=*/1);

    auto* totals_row = new QHBoxLayout();
    m_total_balance_label = new QLabel(this);
    totals_row->addWidget(m_total_balance_label, /*stretch=*/1);
    main_layout->addLayout(totals_row);

    auto* buttons_row = new QHBoxLayout();
    m_new_button = new QPushButton(tr("&New vault..."), this);
    m_fund_button = new QPushButton(tr("&Fund..."), this);
    m_spend_button = new QPushButton(tr("&Spend..."), this);
    m_details_button = new QPushButton(tr("&Details..."), this);
    m_copy_button = new QPushButton(tr("Copy &address"), this);
    m_refresh_button = new QPushButton(tr("&Refresh"), this);

    buttons_row->addWidget(m_new_button);
    buttons_row->addWidget(m_fund_button);
    buttons_row->addWidget(m_spend_button);
    buttons_row->addWidget(m_details_button);
    buttons_row->addWidget(m_copy_button);
    buttons_row->addStretch();
    buttons_row->addWidget(m_refresh_button);
    main_layout->addLayout(buttons_row);

    auto* button_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    main_layout->addWidget(button_box);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);

    connect(m_new_button, &QPushButton::clicked, this, &P2MRVaultDialog::onNewVault);
    connect(m_fund_button, &QPushButton::clicked, this, &P2MRVaultDialog::onFundVault);
    connect(m_spend_button, &QPushButton::clicked, this, &P2MRVaultDialog::onSpend);
    connect(m_details_button, &QPushButton::clicked, this, &P2MRVaultDialog::onDetails);
    connect(m_copy_button, &QPushButton::clicked, this, &P2MRVaultDialog::onCopyAddress);
    connect(m_refresh_button, &QPushButton::clicked, this, &P2MRVaultDialog::refresh);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &P2MRVaultDialog::onSelectionChanged);

    updateButtonState();
}

void P2MRVaultDialog::setModel(WalletModel* model)
{
    m_wallet_model = model;
    m_controller = model ? model->getP2MRController() : nullptr;
    if (m_controller) {
        connect(m_controller, &P2MRController::vaultsChanged,
                this, &P2MRVaultDialog::refresh);
    }
    refresh();
}

void P2MRVaultDialog::refresh()
{
    m_table->setRowCount(0);
    if (!m_controller) {
        m_total_balance_label->setText(tr("No wallet loaded."));
        updateButtonState();
        return;
    }

    auto rows = m_controller->listVaults(/*min_depth=*/1);
    CAmount total{0};
    for (const auto& r : rows) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* label_item = new QTableWidgetItem(r.label);
        auto* address_item = new QTableWidgetItem(r.address);
        QString balance_str = m_wallet_model && m_wallet_model->getOptionsModel()
            ? BTQUnits::formatWithUnit(m_wallet_model->getOptionsModel()->getDisplayUnit(), r.balance)
            : QString::number(r.balance);
        auto* balance_item = new QTableWidgetItem(balance_str);
        auto* leaves_item = new QTableWidgetItem(QString::number(qulonglong(r.leaf_count)));
        auto* state_item = new QTableWidgetItem(r.state);
        QString created_str = r.created_at > 0
            ? QDateTime::fromSecsSinceEpoch(r.created_at).toString(Qt::ISODate)
            : QString();
        auto* created_item = new QTableWidgetItem(created_str);
        auto* id_item = new QTableWidgetItem(r.id);

        m_table->setItem(row, COL_LABEL, label_item);
        m_table->setItem(row, COL_ADDRESS, address_item);
        m_table->setItem(row, COL_BALANCE, balance_item);
        m_table->setItem(row, COL_LEAVES, leaves_item);
        m_table->setItem(row, COL_STATE, state_item);
        m_table->setItem(row, COL_CREATED, created_item);
        m_table->setItem(row, COL_ID, id_item);

        total += r.balance;
    }

    QString total_str = m_wallet_model && m_wallet_model->getOptionsModel()
        ? BTQUnits::formatWithUnit(m_wallet_model->getOptionsModel()->getDisplayUnit(), total)
        : QString::number(total);
    m_total_balance_label->setText(tr("Total P2MR balance: %1").arg(total_str));

    updateButtonState();
}

void P2MRVaultDialog::onSelectionChanged()
{
    updateButtonState();
}

void P2MRVaultDialog::updateButtonState()
{
    const bool has_wallet = m_controller != nullptr;
    const bool has_selection = m_table->currentRow() >= 0;
    m_new_button->setEnabled(has_wallet);
    m_fund_button->setEnabled(has_wallet && has_selection);
    m_spend_button->setEnabled(has_wallet && has_selection);
    m_details_button->setEnabled(has_wallet && has_selection);
    m_copy_button->setEnabled(has_selection);
    m_refresh_button->setEnabled(has_wallet);
}

QString P2MRVaultDialog::selectedVaultId() const
{
    const int row = m_table->currentRow();
    if (row < 0) return {};
    auto* item = m_table->item(row, COL_ID);
    return item ? item->text() : QString();
}

QString P2MRVaultDialog::selectedAddress() const
{
    const int row = m_table->currentRow();
    if (row < 0) return {};
    auto* item = m_table->item(row, COL_ADDRESS);
    return item ? item->text() : QString();
}

void P2MRVaultDialog::onNewVault()
{
    if (!m_controller) return;
    P2MRNewVaultDialog dlg(m_controller, m_platform_style, this);
    dlg.setOfferFunding(true);
    dlg.exec();
}

void P2MRVaultDialog::onFundVault()
{
    if (!m_controller) return;
    const QString id = selectedVaultId();
    if (id.isEmpty()) return;
    auto entry = m_controller->getVault(id);
    if (!entry) {
        QMessageBox::warning(this, windowTitle(), tr("Vault not found."));
        return;
    }
    QMessageBox::information(this, windowTitle(),
        tr("To fund this vault, use the Send tab and paste the address:\n%1")
            .arg(QString::fromStdString(entry->address)));
    QApplication::clipboard()->setText(QString::fromStdString(entry->address));
}

void P2MRVaultDialog::onSpend()
{
    if (!m_controller) return;
    const QString id = selectedVaultId();
    if (id.isEmpty()) return;
    P2MRSpendDialog dlg(m_controller, m_wallet_model, id, m_platform_style, this);
    dlg.exec();
    refresh();
}

void P2MRVaultDialog::onDetails()
{
    if (!m_controller) return;
    const QString id = selectedVaultId();
    if (id.isEmpty()) return;
    auto entry = m_controller->getVault(id);
    if (!entry) {
        QMessageBox::warning(this, windowTitle(), tr("Vault not found."));
        return;
    }
    P2MRDetailsDialog dlg(*entry, this);
    dlg.exec();
}

void P2MRVaultDialog::onCopyAddress()
{
    QString addr = selectedAddress();
    if (addr.isEmpty()) return;
    QApplication::clipboard()->setText(addr);
}
