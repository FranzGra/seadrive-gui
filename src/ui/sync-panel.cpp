#include <QtGlobal>
#include <climits>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#if defined(Q_OS_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "QtAwesome.h"
#include "utils/utils.h"
#include "recent-files-store.h"
#include "design-tokens.h"
#include "glyph-icon.h"

#include "sync-panel.h"

using namespace DesignTokens;

namespace {

const int kPanelWidth = 360;
const int kPanelHeight = 540;
const int kScreenMargin = 12;
const int kMaxRecentRows = 8;
const int kMaxActiveRows = 5;
const int kTickIntervalMSecs = 30 * 1000;
// A click on the tray icon while the panel is open deactivates the panel
// (which hides it) right before the click reaches the tray icon.
const int kReopenGuardMSecs = 350;

const char *kPropFilePath = "sp_file_path";
const char *kPropAction = "sp_action";

enum FooterAction {
    ActionOpenFolder = 1,
    ActionTransfers,
    ActionSettings,
};

int toPercent(quint64 done, quint64 total)
{
    if (total == 0) {
        return 0;
    }
    return (int)qBound<quint64>(0, done * 100 / total, 100);
}

QString px(int size)
{
    return QString::number(size) + "px";
}

QString lastComponent(const QString& path)
{
    int pos = path.lastIndexOf('/');
    return pos >= 0 ? path.mid(pos + 1) : path;
}

} // namespace

