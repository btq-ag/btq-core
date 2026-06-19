// Copyright (c) 2011-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/btq-config.h>
#endif

#include <chainparams.h>
#include <qt/intro.h>
#include <qt/forms/ui_intro.h>
#include <util/chaintype.h>
#include <util/fs.h>

#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>

#include <common/args.h>
#include <clientversion.h>
#include <interfaces/node.h>
#include <util/fs_helpers.h>
#include <validation.h>

#include <QCoreApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QSettings>

#include <cassert>
#include <cmath>

/* Check free space asynchronously to prevent hanging the UI thread.

   Up to one request to check a path is in flight to this thread; when the check()
   function runs, the current path is requested from the associated Intro object.
   The reply is sent back through a signal.

   This ensures that no queue of checking requests is built up while the user is
   still entering the path, and that always the most recently entered path is checked as
   soon as the thread becomes available.
*/
class FreespaceChecker : public QObject
{
    Q_OBJECT

public:
    explicit FreespaceChecker(Intro *intro);

    enum Status {
        ST_OK,
        ST_ERROR
    };

public Q_SLOTS:
    void check();

Q_SIGNALS:
    void reply(int status, const QString &message, quint64 available);

private:
    Intro *intro;
};

#include <qt/intro.moc>

FreespaceChecker::FreespaceChecker(Intro *_intro)
{
    this->intro = _intro;
}

void FreespaceChecker::check()
{
    QString dataDirStr = intro->getPathToCheck();
    fs::path dataDir = GUIUtil::QStringToPath(dataDirStr);
    uint64_t freeBytesAvailable = 0;
    int replyStatus = ST_OK;
    QString replyMessage = tr("A new data directory will be created.");

    /* Find first parent that exists, so that fs::space does not fail */
    fs::path parentDir = dataDir;
    fs::path parentDirOld = fs::path();
    while(parentDir.has_parent_path() && !fs::exists(parentDir))
    {
        parentDir = parentDir.parent_path();

        /* Check if we make any progress, break if not to prevent an infinite loop here */
        if (parentDirOld == parentDir)
            break;

        parentDirOld = parentDir;
    }

    try {
        freeBytesAvailable = fs::space(parentDir).available;
        if(fs::exists(dataDir))
        {
            if(fs::is_directory(dataDir))
            {
                QString separator = "<code>" + QDir::toNativeSeparators("/") + tr("name") + "</code>";
                replyStatus = ST_OK;
                replyMessage = tr("Directory already exists. Add %1 if you intend to create a new directory here.").arg(separator);
            } else {
                replyStatus = ST_ERROR;
                replyMessage = tr("Path already exists, and is not a directory.");
            }
        }
    } catch (const fs::filesystem_error&)
    {
        /* Parent directory does not exist or is not accessible */
        replyStatus = ST_ERROR;
        replyMessage = tr("Cannot create data directory here.");
    }
    Q_EMIT reply(replyStatus, replyMessage, freeBytesAvailable);
}

