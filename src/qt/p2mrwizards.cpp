// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/p2mrwizards.h>

#include <chainparams.h>
#include <qt/btqamountfield.h>
#include <qt/btqunits.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
#include <script/interpreter.h>
#include <util/strencodings.h>

#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr CAmount DEFAULT_P2MR_SPEND_FEE{1000}; // 0.00001 BTQ matches CLI default

QString HexFromBytes(const std::vector<unsigned char>& bytes)
{
    return QString::fromStdString(HexStr(bytes));
}

QJsonArray LeavesToJson(const std::vector<interfaces::WalletP2MRTreeLeaf>& leaves)
{
    QJsonArray arr;
    for (const auto& l : leaves) {
        QJsonObject obj;
        obj["depth"] = int(l.depth);
        obj["leaf_version"] = int(l.leaf_version);
        obj["script"] = HexFromBytes(l.script);
        arr.append(obj);
    }
    return arr;
}

bool ParseLeavesFromJson(const QString& text,
                         std::vector<interfaces::WalletP2MRTreeLeaf>& out,
                         QString& error)
{
    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        error = QObject::tr("JSON parse error: %1").arg(parse_error.errorString());
        return false;
    }
    if (!doc.isArray()) {
        error = QObject::tr("Tree must be a JSON array");
        return false;
    }
    QJsonArray arr = doc.array();
    if (arr.isEmpty()) {
        error = QObject::tr("Tree must contain at least one leaf");
        return false;
    }
    std::vector<interfaces::WalletP2MRTreeLeaf> tmp;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonValue v = arr.at(i);
        if (!v.isObject()) {
            error = QObject::tr("Leaf %1 is not an object").arg(i);
            return false;
        }
        QJsonObject o = v.toObject();
        if (!o.contains("depth") || !o.contains("leaf_version") || !o.contains("script")) {
            error = QObject::tr("Leaf %1 must contain depth, leaf_version, and script").arg(i);
            return false;
        }
        auto read_integer = [&](const char* key, int min, int max, int& out_value) {
            const QJsonValue value = o.value(QString::fromUtf8(key));
            if (!value.isDouble()) {
                error = QObject::tr("Leaf %1 %2 must be an integer").arg(i).arg(QString::fromUtf8(key));
                return false;
            }
            const double number = value.toDouble();
            if (!std::isfinite(number) || std::floor(number) != number) {
                error = QObject::tr("Leaf %1 %2 must be an integer").arg(i).arg(QString::fromUtf8(key));
                return false;
            }
            if (number < min || number > max) {
                error = QObject::tr("Leaf %1 %2 out of range").arg(i).arg(QString::fromUtf8(key));
                return false;
            }
            out_value = static_cast<int>(number);
            return true;
        };

        int depth;
        int leaf_version;
        if (!read_integer("depth", 0, 128, depth)) return false;
        if (!read_integer("leaf_version", 0, 255, leaf_version)) return false;
        if (depth < 0 || depth > 128) {
            error = QObject::tr("Leaf %1 depth out of range").arg(i);
            return false;
        }
        if (leaf_version < 0 || leaf_version > 255) {
            error = QObject::tr("Leaf %1 leaf_version out of range").arg(i);
            return false;
        }
        if ((leaf_version & ~TAPROOT_LEAF_MASK) != 0) {
            error = QObject::tr("Leaf %1 leaf_version parity bit must be unset").arg(i);
            return false;
        }
        if (!o["script"].isString()) {
            error = QObject::tr("Leaf %1 script must be a hex string").arg(i);
            return false;
        }
        const QString script_hex = o["script"].toString();
        auto bytes = TryParseHex<unsigned char>(script_hex.toStdString());
        if (!bytes) {
            error = QObject::tr("Leaf %1 script is not valid hex").arg(i);
            return false;
        }
        interfaces::WalletP2MRTreeLeaf leaf;
        leaf.depth = static_cast<uint8_t>(depth);
        leaf.leaf_version = static_cast<uint8_t>(leaf_version);
        leaf.script = std::move(*bytes);
        tmp.push_back(std::move(leaf));
    }
    out = std::move(tmp);
    return true;
}

QString DefaultOpTrueJson()
{
    QJsonArray arr;
    QJsonObject obj;
    obj["depth"] = 0;
    obj["leaf_version"] = 192; // 0xc0
    obj["script"] = "51";      // OP_TRUE
    arr.append(obj);
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

bool AllowOpTrueTemplate()
{
    return Params().MineBlocksOnDemand();
}

QString FormatAmount(const WalletModel* model, CAmount amount)
{
    if (!model || !model->getOptionsModel()) {
        return QString::number(amount);
    }
    return BTQUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), amount);
}

} // namespace

