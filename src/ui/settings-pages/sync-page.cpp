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
    : SettingsPage(tr("Sincronizzazione"),
                   tr("Stato dei trasferimenti, limiti di velocità e cache."),
                   parent),
      loading_(false),
      search_row_(nullptr)
{
    // --- Status (was in the tray menu)
    addGroup(tr("Stato"));

    QPushButton *transfer_btn = makeButton(tr("Dettagli"));
    transfer_row_ = addRow(tr("Trasferimenti"), tr("Nessun trasferimento in corso."), transfer_btn);
    transfer_btn->setAccessibleName(tr("Mostra l'avanzamento dei trasferimenti"));
    connect(transfer_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showTransferProgressDialog();
    });

    QWidget *errors_ctrl = new QWidget;
    QHBoxLayout *el = new QHBoxLayout(errors_ctrl);
    el->setContentsMargins(0, 0, 0, 0);
    el->setSpacing(12);
    errors_pill_ = makePill(tr("Nessun errore"), "success");
    QPushButton *errors_btn = makeButton(tr("Mostra"));
    errors_btn->setAccessibleName(tr("Mostra gli errori di sincronizzazione"));
    el->addWidget(errors_pill_);
    el->addWidget(errors_btn);
    errors_row_ = addRow(tr("Errori di sincronizzazione"),
                         tr("File che non è stato possibile caricare o scaricare."),
                         errors_ctrl);
    connect(errors_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showSyncErrorsDialog();
    });

#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS)
    QPushButton *search_btn = makeButton(tr("Cerca…"));
    search_row_ = addRow(tr("Cerca file"),
                         tr("Cerca nei file delle librerie sul server."),
                         search_btn);
    search_btn->setAccessibleName(tr("Cerca file"));
    connect(search_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showSearchDialog();
    });
#endif

    // --- Speed limits
    addGroup(tr("Limiti di velocità"));

    download_limit_ = makeSpin(0, 100000, tr(" KB/s"));
    addRow(tr("Velocità massima di download"),
           tr("0 significa nessun limite."),
           download_limit_);
    saveOnChange(download_limit_, [](int v) {
        SettingsManager *mgr = gui->settingsManager();
        if ((int)mgr->maxDownloadRatio() != v) mgr->setMaxDownloadRatio(v);
    });

    upload_limit_ = makeSpin(0, 100000, tr(" KB/s"));
    addRow(tr("Velocità massima di upload"),
           tr("0 significa nessun limite."),
           upload_limit_);
    saveOnChange(upload_limit_, [](int v) {
        SettingsManager *mgr = gui->settingsManager();
        if ((int)mgr->maxUploadRatio() != v) mgr->setMaxUploadRatio(v);
    });

    // --- Cache
    addGroup(tr("Cache"));

    cache_size_ = makeSpin(1, 10000, tr(" GB"));
    addRow(tr("Dimensione massima della cache"),
           tr("Oltre questo limite i file scaricati meno usati vengono liberati."),
           cache_size_);
    saveOnChange(cache_size_, [](int v) {
        SettingsManager *mgr = gui->settingsManager();
        if (mgr->getCacheSizeLimitGB() != v) mgr->setCacheSizeLimitGB(v);
    });

    cache_interval_ = makeSpin(1, 14400, tr(" min"));
    addRow(tr("Intervallo di pulizia della cache"),
           tr("Ogni quanto controllare se la cache supera il limite."),
           cache_interval_);
    saveOnChange(cache_interval_, [](int v) {
        SettingsManager *mgr = gui->settingsManager();
        if (mgr->getCacheCleanIntervalMinutes() != v) mgr->setCacheCleanIntervalMinutes(v);
    });

    // --- Advanced
    addGroup(tr("Avanzate"));

    delete_threshold_ = makeSpin(1, 100000, tr(" file"));
    addRow(tr("Chiedi conferma prima di eliminare molti file"),
           tr("Chiede conferma quando si eliminano più file di questo numero in una volta."),
           delete_threshold_);
    saveOnChange(delete_threshold_, [](int v) {
        SettingsManager *mgr = gui->settingsManager();
        if (mgr->getDeleteConfirmThreshold() != v) mgr->setDeleteConfirmThreshold(v);
    });

    sync_temp_ = new ToggleSwitch;
    addRow(tr("Sincronizza i file temporanei di Office"),
           tr("Carica anche i file temporanei creati da Microsoft Office e LibreOffice."),
           sync_temp_);
    connect(sync_temp_, SIGNAL(toggled(bool)), this, SLOT(onSyncTempToggled(bool)));

    cert_verify_disabled_ = new ToggleSwitch;
    addRow(tr("Non verificare il certificato del server"),
           tr("Sconsigliato: disattiva il controllo del certificato HTTPS durante la sincronizzazione."),
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
    cache_interval_->setValue(mgr->getCacheCleanIntervalMinutes());
    cache_size_->setValue(mgr->getCacheSizeLimitGB());
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
        desc->setText(tr("In upload %1 · in download %2")
                          .arg(translateTransferRate(sent), translateTransferRate(recv)));
    } else {
        desc->setText(tr("Nessun trasferimento in corso."));
    }

    int n_errors = gui->trayIcon() ? gui->trayIcon()->syncErrors().size() : 0;
    if (n_errors == 0) {
        setPill(errors_pill_, tr("Nessun errore"), "success");
    } else {
        setPill(errors_pill_,
                n_errors == 1 ? tr("1 errore") : tr("%1 errori").arg(n_errors),
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
