#include <QtWidgets>
#include <QSet>

#include "seadrive-gui.h"
#include "settings-mgr.h"
#include "account-mgr.h"
#include "rpc/rpc-client.h"
#include "utils/utils.h"
#include "utils/json-utils.h"
#include "src/ui/tray-icon.h"

#include "toggle-switch.h"
#include "sync-page.h"

namespace {

const int kStatusRefreshMSecs = 1000;
const int kSaveDelayMSecs = 600;

} // namespace

SyncPage::SyncPage(QWidget *parent)
    : SettingsPage(tr("Sync"),
                   tr("Transfer status and speed limits."),
                   parent),
      loading_(false),
      search_row_(nullptr)
{
    // --- Status (was in the tray menu)
    addGroup(tr("Status"));

    QPushButton *transfer_btn = makeButton(tr("Details"));
    transfer_row_ = addRow(tr("Transfers"), tr("No transfers in progress."), transfer_btn);
    transfer_btn->setAccessibleName(tr("Show transfer progress"));
    connect(transfer_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showTransferProgressDialog();
    });

    QWidget *errors_ctrl = new QWidget;
    QHBoxLayout *el = new QHBoxLayout(errors_ctrl);
    el->setContentsMargins(0, 0, 0, 0);
    el->setSpacing(12);
    errors_pill_ = makePill(tr("No errors"), "success");
    QPushButton *errors_btn = makeButton(tr("Show"));
    errors_btn->setAccessibleName(tr("Show file sync errors"));
    el->addWidget(errors_pill_);
    el->addWidget(errors_btn);
    errors_row_ = addRow(tr("File Sync Errors"),
                         tr("Files that could not be uploaded or downloaded."),
                         errors_ctrl);
    connect(errors_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showSyncErrorsDialog();
    });

#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS)
    QPushButton *search_btn = makeButton(tr("Search…"));
    search_row_ = addRow(tr("Search files"),
                         tr("Search the files of your libraries on the server."),
                         search_btn);
    search_btn->setAccessibleName(tr("Search files"));
    connect(search_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showSearchDialog();
    });
#endif

    // --- Speed limits
    addGroup(tr("Speed limits"));

    download_limit_ = makeSpin(0, 100000, tr(" KB/s"));
    addRow(tr("Download speed limit"),
           tr("0 means no limit."),
           download_limit_);
    saveOnChange(download_limit_, [](int v) {
        SettingsManager *mgr = gui->settingsManager();
        if ((int)mgr->maxDownloadRatio() != v) mgr->setMaxDownloadRatio(v);
    });

    upload_limit_ = makeSpin(0, 100000, tr(" KB/s"));
    addRow(tr("Upload speed limit"),
           tr("0 means no limit."),
           upload_limit_);
    saveOnChange(upload_limit_, [](int v) {
        SettingsManager *mgr = gui->settingsManager();
        if ((int)mgr->maxUploadRatio() != v) mgr->setMaxUploadRatio(v);
    });

    // --- Advanced
    addGroup(tr("Advanced"));

    delete_threshold_ = makeSpin(1, 100000, tr(" files"));
    addRow(tr("Confirm before deleting many files"),
           tr("Asks for confirmation when more files than this are deleted at once."),
           delete_threshold_);
    saveOnChange(delete_threshold_, [](int v) {
        SettingsManager *mgr = gui->settingsManager();
        if (mgr->getDeleteConfirmThreshold() != v) mgr->setDeleteConfirmThreshold(v);
    });

    sync_temp_ = new ToggleSwitch;
    addRow(tr("Enable sync temporary files of MSOffice/Libreoffice"),
           tr("Also uploads the temporary files created by Microsoft Office and LibreOffice."),
           sync_temp_);
    connect(sync_temp_, SIGNAL(toggled(bool)), this, SLOT(onSyncTempToggled(bool)));

    cert_verify_disabled_ = new ToggleSwitch;
    addRow(tr("Do not verify server certificate in HTTPS syncing"),
           tr("Not recommended: turns off the HTTPS certificate check while syncing."),
           cert_verify_disabled_);
    connect(cert_verify_disabled_, SIGNAL(toggled(bool)), this, SLOT(onCertVerifyToggled(bool)));

    addStretch();

    status_timer_ = new QTimer(this);
    status_timer_->setInterval(kStatusRefreshMSecs);
    connect(status_timer_, SIGNAL(timeout()), this, SLOT(refreshStatus()));
    connect(this, &SettingsPage::pageShown, this, [this] {
        refreshStatus();
        status_timer_->start();
    });
    connect(this, &SettingsPage::pageHidden, status_timer_, &QTimer::stop);
}