// ---------------------------------------------------------------------------
// P2MRNewVaultDialog
// ---------------------------------------------------------------------------

P2MRNewVaultDialog::P2MRNewVaultDialog(P2MRController* controller,
                                       const PlatformStyle* platform_style,
                                       QWidget* parent)
    : QDialog(parent),
      m_controller(controller),
      m_platform_style(platform_style)
{
    setWindowTitle(tr("New P2MR Vault"));
    buildLayout();
}

void P2MRNewVaultDialog::buildLayout()
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_template_combo = new QComboBox(this);
    m_template_combo->addItem(tr("Custom JSON tree"), int(P2MRController::TreeTemplate::Custom));
    if (AllowOpTrueTemplate()) {
        m_template_combo->addItem(tr("OP_TRUE leaf (regtest only)"), int(P2MRController::TreeTemplate::OpTrue));
    }
    form->addRow(tr("Template:"), m_template_combo);

    m_label_edit = new QLineEdit(this);
    m_label_edit->setPlaceholderText(tr("Optional label"));
    form->addRow(tr("Label:"), m_label_edit);

    m_warning_label = new QLabel(tr(
        "The OP_TRUE template produces a vault that anyone who knows the "
        "scriptPubKey can spend. Use it only for regtest or feature testing."), this);
    m_warning_label->setWordWrap(true);
    m_warning_label->setStyleSheet("color: #b85c00; font-weight: bold;");
    layout->addLayout(form);
    layout->addWidget(m_warning_label);

    m_custom_tree_edit = new QPlainTextEdit(this);
    m_custom_tree_edit->setPlaceholderText(tr("Paste a JSON array of leaves in DFS order"));
    m_custom_tree_edit->setVisible(true);
    layout->addWidget(m_custom_tree_edit);

    m_fund_checkbox = new QCheckBox(tr("Fund now from main balance"), this);
    layout->addWidget(m_fund_checkbox);
    m_amount_field = new BTQAmountField(this);
    m_amount_field->setEnabled(false);
    auto* amount_row = new QHBoxLayout();
    amount_row->addWidget(new QLabel(tr("Amount:"), this));
    amount_row->addWidget(m_amount_field, /*stretch=*/1);
    layout->addLayout(amount_row);

    connect(m_fund_checkbox, &QCheckBox::toggled, m_amount_field, &QWidget::setEnabled);

    auto* button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(button_box);
    connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_template_combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &P2MRNewVaultDialog::onTemplateChanged);
    onTemplateChanged(m_template_combo->currentIndex());
}

void P2MRNewVaultDialog::setOfferFunding(bool offer)
{
    m_fund_checkbox->setVisible(offer);
    m_amount_field->setVisible(offer);
}

void P2MRNewVaultDialog::setInitialLabel(const QString& label)
{
    m_label_edit->setText(label);
}

void P2MRNewVaultDialog::onTemplateChanged(int index)
{
    const auto tpl = static_cast<P2MRController::TreeTemplate>(m_template_combo->itemData(index).toInt());
    m_custom_tree_edit->setVisible(tpl == P2MRController::TreeTemplate::Custom);
    m_warning_label->setVisible(tpl == P2MRController::TreeTemplate::OpTrue);
    if (tpl == P2MRController::TreeTemplate::OpTrue) {
        m_custom_tree_edit->setPlainText(DefaultOpTrueJson());
        m_fund_checkbox->setChecked(false);
        m_fund_checkbox->setEnabled(false);
        m_amount_field->setEnabled(false);
    } else {
        if (m_custom_tree_edit->toPlainText() == DefaultOpTrueJson()) {
            m_custom_tree_edit->clear();
        }
        m_fund_checkbox->setEnabled(true);
        m_amount_field->setEnabled(m_fund_checkbox->isChecked());
    }
    adjustSize();
}

std::vector<interfaces::WalletP2MRTreeLeaf> P2MRNewVaultDialog::currentLeaves(QString& error) const
{
    const auto tpl = static_cast<P2MRController::TreeTemplate>(
        m_template_combo->currentData().toInt());
    if (tpl == P2MRController::TreeTemplate::OpTrue) {
        return P2MRController::BuildTreeForTemplate(P2MRController::TreeTemplate::OpTrue);
    }
    std::vector<interfaces::WalletP2MRTreeLeaf> custom;
    if (!ParseLeavesFromJson(m_custom_tree_edit->toPlainText(), custom, error)) {
        return {};
    }
    return custom;
}