SyncPanel::SyncPanel(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      connected_(true),
      syncing_(false),
      sent_rate_(0),
      recv_rate_(0),
      error_count_(0),
      last_check_(QDateTime::currentDateTime()),
      hidden_at_msec_(0),
      status_kind_(StatusOk)
{
    setObjectName("syncPanel");
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(tr("SeaDrive"));
    setFixedSize(kPanelWidth, kPanelHeight);

    createUi();

    tick_timer_ = new QTimer(this);
    tick_timer_->setInterval(kTickIntervalMSecs);
    connect(tick_timer_, &QTimer::timeout, this, &SyncPanel::tick);

    connect(RecentFilesStore::instance(), &RecentFilesStore::changed,
            this, &SyncPanel::refreshList);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &SyncPanel::applyTheme);

    applyTheme();
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void SyncPanel::createUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    // 1px left for the border painted in paintEvent().
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    // --- Header ------------------------------------------------------------
    QWidget *header = new QWidget(this);
    QHBoxLayout *header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(kPadding + 4, kPadding, kPadding - 4, 8);
    header_layout->setSpacing(8);

    title_label_ = new QLabel(header);
    title_label_->setObjectName("spTitle");
    title_label_->setTextFormat(Qt::RichText);
    title_label_->setWordWrap(true);
    header_layout->addWidget(title_label_, 1);

    // "Open online": opens the web interface of the account in the browser.
    web_button_ = new QToolButton(header);
    web_button_->setObjectName("spIconButton");
    web_button_->setToolTip(tr("Open SeaDrive in the browser"));
    web_button_->setAccessibleName(tr("Open SeaDrive in the browser"));
    web_button_->setCursor(Qt::PointingHandCursor);
    web_button_->setFixedSize(36, 36);
    web_button_->setIconSize(QSize(kIconSize, kIconSize));
    web_button_->setAutoRaise(true);
    connect(web_button_, &QToolButton::clicked, this, [this]() {
        hide();
        emit openWebRequested();
    });
    header_layout->addWidget(web_button_, 0, Qt::AlignTop);

    settings_button_ = new QToolButton(header);
    settings_button_->setObjectName("spIconButton");
    settings_button_->setToolTip(tr("Settings"));
    settings_button_->setAccessibleName(tr("Settings"));
    settings_button_->setCursor(Qt::PointingHandCursor);
    settings_button_->setFixedSize(36, 36);
    settings_button_->setIconSize(QSize(kIconSize, kIconSize));
    settings_button_->setAutoRaise(true);
    connect(settings_button_, &QToolButton::clicked, this, [this]() {
        hide();
        emit settingsRequested();
    });
    header_layout->addWidget(settings_button_, 0, Qt::AlignTop);
    root->addWidget(header);

    // --- Status box ---------------------------------------------------------
    QWidget *status_wrap = new QWidget(this);
    QVBoxLayout *status_wrap_layout = new QVBoxLayout(status_wrap);
    status_wrap_layout->setContentsMargins(kPadding, 4, kPadding, kPadding);

    status_box_ = new QFrame(status_wrap);
    status_box_->setObjectName("spStatusBox");
    status_box_->installEventFilter(this);
    QHBoxLayout *status_layout = new QHBoxLayout(status_box_);
    status_layout->setContentsMargins(kPadding, 14, kPadding, 14);
    status_layout->setSpacing(kRowPadding);

    status_icon_ = new QLabel(status_box_);
    status_icon_->setFixedSize(kStatusBadge, kStatusBadge);
    status_layout->addWidget(status_icon_, 0, Qt::AlignVCenter);

    QVBoxLayout *status_text = new QVBoxLayout;
    status_text->setSpacing(4);
    status_title_ = new QLabel(status_box_);
    status_title_->setObjectName("spStatusTitle");
    status_title_->setWordWrap(true);
    status_title_->setTextFormat(Qt::PlainText);
    status_detail_ = new QLabel(status_box_);
    status_detail_->setObjectName("spStatusDetail");
    status_detail_->setWordWrap(true);
    status_detail_->setTextFormat(Qt::PlainText);
    status_progress_ = new QProgressBar(status_box_);
    status_progress_->setObjectName("spProgress");
    status_progress_->setTextVisible(false);
    status_progress_->setFixedHeight(6);
    status_progress_->hide();
    status_text->addWidget(status_title_);
    status_text->addWidget(status_detail_);
    status_text->addSpacing(2);
    status_text->addWidget(status_progress_);
    status_layout->addLayout(status_text, 1);

    status_wrap_layout->addWidget(status_box_);
    root->addWidget(status_wrap);

    // --- Recent files -------------------------------------------------------
    list_title_ = new QLabel(tr("Recent files"), this);
    list_title_->setObjectName("spSectionTitle");
    list_title_->setContentsMargins(kPadding + 4, 0, kPadding, 6);
    root->addWidget(list_title_);

    scroll_area_ = new QScrollArea(this);
    scroll_area_->setObjectName("spScroll");
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area_->viewport()->setAutoFillBackground(false);

    list_container_ = new QWidget;
    list_container_->setObjectName("spListContainer");
    list_container_->setAutoFillBackground(false);
    list_layout_ = new QVBoxLayout(list_container_);
    list_layout_->setContentsMargins(8, 0, 8, 8);
    list_layout_->setSpacing(2);

    empty_label_ = new QLabel(tr("No recently transferred files.\n"
                                 "Files you upload or download will appear here."),
                              list_container_);
    empty_label_->setObjectName("spEmpty");
    empty_label_->setWordWrap(true);
    empty_label_->setAlignment(Qt::AlignCenter);
    empty_label_->setContentsMargins(kPadding, 24, kPadding, 24);
    list_layout_->addWidget(empty_label_);
    list_layout_->addStretch(1);

    scroll_area_->setWidget(list_container_);
    root->addWidget(scroll_area_, 1);

    // --- Footer -------------------------------------------------------------
    footer_ = new QFrame(this);
    footer_->setObjectName("spFooter");
    QHBoxLayout *footer_layout = new QHBoxLayout(footer_);
    footer_layout->setContentsMargins(8, 8, 8, 10);
    footer_layout->setSpacing(4);

    open_folder_button_ = createFooterButton(icon_folder_open,
                                             tr("Open folder"),
                                             ActionOpenFolder);
    transfer_button_ = createFooterButton(icon_exchange,
                                          tr("Transfer progress"),
                                          ActionTransfers);
    footer_layout->addWidget(open_folder_button_, 1);
    footer_layout->addWidget(transfer_button_, 1);
    root->addWidget(footer_);
}