QSpinBox *SyncPage::makeSpin(int min, int max, const QString& suffix)
{
    QSpinBox *spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setSuffix(suffix);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    spin->setKeyboardTracking(false);
    spin->setMinimumWidth(120);
    return spin;
}

void SyncPage::saveOnChange(QSpinBox *spin, std::function<void(int)> save)
{
    QTimer *timer = new QTimer(spin);
    timer->setSingleShot(true);
    timer->setInterval(kSaveDelayMSecs);
    connect(timer, &QTimer::timeout, spin, [spin, save] { save(spin->value()); });
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, timer] {
        if (!loading_) timer->start();
    });
    connect(spin, &QSpinBox::editingFinished, spin, [this, spin, timer, save] {
        if (loading_) return;
        timer->stop();
        save(spin->value());
    });
}

void SyncPage::load()
{
    SettingsManager *mgr = gui->settingsManager();
    loading_ = true;

    download_limit_->setValue(mgr->maxDownloadRatio());
    upload_limit_->setValue(mgr->maxUploadRatio());
    delete_threshold_->setValue(mgr->getDeleteConfirmThreshold());
    sync_temp_->setCheckedImmediate(mgr->syncExtraTempFile());
    cert_verify_disabled_->setCheckedImmediate(mgr->getHttpSyncCertVerifyDisabled());

    // Search is offered only for Pro servers, as in the old tray menu.
    if (search_row_) {
        bool has_pro = false;
        const auto accounts = gui->accountManager()->allAccounts();
        for (const Account& account : accounts) {
            if (account.isPro()) {
                has_pro = true;
                break;
            }
        }
        search_row_->setVisible(has_pro);
        refreshSeparators();
    }

    loading_ = false;
    refreshStatus();
}

void SyncPage::refreshStatus()
{
    // Transfer rate: same RPC the message poller uses for the tray menu.
    qint64 sent = 0, recv = 0;
    bool syncing = false;
    QSet<QString> domains;
    const auto accounts = gui->accountManager()->activeAccounts();
    for (const Account& account : accounts) {
        domains.insert(account.domainID());
    }
    for (const QString& domain : domains) {
        SeafileRpcClient *rpc = gui->rpcClient(domain);
        if (!rpc || !rpc->isConnected()) {
            continue;
        }
        json_t *ret = nullptr;
        if (!rpc->getGlobalSyncStatus(&ret)) {
            continue;
        }
        Json json(ret);
        if (json.getLong("is_syncing")) {
            syncing = true;
            sent += json.getLong("sent_bytes");
            recv += json.getLong("recv_bytes");
        }
        json_decref(ret);
    }

    QLabel *desc = rowDescription(transfer_row_);
    if (syncing) {
        desc->setText(tr("Up %1, Down %2")
                          .arg(translateTransferRate(sent), translateTransferRate(recv)));
    } else {
        desc->setText(tr("No transfers in progress."));
    }

    int n_errors = gui->trayIcon() ? gui->trayIcon()->syncErrors().size() : 0;
    if (n_errors == 0) {
        setPill(errors_pill_, tr("No errors"), "success");
    } else {
        setPill(errors_pill_,
                tr("%n error(s)", nullptr, n_errors),
                "warning");
    }
}

void SyncPage::onSyncTempToggled(bool checked)
{
    if (loading_) return;
    gui->settingsManager()->setSyncExtraTempFile(checked);
}

void SyncPage::onCertVerifyToggled(bool checked)
{
    if (loading_) return;
    gui->settingsManager()->setHttpSyncCertVerifyDisabled(checked);
}