namespace {
//! Return pruning size that will be used if automatic pruning is enabled.
int GetPruneTargetGB()
{
    int64_t prune_target_mib = gArgs.GetIntArg("-prune", 0);
    // >1 means automatic pruning is enabled by config, 1 means manual pruning, 0 means no pruning.
    return prune_target_mib > 1 ? PruneMiBtoGB(prune_target_mib) : DEFAULT_PRUNE_TARGET_GB;
}

QString ChainDisplayName(ChainType chain)
{
    switch (chain) {
    case ChainType::BTQMAIN:
        return QCoreApplication::translate("Intro", "Mainnet");
    case ChainType::BTQTEST:
        return QCoreApplication::translate("Intro", "Testnet");
    case ChainType::BTQSIGNET:
        return QCoreApplication::translate("Intro", "Signet");
    case ChainType::BTQREGTEST:
        return QCoreApplication::translate("Intro", "Regtest");
    }
    assert(false);
}

QString ChainHighlightColor(ChainType chain)
{
    switch (chain) {
    case ChainType::BTQMAIN:
        return QStringLiteral("#00f0ff");
    case ChainType::BTQTEST:
        return QStringLiteral("#00f0ff");
    case ChainType::BTQSIGNET:
        return QStringLiteral("#d8b4fe");
    case ChainType::BTQREGTEST:
        return QStringLiteral("#fcd34d");
    }
    assert(false);
}

void ApplyBitcoinQuantumIntroStyle(QDialog* dialog, Ui::Intro* ui, ChainType chain)
{
    const QString hi{ChainHighlightColor(chain)};

    ui->bannerFrame->setStyleSheet(QStringLiteral(
        "QFrame#bannerFrame {"
        "  background-color: #0c1017;"
        "  border: 1px solid rgba(0,240,255,0.30);"
        "  border-radius: 16px;"
        "}"));

    ui->brandWordmarkLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; }"));
    ui->networkLockupLogoLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; }"));

    ui->welcomeLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,0.76); font-size: 15px; font-weight: 500; }"));

    ui->networkEyebrowLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,0.45); font-size: 10px; font-weight: 800; letter-spacing: 3px; }"));

    ui->networkNameLabel->setStyleSheet(QString(
        "QLabel { color: %1; font-size: 32px; font-weight: 800; letter-spacing: 3px; }").arg(hi));

    ui->networkCliLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,0.78); font-size: 13px; }"));

    dialog->setStyleSheet(QStringLiteral(
        "QDialog#Intro { background-color: #06080c; }"
        "QLabel { color: rgba(255,255,255,0.82); }"
        "QLabel#dataDirSectionLabel, QLabel#syncSectionLabel {"
        "  color: rgba(0,240,255,0.72); font-size: 10px; font-weight: 800;"
        "  letter-spacing: 2.5px; margin-top: 4px;"
        "}"
        "QLabel#storageLabel, QLabel#sizeWarningLabel,"
        "QLabel#lblExplanation1, QLabel#lblExplanation2, QLabel#lblExplanation3 {"
        "  font-family: \"Consolas\", \"DejaVu Sans Mono\", \"Courier New\", monospace;"
        "  font-size: 13px; color: rgba(255,255,255,0.74);"
        "}"
        "QLabel#freeSpace {"
        "  font-family: \"Consolas\", \"DejaVu Sans Mono\", \"Courier New\", monospace;"
        "  font-size: 13px; color: rgba(255,255,255,0.78);"
        "}"
        "QLineEdit {"
        "  background-color: #0c1017; color: rgba(255,255,255,0.94);"
        "  selection-background-color: #00f0ff; selection-color: #06080c;"
        "  border: 1px solid rgba(0,240,255,0.25); border-radius: 10px;"
        "  padding: 8px 12px; font-size: 13px;"
        "  font-family: \"Consolas\", \"DejaVu Sans Mono\", \"Courier New\", monospace;"
        "}"
        "QPushButton#ellipsisButton {"
        "  background-color: #0c1017; color: rgba(255,255,255,0.92);"
        "  border: 1px solid rgba(0,240,255,0.35); border-radius: 10px;"
        "  font-weight: 700; padding: 8px;"
        "}"
        "QPushButton#ellipsisButton:hover {"
        "  background-color: #111927; border-color: rgba(0,240,255,0.6);"
        "}"
        "QRadioButton { font-size: 13px; color: rgba(255,255,255,0.92); spacing: 8px;"
        "  font-family: \"Consolas\", \"DejaVu Sans Mono\", \"Courier New\", monospace;"
        "}"
        "QRadioButton::indicator { width: 18px; height: 18px; }"
        "QCheckBox { font-size: 13px; color: rgba(255,255,255,0.90);"
        "  font-family: \"Consolas\", \"DejaVu Sans Mono\", \"Courier New\", monospace;"
        "}"
        "QSpinBox {"
        "  background-color: #0c1017; color: rgba(255,255,255,0.94);"
        "  selection-background-color: #00f0ff; selection-color: #06080c;"
        "  padding: 6px 10px; border-radius: 10px;"
        "  border: 1px solid rgba(0,240,255,0.25);"
        "  font-family: \"Consolas\", \"DejaVu Sans Mono\", \"Courier New\", monospace;"
        "}"
        "QDialogButtonBox QPushButton {"
        "  min-width: 108px; padding: 10px 20px; border-radius: 10px;"
        "  font-weight: 700; font-size: 13px;"
        "  font-family: \"Consolas\", \"DejaVu Sans Mono\", \"Courier New\", monospace;"
        "}"
        "QDialogButtonBox QPushButton:default {"
        "  background-color: #00f0ff; color: #06080c; border: none;"
        "}"
        "QDialogButtonBox QPushButton:!default {"
        "  background-color: #0c1017; color: rgba(255,255,255,0.92);"
        "  border: 1px solid rgba(0,240,255,0.35);"
        "}"
        "QDialogButtonBox QPushButton:!default:hover {"
        "  border-color: rgba(0,240,255,0.65); background-color: #131b27;"
        "}"
    ));
}
} // namespace

