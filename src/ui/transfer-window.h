#ifndef SEADRIVE_GUI_TRANSFER_WINDOW_H
#define SEADRIVE_GUI_TRANSFER_WINDOW_H

// "Stato del trasferimento": one window that replaces TransferProgressDialog
// and SyncErrorsDialog. It shows files being transferred (polled from the
// daemon once a second while the window is visible), recently completed files
// (from RecentFilesStore) and sync errors, as a list of rows that adapt to the
// window width and wrap long text instead of truncating it.

#include <QWidget>
#include <QList>
#include <QString>
#include <QIcon>

#include "rpc/sync-error.h"

class QTimer;
class QLabel;
class QLineEdit;
class QPushButton;
class QButtonGroup;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class QTextLayout;

// Data for one row of the list.
struct TransferRowData {
    enum Kind {
        Downloading = 0,
        Uploading,
        Downloaded,
        Uploaded,
        Error,
    };

    Kind kind = Downloading;
    QString key;          // stable id used to reuse row widgets
    QString name;         // file name (bold)
    QString folder;       // folder shown under the name
    QString status;       // "Ricezione · 60%", "Inviato · 5 min fa", error text
    QString detail;       // size or time, second line on the right
    QString pill;         // "Inviato" / "Ricevuto" for completed files
    int progress = -1;    // 0..100 for files in progress, -1 otherwise
    qint64 sort_time = 0; // msecs since epoch, newest first

    // Used to find the local file on double click.
    QString file_path;    // absolute, or relative to the account sync root
    QString server;
    QString username;
    SyncError error;      // only for Kind == Error

    bool isInProgress() const { return kind == Downloading || kind == Uploading; }
    bool isUpload() const { return kind == Uploading || kind == Uploaded; }
};


// One painted row. Text wraps (at word boundaries, or anywhere for very long
// names) and the row height follows the available width.
class TransferRow : public QWidget
{
    Q_OBJECT
public:
    TransferRow(QWidget *parent = 0);

    void setData(const TransferRowData& data);
    const TransferRowData& data() const { return data_; }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated(TransferRow *row);
    void contextMenuRequested(TransferRow *row, const QPoint& global_pos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct Geometry {
        bool stacked = false;
        QRectF badge;
        QRectF pill;
        QRectF name;
        QRectF folder;
        QRectF status;
        QRectF detail;
        QRectF bar;
        int height = 0;
    };
    Geometry computeGeometry(int width) const;
    QString stackedStatus() const;
    int iconForKind() const;

    TransferRowData data_;
    bool hovered_;
};


class TransferWindow : public QWidget
{
    Q_OBJECT
public:
    enum Filter {
        FilterAll = 0,
        FilterInProgress,
        FilterUploaded,
        FilterDownloaded,
        FilterErrors,
    };

    TransferWindow(QWidget *parent = 0);

    // Show the window with the given filter selected.
    void showWithFilter(Filter filter);
    void setFilter(Filter filter);

    // Same meaning as in the old SyncErrorsDialog (used by the tray icon to
    // decide whether to show the "sync errors" state).
    qint64 getLastOpenSyncDialogTimestamp() const;

public slots:
    void refresh();
    void updateErrors();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onFilterClicked(int id);
    void onSearchChanged(const QString& text);
    void onRowActivated(TransferRow *row);
    void onRowContextMenu(TransferRow *row, const QPoint& global_pos);
    void onColorSchemeChanged();
    void onRecentFilesChanged();

private:
    void createHeader(QVBoxLayout *layout);
    void createFilters(QVBoxLayout *layout);
    void createList(QVBoxLayout *layout);
    void applyStyle();

    void pollTransfers();
    void pollRates();
    void rebuildRows();
    void updateFilterLabels();
    void updateEmptyView();
    void markErrorsSeen();

    QList<TransferRowData> collectRows() const;
    bool matchesFilter(const TransferRowData& row) const;
    QString localPathForRow(const TransferRowData& row, QString *root = nullptr) const;
    QString localPathForError(const TransferRowData& row, QString *root) const;
    void openFolderOfRow(const TransferRowData& row);
    void deleteSyncError(const SyncError& error);

    // UI
    QLabel *title_label_;
    QLabel *rate_label_;
    QButtonGroup *filter_group_;
    QPushButton *filter_buttons_[5];
    QLineEdit *search_edit_;
    QStackedWidget *stack_;
    QScrollArea *scroll_area_;
    QWidget *list_widget_;
    QVBoxLayout *list_layout_;
    QList<TransferRow*> rows_;
    QWidget *empty_view_;
    QLabel *empty_icon_;
    QLabel *empty_title_;
    QLabel *empty_text_;
    QLabel *help_link_;

    // State
    QTimer *poll_timer_;
    Filter filter_;
    QString search_text_;
    QList<TransferRowData> in_progress_;
    QList<SyncError> errors_;
    qint64 up_rate_;
    qint64 down_rate_;
    qint64 last_open_sync_error_timestamp_;
};

#endif // SEADRIVE_GUI_TRANSFER_WINDOW_H
