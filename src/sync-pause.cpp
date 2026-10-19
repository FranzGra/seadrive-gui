#include <QCoreApplication>
#include <QSet>
#include <QSettings>
#include <QTimer>

#include "utils/utils.h"
#include "seadrive-gui.h"
#include "account-mgr.h"
#include "rpc/rpc-client.h"

#include "sync-pause.h"

namespace {

const char *kSettingsGroup = "SyncPause";
const char *kUntilKey = "until";
// Do not hammer a daemon that refuses the call (e.g. still starting).
const qint64 kRetryIntervalMSecs = 3000;
// QTimer takes an int: wake up at most every 12 hours and check again.
const qint64 kMaxTimerMSecs = 12LL * 3600 * 1000;

} // namespace

SyncPauseManager *SyncPauseManager::instance()
{
    static SyncPauseManager *manager = nullptr;
    if (!manager) {
        manager = new SyncPauseManager(qApp);
    }
    return manager;
}

SyncPauseManager::SyncPauseManager(QObject *parent)
    : QObject(parent),
      until_msec_(0),
      resume_pending_(false),
      last_apply_msec_(0)
{
    resume_timer_ = new QTimer(this);
    resume_timer_->setSingleShot(true);
    connect(resume_timer_, &QTimer::timeout, this, [this]() {
        if (until_msec_ > 0 && QDateTime::currentMSecsSinceEpoch() >= until_msec_) {
            qWarning("sync pause: the pause is over, resuming");
            resume();
        } else {
            scheduleResumeTimer();
        }
    });

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    until_msec_ = settings.value(kUntilKey, 0).toLongLong();
    settings.endGroup();

    if (until_msec_ != 0) {
        qWarning("sync pause: remembered pause %s",
                 until_msec_ < 0 ? "until resumed"
                                 : toCStr(QDateTime::fromMSecsSinceEpoch(until_msec_)
                                              .toString("dd-MM-yyyy HH:mm")));
    }
    scheduleResumeTimer();
}

int SyncPauseManager::autoSyncFromJson(const json_t *status)
{
    if (!status) {
        return -1;
    }
    json_t *value = json_object_get(status, "auto_sync_enabled");
    if (!value) {
        return -1;
    }
    if (json_is_boolean(value)) {
        return json_is_true(value) ? 1 : 0;
    }
    if (json_is_integer(value)) {
        return json_integer_value(value) != 0 ? 1 : 0;
    }
    return -1;
}

bool SyncPauseManager::isPaused() const
{
    for (bool enabled : engine_enabled_) {
        if (!enabled) {
            return true;
        }
    }
    return false;
}

QDateTime SyncPauseManager::resumeTime() const
{
    if (until_msec_ > 0) {
        return QDateTime::fromMSecsSinceEpoch(until_msec_);
    }
    return QDateTime();
}

bool SyncPauseManager::wantPaused() const
{
    if (until_msec_ < 0) {
        return true;
    }
    return until_msec_ > 0 && QDateTime::currentMSecsSinceEpoch() < until_msec_;
}

void SyncPauseManager::setStoredUntil(qint64 until_msec)
{
    until_msec_ = until_msec;
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    if (until_msec == 0) {
        settings.remove(kUntilKey);
    } else {
        settings.setValue(kUntilKey, until_msec);
    }
    settings.endGroup();
    scheduleResumeTimer();
}

void SyncPauseManager::scheduleResumeTimer()
{
    resume_timer_->stop();
    if (until_msec_ <= 0) {
        return;
    }
    qint64 left = until_msec_ - QDateTime::currentMSecsSinceEpoch();
    resume_timer_->start(int(qBound<qint64>(0, left, kMaxTimerMSecs)));
}