Intro::Intro(QWidget *parent, ChainType chain, int64_t blockchain_size_gb, int64_t chain_state_size_gb) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::Intro),
    m_blockchain_size_gb(blockchain_size_gb),
    m_chain_state_size_gb(chain_state_size_gb),
    m_prune_target_gb{GetPruneTargetGB()}
{
    ui->setupUi(this);
    setWindowTitle(tr("%1 — %2").arg(tr("Welcome"), ChainDisplayName(chain)));

    {
        const bool show_top_wordmark = (chain != ChainType::BTQTEST);
        ui->brandWordmarkLabel->setVisible(show_top_wordmark);
        if (show_top_wordmark) {
            constexpr int wordmark_width{568};
            const QPixmap wm(QStringLiteral(":/icons/bitcoin-quantum-wordmark"));
            if (!wm.isNull()) {
                ui->brandWordmarkLabel->setPixmap(wm.scaledToWidth(wordmark_width, Qt::SmoothTransformation));
            }
        } else {
            ui->brandWordmarkLabel->clear();
        }

        if (chain == ChainType::BTQTEST) {
            constexpr int lockup_width{620};
            const QPixmap lk(QStringLiteral(":/icons/bitcoin-quantum-testnet-lockup"));
            if (!lk.isNull()) {
                ui->networkLockupLogoLabel->setPixmap(lk.scaledToWidth(lockup_width, Qt::SmoothTransformation));
            }
            ui->networkLockupLogoLabel->setVisible(true);
        } else {
            ui->networkLockupLogoLabel->clear();
            ui->networkLockupLogoLabel->setVisible(false);
        }
    }

    ui->welcomeLabel->setText(ui->welcomeLabel->text().arg(PACKAGE_NAME));
    ui->networkEyebrowLabel->setText(tr("ACTIVE NETWORK"));

    ui->networkNameLabel->setText(ChainDisplayName(chain).toUpper());

    ui->networkCliLabel->setText(
        tr("<p style=\"margin-top:0;color:rgba(255,255,255,0.72);\">"
           "%1 keeps a dedicated subfolder for this network inside the data directory you pick below."
           "</p>")
            .arg(QString::fromUtf8(PACKAGE_NAME).toHtmlEscaped()));
    ui->networkCliLabel->setToolTip(QString());

    ApplyBitcoinQuantumIntroStyle(this, ui, chain);

    ui->storageLabel->setText(ui->storageLabel->text().arg(PACKAGE_NAME));

    /* Use the multi-arg QString overload so %1-%4 always bind correctly on all
       platforms (avoid MSVC picking the wrong QString::arg overload chain). */
    ui->lblExplanation1->setText(ui->lblExplanation1->text().arg(
        QString::fromUtf8(PACKAGE_NAME),
        QString::number(static_cast<long long>(m_blockchain_size_gb)),
        QString::number(static_cast<int>(BTQ_LAUNCH_YEAR)),
        tr("BTQ")));
    ui->lblExplanation2->setText(ui->lblExplanation2->text().arg(PACKAGE_NAME));

    const int min_prune_target_GB = std::ceil(MIN_DISK_SPACE_FOR_BLOCK_FILES / 1e9);
    ui->pruneGB->setRange(min_prune_target_GB, std::numeric_limits<int>::max());
    if (gArgs.GetIntArg("-prune", 0) > 1) { // -prune=1 means enabled, above that it's a size in MiB
        ui->prune->setChecked(true);
        ui->prune->setEnabled(false);
    }
    ui->pruneGB->setValue(m_prune_target_gb);
    ui->pruneGB->setToolTip(ui->prune->toolTip());
    ui->lblPruneSuffix->setToolTip(ui->prune->toolTip());
    UpdatePruneLabels(ui->prune->isChecked());

    connect(ui->prune, &QCheckBox::toggled, [this](bool prune_checked) {
        UpdatePruneLabels(prune_checked);
        UpdateFreeSpaceLabel();
    });
    connect(ui->pruneGB, qOverload<int>(&QSpinBox::valueChanged), [this](int prune_GB) {
        m_prune_target_gb = prune_GB;
        UpdatePruneLabels(ui->prune->isChecked());
        UpdateFreeSpaceLabel();
    });

    startThread();
}

