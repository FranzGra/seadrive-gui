#ifndef SEADRIVE_GUI_SYNC_PAUSE_H
#define SEADRIVE_GUI_SYNC_PAUSE_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>

#include <jansson.h>

class QTimer;
class SeafileRpcClient;

/**
 * Pause / resume of the sync ("Sospendi per 30 min / 2 h / 8 h / finché non
 * riprendo").
 *
 * The daemon has the switch (seafile_disable_auto_sync /
 * seafile_enable_auto_sync) but forgets it when it restarts. The client
 * therefore remembers the pause in QSettings (end time, or "until resumed")
 * and:
 *   - resumes by itself when the end time is reached (timer, and a check at
 *     every status poll, so a sleeping PC resumes on wake-up);
 *   - pauses the daemon again when it comes back running while the pause is
 *     still valid (daemon restart, reconnection).
 *
 * What the UI shows comes from the daemon ("auto_sync_enabled" in the
 * global sync status, pushed here by MessagePoller), not from the local
 * flag alone.
 */
class SyncPauseManager : public QObject {
    Q_OBJECT
public:
    static SyncPauseManager *instance();

    // minutes <= 0: until the user resumes.
    void pauseFor(int minutes);
    void resume();

    // True once the daemon reported its state at least once.
    bool isStateKnown() const { return !engine_enabled_.isEmpty(); }
    // The daemon reports that the sync is paused.
    bool isPaused() const;
    // End of the pause planned by this client (invalid when the pause has no
    // end or was not started by this client).
    QDateTime resumeTime() const;

    // Called by MessagePoller with "auto_sync_enabled" of each daemon.
    void updateFromEngine(const QString& domain_id, bool auto_sync_enabled);

    // "auto_sync_enabled" of a global sync status object: 1 enabled,
    // 0 paused, -1 field missing.
    static int autoSyncFromJson(const json_t *status);

signals:
    void changed();

private:
    explicit SyncPauseManager(QObject *parent = nullptr);
    Q_DISABLE_COPY(SyncPauseManager)

    // A pause is stored and its end (if any) is still in the future.
    bool wantPaused() const;
    void setStoredUntil(qint64 until_msec);
    void scheduleResumeTimer();
    bool applyToEngine(bool enabled);
    void refreshFromEngine();
    QList<SeafileRpcClient *> clients() const;

    // 0: no pause; -1: until resumed; > 0: end, msecs since epoch.
    qint64 until_msec_;
    // The last resume failed (daemon not ready): try again at the next poll.
    bool resume_pending_;
    qint64 last_apply_msec_;
    QHash<QString, bool> engine_enabled_;
    QTimer *resume_timer_;
};

#endif // SEADRIVE_GUI_SYNC_PAUSE_H