void SyncPauseManager::pauseFor(int minutes)
{
    qint64 until = -1;
    if (minutes > 0) {
        until = QDateTime::currentMSecsSinceEpoch() + qint64(minutes) * 60 * 1000;
    }
    setStoredUntil(until);
    resume_pending_ = false;
    qWarning("sync pause: pausing %s",
             until < 0 ? "until resumed"
                       : toCStr(QString("for %1 minutes, until %2")
                                    .arg(minutes)
                                    .arg(QDateTime::fromMSecsSinceEpoch(until)
                                             .toString("dd-MM-yyyy HH:mm"))));
    applyToEngine(false);
    refreshFromEngine();
    emit changed();
}

void SyncPauseManager::resume()
{
    setStoredUntil(0);
    qWarning("sync pause: resuming");
    resume_pending_ = !applyToEngine(true);
    refreshFromEngine();
    emit changed();
}

QList<SeafileRpcClient *> SyncPauseManager::clients() const
{
    // One daemon on Windows and Linux (EMPTY_DOMAIN_ID), one per account on
    // macOS.
    QList<SeafileRpcClient *> list;
    if (!gui) {
        return list;
    }
    QSet<QString> domains;
    domains.insert(EMPTY_DOMAIN_ID);
    const auto accounts = gui->accountManager()->activeAccounts();
    for (const Account& account : accounts) {
        domains.insert(account.domainID());
    }
    for (const QString& domain : domains) {
        SeafileRpcClient *rpc = gui->rpcClient(domain);
        if (rpc && rpc->isConnected()) {
            list.append(rpc);
        }
    }
    return list;
}

bool SyncPauseManager::applyToEngine(bool enabled)
{
    last_apply_msec_ = QDateTime::currentMSecsSinceEpoch();
    const QList<SeafileRpcClient *> list = clients();
    bool ok = !list.isEmpty();
    for (SeafileRpcClient *rpc : list) {
        if (!rpc->setAutoSync(enabled)) {
            ok = false;
        }
    }
    if (!ok) {
        qWarning("sync pause: unable to %s the sync now, will retry",
                 enabled ? "resume" : "pause");
    }
    return ok;
}

void SyncPauseManager::refreshFromEngine()
{
    // Read the state back right away, so the panel does not wait for the
    // next poll to show it.
    bool any_changed = false;
    for (SeafileRpcClient *rpc : clients()) {
        json_t *ret = nullptr;
        if (!rpc->getGlobalSyncStatus(&ret)) {
            continue;
        }
        int state = autoSyncFromJson(ret);
        json_decref(ret);
        if (state < 0) {
            continue;
        }
        bool enabled = state == 1;
        if (!engine_enabled_.contains(rpc->domainID()) ||
            engine_enabled_.value(rpc->domainID()) != enabled) {
            engine_enabled_.insert(rpc->domainID(), enabled);
            any_changed = true;
        }
    }
    if (any_changed) {
        emit changed();
    }
}

void SyncPauseManager::updateFromEngine(const QString& domain_id, bool auto_sync_enabled)
{
    const bool known = engine_enabled_.contains(domain_id);
    const bool was = engine_enabled_.value(domain_id, true);
    engine_enabled_.insert(domain_id, auto_sync_enabled);
    if (!known || was != auto_sync_enabled) {
        qWarning("sync pause: daemon reports auto sync %s",
                 auto_sync_enabled ? "enabled" : "disabled");
        emit changed();
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // The pause is over (e.g. the PC was asleep when the timer was due).
    if (until_msec_ > 0 && now >= until_msec_) {
        qWarning("sync pause: the pause is over, resuming");
        resume();
        return;
    }

    if (now - last_apply_msec_ < kRetryIntervalMSecs) {
        return;
    }

    if (wantPaused() && auto_sync_enabled) {
        // The daemon restarted (it forgets the pause) or did not accept
        // the call yet: pause it again.
        qWarning("sync pause: daemon is syncing during a pause, pausing it again");
        if (applyToEngine(false)) {
            refreshFromEngine();
        }
    } else if (!wantPaused() && resume_pending_ && !auto_sync_enabled) {
        resume_pending_ = !applyToEngine(true);
        refreshFromEngine();
    } else if (!wantPaused() && resume_pending_ && auto_sync_enabled) {
        resume_pending_ = false;
    }
}
