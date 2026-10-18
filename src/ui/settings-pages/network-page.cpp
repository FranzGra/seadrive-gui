#include <QtWidgets>
#include <QSettings>

#include "seadrive-gui.h"
#include "settings-mgr.h"
#include "utils/utils.h"

#include "toggle-switch.h"
#include "network-page.h"

namespace {

// Same group/keys as the old SettingsDialog, so remembered values survive.
const char *kSettingsGroupForSettingsDialog = "SettingsDialog";
const int kSaveDelayMSecs = 800;

bool needsServer(SettingsManager::ProxyType type)
{
    return type == SettingsManager::HttpProxy || type == SettingsManager::SocksProxy;
}

} // namespace

NetworkPage::NetworkPage(QWidget *parent)
    : SettingsPage(tr("Rete e proxy"),
                   tr("Come %1 si collega ai server. Le modifiche si salvano da sole.")
                       .arg(getBrand()),
                   parent),
      loading_(false)
{
    addGroup(tr("Proxy"));

    type_ = new QComboBox;
    type_->insertItem(SettingsManager::NoProxy, tr("Nessuno"));
    type_->insertItem(SettingsManager::HttpProxy, tr("Proxy HTTP"));
    type_->insertItem(SettingsManager::SocksProxy, tr("Proxy SOCKS5"));
    type_->insertItem(SettingsManager::SystemProxy, tr("Proxy di sistema"));
    type_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    addRow(tr("Tipo di proxy"),
           tr("\"Proxy di sistema\" usa le impostazioni del computer."),
           type_);
    connect(type_, SIGNAL(currentIndexChanged(int)), this, SLOT(onTypeChanged(int)));

    host_ = new QLineEdit;
    host_->setPlaceholderText(tr("proxy.esempio.it"));
    host_->setMinimumWidth(220);
    host_row_ = addRow(tr("Server"), tr("Nome o indirizzo IP del proxy."), host_);
    connect(host_, SIGNAL(textEdited(const QString&)), this, SLOT(onFieldChanged()));
    connect(host_, SIGNAL(editingFinished()), this, SLOT(applyProxy()));

    port_ = new QSpinBox;
    port_->setRange(0, 65535);
    port_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    port_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    port_->setKeyboardTracking(false);
    port_->setMinimumWidth(120);
    port_row_ = addRow(tr("Porta"), QString(), port_);
    connect(port_, SIGNAL(valueChanged(int)), this, SLOT(onFieldChanged()));
    connect(port_, SIGNAL(editingFinished()), this, SLOT(applyProxy()));

    require_password_ = new ToggleSwitch;
    require_row_ = addRow(tr("Il proxy richiede una password"), QString(), require_password_);
    connect(require_password_, SIGNAL(toggled(bool)), this, SLOT(onRequirePasswordToggled(bool)));

    username_ = new QLineEdit;
    username_->setMinimumWidth(220);
    username_row_ = addRow(tr("Nome utente"), QString(), username_);
    connect(username_, SIGNAL(textEdited(const QString&)), this, SLOT(onFieldChanged()));
    connect(username_, SIGNAL(editingFinished()), this, SLOT(applyProxy()));

    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    password_->setMinimumWidth(220);
    password_row_ = addRow(tr("Password"), QString(), password_);
    connect(password_, SIGNAL(textEdited(const QString&)), this, SLOT(onFieldChanged()));
    connect(password_, SIGNAL(editingFinished()), this, SLOT(applyProxy()));

    status_ = addNote(QString());
    status_->setVisible(false);
    status_->setAccessibleName(tr("Stato del proxy"));

    addStretch();

    save_timer_ = new QTimer(this);
    save_timer_->setSingleShot(true);
    save_timer_->setInterval(kSaveDelayMSecs);
    connect(save_timer_, SIGNAL(timeout()), this, SLOT(applyProxy()));
}

void NetworkPage::load()
{
    SettingsManager *mgr = gui->settingsManager();
    loading_ = true;

    mgr->loadProxySettings();
    SettingsManager::SeafileProxy proxy = mgr->getProxy();

    type_->setCurrentIndex(proxy.type);
    host_->setText(proxy.host);
    port_->setValue(proxy.port);
    username_->setText(proxy.username);
    password_->setText(proxy.password);
    require_password_->setCheckedImmediate(!proxy.username.isEmpty());

    updateVisibility();
    showStatus(QString(), false);
    loading_ = false;
}