QFrame *SyncPanel::createFooterButton(int iconCode, const QString& text, int action)
{
    QFrame *button = new QFrame(this);
    button->setObjectName("spFooterButton");
    button->setProperty(kPropAction, action);
    button->setProperty("sp_icon", iconCode);
    button->setAttribute(Qt::WA_Hover);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setAccessibleName(text);
    button->installEventFilter(this);

    // Icon and text side by side, text on a single line (no wrapping).
    QHBoxLayout *layout = new QHBoxLayout(button);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addStretch(1);

    QLabel *icon = new QLabel(button);
    icon->setObjectName("spFooterIcon");
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(icon);

    QLabel *label = new QLabel(text, button);
    label->setObjectName("spFooterLabel");
    label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    label->setWordWrap(false);
    label->setTextFormat(Qt::PlainText);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(label);
    layout->addStretch(1);

    return button;
}

QWidget *SyncPanel::createFileRow(const QString& path,
                                  bool upload,
                                  const QString& meta,
                                  int progressPercent)
{
    QFrame *row = new QFrame(list_container_);
    row->setObjectName("spFileRow");
    row->setProperty(kPropFilePath, path);
    row->setAttribute(Qt::WA_Hover);
    row->setCursor(Qt::PointingHandCursor);
    row->setFocusPolicy(Qt::StrongFocus);
    row->setToolTip(tr("Open the folder containing the file\n%1")
                        .arg(QDir::toNativeSeparators(path)));
    row->installEventFilter(this);

    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(kRowPadding, 10, kRowPadding, 10);
    layout->setSpacing(kRowPadding);

    QLabel *icon = new QLabel(row);
    icon->setFixedSize(kStatusBadge, kStatusBadge);
    icon->setAlignment(Qt::AlignCenter);
    // Direction always visible: arrow up (blue) = sent, arrow down (teal) =
    // received, while in progress and once completed.
    icon->setPixmap(GlyphIcon::pixmap(upload ? icon_arrow_up : icon_arrow_down,
                                      upload ? sentText() : receivedText(),
                                      devicePixelRatioF(), kStatusBadge, kStatusGlyph,
                                      upload ? sentBg() : receivedBg(), kRadius));
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(icon, 0, Qt::AlignTop);

    QVBoxLayout *text = new QVBoxLayout;
    text->setSpacing(3);

    QLabel *name = new QLabel(breakable(lastComponent(path)), row);
    name->setObjectName("spFileName");
    name->setTextFormat(Qt::PlainText);
    name->setWordWrap(true);
    name->setAttribute(Qt::WA_TransparentForMouseEvents);
    text->addWidget(name);

    QLabel *meta_label = new QLabel(breakable(meta), row);
    meta_label->setObjectName("spFileMeta");
    meta_label->setTextFormat(Qt::PlainText);
    meta_label->setWordWrap(true);
    meta_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    if (progressPercent < 0) {
        // Completed file: "Uploaded" / "Downloaded" pill, same color as the arrow.
        QHBoxLayout *meta_line = new QHBoxLayout;
        meta_line->setSpacing(8);
        QLabel *pill = new QLabel(upload ? tr("Uploaded") : tr("Downloaded"), row);
        pill->setObjectName(upload ? "spPillSent" : "spPillReceived");
        pill->setFixedHeight(kPillHeight);
        pill->setAlignment(Qt::AlignCenter);
        pill->setAttribute(Qt::WA_TransparentForMouseEvents);
        meta_line->addWidget(pill, 0, Qt::AlignTop);
        meta_line->addWidget(meta_label, 1);
        text->addLayout(meta_line);
    } else {
        text->addWidget(meta_label);
    }

    if (progressPercent >= 0) {
        QProgressBar *bar = new QProgressBar(row);
        bar->setObjectName(upload ? "spProgress" : "spProgressReceived");
        bar->setTextVisible(false);
        bar->setFixedHeight(4);
        bar->setRange(0, 100);
        bar->setValue(progressPercent);
        bar->setAttribute(Qt::WA_TransparentForMouseEvents);
        text->addSpacing(3);
        text->addWidget(bar);
    }

    layout->addLayout(text, 1);
    return row;
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

QIcon SyncPanel::faIcon(int code, const QColor& color)
{
    if (!awesome) {
        return QIcon();
    }
    return awesome->icon(code, color);
}

void SyncPanel::applyTheme()
{
    const QString font_family =
        QStringLiteral("font-family: \"Segoe UI Variable Text\", \"Segoe UI\", "
                       "-apple-system, \"Helvetica Neue\", sans-serif;");

    QString style;
    style += QString("QWidget#syncPanel { background: transparent; %1 }")
                 .arg(font_family);
    style += QString("QWidget#syncPanel QLabel { color: %1; font-size: %2;"
                     " background: transparent; }")
                 .arg(css(textPrimary()), px(kFontBody));
    style += QString("QWidget#syncPanel QLabel#spTitle { font-size: %1; }").arg(px(kFontBody + 1));
    style += QString("QWidget#syncPanel QLabel#spStatusTitle { font-size: %1; font-weight: 600; }")
                 .arg(px(kFontBody + 1));
    style += QString("QWidget#syncPanel QLabel#spStatusDetail { font-size: %1; }").arg(px(kFontSmall));
    style += QString("QWidget#syncPanel QLabel#spSectionTitle { color: %1; font-size: %2;"
                     " font-weight: 600; }")
                 .arg(css(textSecondary()), px(kFontSmall));
    style += QString("QWidget#syncPanel QLabel#spEmpty { color: %1; font-size: %2; }")
                 .arg(css(textMuted()), px(kFontSmall));
    style += QString("QWidget#syncPanel QLabel#spFileName { font-size: %1; }").arg(px(kFontBody));
    style += QString("QWidget#syncPanel QLabel#spFileMeta { color: %1; font-size: %2; }")
                 .arg(css(textSecondary()), px(kFontSmall));
    style += QString("QWidget#syncPanel QLabel#spPillSent { background: %1; color: %2;"
                     " border-radius: %3; padding: 0px 8px; font-size: %4; }")
                 .arg(css(sentBg()), css(sentText()), px(kPillHeight / 2), px(kFontSmall));
    style += QString("QWidget#syncPanel QLabel#spPillReceived { background: %1; color: %2;"
                     " border-radius: %3; padding: 0px 8px; font-size: %4; }")
                 .arg(css(receivedBg()), css(receivedText()), px(kPillHeight / 2), px(kFontSmall));
    style += QString("QWidget#syncPanel QLabel#spFooterLabel { color: %1; font-size: %2; }")
                 .arg(css(textSecondary()), px(kFontSmall));

    style += QString("QFrame#spFileRow { background: transparent; border: 1px solid transparent;"
                     " border-radius: %1; }"
                     "QFrame#spFileRow:hover { background: %2; }"
                     "QFrame#spFileRow:focus { border: 1px solid %3; }")
                 .arg(px(kRadius), css(surfaceHover()), css(accentText()));

    style += QString("QFrame#spFooter { background: %1; border: none;"
                     " border-top: 1px solid %2;"
                     " border-bottom-left-radius: %3; border-bottom-right-radius: %3; }")
                 .arg(css(surfaceCard()), css(border()), px(kRadiusCard));
    style += QString("QFrame#spFooterButton { background: transparent;"
                     " border: 1px solid transparent; border-radius: %1; }"
                     "QFrame#spFooterButton:hover { background: %2; }"
                     "QFrame#spFooterButton:focus { border: 1px solid %3; }")
                 .arg(px(kRadius), css(surfaceHover()), css(accentText()));

    style += QString("QToolButton#spIconButton { background: transparent; border: none;"
                     " border-radius: %1; }"
                     "QToolButton#spIconButton:hover { background: %2; }"
                     "QToolButton#spIconButton:focus { border: 1px solid %3; }")
                 .arg(px(kRadius), css(surfaceHover()), css(accentText()));

    style += QString("QProgressBar#spProgress { background: %1; border: none;"
                     " border-radius: 2px; }"
                     "QProgressBar#spProgress::chunk { background: %2; border-radius: 2px; }"
                     "QProgressBar#spProgressReceived { background: %1; border: none;"
                     " border-radius: 2px; }"
                     "QProgressBar#spProgressReceived::chunk { background: %3; border-radius: 2px; }")
                 .arg(css(border()), css(accentText()), css(receivedText()));

    style += "QScrollArea#spScroll, QWidget#spListContainer { background: transparent; }";
    style += QString("QScrollArea#spScroll QScrollBar:vertical { background: transparent;"
                     " width: 8px; margin: 2px; }"
                     "QScrollArea#spScroll QScrollBar::handle:vertical { background: %1;"
                     " border-radius: 2px; min-height: 24px; }"
                     "QScrollArea#spScroll QScrollBar::add-line:vertical,"
                     "QScrollArea#spScroll QScrollBar::sub-line:vertical { height: 0; }"
                     "QScrollArea#spScroll QScrollBar::add-page:vertical,"
                     "QScrollArea#spScroll QScrollBar::sub-page:vertical { background: none; }")
                 .arg(css(textMuted()));

    setStyleSheet(style);

    settings_button_->setIcon(faIcon(icon_cog, textSecondary()));
    web_button_->setIcon(faIcon(icon_globe, textSecondary()));
    for (QFrame *button : {open_folder_button_, transfer_button_}) {
        QLabel *icon = button->findChild<QLabel *>("spFooterIcon");
        if (icon) {
            icon->setPixmap(faIcon(button->property("sp_icon").toInt(), textPrimary())
                                .pixmap(QSize(kIconSize, kIconSize)));
        }
    }

    refreshStatus();
    refreshList();
    update();
}

void SyncPanel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, kRadiusCard, kRadiusCard);
    painter.fillPath(path, surfacePage());
    painter.setPen(QPen(border(), 1));
    painter.drawPath(path);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void SyncPanel::setAccountName(const QString& name)
{
    if (account_name_ == name) {
        return;
    }
    account_name_ = name;
    refreshStatus();
}