Intro::~Intro()
{
    delete ui;
    /* Ensure thread is finished before it is deleted */
    thread->quit();
    thread->wait();
}

QString Intro::getDataDirectory()
{
    return ui->dataDirectory->text();
}

void Intro::setDataDirectory(const QString &dataDir)
{
    ui->dataDirectory->setText(dataDir);
    if(dataDir == GUIUtil::getDefaultDataDirectory())
    {
        ui->dataDirDefault->setChecked(true);
        ui->dataDirectory->setEnabled(false);
        ui->ellipsisButton->setEnabled(false);
    } else {
        ui->dataDirCustom->setChecked(true);
        ui->dataDirectory->setEnabled(true);
        ui->ellipsisButton->setEnabled(true);
    }
}

int64_t Intro::getPruneMiB() const
{
    switch (ui->prune->checkState()) {
    case Qt::Checked:
        return PruneGBtoMiB(m_prune_target_gb);
    case Qt::Unchecked: default:
        return 0;
    }
}

bool Intro::showIfNeeded(bool& did_show_intro, int64_t& prune_MiB)
{
    did_show_intro = false;

    QSettings settings;
    /* If data directory provided on command line, no need to look at settings
       or show a picking dialog */
    if(!gArgs.GetArg("-datadir", "").empty())
        return true;
    /* 1) Default data directory for operating system */
    QString dataDir = GUIUtil::getDefaultDataDirectory();
    /* 2) Allow QSettings to override default dir */
    dataDir = settings.value("strDataDir", dataDir).toString();

    if(!fs::exists(GUIUtil::QStringToPath(dataDir)) || gArgs.GetBoolArg("-choosedatadir", DEFAULT_CHOOSE_DATADIR) || settings.value("fReset", false).toBool() || gArgs.GetBoolArg("-resetguisettings", false))
    {
        /* Use selectParams here to guarantee Params() can be used by node interface */
        try {
            SelectParams(gArgs.GetChainType());
        } catch (const std::exception&) {
            return false;
        }

        /* If current default data directory does not exist, let the user choose one */
        const ChainType intro_chain{gArgs.GetChainType()};
        Intro intro(nullptr, intro_chain, Params().AssumedBlockchainSize(), Params().AssumedChainStateSize());
        intro.setDataDirectory(dataDir);
        intro.setWindowIcon(QIcon(":icons/btq"));
        did_show_intro = true;

        while(true)
        {
            if(!intro.exec())
            {
                /* Cancel clicked */
                return false;
            }
            dataDir = intro.getDataDirectory();
            try {
                if (TryCreateDirectories(GUIUtil::QStringToPath(dataDir))) {
                    // If a new data directory has been created, make wallets subdirectory too
                    TryCreateDirectories(GUIUtil::QStringToPath(dataDir) / "wallets");
                }
                break;
            } catch (const fs::filesystem_error&) {
                QMessageBox::critical(nullptr, PACKAGE_NAME,
                    tr("Error: Specified data directory \"%1\" cannot be created.").arg(dataDir));
                /* fall through, back to choosing screen */
            }
        }

        // Additional preferences:
        prune_MiB = intro.getPruneMiB();

        settings.setValue("strDataDir", dataDir);
        settings.setValue("fReset", false);
    }
    /* Only override -datadir if different from the default, to make it possible to
     * override -datadir in the btq.conf file in the default data directory
     * (to be consistent with btqd behavior)
     */
    if(dataDir != GUIUtil::getDefaultDataDirectory()) {
        gArgs.SoftSetArg("-datadir", fs::PathToString(GUIUtil::QStringToPath(dataDir))); // use OS locale for path setting
    }
    return true;
}

void Intro::setStatus(int status, const QString &message, quint64 bytesAvailable)
{
    switch(status)
    {
    case FreespaceChecker::ST_OK:
        ui->errorMessage->setText(message);
        ui->errorMessage->setStyleSheet("");
        break;
    case FreespaceChecker::ST_ERROR:
        ui->errorMessage->setText(tr("Error") + ": " + message);
        ui->errorMessage->setStyleSheet("QLabel { color: #fca5a5; }");
        break;
    }
    /* Indicate number of bytes available */
    if(status == FreespaceChecker::ST_ERROR)
    {
        ui->freeSpace->setText("");
    } else {
        m_bytes_available = bytesAvailable;
        if (ui->prune->isEnabled() && !(gArgs.IsArgSet("-prune") && gArgs.GetIntArg("-prune", 0) == 0)) {
            ui->prune->setChecked(m_bytes_available < (m_blockchain_size_gb + m_chain_state_size_gb + 10) * GB_BYTES);
        }
        UpdateFreeSpaceLabel();
    }
    /* Don't allow confirm in ERROR state */
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(status != FreespaceChecker::ST_ERROR);
}

