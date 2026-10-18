#include <QtWidgets>

#include "seadrive-gui.h"
#include "account-mgr.h"
#include "rpc/rpc-client.h"
#include "utils/utils.h"
#include "src/ui/tray-icon.h"
#include "src/ui/design-tokens.h"

#include "account-page.h"

namespace {

const int kAvatarSize = 40;
const int kRefreshMSecs = 3000;
#if defined(Q_OS_WIN32)
const char *const kPreconfigureUseKerberosLogin = "PreconfigureUseKerberosLogin";
#endif

QLabel *plainLabel(const QString& text, const char *object_name)
{
    QLabel *l = new QLabel(text);
    l->setObjectName(object_name);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    return l;
}

} // namespace

// ---------------------------------------------------------------------------
// AccountDisplay
// ---------------------------------------------------------------------------

namespace AccountDisplay {

QString displayName(const Account& account)
{
    if (!account.accountInfo.name.isEmpty()) {
        return account.accountInfo.name;
    }
    return account.username;
}

QString initials(const QString& name)
{
    QString clean = name;
    int at = clean.indexOf('@');
    if (at > 0) {
        clean = clean.left(at);
    }
    const QStringList parts = clean.split(QRegularExpression("[\\s._-]+"), Qt::SkipEmptyParts);
    QString out;
    for (const QString& part : parts) {
        out += part.at(0).toUpper();
        if (out.size() == 2) break;
    }
    if (out.isEmpty() && !clean.isEmpty()) {
        out = clean.left(1).toUpper();
    }
    return out.isEmpty() ? QString("?") : out;
}

Status status(const Account& account)
{
    if (!account.isValid()) {
        return StatusLoginRequired;
    }
    SeafileRpcClient *rpc = gui->rpcClient(account.domainID());
    if (rpc && rpc->isConnected()) {
        return StatusConnected;
    }
    return StatusNotConnected;
}

QString statusText(Status status)
{
    switch (status) {
    case StatusConnected:
        return QCoreApplication::translate("AccountPage", "Connesso");
    case StatusNotConnected:
        return QCoreApplication::translate("AccountPage", "Non connesso");
    case StatusLoginRequired:
    default:
        return QCoreApplication::translate("AccountPage", "Accesso richiesto");
    }
}

QString statusPillKind(Status status)
{
    switch (status) {
    case StatusConnected:
        return "success";
    case StatusNotConnected:
        return "warning";
    case StatusLoginRequired:
    default:
        return "danger";
    }
}

QPixmap avatar(const QString& name, int size, qreal dpr)
{
    using namespace DesignTokens;

    QPixmap pix(qRound(size * dpr), qRound(size * dpr));
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(accentBg());
    p.drawEllipse(QRectF(0, 0, size, size));

    QFont f = p.font();
    f.setPixelSize(qMax(kFontSmall, size * 2 / 5));
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(accentText());
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initials(name));
    return pix;
}

} // namespace AccountDisplay

// ---------------------------------------------------------------------------
// AccountPage
// ---------------------------------------------------------------------------

AccountPage::AccountPage(QWidget *parent)
    : SettingsPage(tr("Account"),
                   tr("Gli account collegati a %1 e il loro stato.").arg(getBrand()),
                   parent),
      busy_(false)
{
    addGroup(tr("Account collegati"));

    list_ = new QWidget;
    list_layout_ = new QVBoxLayout(list_);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(0);

    // The list draws its own rows and dividers: put it in a card without the
    // standard row padding.
    QWidget *row = addWideRow(list_);
    if (QLayout *l = row->layout()) {
        // inner layout is the second item of the row shell
        if (l->count() > 1) {
            if (QLayout *inner = l->itemAt(1)->layout()) {
                inner->setContentsMargins(0, 0, 0, 0);
            }
        }
    }

    QWidget *actions = new QWidget;
    QHBoxLayout *al = new QHBoxLayout(actions);
    al->setContentsMargins(0, 16, 0, 0);
    add_btn_ = makeButton(tr("Aggiungi un account"), ButtonPrimary);
    al->addWidget(add_btn_);
    al->addStretch(1);
    addPageWidget(actions);
    connect(add_btn_, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showLoginDialog();
    });