void SyncPanel::setRootPath(const QString& root)
{
    root_path_ = root;
}

void SyncPanel::setConnected(bool connected)
{
    if (connected_ == connected) {
        return;
    }
    connected_ = connected;
    refreshStatus();
}

void SyncPanel::setSyncing(bool syncing, qint64 sentRate, qint64 recvRate)
{
    syncing_ = syncing;
    sent_rate_ = syncing ? sentRate : 0;
    recv_rate_ = syncing ? recvRate : 0;
    last_check_ = QDateTime::currentDateTime();
    if (isVisible()) {
        refreshStatus();
    }
}

void SyncPanel::setErrorCount(int count)
{
    if (error_count_ == count) {
        return;
    }
    error_count_ = count;
    refreshStatus();
}

void SyncPanel::setTransfers(const TransferProgress& progress)
{
    transfers_ = progress;
    last_check_ = QDateTime::currentDateTime();
    if (isVisible()) {
        refreshStatus();
        refreshList();
    }
}

void SyncPanel::refreshStatus()
{
    // Header
    QString title = QString("<span style=\"font-weight:600; font-size:%1px;\">SeaDrive</span>")
                        .arg(kFontTitle);
    if (!account_name_.isEmpty()) {
        title += QString(" <span style=\"color:%1; font-size:%2px;\">&middot; %3</span>")
                     .arg(textSecondary().name(), QString::number(kFontBody),
                          account_name_.toHtmlEscaped());
    }
    title_label_->setText(title);

    // Status box
    int active = transfers_.uploading_files.size() + transfers_.downloading_files.size();
    quint64 done = 0, total = 0;
    for (const TransferringInfo& info : transfers_.uploading_files) {
        done += info.transferred_bytes;
        total += info.total_bytes;
    }
    for (const TransferringInfo& info : transfers_.downloading_files) {
        done += info.transferred_bytes;
        total += info.total_bytes;
    }

    QColor bg, fg;
    int icon_code;
    QString title_text, detail_text;
    bool show_progress = false;

    if (!connected_) {
        status_kind_ = StatusOffline;
        bg = surfaceHover();
        fg = textSecondary();
        icon_code = icon_unlink;
        title_text = tr("Not connected");
        detail_text = tr("SeaDrive will retry on its own as soon as the server is reachable.");
    } else if (syncing_ || active > 0) {
        status_kind_ = StatusSyncing;
        bg = accentBg();
        fg = accentText();
        icon_code = icon_refresh;
        title_text = tr("Syncing");
        QString rates = tr("↓ %1   ↑ %2").arg(formatRate(recv_rate_), formatRate(sent_rate_));
        if (active > 0) {
            detail_text = tr("%n file(s)", nullptr, active)
                          + QStringLiteral(" · ") + rates;
        } else {
            detail_text = rates;
        }
        show_progress = true;
        if (total > 0) {
            status_progress_->setRange(0, 100);
            status_progress_->setValue(toPercent(done, total));
        } else {
            // Busy indicator: the daemon is working but has no byte count.
            status_progress_->setRange(0, 0);
        }
    } else if (error_count_ > 0) {
        status_kind_ = StatusErrors;
        bg = warningBg();
        fg = warningText();
        icon_code = icon_warning_sign;
        title_text = tr("%n sync error(s)", nullptr, error_count_);
        detail_text = tr("Click here to see the details");
    } else {
        status_kind_ = StatusOk;
        bg = successBg();
        fg = successText();
        icon_code = icon_check_sign;
        title_text = tr("Everything is synced");
        detail_text = tr("You can work · last check %1").arg(relativeTime(last_check_));
    }

    status_box_->setStyleSheet(
        QString("QFrame#spStatusBox { background: %1; border: 1px solid %2;"
                " border-radius: %3; }"
                "QWidget#syncPanel QLabel#spStatusTitle { color: %4; }"
                "QWidget#syncPanel QLabel#spStatusDetail { color: %4; }")
            .arg(css(bg), css(border()), px(kRadiusCard), css(fg)));
    status_box_->setCursor(status_kind_ == StatusErrors ? Qt::PointingHandCursor
                                                        : Qt::ArrowCursor);
    status_icon_->setPixmap(GlyphIcon::pixmap(icon_code, fg, devicePixelRatioF()));
    status_title_->setText(title_text);
    status_detail_->setText(detail_text);
    status_progress_->setVisible(show_progress);
}

