#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include "utils/utils.h"
#include "rpc/transfer-progress.h"

#include "recent-files-store.h"

namespace {

const char *kStoreFileName = "recent-files.json";
const int kStoreVersion = 1;
const int kSaveDelayMSecs = 2000;

QString normalizedPath(const QString& path)
{
    QString p = path;
    p.replace('\\', '/');
    while (p.endsWith('/')) {
        p.chop(1);
    }
    return p;
}

QString parentOf(const QString& path)
{
    int pos = path.lastIndexOf('/');
    return pos > 0 ? path.left(pos) : QString();
}

QString directionToString(RecentFileEntry::Direction dir)
{
    return dir == RecentFileEntry::Upload ? QStringLiteral("upload")
                                          : QStringLiteral("download");
}

} // namespace

RecentFilesStore* RecentFilesStore::instance()
{
    static RecentFilesStore *store = nullptr;
    if (!store) {
        store = new RecentFilesStore(qApp);
    }
    return store;
}

RecentFilesStore::RecentFilesStore(QObject *parent)
    : QObject(parent),
      first_update_(true)
{
    save_timer_ = new QTimer(this);
    save_timer_->setSingleShot(true);
    save_timer_->setInterval(kSaveDelayMSecs);
    connect(save_timer_, &QTimer::timeout, this, &RecentFilesStore::save);

    // Make sure pending changes are written when the application quits.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (save_timer_->isActive()) {
            save_timer_->stop();
            save();
        }
    });

    load();
}

QString RecentFilesStore::storePath() const
{
    return QDir(seadriveInternalDir()).filePath(kStoreFileName);
}

QList<RecentFileEntry> RecentFilesStore::entries() const
{
    QList<RecentFileEntry> sorted = entries_;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const RecentFileEntry& a, const RecentFileEntry& b) {
                         return a.time > b.time;
                     });
    return sorted;
}

void RecentFilesStore::addEntry(const RecentFileEntry& e)
{
    RecentFileEntry entry = e;
    entry.path = normalizedPath(entry.path);
    if (entry.path.isEmpty()) {
        return;
    }
    if (entry.folder.isEmpty()) {
        entry.folder = parentOf(entry.path);
    }
    if (!entry.time.isValid()) {
        entry.time = QDateTime::currentDateTime();
    }

    // The same file transferred again moves to the top instead of being
    // listed twice.
    for (int i = 0; i < entries_.size(); i++) {
        if (entries_[i].sameFile(entry)) {
            entries_.removeAt(i);
            break;
        }
    }

    entries_.prepend(entry);
    while (entries_.size() > kMaxEntries) {
        entries_.removeLast();
    }

    scheduleSave();
    emit changed();
}

bool RecentFilesStore::containsFile(const RecentFileEntry& file) const
{
    for (const RecentFileEntry& entry : entries_) {
        if (entry.sameFile(file)) {
            return true;
        }
    }
    return false;
}

void RecentFilesStore::processList(const QList<TransferredInfo>& files,
                                   RecentFileEntry::Direction dir,
                                   QSet<QString> *previous,
                                   bool *added)
{
    QSet<QString> current;
    const QDateTime now = QDateTime::currentDateTime();

    // The daemon lists the most recent file first: walk the list backwards
    // so that, after prepending, the newest one ends up on top.
    for (int i = files.size() - 1; i >= 0; i--) {
        const TransferredInfo& info = files.at(i);
        const QString path = normalizedPath(info.file_path);
        // Same path in two accounts = two different files.
        const QString key = info.server + QLatin1Char('|') + info.username +
                            QLatin1Char('|') + path;
        if (path.isEmpty() || current.contains(key)) {
            continue;
        }
        current.insert(key);

        if (previous->contains(key)) {
            continue;
        }

        RecentFileEntry entry;
        entry.path = path;
        entry.folder = parentOf(path);
        entry.direction = dir;
        entry.time = now;
        entry.size = 0;
        entry.server = info.server;
        entry.username = info.username;

        if (first_update_ && containsFile(entry)) {
            continue;
        }

        for (int j = 0; j < entries_.size(); j++) {
            if (entries_[j].sameFile(entry)) {
                entries_.removeAt(j);
                break;
            }
        }
        entries_.prepend(entry);
        *added = true;
    }

    *previous = current;
}

void RecentFilesStore::updateFromTransferProgress(const TransferProgress& up,
                                                  const TransferProgress& down)
{
    bool added = false;
    processList(up.uploaded_files, RecentFileEntry::Upload, &last_uploaded_, &added);
    processList(down.downloaded_files, RecentFileEntry::Download, &last_downloaded_, &added);
    first_update_ = false;

    if (added) {
        while (entries_.size() > kMaxEntries) {
            entries_.removeLast();
        }
        scheduleSave();
        emit changed();
    }
}

void RecentFilesStore::updateFromTransferProgress(const TransferProgress& progress)
{
    updateFromTransferProgress(progress, progress);
}

void RecentFilesStore::clear()
{
    if (entries_.isEmpty()) {
        return;
    }
    entries_.clear();
    scheduleSave();
    emit changed();
}

void RecentFilesStore::scheduleSave()
{
    save_timer_->start();
}

void RecentFilesStore::load()
{
    QFile file(storePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("recent files: unable to parse %s: %s",
                 toCStr(storePath()), toCStr(error.errorString()));
        return;
    }

    const QJsonArray array = doc.object().value("entries").toArray();
    for (const QJsonValue& value : array) {
        const QJsonObject obj = value.toObject();
        RecentFileEntry entry;
        entry.path = normalizedPath(obj.value("path").toString());
        if (entry.path.isEmpty()) {
            continue;
        }
        entry.folder = obj.value("folder").toString();
        if (entry.folder.isEmpty()) {
            entry.folder = parentOf(entry.path);
        }
        entry.direction = obj.value("direction").toString() == "download"
                              ? RecentFileEntry::Download
                              : RecentFileEntry::Upload;
        entry.time = QDateTime::fromMSecsSinceEpoch(
            (qint64)obj.value("time").toDouble());
        entry.size = (qint64)obj.value("size").toDouble();
        entry.server = obj.value("server").toString();
        entry.username = obj.value("username").toString();
        entries_.append(entry);
        if (entries_.size() >= kMaxEntries) {
            break;
        }
    }
}

void RecentFilesStore::save()
{
    QJsonArray array;
    for (const RecentFileEntry& entry : entries_) {
        QJsonObject obj;
        obj.insert("path", entry.path);
        obj.insert("folder", entry.folder);
        obj.insert("direction", directionToString(entry.direction));
        obj.insert("time", (double)entry.time.toMSecsSinceEpoch());
        obj.insert("size", (double)entry.size);
        if (!entry.server.isEmpty()) {
            obj.insert("server", entry.server);
            obj.insert("username", entry.username);
        }
        array.append(obj);
    }

    QJsonObject root;
    root.insert("version", kStoreVersion);
    root.insert("entries", array);

    QDir().mkpath(seadriveInternalDir());

    QSaveFile file(storePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("recent files: unable to write %s", toCStr(storePath()));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qWarning("recent files: unable to commit %s", toCStr(storePath()));
    }
}