void P2MRNewVaultDialog::accept()
{
    if (!m_controller) {
        QMessageBox::warning(this, windowTitle(), tr("Wallet not connected."));
        return;
    }
    QString error;
    auto leaves = currentLeaves(error);
    if (leaves.empty()) {
        QMessageBox::warning(this, windowTitle(),
                             error.isEmpty() ? tr("Tree is empty") : error);
        return;
    }

    const QString label = m_label_edit->text();
    const auto tpl = static_cast<P2MRController::TreeTemplate>(m_template_combo->currentData().toInt());
    if (tpl == P2MRController::TreeTemplate::OpTrue && m_fund_checkbox->isChecked()) {
        QMessageBox::warning(this, windowTitle(), tr("The OP_TRUE testing template cannot be funded from the GUI."));
        return;
    }
    if (m_fund_checkbox->isVisible() && m_fund_checkbox->isChecked()) {
        const CAmount amount = m_amount_field->value();
        if (amount <= 0) {
            QMessageBox::warning(this, windowTitle(), tr("Enter a positive funding amount."));
            return;
        }
        interfaces::WalletP2MRFunded funded;
        if (!m_controller->createAndFundVault(leaves, amount, label, /*subtract_fee=*/false, funded, error)) {
            QMessageBox::critical(this, windowTitle(), error);
            return;
        }
        m_created_address = QString::fromStdString(funded.created.address);
        m_created_id = QString::fromStdString(funded.created.id);
        QMessageBox::information(this, windowTitle(),
            tr("Vault created and funded.\nAddress: %1\nFunding txid: %2")
                .arg(m_created_address, QString::fromStdString(funded.txid.GetHex())));
    } else {
        interfaces::WalletP2MRCreated created;
        if (!m_controller->createVault(leaves, label, created, error)) {
            QMessageBox::critical(this, windowTitle(), error);
            return;
        }
        m_created_address = QString::fromStdString(created.address);
        m_created_id = QString::fromStdString(created.id);
        QMessageBox::information(this, windowTitle(),
            tr("Vault created.\nAddress: %1").arg(m_created_address));
    }

    QDialog::accept();
}

// ---------------------------------------------------------------------------
// P2MRSpendDialog
// ---------------------------------------------------------------------------

P2MRSpendDialog::P2MRSpendDialog(P2MRController* controller,
                                 WalletModel* wallet_model,
                                 QString vault_id,
                                 const PlatformStyle* platform_style,
                                 QWidget* parent)
    : QDialog(parent),
      m_controller(controller),
      m_wallet_model(wallet_model),
      m_vault_id(std::move(vault_id))
{
    setWindowTitle(tr("Spend from P2MR Vault"));
    buildLayout(platform_style);
}

void P2MRSpendDialog::buildLayout(const PlatformStyle* /*platform_style*/)
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto* vault_label = new QLabel(m_vault_id, this);
    vault_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Vault id:"), vault_label);

    m_dest_edit = new QLineEdit(this);
    m_dest_edit->setPlaceholderText(tr("Recipient BTQ address"));
    form->addRow(tr("To:"), m_dest_edit);

    m_amount_field = new BTQAmountField(this);
    form->addRow(tr("Amount:"), m_amount_field);

    m_fee_field = new BTQAmountField(this);
    m_fee_field->setValue(DEFAULT_P2MR_SPEND_FEE);
    form->addRow(tr("Fee:"), m_fee_field);

    layout->addLayout(form);

    m_preview_label = new QLabel(this);
    m_preview_label->setWordWrap(true);
    m_preview_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_preview_label);

    auto* button_row = new QHBoxLayout();
    m_prepare_button = new QPushButton(tr("Prepare and test"), this);
    m_broadcast_button = new QPushButton(tr("Broadcast"), this);
    m_broadcast_button->setEnabled(false);
    button_row->addWidget(m_prepare_button);
    button_row->addWidget(m_broadcast_button);
    button_row->addStretch();
    layout->addLayout(button_row);

    auto* button_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(button_box);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_prepare_button, &QPushButton::clicked, this, &P2MRSpendDialog::onPrepare);
    connect(m_broadcast_button, &QPushButton::clicked, this, &P2MRSpendDialog::onBroadcast);

    // Any edit invalidates the prepared spend.
    auto invalidate = [this]() {
        m_prepared.reset();
        m_broadcast_button->setEnabled(false);
        m_preview_label->setText({});
    };
    connect(m_dest_edit, &QLineEdit::textChanged, this, invalidate);
    connect(m_amount_field, &BTQAmountField::valueChanged, this, invalidate);
    connect(m_fee_field, &BTQAmountField::valueChanged, this, invalidate);
}