void SyncPanel::clearList()
{
    // Remove every row, keep the empty label and the final stretch.
    for (int i = list_layout_->count() - 1; i >= 0; i--) {
        QLayoutItem *item = list_layout_->itemAt(i);
        QWidget *w = item->widget();
        if (w && w != empty_label_) {
            list_layout_->takeAt(i);
            w->hide();
            w->deleteLater();
        }
    }
}

void SyncPanel::refreshList()
{
    clearList();

    int insert_at = 0;
    auto addRow = [&](QWidget *row) {
        list_layout_->insertWidget(insert_at++, row);
    };

    // Files in progress first, with their own bar.
    int active_rows = 0;
    auto addActive = [&](const QList<TransferringInfo>& list, bool upload) {
        for (const TransferringInfo& info : list) {
            if (active_rows >= kMaxActiveRows) {
                return;
            }
            QString path = info.file_path;
            path.replace('\\', '/');
            const QString folder = lastComponent(path.section('/', 0, -2));
            const int percent = toPercent(info.transferred_bytes, info.total_bytes);
            QString meta = upload ? tr("Uploading") : tr("Downloading");
            if (!folder.isEmpty()) {
                meta += QStringLiteral(" · ") + folder;
            }
            meta += QStringLiteral(" · %1%").arg(percent);
            addRow(createFileRow(path, upload, meta, percent));
            active_rows++;
        }
    };
    addActive(transfers_.uploading_files, true);
    addActive(transfers_.downloading_files, false);

    // Then the history.
    const QList<RecentFileEntry> entries = RecentFilesStore::instance()->entries();
    int recent_rows = 0;
    for (const RecentFileEntry& entry : entries) {
        if (recent_rows >= kMaxRecentRows) {
            break;
        }
        const bool upload = entry.direction == RecentFileEntry::Upload;
        // The direction is in the pill: here only folder and time.
        QString meta;
        const QString folder = lastComponent(entry.folder);
        if (!folder.isEmpty()) {
            meta = folder + QStringLiteral(" · ");
        }
        meta += relativeTime(entry.time);
        addRow(createFileRow(entry.path, upload, meta, -1));
        recent_rows++;
    }

    empty_label_->setVisible(insert_at == 0);
}