void NetworkPage::updateVisibility()
{
    SettingsManager::ProxyType type =
        static_cast<SettingsManager::ProxyType>(type_->currentIndex());
    bool server = needsServer(type);
    bool auth = server && require_password_->isChecked();

    host_row_->setVisible(server);
    port_row_->setVisible(server);
    require_row_->setVisible(server);
    username_row_->setVisible(auth);
    password_row_->setVisible(auth);
    refreshSeparators();
}

void NetworkPage::prefillRemembered()
{
    // Same behaviour as the old dialog: reuse the last host/port typed for
    // this proxy type.
    SettingsManager::ProxyType type =
        static_cast<SettingsManager::ProxyType>(type_->currentIndex());
    if (!needsServer(type)) {
        return;
    }
    QString prefix = type == SettingsManager::HttpProxy ? "http" : "socks";
    QSettings settings;
    settings.beginGroup(kSettingsGroupForSettingsDialog);
    if (host_->text().trimmed().isEmpty()) {
        QString key = prefix + "_proxy_host";
        if (settings.contains(key)) {
            host_->setText(settings.value(key).toString());
        }
    }
    if (port_->value() == 0) {
        QString key = prefix + "_proxy_port";
        if (settings.contains(key)) {
            port_->setValue(settings.value(key).toInt());
        }
    }
    settings.endGroup();
}

void NetworkPage::onTypeChanged(int)
{
    if (loading_) return;
    loading_ = true;
    prefillRemembered();
    loading_ = false;
    updateVisibility();
    save_timer_->stop();
    applyProxy();
}

void NetworkPage::onRequirePasswordToggled(bool)
{
    if (loading_) return;
    updateVisibility();
    save_timer_->stop();
    applyProxy();
}

void NetworkPage::onFieldChanged()
{
    if (loading_) return;
    save_timer_->start();
}

QString NetworkPage::validate() const
{
    SettingsManager::ProxyType type =
        static_cast<SettingsManager::ProxyType>(type_->currentIndex());
    if (!needsServer(type)) {
        return QString();
    }
    if (host_->text().trimmed().isEmpty()) {
        return tr("Inserisci l'indirizzo del server proxy.");
    }
    if (port_->value() == 0) {
        return tr("Inserisci una porta valida per il proxy.");
    }
    if (require_password_->isChecked()) {
        if (username_->text().trimmed().isEmpty()) {
            return tr("Inserisci il nome utente del proxy.");
        }
        if (password_->text().trimmed().isEmpty()) {
            return tr("Inserisci la password del proxy.");
        }
    }
    return QString();
}

void NetworkPage::rememberHostPort()
{
    SettingsManager::ProxyType type =
        static_cast<SettingsManager::ProxyType>(type_->currentIndex());
    QSettings settings;
    settings.beginGroup(kSettingsGroupForSettingsDialog);
    if (type == SettingsManager::HttpProxy) {
        settings.setValue("http_proxy_host", host_->text().trimmed());
        settings.setValue("http_proxy_port", port_->value());
    } else if (type == SettingsManager::SocksProxy) {
        settings.setValue("socks_proxy_host", host_->text().trimmed());
        settings.setValue("socks_proxy_port", port_->value());
    }
    settings.endGroup();
}

void NetworkPage::applyProxy()
{
    if (loading_) return;
    save_timer_->stop();

    QString problem = validate();
    if (!problem.isEmpty()) {
        showStatus(problem, true);
        return;
    }

    SettingsManager *mgr = gui->settingsManager();
    SettingsManager::ProxyType type =
        static_cast<SettingsManager::ProxyType>(type_->currentIndex());
    SettingsManager::SeafileProxy new_proxy(type);
    if (needsServer(type)) {
        new_proxy.host = host_->text().trimmed();
        new_proxy.port = port_->value();
        if (require_password_->isChecked()) {
            new_proxy.username = username_->text().trimmed();
            new_proxy.password = password_->text().trimmed();
        }
        rememberHostPort();
    }

    if (new_proxy != mgr->getProxy()) {
        mgr->setProxy(new_proxy);
        showStatus(tr("Impostazioni del proxy salvate."), false);
    } else {
        showStatus(QString(), false);
    }
}

void NetworkPage::showStatus(const QString& text, bool error)
{
    status_->setText(text);
    status_->setObjectName(error ? "rowError" : "rowSuccess");
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
    status_->setVisible(!text.isEmpty());
}