void Intro::UpdateFreeSpaceLabel()
{
    QString freeString = tr("%n GB of space available", "", m_bytes_available / GB_BYTES);
    if (m_bytes_available < m_required_space_gb * GB_BYTES) {
        freeString += " " + tr("(of %n GB needed)", "", m_required_space_gb);
        ui->freeSpace->setStyleSheet("QLabel { color: #fca5a5; font-weight: 600; }");
    } else if (m_bytes_available / GB_BYTES - m_required_space_gb < 10) {
        freeString += " " + tr("(%n GB needed for full chain)", "", m_required_space_gb);
        ui->freeSpace->setStyleSheet("QLabel { color: #fde047; font-weight: 600; }");
    } else {
        ui->freeSpace->setStyleSheet("");
    }
    ui->freeSpace->setText(freeString + ".");
}

void Intro::on_dataDirectory_textChanged(const QString &dataDirStr)
{
    /* Disable OK button until check result comes in */
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    checkPath(dataDirStr);
}

void Intro::on_ellipsisButton_clicked()
{
    QString dir = QDir::toNativeSeparators(QFileDialog::getExistingDirectory(nullptr, tr("Choose data directory"), ui->dataDirectory->text()));
    if(!dir.isEmpty())
        ui->dataDirectory->setText(dir);
}

void Intro::on_dataDirDefault_clicked()
{
    setDataDirectory(GUIUtil::getDefaultDataDirectory());
}

void Intro::on_dataDirCustom_clicked()
{
    ui->dataDirectory->setEnabled(true);
    ui->ellipsisButton->setEnabled(true);
}

void Intro::startThread()
{
    thread = new QThread(this);
    FreespaceChecker *executor = new FreespaceChecker(this);
    executor->moveToThread(thread);

    connect(executor, &FreespaceChecker::reply, this, &Intro::setStatus);
    connect(this, &Intro::requestCheck, executor, &FreespaceChecker::check);
    /*  make sure executor object is deleted in its own thread */
    connect(thread, &QThread::finished, executor, &QObject::deleteLater);

    thread->start();
}

void Intro::checkPath(const QString &dataDir)
{
    mutex.lock();
    pathToCheck = dataDir;
    if(!signalled)
    {
        signalled = true;
        Q_EMIT requestCheck();
    }
    mutex.unlock();
}

QString Intro::getPathToCheck()
{
    QString retval;
    mutex.lock();
    retval = pathToCheck;
    signalled = false; /* new request can be queued now */
    mutex.unlock();
    return retval;
}

void Intro::UpdatePruneLabels(bool prune_checked)
{
    m_required_space_gb = m_blockchain_size_gb + m_chain_state_size_gb;
    QString storageRequiresMsg = tr("At least %1 GB of data will be stored in this directory, and it will grow over time.");
    if (prune_checked && m_prune_target_gb <= m_blockchain_size_gb) {
        m_required_space_gb = m_prune_target_gb + m_chain_state_size_gb;
        storageRequiresMsg = tr("Approximately %1 GB of data will be stored in this directory.");
    }
    ui->lblExplanation3->setVisible(prune_checked);
    ui->pruneGB->setEnabled(prune_checked);
    static constexpr uint64_t nPowTargetSpacing = 1 * 60;  // BTQ: 1-minute blocks (from chainparams, which we don't have at this stage)
    static constexpr uint32_t expected_block_data_size = 2250000;  // includes undo data
    const uint64_t expected_backup_days = m_prune_target_gb * 1e9 / (uint64_t(expected_block_data_size) * 86400 / nPowTargetSpacing);
    ui->lblPruneSuffix->setText(
        //: Explanatory text on the capability of the current prune target.
        tr("(sufficient to restore backups %n day(s) old)", "", expected_backup_days));
    ui->sizeWarningLabel->setText(
        tr("%1 will download and store a copy of the BTQ block chain.").arg(PACKAGE_NAME) + " " +
        storageRequiresMsg.arg(m_required_space_gb) + " " +
        tr("The wallet will also be stored in this directory.")
    );
    this->adjustSize();
}