void SyncPanel::tick()
{
    refreshStatus();
    refreshList();
}

// ---------------------------------------------------------------------------
// Show / hide
// ---------------------------------------------------------------------------

void SyncPanel::showPanel(const QRect& trayGeometry)
{
    refreshStatus();
    refreshList();
    positionNear(trayGeometry);
    show();
    raise();
    activateWindow();
#if defined(Q_OS_WIN32)
    // The tray click gives us the right to take the foreground: use it, so
    // that the panel gets the focus and can hide itself when it loses it.
    ::SetForegroundWindow(reinterpret_cast<HWND>(winId()));
#endif
    setFocus();
}

void SyncPanel::togglePanel(const QRect& trayGeometry)
{
    if (isVisible()) {
        hide();
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() - hidden_at_msec_ < kReopenGuardMSecs) {
        // The click that just closed the panel (by deactivating it) is the
        // same click that brought us here.
        return;
    }
    showPanel(trayGeometry);
}

void SyncPanel::positionNear(const QRect& tray)
{
    QScreen *screen = nullptr;
    if (tray.isValid()) {
        screen = QGuiApplication::screenAt(tray.center());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect avail = screen->availableGeometry();
    const QRect full = screen->geometry();
    const int w = width();
    const int h = height();
    int x, y;

    if (tray.isValid() && !tray.isEmpty()) {
        if (tray.top() >= avail.bottom() - 1) {
            // Taskbar at the bottom (the usual case).
            x = tray.center().x() - w / 2;
            y = avail.bottom() - h - kScreenMargin + 1;
        } else if (tray.bottom() <= avail.top() + 1) {
            x = tray.center().x() - w / 2;
            y = avail.top() + kScreenMargin;
        } else if (tray.left() >= avail.right() - 1) {
            x = avail.right() - w - kScreenMargin + 1;
            y = tray.center().y() - h / 2;
        } else if (tray.right() <= avail.left() + 1) {
            x = avail.left() + kScreenMargin;
            y = tray.center().y() - h / 2;
        } else {
            // Tray inside the available area (e.g. overflow flyout, auto
            // hidden taskbar): open above the icon.
            x = tray.center().x() - w / 2;
            y = tray.top() - h - kScreenMargin;
        }
    } else {
        // No tray geometry: use the corner where the taskbar is.
        x = avail.right() - w - kScreenMargin + 1;
        y = avail.bottom() - h - kScreenMargin + 1;
        if (avail.top() > full.top()) {
            y = avail.top() + kScreenMargin;
        }
        if (avail.left() > full.left()) {
            x = avail.left() + kScreenMargin;
        }
    }

    x = qBound(avail.left() + kScreenMargin, x, avail.right() - w - kScreenMargin + 1);
    y = qBound(avail.top() + kScreenMargin, y, avail.bottom() - h - kScreenMargin + 1);
    move(x, y);
}

void SyncPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    tick_timer_->start();
}

