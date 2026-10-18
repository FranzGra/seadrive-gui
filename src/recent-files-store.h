#ifndef SEADRIVE_GUI_RECENT_FILES_STORE_H
#define SEADRIVE_GUI_RECENT_FILES_STORE_H

#include <QObject>
#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>

class QTimer;
class TransferProgress;

/**
 * One file recently uploaded to / downloaded from the server.
 *
 * `path` is the path as reported by the daemon (relative to the SeaDrive
 * root, e.g. "My Library/docs/report.pdf"); `folder` is its parent path in
 * the same form ("My Library/docs"). `size` is 0 when unknown (the daemon
 * does not report the size of finished transfers).
 */
struct RecentFileEntry {
    enum Direction { Upload, Download };

    QString path;
    QString folder;
    Direction direction = Upload;
    QDateTime time;
    qint64 size = 0;
};

/**
 * Keeps the history of the last transferred files (max 200, newest first)
 * and persists it to `<seadriveInternalDir>/recent-files.json`, so that the
 * history survives a restart of the client.
 */
class RecentFilesStore : public QObject {
    Q_OBJECT
public:
    static RecentFilesStore* instance();

    static const int kMaxEntries = 200;

    void addEntry(const RecentFileEntry& e);
    // Newest first (sorted by time, most recent on top).
    QList<RecentFileEntry> entries() const;

    // Adds the files listed in `up.uploaded_files` and
    // `down.downloaded_files` that were not there at the previous call,
    // with time = now. On the first call after start, a file is skipped if
    // the history already contains it (the daemon keeps its own rolling
    // list, so it would otherwise be added again at every restart).
    void updateFromTransferProgress(const TransferProgress& up,
                                    const TransferProgress& down);

    // Same as above, for a TransferProgress filled by
    // TransferProgress::fromJSON(upload, download, progress), which already
    // contains both lists.
    void updateFromTransferProgress(const TransferProgress& progress);

    void clear();

signals:
    void changed();

private slots:
    void save();

private:
    explicit RecentFilesStore(QObject *parent = nullptr);
    Q_DISABLE_COPY(RecentFilesStore)

    void load();
    void scheduleSave();
    QString storePath() const;
    bool containsPath(const QString& path, RecentFileEntry::Direction dir) const;
    void processList(const QStringList& paths,
                     RecentFileEntry::Direction dir,
                     QSet<QString> *previous,
                     bool *added);

    QList<RecentFileEntry> entries_;
    QSet<QString> last_uploaded_;
    QSet<QString> last_downloaded_;
    bool first_update_;
    QTimer *save_timer_;
};

#endif // SEADRIVE_GUI_RECENT_FILES_STORE_H
