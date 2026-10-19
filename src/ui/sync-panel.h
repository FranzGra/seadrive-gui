#ifndef SEADRIVE_GUI_SYNC_PANEL_H
#define SEADRIVE_GUI_SYNC_PANEL_H

#include <QWidget>
#include <QDateTime>
#include <QList>

#include "rpc/transfer-progress.h"

class QLabel;
class QFrame;
class QMenu;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QTimer;
class QIcon;
class QColor;

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
 *   - setConnected(bool)                       false -> grey "Non connesso"
 *   - setSyncing(bool, sentRate, recvRate)     rates in bytes/s
 *   - setErrorCount(int)                       > 0 -> yellow box
 *   - setTransfers(const TransferProgress&)    files in progress (bars)
 *   - setAccountName(QString)                  shown in the header
 *   - setRootPath(QString)                     SeaDrive root, used to open
 *                                              the folder of a clicked file
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

public slots:
    void setAccountName(const QString& name);
    void setRootPath(const QString& root);
    void setConnected(bool connected);
    void setSyncing(bool syncing, qint64 sentRate, qint64 recvRate);
    void setErrorCount(int count);
    void setTransfers(const TransferProgress& progress);

signals:
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

    static QString relativeTime(const QDateTime& time);
    static QString formatRate(qint64 rate);
    static QString breakable(const QString& text);
    static QIcon faIcon(int code, const QColor& color);

    // Header
    QLabel *title_label_;
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

    QString account_name_;
    QString root_path_;
    bool connected_;
    bool syncing_;
    qint64 sent_rate_;
    qint64 recv_rate_;
    int error_count_;
    TransferProgress transfers_;
    QDateTime last_check_;
    qint64 hidden_at_msec_;

    enum StatusKind { StatusOk, StatusSyncing, StatusErrors, StatusOffline, StatusPaused };
    StatusKind status_kind_;
};

#endif // SEADRIVE_GUI_SYNC_PANEL_H