#if defined(Q_OS_WIN32)
    QVariant use_kerberos_login =
        gui->readPreconfigureExpandedString(kPreconfigureUseKerberosLogin, "0");
    if (use_kerberos_login.toBool()) {
        add_btn_->hide();
    }
#endif

    addStretch();

    connect(gui->accountManager(), SIGNAL(accountInfoUpdated(const Account&)),
            this, SLOT(refreshIfChanged()));

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(kRefreshMSecs);
    connect(refresh_timer_, SIGNAL(timeout()), this, SLOT(refreshIfChanged()));
    connect(this, &SettingsPage::pageShown, refresh_timer_, [this] { refresh_timer_->start(); });
    connect(this, &SettingsPage::pageHidden, refresh_timer_, &QTimer::stop);
}

void AccountPage::load()
{
    rebuild();
}

void AccountPage::themeChanged()
{
    rebuild();
}

QString AccountPage::signature() const
{
    QStringList parts;
    const auto accounts = gui->accountManager()->allAccounts();
    for (const Account& a : accounts) {
        parts << a.serverUrl.toString() << a.username
              << AccountDisplay::displayName(a)
              << QString::number(AccountDisplay::status(a))
              << QString::number(a.accountInfo.usedStorage)
              << QString::number(a.accountInfo.totalStorage);
    }
    return parts.join('\n');
}

void AccountPage::refreshIfChanged()
{
    // Rebuilding deletes the buttons: never do it while one of their
    // confirmation dialogs is open.
    if (busy_) return;
    if (signature() != last_signature_) {
        rebuild();
    }
}

