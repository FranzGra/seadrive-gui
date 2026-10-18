#ifndef SEADRIVE_GUI_ACCOUNT_PAGE_H
#define SEADRIVE_GUI_ACCOUNT_PAGE_H

#include <QPixmap>

#include "settings-page.h"
#include "account.h"

class QVBoxLayout;
class QTimer;

// Shared helpers for the account list and the sidebar header.
namespace AccountDisplay {

enum Status {
    StatusConnected = 0,     // logged in and the daemon is reachable
    StatusNotConnected,      // logged in but the daemon is not reachable yet
    StatusLoginRequired,     // token missing or expired
};

QString displayName(const Account& account);
QString initials(const QString& name);
Status status(const Account& account);
QString statusText(Status status);
QString statusPillKind(Status status);
// Round avatar with initials, painted with the design tokens.
QPixmap avatar(const QString& name, int size, qreal dpr);

} // namespace AccountDisplay

// "Account": every account with server, quota and status, plus the actions
// that used to live in the tray "Accounts" submenu (log in, resync, remove,
// add an account).
class AccountPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit AccountPage(QWidget *parent = nullptr);

    void load() override;
    void themeChanged() override;

signals:
    // Emitted after the list changed, so the sidebar header can refresh.
    void accountsChanged();

private slots:
    void refreshIfChanged();
    void rebuild();

private:
    QWidget *createAccountItem(const Account& account);
    QString signature() const;

    void loginAccount(const Account& account);
    void resyncAccount(const Account& account);
    void deleteAccount(const Account& account);

    QWidget *list_;
    QVBoxLayout *list_layout_;
    QPushButton *add_btn_;
    QString last_signature_;
    QTimer *refresh_timer_;
    // True while a confirmation dialog is open (see EncryptedReposPage).
    bool busy_;
};

#endif // SEADRIVE_GUI_ACCOUNT_PAGE_H