void SyncPanel::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    tick_timer_->stop();
    hidden_at_msec_ = QDateTime::currentMSecsSinceEpoch();
}

void SyncPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && isVisible() && !isActiveWindow()) {
        // Lost the focus: behave like a flyout. Hide on the next event loop
        // turn, Qt does not like hiding a window inside its activation change.
        QTimer::singleShot(0, this, [this]() {
            if (!isActiveWindow()) {
                hide();
            }
        });
    }
    QWidget::changeEvent(event);
}

void SyncPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

bool SyncPanel::eventFilter(QObject *obj, QEvent *event)
{
    QWidget *w = qobject_cast<QWidget *>(obj);
    if (!w) {
        return QWidget::eventFilter(obj, event);
    }

    bool activate = false;
    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        activate = me->button() == Qt::LeftButton && w->rect().contains(me->position().toPoint());
    } else if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        activate = ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter ||
                   ke->key() == Qt::Key_Space;
    }
    if (!activate) {
        return QWidget::eventFilter(obj, event);
    }

    if (w == status_box_) {
        if (status_kind_ == StatusErrors) {
            hide();
            emit syncErrorsRequested();
        }
        return true;
    }

    const QVariant file_path = w->property(kPropFilePath);
    if (file_path.isValid()) {
        openFileFolder(file_path.toString());
        return true;
    }

    const QVariant action = w->property(kPropAction);
    if (action.isValid()) {
        triggerAction(action.toInt());
        return true;
    }

    return QWidget::eventFilter(obj, event);
}