void AccountPage::rebuild()
{
    last_signature_ = signature();

    while (QLayoutItem *item = list_layout_->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    const auto accounts = gui->accountManager()->allAccounts();
    if (accounts.isEmpty()) {
        QWidget *empty = new QWidget;
        QVBoxLayout *el = new QVBoxLayout(empty);
        el->setContentsMargins(DesignTokens::kPadding, 20, DesignTokens::kPadding, 20);
        QLabel *title = plainLabel(tr("Nessun account"), "rowTitle");
        QLabel *desc = plainLabel(tr("Aggiungi un account per iniziare a usare %1.").arg(getBrand()),
                                  "rowDesc");
        el->addWidget(title);
        el->addWidget(desc);
        list_layout_->addWidget(empty);
    } else {
        for (int i = 0; i < accounts.size(); i++) {
            if (i > 0) {
                QFrame *sep = new QFrame;
                sep->setObjectName("rowSeparator");
                sep->setFixedHeight(1);
                list_layout_->addWidget(sep);
            }
            list_layout_->addWidget(createAccountItem(accounts.at(i)));
        }
    }

    emit accountsChanged();
}

QWidget *AccountPage::createAccountItem(const Account& account)
{
    using namespace DesignTokens;

    QWidget *item = new QWidget;
    QVBoxLayout *v = new QVBoxLayout(item);
    v->setContentsMargins(kPadding, kRowPadding + 2, kPadding, kRowPadding + 2);
    v->setSpacing(10);

    // Top: avatar | name, user and server, quota | status pill
    QHBoxLayout *top = new QHBoxLayout;
    top->setSpacing(kPadding - 4);

    QString name = AccountDisplay::displayName(account);
    QLabel *avatar = new QLabel;
    avatar->setFixedSize(kAvatarSize, kAvatarSize);
    avatar->setPixmap(AccountDisplay::avatar(name, kAvatarSize, devicePixelRatioF()));
    top->addWidget(avatar, 0, Qt::AlignTop);

    QVBoxLayout *texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(plainLabel(name, "rowTitleStrong"));

    QString detail = account.serverUrl.host();
    if (account.username != name) {
        detail = account.username + QString::fromUtf8(" · ") + detail;
    }
    texts->addWidget(plainLabel(detail, "rowDesc"));

    qint64 total = account.accountInfo.totalStorage;
    qint64 used = account.accountInfo.usedStorage;
    if (total > 0 && used >= 0) {
        texts->addWidget(plainLabel(tr("Spazio usato: %1 di %2")
                                        .arg(readableFileSize(used), readableFileSize(total)),
                                    "rowDesc"));
    }
    top->addLayout(texts, 1);

    AccountDisplay::Status st = AccountDisplay::status(account);
    QLabel *pill = makePill(AccountDisplay::statusText(st), AccountDisplay::statusPillKind(st));
    top->addWidget(pill, 0, Qt::AlignTop);
    v->addLayout(top);

    // Bottom: actions, aligned with the text column
    QHBoxLayout *actions = new QHBoxLayout;
    actions->setContentsMargins(kAvatarSize + kPadding - 4, 0, 0, 0);
    actions->setSpacing(8);

    if (st == AccountDisplay::StatusLoginRequired) {
        QPushButton *login = makeButton(tr("Accedi"), ButtonPrimary);
        login->setAccessibleName(tr("Accedi a %1").arg(account.serverUrl.host()));
        connect(login, &QPushButton::clicked, this, [this, account] { loginAccount(account); });
        actions->addWidget(login);
    }

    QPushButton *resync = makeButton(tr("Risincronizza"));
    resync->setAccessibleName(tr("Risincronizza l'account %1").arg(name));
    connect(resync, &QPushButton::clicked, this, [this, account] {
        busy_ = true;
        resyncAccount(account);
        busy_ = false;
    });
    actions->addWidget(resync);

    QPushButton *remove = makeButton(tr("Rimuovi"), ButtonDanger);
    remove->setAccessibleName(tr("Rimuovi l'account %1").arg(name));
    connect(remove, &QPushButton::clicked, this, [this, account] {
        busy_ = true;
        deleteAccount(account);
        busy_ = false;
    });
    actions->addWidget(remove);
    actions->addStretch(1);
    v->addLayout(actions);

    return item;
}

void AccountPage::loginAccount(const Account& account)
{
    gui->trayIcon()->showLoginDialog(account);
}

// Same checks and questions as SeafileTrayIcon::resyncAccount().
void AccountPage::resyncAccount(const Account& account)
{
    SeafileRpcClient *rpc_client = gui->rpcClient(account.domainID());
    if (!rpc_client || !rpc_client->isConnected()) {
        gui->warningBox(tr("Impossibile collegarsi al processo in background, riprova più tardi."), window());
        return;
    }

#ifndef Q_OS_LINUX
    if (rpc_client->isAccountUploading(account)) {
        gui->warningBox(tr("Ci sono modifiche in fase di caricamento per questo account, riprova più tardi."), window());
        return;
    }
#endif

#if defined(Q_OS_MACOS)
    QString question = tr("Vuoi risincronizzare l'account di \"%1\"? Dopo la risincronizzazione trovi ancora i file scaricati in ~/Library/CloudStorage.").arg(account.serverUrl.toString());
#else
    QString question = tr("Vuoi risincronizzare l'account di \"%1\"? I file scaricati e quelli in caricamento non verranno eliminati.").arg(account.serverUrl.toString());
#endif

    if (!gui->yesOrNoBox(question, window(), false)) {
        return;
    }

    gui->accountManager()->resyncAccount(account);
    rebuild();
}

// Same checks and questions as SeafileTrayIcon::deleteAccount().
void AccountPage::deleteAccount(const Account& account)
{
    SeafileRpcClient *rpc_client = gui->rpcClient(account.domainID());
#ifdef Q_OS_MACOS
    if (!rpc_client || !rpc_client->isConnected()) {
        gui->warningBox(tr("Impossibile collegarsi al processo in background, riprova più tardi."), window());
        return;
    }
#endif

#ifndef Q_OS_LINUX
    if (rpc_client && rpc_client->isAccountUploading(account)) {
        gui->warningBox(tr("Ci sono modifiche in fase di caricamento per questo account, riprova più tardi."), window());
        return;
    }
#endif

#if defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
    QString question = tr("Vuoi rimuovere l'account di \"%1\"? I file scaricati e quelli in caricamento non verranno eliminati.").arg(account.serverUrl.toString());
#else
    QString question = tr("Vuoi rimuovere l'account di \"%1\"? Dopo la rimozione trovi ancora i file scaricati in ~/Library/CloudStorage.").arg(account.serverUrl.toString());
#endif

    if (!gui->yesOrNoBox(question, window(), false)) {
        return;
    }

    gui->accountManager()->removeAccount(account);
    rebuild();
}
