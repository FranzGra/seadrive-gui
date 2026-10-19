#ifndef SEADRIVE_GUI_SYNC_PANEL_H
#define SEADRIVE_GUI_SYNC_PANEL_H

#include <QWidget>
#include <QDateTime>
#include <QList>
#include <QUrl>

#include "rpc/transfer-progress.h"
#include "rpc/sync-error.h"

class QLabel;
class QFrame;
class QHBoxLayout;
class QMenu;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QTimer;
class QIcon;
class QColor;

// One account as shown by the panel (filled by the tray icon from the
// account manager).
struct SyncPanelAccount {
    QString id;        // Account::getSignature(): remembers the choice
    QString name;      // display name
    QUrl server;
    QString username;
    QString root;      // sync root of the account

    bool operator==(const SyncPanelAccount& o) const {
        return id == o.id && name == o.name && server == o.server &&
               username == o.username && root == o.root;
    }
};

/**
 * The main popup of the client, OneDrive style: it opens with a left click
 * on the tray icon, right above the notification area, has no title bar and
 * hides itself as soon as it loses the focus.
 *
 * Usage (from the tray icon):
 *
 *     panel->togglePanel(geometry());   // QSystemTrayIcon::geometry()
 *
 * State is pushed from the outside (MessagePoller):
 *
 *   - setAccounts(QList<SyncPanelAccount>)     accounts for the header
 *   - setErrors(errors, networkErrors)         all accounts; the panel
 *                                              keeps those of the chosen one
 *   - setSyncing(bool, sentRate, recvRate)     rates in bytes/s
 *   - setTransfers(const TransferProgress&)    files in progress (bars)
 *
 * With one account the header reads "SeaDrive · name" as before. With two
 * or more the name becomes a selector (avatar, name, chevron): status box,
 * file list, "Open folder" and "Open online" follow the chosen account.
 *
 * Priority of the status box: paused > not connected > syncing > errors >
 * all synced. "Paused" comes from the daemon (SyncPauseManager).
 * The "Ultimi file" list reads RecentFilesStore::instance() and refreshes on
 * its changed() signal.
 */
class SyncPanel : public QWidget {
    Q_OBJECT
public:
    explicit SyncPanel(QWidget *parent = nullptr);

    // Shows the panel next to the tray icon. `trayGeometry` is
    // QSystemTrayIcon::geometry(); when empty the panel goes to the corner
    // of the screen where the taskbar is.
    void showPanel(const QRect& trayGeometry = QRect());
    // Shows the panel if hidden, hides it if visible. A click on the tray
    // icon while the panel is open first deactivates it (and hides it):
    // this call then does not reopen it.
    void togglePanel(const QRect& trayGeometry = QRect());

    // Fills `menu` with the "Pause for" entries (30 min, 2 h, 8 h, until
    // resumed). Shared with the tray icon menu.
    static void addPauseActions(QMenu *menu);

    // The chosen account (the first one if none was chosen yet). Empty id
    // when there is no account.
    SyncPanelAccount currentAccount() const;
    bool hasManyAccounts() const { return accounts_.size() >= 2; }

public slots:
    void setAccounts(const QList<SyncPanelAccount>& accounts);
    void setErrors(const QList<SyncError>& errors, const QList<SyncError>& networkErrors);
    void setSyncing(bool syncing, qint64 sentRate, qint64 recvRate);
    void setTransfers(const TransferProgress& progress);

signals:
    void addAccountRequested();
    void manageAccountsRequested();
    void settingsRequested();
    void openWebRequested();
    void openFolderRequested();
    void transferWindowRequested();
    void syncErrorsRequested();
    // Emitted when a row is clicked, with the absolute path of the file.
    // The panel already opens the containing folder itself.
    void fileActivated(const QString& absolutePath);

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void refreshStatus();
    void refreshList();
    void applyTheme();
    void tick();
    void onPauseButtonClicked();
    void onAccountButtonClicked();

private:
    Q_DISABLE_COPY(SyncPanel)

    void createUi();
    QWidget *createFileRow(const QString& path,
                           bool upload,
                           const QString& meta,
                           int progressPercent);
    QFrame *createFooterButton(int iconCode, const QString& text, int action);
    void triggerAction(int action);
    void positionNear(const QRect& trayGeometry);
    void clearList();
    void openFileFolder(const QString& path);
    QString absolutePath(const QString& path) const;
    QString rootPath() const;
    void giveFocusBackAfterMenu(bool chosen);

    // Accounts
    void selectAccount(const QString& id);
    void refreshAccountButton();
    bool belongsToCurrent(const QString& server, const QString& username) const;
    static bool belongsTo(const SyncPanelAccount& account,
                          const QString& server, const QString& username);
    int errorCountFor(const SyncPanelAccount& account) const;
    bool isConnected(const SyncPanelAccount& account) const;
    QWidget *createAccountMenuRow(const SyncPanelAccount& account, bool current);

    static QString relativeTime(const QDateTime& time);
    static QString formatRate(qint64 rate);
    static QString breakable(const QString& text);
    static QIcon faIcon(int code, const QColor& color);

    // Header
    QLabel *title_label_;
    QHBoxLayout *title_layout_ = nullptr;
    // Account selector, shown with two accounts or more.
    QToolButton *account_button_ = nullptr;
    QLabel *account_avatar_ = nullptr;
    QLabel *account_name_label_ = nullptr;
    QLabel *account_chevron_ = nullptr;
    QToolButton *settings_button_;
    QToolButton *web_button_ = nullptr;
    // Pause: opens the "Pause for" menu; while paused it resumes (play).
    QToolButton *pause_button_ = nullptr;

    // Status box
    QFrame *status_box_;
    QLabel *status_icon_;
    QLabel *status_title_;
    QLabel *status_detail_;
    QProgressBar *status_progress_;
    QPushButton *resume_button_;

    // Recent files
    QLabel *list_title_;
    QScrollArea *scroll_area_;
    QWidget *list_container_;
    QVBoxLayout *list_layout_;
    QLabel *empty_label_;

    // Footer
    QFrame *footer_;
    QFrame *open_folder_button_;
    QFrame *transfer_button_;

    QTimer *tick_timer_;

    QList<SyncPanelAccount> accounts_;
    QString current_id_;
    QList<SyncError> errors_;
    QList<SyncError> network_errors_;
    bool syncing_;
    qint64 sent_rate_;
    qint64 recv_rate_;
    TransferProgress transfers_;
    QDateTime last_check_;
    qint64 hidden_at_msec_;

    enum StatusKind { StatusOk, StatusSyncing, StatusErrors, StatusOffline, StatusPaused };
    StatusKind status_kind_;
};

#endif // SEADRIVE_GUI_SYNC_PANEL_H