void P2MRSpendDialog::onPrepare()
{
    if (!m_controller || !m_wallet_model) return;

    // Hold the unlock context for the duration of this method so signing
    // sees the wallet unlocked. UnlockContext is non-copyable/movable so it
    // must be stack-constructed directly via copy elision.
    WalletModel::UnlockContext ctx(m_wallet_model->requestUnlock());
    if (!ctx.isValid()) {
        QMessageBox::warning(this, windowTitle(), tr("Wallet unlock cancelled."));
        return;
    }

    P2MRController::PreparedSpend prepared;
    QString error;
    if (!m_controller->prepareSpend(m_vault_id, m_dest_edit->text(),
                                    m_amount_field->value(), m_fee_field->value(),
                                    prepared, error)) {
        QMessageBox::critical(this, windowTitle(), error);
        return;
    }

    m_prepared = prepared;
    m_broadcast_button->setEnabled(true);

    const CAmount change_amount = prepared.spend.change_amount;
    QString preview;
    preview += tr("Txid: %1").arg(prepared.txid) + "\n";
    preview += tr("Input: %1 (%2 sats)")
                   .arg(QString::fromStdString(prepared.spend.input.hash.GetHex()))
                   .arg(qint64(prepared.spend.input_amount)) + "\n";
    preview += tr("Send amount: %1").arg(FormatAmount(m_wallet_model, prepared.spend.send_amount)) + "\n";
    if (prepared.spend.has_change) {
        preview += tr("Change: %1").arg(FormatAmount(m_wallet_model, change_amount)) + "\n";
    } else {
        preview += tr("No change (dust threshold)") + "\n";
    }
    preview += tr("Effective fee: %1").arg(FormatAmount(m_wallet_model, prepared.spend.effective_fee)) + "\n";
    preview += tr("Sign complete: %1").arg(prepared.spend.sign_complete ? tr("yes") : tr("no")) + "\n";
    preview += tr("Mempool accept: %1").arg(prepared.spend.mempool_allowed ? tr("yes") : tr("no"));
    m_preview_label->setText(preview);
}

void P2MRSpendDialog::onBroadcast()
{
    if (!m_controller || !m_prepared) return;
    const auto answer = QMessageBox::question(this, windowTitle(),
        tr("Broadcast this transaction to the network?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    QString error;
    QString txid;
    if (!m_controller->broadcastPreparedSpend(*m_prepared, txid, error)) {
        QMessageBox::critical(this, windowTitle(), error);
        return;
    }
    m_broadcast_txid = txid;
    QMessageBox::information(this, windowTitle(),
        tr("Broadcast successful. txid: %1").arg(txid));
    accept();
}

void P2MRSpendDialog::accept() { QDialog::accept(); }

// ---------------------------------------------------------------------------
// P2MRDetailsDialog
// ---------------------------------------------------------------------------

P2MRDetailsDialog::P2MRDetailsDialog(const interfaces::WalletP2MREntry& entry, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("P2MR Vault Details"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto add_row = [&](const QString& label, const QString& value) {
        auto* w = new QLabel(value, this);
        w->setWordWrap(true);
        w->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, w);
    };

    add_row(tr("Id:"), QString::fromStdString(entry.id));
    add_row(tr("Address:"), QString::fromStdString(entry.address));
    add_row(tr("Label:"), QString::fromStdString(entry.label));
    add_row(tr("State:"), QString::fromStdString(entry.state));
    add_row(tr("Merkle root:"), QString::fromStdString(HexStr(entry.merkle_root)));
    add_row(tr("scriptPubKey:"), QString::fromStdString(HexStr(entry.script_pub_key)));
    add_row(tr("Created (UNIX):"), QString::number(entry.created_at));

    layout->addLayout(form);

    layout->addWidget(new QLabel(tr("Tree (DFS order):"), this));
    auto* tree_view = new QPlainTextEdit(this);
    tree_view->setReadOnly(true);
    tree_view->setPlainText(QString::fromUtf8(QJsonDocument(LeavesToJson(entry.tree))
                                                  .toJson(QJsonDocument::Indented)));
    layout->addWidget(tree_view);

    auto* button_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(button_box);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
}