void SyncPanel::triggerAction(int action)
{
    hide();
    switch (action) {
    case ActionOpenFolder:
        emit openFolderRequested();
        break;
    case ActionTransfers:
        emit transferWindowRequested();
        break;
    case ActionSettings:
        emit settingsRequested();
        break;
    default:
        break;
    }
}

QString SyncPanel::absolutePath(const QString& path) const
{
    if (QDir::isAbsolutePath(path) || root_path_.isEmpty()) {
        return QDir::cleanPath(path);
    }
    return QDir::cleanPath(QDir(root_path_).filePath(path));
}

void SyncPanel::openFileFolder(const QString& path)
{
    const QString abs = absolutePath(path);
    hide();
    emit fileActivated(abs);

    if (QFileInfo::exists(abs)) {
        // Opens the folder in Explorer/Finder with the file selected.
        showInGraphicalShell(abs);
        return;
    }
    // The file was moved or deleted: open the closest existing parent.
    QString dir = QFileInfo(abs).absolutePath();
    while (!dir.isEmpty() && !QFileInfo::exists(dir)) {
        const QString parent = QFileInfo(dir).absolutePath();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    // Never climb above the SeaDrive root.
    if (!root_path_.isEmpty() &&
        (dir.isEmpty() || !QDir::cleanPath(dir).startsWith(QDir::cleanPath(root_path_),
                                                          Qt::CaseInsensitive))) {
        dir = root_path_;
    }
    if (!dir.isEmpty() && QFileInfo(dir).isDir()) {
        openUrl(QUrl::fromLocalFile(dir));
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString SyncPanel::relativeTime(const QDateTime& time)
{
    if (!time.isValid()) {
        return QString();
    }
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secs = time.secsTo(now);
    if (secs < 60) {
        return tr("Just now");
    }
    if (secs < 3600) {
        const int mins = int(secs / 60);
        return mins == 1 ? tr("1 minute ago") : tr("%1 minutes ago").arg(mins);
    }
    const qint64 days = time.date().daysTo(now.date());
    if (days == 0) {
        const int hours = int(secs / 3600);
        return hours == 1 ? tr("1 hour ago") : tr("%1 hours ago").arg(hours);
    }
    if (days == 1) {
        return tr("Yesterday at %1").arg(time.toString("HH:mm"));
    }
    if (days < 7) {
        return tr("%1 days ago").arg(days);
    }
    return time.toString("dd-MM-yyyy");
}

QString SyncPanel::formatRate(qint64 rate)
{
    return translateTransferRate(int(qBound(qint64(0), rate, qint64(INT_MAX))));
}

QString SyncPanel::breakable(const QString& text)
{
    // QLabel only wraps at break opportunities: add zero width spaces after
    // separators and inside very long runs, so long file names wrap instead
    // of widening the panel or being cut.
    const QChar zwsp(0x200B);
    const int kMaxRun = 14;
    QString out;
    out.reserve(text.size() + text.size() / 4);
    int run = 0;
    for (const QChar c : text) {
        out.append(c);
        if (c.isSpace()) {
            run = 0;
            continue;
        }
        run++;
        if (c == '/' || c == '\\' || c == '_' || c == '-' || c == '.' || run >= kMaxRun) {
            out.append(zwsp);
            run = 0;
        }
    }
    return out;
}
