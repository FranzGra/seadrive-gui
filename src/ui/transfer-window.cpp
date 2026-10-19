#include "transfer-window.h"

#include <QtGlobal>
#include <QtWidgets>
#include <QTextLayout>
#include <QScopedPointer>
#include <QDateTime>
#include <QStyleHints>

#include <algorithm>

#include "QtAwesome.h"
#include "i18n.h"
#include "seadrive-gui.h"
#include "account-mgr.h"
#include "settings-mgr.h"
#include "recent-files-store.h"
#include "rpc/rpc-client.h"
#include "rpc/transfer-progress.h"
#include "ui/design-tokens.h"
#include "ui/modern-menu.h"
#include "ui/glyph-icon.h"
#include "ui/tray-icon.h"
#include "utils/utils.h"
#include "utils/file-utils.h"

namespace DT = DesignTokens;

namespace {

const int kRefreshIntervalMSecs = 1000;
const int kBadgeSize = DT::kIconSize + DT::kRowPadding;   // 32
const int kBarHeight = 4;
const int kPillPadding = 8;              // left/right padding inside the direction pill
const int kColumnGap = DT::kPadding;
const int kStackedBelowWidth = 340;       // text area narrower than this: status goes under the name
const int kPageMargin = DT::kPadding + DT::kPadding / 2;  // 24

enum {
    INDEX_EMPTY_VIEW = 0,
    INDEX_LIST_VIEW,
};

// Used with QScopedPointer for json_t
struct JsonPointerCustomDeleter {
    static inline void cleanup(json_t *json) {
        if (json)
            json_decref(json);
    }
};

// Short, plain status text for a sync error, shown in the error rows. The
// file name is already in the row, so it is not repeated here. The long
// English texts of SyncError::syncErrorIdToErrorStr() stay untouched for the
// other dialogs and the translation files.
QString shortSyncErrorText(int error_id)
{
    switch (error_id) {
    case SYNC_ERROR_ID_FILE_LOCKED_BY_APP:     return QCoreApplication::translate("TransferWindow", "Open in another program");
    case SYNC_ERROR_ID_FOLDER_LOCKED_BY_APP:   return QCoreApplication::translate("TransferWindow", "Folder in use by another program");
    case SYNC_ERROR_ID_FILE_LOCKED:            return QCoreApplication::translate("TransferWindow", "Locked by another user");
    case SYNC_ERROR_ID_INVALID_PATH:           return QCoreApplication::translate("TransferWindow", "Invalid path");
    case SYNC_ERROR_ID_INDEX_ERROR:            return QCoreApplication::translate("TransferWindow", "Error when indexing files");
    case SYNC_ERROR_ID_ACCESS_DENIED:          return QCoreApplication::translate("TransferWindow", "You don't have enough permission for this library");
    case SYNC_ERROR_ID_QUOTA_FULL:             return QCoreApplication::translate("TransferWindow", "The storage quota has been used up");
    case SYNC_ERROR_ID_NETWORK:                return QCoreApplication::translate("TransferWindow", "Network error");
    case SYNC_ERROR_ID_RESOLVE_PROXY:          return QCoreApplication::translate("TransferWindow", "Failed to resolve network proxy");
    case SYNC_ERROR_ID_RESOLVE_HOST:           return QCoreApplication::translate("TransferWindow", "Failed to resolve remote server");
    case SYNC_ERROR_ID_CONNECT:                return QCoreApplication::translate("TransferWindow", "Failed to connect to server");
    case SYNC_ERROR_ID_SSL:                    return QCoreApplication::translate("TransferWindow", "SSL error");
    case SYNC_ERROR_ID_TX:                     return QCoreApplication::translate("TransferWindow", "Error in network transmission");
    case SYNC_ERROR_ID_TX_TIMEOUT:             return QCoreApplication::translate("TransferWindow", "Timeout in network transmission");
    case SYNC_ERROR_ID_UNHANDLED_REDIRECT:     return QCoreApplication::translate("TransferWindow", "Failed to handle http redirection");
    case SYNC_ERROR_ID_SERVER:                 return QCoreApplication::translate("TransferWindow", "Server internal error");
    case SYNC_ERROR_ID_LOCAL_DATA_CORRUPT:     return QCoreApplication::translate("TransferWindow", "Local data is corrupt");
    case SYNC_ERROR_ID_WRITE_LOCAL_DATA:       return QCoreApplication::translate("TransferWindow", "Failed to write local data");
    case SYNC_ERROR_ID_PERM_NOT_SYNCABLE:      return QCoreApplication::translate("TransferWindow", "Syncing is denied by cloud-only permission settings");
    case SYNC_ERROR_ID_NO_WRITE_PERMISSION:    return QCoreApplication::translate("TransferWindow", "Do not have write permission to the library");
    case SYNC_ERROR_ID_FOLDER_PERM_DENIED:     return QCoreApplication::translate("TransferWindow", "Folder without write permission");
    case SYNC_ERROR_ID_PATH_END_SPACE_PERIOD:  return QCoreApplication::translate("TransferWindow", "The name ends with a space or a period");
    case SYNC_ERROR_ID_PATH_INVALID_CHARACTER: return QCoreApplication::translate("TransferWindow", "The name contains invalid characters (| :)");
    case SYNC_ERROR_ID_UPDATE_TO_READ_ONLY_REPO: return QCoreApplication::translate("TransferWindow", "Read-only library: it will not be uploaded");
    case SYNC_ERROR_ID_CONFLICT:               return QCoreApplication::translate("TransferWindow", "Changed on both sides: saved as a conflict copy");
    case SYNC_ERROR_ID_UPDATE_NOT_IN_REPO:     return QCoreApplication::translate("TransferWindow", "Not in a library: it will not be uploaded");
    case SYNC_ERROR_ID_LIBRARY_TOO_LARGE:      return QCoreApplication::translate("TransferWindow", "Library cannot be synced since it has too many files.");
    case SYNC_ERROR_ID_MOVE_NOT_IN_REPO:       return QCoreApplication::translate("TransferWindow", "You can only move within or between libraries");
    case SYNC_ERROR_ID_DEL_CONFIRMATION_PENDING: return QCoreApplication::translate("TransferWindow", "Waiting for confirmation to delete files");
    case SYNC_ERROR_ID_INVALID_PATH_ON_WINDOWS: return QCoreApplication::translate("TransferWindow", "The name contains symbols not allowed by Windows");
    case SYNC_ERROR_ID_TOO_MANY_FILES:         return QCoreApplication::translate("TransferWindow", "Library file limit reached");
    case SYNC_ERROR_ID_BLOCK_MISSING:          return QCoreApplication::translate("TransferWindow", "Failed to upload file blocks. Please check network or firewall.");
    case SYNC_ERROR_ID_CHECKOUT_FILE:          return QCoreApplication::translate("TransferWindow", "Failed to download file. Please check disk space or folder permissions");
    case SYNC_ERROR_ID_CASE_CONFLICT:          return QCoreApplication::translate("TransferWindow", "Upper/lower case conflict: not downloaded");
    case SYNC_ERROR_ID_RENAME_WORKTREE:        return QCoreApplication::translate("TransferWindow", "Failed to change library name since some files are opened in it");
    case SYNC_ERROR_ID_GENERAL_ERROR:          return QCoreApplication::translate("TransferWindow", "Unknown error");
    default:                                   return QString();
    }
}

QString normalizedPath(const QString& path)
{
    QString p = path;
    p.replace('\\', '/');
    while (p.endsWith('/') && p.size() > 1)
        p.chop(1);
    return p;
}

QString baseName(const QString& path)
{
    QString p = normalizedPath(path);
    int i = p.lastIndexOf('/');
    return i < 0 ? p : p.mid(i + 1);
}

QString parentPath(const QString& path)
{
    QString p = normalizedPath(path);
    int i = p.lastIndexOf('/');
    return i <= 0 ? QString() : p.left(i);
}

QString displayFolder(const QString& folder)
{
    if (folder.isEmpty())
        return QCoreApplication::translate("TransferWindow", "Root folder");
    return QDir::toNativeSeparators(folder);
}

QString relativeTime(const QDateTime& time)
{
    if (!time.isValid())
        return QString();
    qint64 secs = time.secsTo(QDateTime::currentDateTime());
    if (secs < 60)
        return QCoreApplication::translate("TransferWindow", "Just now");
    if (secs < 3600)
        return QCoreApplication::translate("TransferWindow", "%1 min ago").arg(secs / 60);
    if (secs < 24 * 3600)
        return QCoreApplication::translate("TransferWindow", "%1 h ago").arg(secs / 3600);
    if (time.date().daysTo(QDate::currentDate()) == 1)
        return QCoreApplication::translate("TransferWindow", "Yesterday");
    return time.toString("dd-MM-yyyy");
}

QFont pixelFont(const QFont& base, int pixel_size, bool bold = false)
{
    QFont f(base);
    f.setPixelSize(pixel_size);
    f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    return f;
}

// Lays out `text` in `width`, wrapping at word boundaries and, for long names
// without spaces, anywhere. Returns the height used.
qreal layoutText(QTextLayout *layout, const QString& text, const QFont& font,
                 qreal width, bool align_right = false)
{
    layout->setText(text);
    layout->setFont(font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout->setTextOption(option);
    layout->beginLayout();
    qreal y = 0;
    while (true) {
        QTextLine line = layout->createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(qMax<qreal>(1.0, width));
        qreal x = align_right ? qMax<qreal>(0.0, width - line.naturalTextWidth()) : 0;
        line.setPosition(QPointF(x, y));
        y += line.height();
    }
    layout->endLayout();
    return y;
}

qreal textHeight(const QString& text, const QFont& font, qreal width)
{
    if (text.isEmpty())
        return 0;
    QTextLayout layout;
    return layoutText(&layout, text, font, width);
}

void drawText(QPainter *painter, const QRectF& rect, const QString& text,
              const QFont& font, const QColor& color, bool align_right = false)
{
    if (text.isEmpty())
        return;
    QTextLayout layout;
    layoutText(&layout, text, font, rect.width(), align_right);
    painter->setPen(color);
    layout.draw(painter, rect.topLeft());
}

QIcon faIcon(int character, const QColor& color)
{
    return awesome->icon(character, color);
}


// A flow layout: items go left to right and wrap to a new line when the
// width is not enough. The last item (the search field) is pushed to the
// right edge, or takes the full line when it wraps alone.
class PillFlowLayout : public QLayout
{
public:
    PillFlowLayout(QWidget *parent = nullptr, int hspacing = 8, int vspacing = 8)
        : QLayout(parent), hspacing_(hspacing), vspacing_(vspacing) {
        setContentsMargins(0, 0, 0, 0);
    }
    ~PillFlowLayout() {
        QLayoutItem *item;
        while ((item = takeAt(0)))
            delete item;
    }

    void addItem(QLayoutItem *item) override { items_.append(item); }
    int count() const override { return items_.size(); }
    QLayoutItem *itemAt(int index) const override { return items_.value(index); }
    QLayoutItem *takeAt(int index) override {
        return (index >= 0 && index < items_.size()) ? items_.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        return doLayout(QRect(0, 0, width, 0), true);
    }
    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize size;
        for (QLayoutItem *item : items_)
            size = size.expandedTo(item->minimumSize());
        QMargins m = contentsMargins();
        size += QSize(m.left() + m.right(), m.top() + m.bottom());
        return size;
    }

private:
    int doLayout(const QRect& rect, bool test_only) const {
        QMargins m = contentsMargins();
        QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());

        // Split into lines.
        QList<QList<int>> lines;
        QList<int> current;
        int x = 0;
        for (int i = 0; i < items_.size(); i++) {
            if (items_[i]->isEmpty())
                continue;
            int w = items_[i]->sizeHint().width();
            if (!current.isEmpty() && x + w > area.width()) {
                lines.append(current);
                current.clear();
                x = 0;
            }
            current.append(i);
            x += w + hspacing_;
        }
        if (!current.isEmpty())
            lines.append(current);

        int last = items_.size() - 1;
        int y = area.y();
        for (int l = 0; l < lines.size(); l++) {
            const QList<int>& line = lines[l];
            int line_height = 0;
            for (int i : line)
                line_height = qMax(line_height, items_[i]->sizeHint().height());

            int lx = area.x();
            for (int i : line) {
                QSize hint = items_[i]->sizeHint();
                int w = qMin(hint.width(), area.width());
                if (i == last) {
                    if (line.size() == 1) {
                        w = area.width();
                    } else {
                        lx = qMax(lx, area.x() + area.width() - w);
                    }
                }
                if (!test_only) {
                    items_[i]->setGeometry(QRect(lx, y + (line_height - hint.height()) / 2,
                                                 w, hint.height()));
                }
                lx += w + hspacing_;
            }
            y += line_height;
            if (l + 1 < lines.size())
                y += vspacing_;
        }
        return y - rect.y() + m.bottom();
    }

    QList<QLayoutItem*> items_;
    int hspacing_;
    int vspacing_;
};

} // namespace


// ---------------------------------------------------------------------------
// TransferRow

TransferRow::TransferRow(QWidget *parent)
    : QWidget(parent),
      hovered_(false)
{
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void TransferRow::setData(const TransferRowData& data)
{
    bool layout_changed = data.name != data_.name || data.folder != data_.folder ||
                          data.status != data_.status || data.detail != data_.detail ||
                          (data.progress < 0) != (data_.progress < 0) ||
                          data.kind != data_.kind;
    data_ = data;

    QString tip = data_.file_path.isEmpty() ? data_.name
                                            : QDir::toNativeSeparators(data_.file_path);
    tip += "\n" + tr("Double-click to open the folder");
    setToolTip(tip);

    if (layout_changed)
        updateGeometry();
    update();
}

QString TransferRow::stackedStatus() const
{
    if (data_.detail.isEmpty())
        return data_.status;
    if (data_.status.isEmpty())
        return data_.detail;
    return data_.status + QString::fromUtf8(" \xc2\xb7 ") + data_.detail;
}

TransferRow::Geometry TransferRow::computeGeometry(int width) const
{
    Geometry g;
    const int pad = DT::kRowPadding;
    const QFont name_font = pixelFont(font(), DT::kFontBody, true);
    const QFont small_font = pixelFont(font(), DT::kFontSmall);

    const qreal x0 = pad + kBadgeSize + pad;
    const qreal avail = qMax<qreal>(40.0, width - x0 - pad);
    g.stacked = avail < kStackedBelowWidth;

    QFontMetricsF pill_fm(small_font);
    const qreal pill_w = data_.pill.isEmpty()
        ? 0 : pill_fm.horizontalAdvance(data_.pill) + 2 * kPillPadding;
    const qreal pill_h = DT::kPillHeight;

    qreal left_w = avail;
    qreal right_w = 0;
    if (!g.stacked) {
        QFontMetricsF fm(small_font);
        qreal natural = qMax(qMax(fm.horizontalAdvance(data_.status),
                                  fm.horizontalAdvance(data_.detail)), pill_w) + 2;
        right_w = qMin(natural, avail * 0.42);
        left_w = avail - right_w - kColumnGap;
    }

    qreal y = pad;
    qreal name_h = textHeight(data_.name, name_font, left_w);
    g.name = QRectF(x0, y, left_w, name_h);
    y += name_h;
    if (!data_.folder.isEmpty()) {
        y += 2;
        qreal folder_h = textHeight(data_.folder, small_font, left_w);
        g.folder = QRectF(x0, y, left_w, folder_h);
        y += folder_h;
    }
    qreal two_lines_h = y - pad;

    if (g.stacked) {
        QString text = stackedStatus();
        if (!text.isEmpty() || pill_w > 0) {
            y += 4;
            qreal text_x = x0;
            if (pill_w > 0) {
                g.pill = QRectF(x0, y, pill_w, pill_h);
                text_x += pill_w + 8;
            }
            qreal text_w = qMax<qreal>(20.0, x0 + avail - text_x);
            qreal h = text.isEmpty() ? 0 : textHeight(text, small_font, text_w);
            // Text vertically centered on the pill line.
            qreal ty = pill_w > 0 ? y + qMax<qreal>(0.0, (pill_h - pill_fm.height()) / 2) : y;
            if (!text.isEmpty())
                g.status = QRectF(text_x, ty, text_w, h);
            y = qMax(y + (pill_w > 0 ? pill_h : 0), ty + h);
        }
    } else {
        qreal ry = pad;
        qreal rx = width - pad - right_w;
        if (pill_w > 0) {
            g.pill = QRectF(width - pad - pill_w, ry, pill_w, pill_h);
            ry += pill_h + 4;
        }
        if (!data_.status.isEmpty()) {
            qreal h = textHeight(data_.status, small_font, right_w);
            g.status = QRectF(rx, ry, right_w, h);
            ry += h + 2;
        }
        if (!data_.detail.isEmpty()) {
            qreal h = textHeight(data_.detail, small_font, right_w);
            g.detail = QRectF(rx, ry, right_w, h);
            ry += h;
        }
        y = qMax(y, ry);
    }

    qreal content_h = qMax<qreal>(y - pad, kBadgeSize);
    qreal badge_y = pad + qMax<qreal>(0.0, (qMin<qreal>(two_lines_h, content_h) - kBadgeSize) / 2);
    g.badge = QRectF(pad, badge_y, kBadgeSize, kBadgeSize);

    qreal bottom = pad + content_h;
    if (data_.progress >= 0) {
        bottom += 10;
        g.bar = QRectF(x0, bottom, avail, kBarHeight);
        bottom += kBarHeight;
    }
    g.height = qCeil(bottom + pad);
    return g;
}

int TransferRow::heightForWidth(int width) const
{
    return computeGeometry(width).height;
}

QSize TransferRow::sizeHint() const
{
    int w = qMax(width(), 400);
    return QSize(w, heightForWidth(w));
}

QSize TransferRow::minimumSizeHint() const
{
    // Small minimum width: the row never forces the window wider, the text
    // wraps instead.
    return QSize(kBadgeSize + 3 * DT::kRowPadding + 40, kBadgeSize + 2 * DT::kRowPadding);
}

int TransferRow::iconForKind() const
{
    // The direction is always visible: arrow up = sent, arrow down =
    // received, both while in progress and once completed.
    switch (data_.kind) {
    case TransferRowData::Downloading:
    case TransferRowData::Downloaded:
        return icon_arrow_down;
    case TransferRowData::Uploading:
    case TransferRowData::Uploaded:
        return icon_arrow_up;
    case TransferRowData::Error:
    default:
        return icon_warning_sign;
    }
}

void TransferRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const Geometry g = computeGeometry(width());

    if (hovered_) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(DT::surfaceHover());
        painter.drawRoundedRect(QRectF(rect()), DT::kRadius, DT::kRadius);
    }

    // Badge with the icon
    QColor badge_bg;
    QColor badge_fg;
    QColor status_color;
    switch (data_.kind) {
    case TransferRowData::Downloading:
        badge_bg = DT::receivedBg();
        badge_fg = DT::receivedText();
        status_color = DT::receivedText();
        break;
    case TransferRowData::Uploading:
        badge_bg = DT::sentBg();
        badge_fg = DT::sentText();
        status_color = DT::sentText();
        break;
    case TransferRowData::Downloaded:
        badge_bg = DT::receivedBg();
        badge_fg = DT::receivedText();
        status_color = DT::textSecondary();
        break;
    case TransferRowData::Uploaded:
        badge_bg = DT::sentBg();
        badge_fg = DT::sentText();
        status_color = DT::textSecondary();
        break;
    case TransferRowData::Error:
    default:
        badge_bg = DT::warningBg();
        badge_fg = DT::warningText();
        status_color = DT::warningText();
        break;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(badge_bg);
    painter.drawRoundedRect(g.badge, DT::kRadius, DT::kRadius);
    GlyphIcon::paint(&painter, g.badge, iconForKind(), badge_fg);

    const QFont name_font = pixelFont(font(), DT::kFontBody, true);
    const QFont small_font = pixelFont(font(), DT::kFontSmall);

    // "Uploaded" / "Downloaded" pill, same color as the arrow.
    if (!data_.pill.isEmpty() && !g.pill.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(badge_bg);
        painter.drawRoundedRect(g.pill, g.pill.height() / 2, g.pill.height() / 2);
        painter.setFont(small_font);
        painter.setPen(badge_fg);
        painter.drawText(g.pill, Qt::AlignCenter, data_.pill);
    }

    drawText(&painter, g.name, data_.name, name_font, DT::textPrimary());
    drawText(&painter, g.folder, data_.folder, small_font, DT::textSecondary());

    if (g.stacked) {
        drawText(&painter, g.status, stackedStatus(), small_font, status_color);
    } else {
        drawText(&painter, g.status, data_.status, small_font, status_color, true);
        drawText(&painter, g.detail, data_.detail, small_font, DT::textSecondary(), true);
    }

    if (data_.progress >= 0 && !g.bar.isEmpty()) {
        const qreal r = kBarHeight / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(DT::border());
        painter.drawRoundedRect(g.bar, r, r);
        QRectF fill = g.bar;
        fill.setWidth(g.bar.width() * qBound(0, data_.progress, 100) / 100.0);
        if (fill.width() > 0) {
            painter.setBrush(badge_fg);   // bar in the direction color
            painter.drawRoundedRect(fill, r, r);
        }
    }
}

void TransferRow::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void TransferRow::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

void TransferRow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit activated(this);
    QWidget::mouseDoubleClickEvent(event);
}

void TransferRow::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(this, event->globalPos());
}


// ---------------------------------------------------------------------------
// TransferWindow

TransferWindow::TransferWindow(QWidget *parent)
    : QWidget(parent, Qt::Window),
      search_edit_(nullptr),
      empty_icon_(nullptr),
      help_link_(nullptr),
      poll_timer_(nullptr),
      filter_(FilterAll),
      up_rate_(0),
      down_rate_(0)
{
    setObjectName("transferWindow");
    setWindowTitle(tr("Transfer Progress"));
    setWindowIcon(QIcon(":/images/seafile.png"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumSize(QSize(480, 400));
    resize(QSize(680, 560));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPageMargin, kPageMargin - 4, kPageMargin, kPageMargin);
    layout->setSpacing(DT::kPadding);

    createHeader(layout);
    createFilters(layout);
    createList(layout);

    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(kRefreshIntervalMSecs);
    connect(poll_timer_, SIGNAL(timeout()), this, SLOT(refresh()));

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &TransferWindow::onColorSchemeChanged);
    connect(RecentFilesStore::instance(), &RecentFilesStore::changed,
            this, &TransferWindow::onRecentFilesChanged);

    last_open_sync_error_timestamp_ = gui->settingsManager()->getLastOpenSyncDialogTimestamp();

    applyStyle();
    updateErrors();
    updateFilterLabels();
    rebuildRows();
}

void TransferWindow::createHeader(QVBoxLayout *layout)
{
    QHBoxLayout *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(DT::kPadding);

    title_label_ = new QLabel(tr("Transfer Progress"));
    title_label_->setObjectName("twTitle");
    title_label_->setWordWrap(true);

    rate_label_ = new QLabel;
    rate_label_->setObjectName("twRate");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setToolTip(tr("Download and upload speed"));

    header->addWidget(title_label_, 1);
    header->addWidget(rate_label_, 0, Qt::AlignRight | Qt::AlignVCenter);
    layout->addLayout(header);
}

void TransferWindow::createFilters(QVBoxLayout *layout)
{
    QWidget *bar = new QWidget;
    bar->setObjectName("twFilterBar");
    PillFlowLayout *flow = new PillFlowLayout(bar, 8, 8);

    filter_group_ = new QButtonGroup(this);
    filter_group_->setExclusive(true);
    for (int i = 0; i < 5; i++) {
        QPushButton *button = new QPushButton;
        button->setObjectName("twPill");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::TabFocus);
        filter_group_->addButton(button, i);
        flow->addWidget(button);
        filter_buttons_[i] = button;
    }
    filter_buttons_[FilterAll]->setChecked(true);
    connect(filter_group_, &QButtonGroup::idClicked,
            this, &TransferWindow::onFilterClicked);

    search_edit_ = new QLineEdit;
    search_edit_->setObjectName("twSearch");
    search_edit_->setPlaceholderText(tr("Search for a file"));
    search_edit_->setClearButtonEnabled(true);
    // Minimum 240 px; it stretches to the full line when it wraps alone.
    search_edit_->setMinimumWidth(240);
    connect(search_edit_, &QLineEdit::textChanged,
            this, &TransferWindow::onSearchChanged);
    flow->addWidget(search_edit_);

    layout->addWidget(bar);
}

void TransferWindow::createList(QVBoxLayout *layout)
{
    QFrame *card = new QFrame;
    card->setObjectName("twCard");
    QVBoxLayout *card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(1, 1, 1, 1);
    card_layout->setSpacing(0);

    stack_ = new QStackedWidget;

    // Empty view
    empty_view_ = new QWidget;
    QVBoxLayout *empty_layout = new QVBoxLayout(empty_view_);
    empty_layout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    empty_layout->setSpacing(DT::kRowPadding / 2);
    empty_layout->addStretch(1);
    empty_icon_ = new QLabel;
    empty_icon_->setAlignment(Qt::AlignCenter);
    empty_title_ = new QLabel;
    empty_title_->setObjectName("twEmptyTitle");
    empty_title_->setAlignment(Qt::AlignCenter);
    empty_title_->setWordWrap(true);
    empty_text_ = new QLabel;
    empty_text_->setObjectName("twEmptyText");
    empty_text_->setAlignment(Qt::AlignCenter);
    empty_text_->setWordWrap(true);
    empty_layout->addWidget(empty_icon_);
    empty_layout->addSpacing(DT::kRowPadding / 2);
    empty_layout->addWidget(empty_title_);
    empty_layout->addWidget(empty_text_);
    empty_layout->addStretch(2);

    // List view
    scroll_area_ = new QScrollArea;
    scroll_area_->setObjectName("twScroll");
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_widget_ = new QWidget;
    list_widget_->setObjectName("twList");
    list_layout_ = new QVBoxLayout(list_widget_);
    list_layout_->setContentsMargins(6, 6, 6, 6);
    list_layout_->setSpacing(2);
    list_layout_->addStretch(1);
    scroll_area_->setWidget(list_widget_);

    stack_->insertWidget(INDEX_EMPTY_VIEW, empty_view_);
    stack_->insertWidget(INDEX_LIST_VIEW, scroll_area_);
    card_layout->addWidget(stack_);
    layout->addWidget(card, 1);

    // Link to the description of the sync errors, shown with the errors.
    QString url;
    if (I18NHelper::getInstance()->isTargetLanguage("zh_CN")) {
        url = "https://cloud.seafile.com/wiki/publish/seafile-user-manual/7he2/";
    } else {
        url = "https://help.seafile.com/faq/#meaning-file-syncing-errors";
    }
    help_link_ = new QLabel;
    help_link_->setObjectName("twHelp");
    help_link_->setTextFormat(Qt::RichText);
    help_link_->setWordWrap(true);
    help_link_->setProperty("help_url", url);
    connect(help_link_, &QLabel::linkActivated, this, [](const QString& link) {
        openUrl(QUrl(link));
    });
    layout->addWidget(help_link_);
}

void TransferWindow::applyStyle()
{
    const QString page = DT::css(DT::surfacePage());
    const QString card = DT::css(DT::surfaceCard());
    const QString hover = DT::css(DT::surfaceHover());
    const QString text = DT::css(DT::textPrimary());
    const QString text2 = DT::css(DT::textSecondary());
    const QString border = DT::css(DT::border());
    const QString accent = DT::css(DT::accentText());
    const QString accent_bg = DT::css(DT::accentBg());
    const QString warn = DT::css(DT::warningText());
    const QString warn_bg = DT::css(DT::warningBg());

    QString qss = QString(
        "#transferWindow { background: %1; }"
        "#transferWindow QLabel { color: %4; font-size: %11px; background: transparent; }"
        "QLabel#twTitle { color: %4; font-size: %12px; font-weight: 600; }"
        "QLabel#twRate { color: %5; font-size: %13px; }"
        "QLabel#twEmptyTitle { color: %4; font-size: %11px; font-weight: 600; }"
        "QLabel#twEmptyText { color: %5; font-size: %13px; }"
        "QLabel#twHelp { color: %5; font-size: %13px; }"
        "#twFilterBar { background: transparent; }"
        "QPushButton#twPill { background: %2; color: %5; border: 1px solid %6;"
        "  border-radius: 15px; padding: 5px 14px; font-size: %13px; min-height: 18px; }"
        "QPushButton#twPill:hover { background: %3; color: %4; }"
        "QPushButton#twPill:checked { background: %8; color: %7; border-color: %8; font-weight: 600; }"
        "QPushButton#twPill:focus { border-color: %7; }"
        "QPushButton#twPill[warning=\"true\"] { color: %9; }"
        "QPushButton#twPill[warning=\"true\"]:checked { background: %10; color: %9; border-color: %10; }"
        "QLineEdit#twSearch { background: %2; color: %4; border: 1px solid %6;"
        "  border-radius: %14px; padding: 5px 8px; font-size: %13px; min-height: 20px; }"
        "QLineEdit#twSearch:focus { border-color: %7; }"
        "QFrame#twCard { background: %2; border: 1px solid %6; border-radius: %15px; }"
        "QScrollArea#twScroll, QWidget#twList { background: transparent; border: none; }"
        "QScrollArea#twScroll > QWidget > QWidget { background: transparent; }"
        "#transferWindow QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px; }"
        "#transferWindow QScrollBar::handle:vertical { background: %6; border-radius: 3px; min-height: 32px; }"
        "#transferWindow QScrollBar::add-line:vertical, #transferWindow QScrollBar::sub-line:vertical { height: 0; }"
        "#transferWindow QScrollBar::add-page:vertical, #transferWindow QScrollBar::sub-page:vertical { background: transparent; }")
        .arg(page, card, hover, text, text2, border, accent, accent_bg, warn)
        .arg(warn_bg)
        .arg(DT::kFontBody)
        .arg(DT::kFontTitle)
        .arg(DT::kFontSmall)
        .arg(DT::kRadius)
        .arg(DT::kRadiusCard);
    setStyleSheet(qss);

    // Icons depend on the theme colors.
    QList<QAction*> actions = search_edit_->actions();
    for (QAction *action : actions) {
        if (action->objectName() == "twSearchIcon") {
            search_edit_->removeAction(action);
            action->deleteLater();
        }
    }
    QAction *search_icon = new QAction(faIcon(icon_search, DT::textSecondary()), QString(), search_edit_);
    search_icon->setObjectName("twSearchIcon");
    search_edit_->addAction(search_icon, QLineEdit::LeadingPosition);

    empty_icon_->setPixmap(GlyphIcon::pixmap(icon_exchange, DT::textMuted(), devicePixelRatioF(),
                                             64, 28, DT::surfaceHover(), DT::kRadiusCard));

    help_link_->setText(QString("<a href=\"%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
                            .arg(help_link_->property("help_url").toString(),
                                 DT::accentText().name(),
                                 tr("Descriptions for sync errors")));

    for (TransferRow *row : rows_)
        row->update();
}

void TransferWindow::onColorSchemeChanged()
{
    applyStyle();
}

void TransferWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ThemeChange && search_edit_ && empty_icon_ && help_link_)
        applyStyle();
}

void TransferWindow::showWithFilter(Filter filter)
{
    setFilter(filter);
    show();
    raise();
    activateWindow();
}

void TransferWindow::setFilter(Filter filter)
{
    filter_ = filter;
    if (filter_buttons_[filter])
        filter_buttons_[filter]->setChecked(true);
    if (filter_ == FilterErrors)
        markErrorsSeen();
    rebuildRows();
}

qint64 TransferWindow::getLastOpenSyncDialogTimestamp() const
{
    return last_open_sync_error_timestamp_;
}

void TransferWindow::markErrorsSeen()
{
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch() / 1000;
    last_open_sync_error_timestamp_ = timestamp;
    gui->settingsManager()->setLastOpenSyncDialogTimestamp(timestamp);
}

void TransferWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The window lists the sync errors too: opening it counts as having seen
    // them, as with the old sync errors dialog.
    markErrorsSeen();
    refresh();
    poll_timer_->start();
}

void TransferWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    poll_timer_->stop();
}

void TransferWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    hide();
}

void TransferWindow::refresh()
{
    pollTransfers();
    pollRates();
    errors_ = gui->trayIcon() ? gui->trayIcon()->syncErrors() : QList<SyncError>();
    updateFilterLabels();
    rebuildRows();
}

void TransferWindow::updateErrors()
{
    errors_ = gui->trayIcon() ? gui->trayIcon()->syncErrors() : QList<SyncError>();
    updateFilterLabels();
    if (isVisible())
        rebuildRows();
}

void TransferWindow::onRecentFilesChanged()
{
    if (isVisible())
        rebuildRows();
}

void TransferWindow::pollTransfers()
{
    TransferProgress progress;
    bool ok = false;

#if defined(Q_OS_MACOS)
    auto accounts = gui->accountManager()->activeAccounts();
    for (int i = 0; i < accounts.size(); i++) {
        SeafileRpcClient *rpc_client = gui->rpcClient(accounts.at(i).domainID());
        if (!rpc_client)
            continue;
        json_t *upload_reply = nullptr, *download_reply = nullptr;
        if (!rpc_client->getUploadProgress(&upload_reply))
            continue;
        QScopedPointer<json_t, JsonPointerCustomDeleter> upload(upload_reply);
        if (!rpc_client->getDownloadProgress(&download_reply))
            continue;
        QScopedPointer<json_t, JsonPointerCustomDeleter> download(download_reply);
        TransferProgress::fromJSON(upload.data(), download.data(), progress);
        ok = true;
    }
#else
    SeafileRpcClient *rpc_client = gui->rpcClient(EMPTY_DOMAIN_ID);
    json_t *upload_reply = nullptr, *download_reply = nullptr;
    if (rpc_client && rpc_client->getUploadProgress(&upload_reply)) {
        QScopedPointer<json_t, JsonPointerCustomDeleter> upload(upload_reply);
        if (rpc_client->getDownloadProgress(&download_reply)) {
            QScopedPointer<json_t, JsonPointerCustomDeleter> download(download_reply);
            TransferProgress::fromJSON(upload.data(), download.data(), progress);
            ok = true;
        }
    }
#endif

    if (!ok) {
        in_progress_.clear();
        return;
    }

    // The history of completed files (RecentFilesStore) is fed only by
    // MessagePoller::checkSyncStatus(), the single source.

    QList<TransferRowData> rows;
    auto add = [&rows](const TransferringInfo& info, bool upload) {
        TransferRowData row;
        row.kind = upload ? TransferRowData::Uploading : TransferRowData::Downloading;
        row.key = QString(upload ? "u:" : "d:") + info.server + "|" + info.username + "|" + info.file_path;
        row.file_path = normalizedPath(info.file_path);
        row.server = info.server;
        row.username = info.username;
        row.name = baseName(info.file_path);
        row.folder = displayFolder(parentPath(info.file_path));
        row.progress = info.total_bytes > 0
            ? int(qBound<quint64>(0, info.transferred_bytes * 100 / info.total_bytes, 100))
            : 0;
        row.status = (upload ? QCoreApplication::translate("TransferWindow", "Uploading · %1%")
                             : QCoreApplication::translate("TransferWindow", "Downloading · %1%"))
                         .arg(row.progress);
        if (info.total_bytes > 0) {
            row.detail = QCoreApplication::translate("TransferWindow", "%1 of %2")
                             .arg(readableFileSize(info.transferred_bytes),
                                  readableFileSize(info.total_bytes));
        }
        row.sort_time = QDateTime::currentMSecsSinceEpoch();
        rows.append(row);
    };
    for (const TransferringInfo& info : progress.downloading_files)
        add(info, false);
    for (const TransferringInfo& info : progress.uploading_files)
        add(info, true);
    in_progress_ = rows;
}

void TransferWindow::pollRates()
{
    qint64 up = 0, down = 0;
#if defined(Q_OS_MACOS)
    auto accounts = gui->accountManager()->activeAccounts();
    for (int i = 0; i < accounts.size(); i++) {
        SeafileRpcClient *rpc_client = gui->rpcClient(accounts.at(i).domainID());
        if (!rpc_client)
            continue;
        int rate = 0;
        if (rpc_client->getUploadRate(&rate) == 0)
            up += rate;
        if (rpc_client->getDownloadRate(&rate) == 0)
            down += rate;
    }
#else
    SeafileRpcClient *rpc_client = gui->rpcClient(EMPTY_DOMAIN_ID);
    if (rpc_client) {
        int rate = 0;
        if (rpc_client->getUploadRate(&rate) == 0)
            up = rate;
        if (rpc_client->getDownloadRate(&rate) == 0)
            down = rate;
    }
#endif
    up_rate_ = up;
    down_rate_ = down;
    rate_label_->setText(QString::fromUtf8("\xe2\x86\x93 %1    \xe2\x86\x91 %2")
                             .arg(translateTransferRate(int(down_rate_)),
                                  translateTransferRate(int(up_rate_))));
}

QList<TransferRowData> TransferWindow::collectRows() const
{
    QList<TransferRowData> rows;

    // 1. In progress
    rows.append(in_progress_);

    // 2. Errors
    for (const SyncError& error : errors_) {
        TransferRowData row;
        row.kind = TransferRowData::Error;
        row.key = "e:" + error.domain_id + "|" + error.repo_id + "|" + error.path +
                  "|" + QString::number(error.error_id);
        row.error = error;
        QString path = normalizedPath(error.path);
        if (path.startsWith('/'))
            path = path.mid(1);
        row.name = path.isEmpty() ? error.repo_name : baseName(path);
        QString folder = error.repo_name;
        QString parent = parentPath(path);
        if (!parent.isEmpty())
            folder = folder.isEmpty() ? parent : folder + "/" + parent;
        row.folder = displayFolder(folder);
        row.status = shortSyncErrorText(error.error_id);
        if (row.status.isEmpty()) {
            row.status = error.error_str.isEmpty()
                ? SyncError::syncErrorIdToErrorStr(error.error_id, error.path)
                : error.error_str;
        }
        if (error.timestamp > 0) {
            row.detail = relativeTime(QDateTime::fromSecsSinceEpoch(error.timestamp));
            row.sort_time = error.timestamp * 1000;
        }
        row.file_path = path;
        row.server = error.server;
        rows.append(row);
    }

    // 3. Completed, newest first
    QList<RecentFileEntry> entries = RecentFilesStore::instance()->entries();
    std::stable_sort(entries.begin(), entries.end(),
                     [](const RecentFileEntry& a, const RecentFileEntry& b) {
                         return a.time > b.time;
                     });
    for (const RecentFileEntry& entry : entries) {
        TransferRowData row;
        bool upload = entry.direction == RecentFileEntry::Upload;
        row.kind = upload ? TransferRowData::Uploaded : TransferRowData::Downloaded;
        row.key = QString(upload ? "U:" : "D:") + entry.path + "|" +
                  QString::number(entry.time.toMSecsSinceEpoch());
        row.file_path = normalizedPath(entry.path);
        row.name = baseName(entry.path);
        row.folder = displayFolder(entry.folder.isEmpty() ? parentPath(entry.path)
                                                          : normalizedPath(entry.folder));
        // Direction in the pill, time and size in the status line.
        row.pill = upload ? QCoreApplication::translate("TransferWindow", "Uploaded") : QCoreApplication::translate("TransferWindow", "Downloaded");
        row.status = relativeTime(entry.time);
        if (entry.size > 0) {
            QString size = readableFileSize(entry.size);
            row.status = row.status.isEmpty()
                ? size
                : row.status + QString::fromUtf8(" \xc2\xb7 ") + size;
        }
        row.sort_time = entry.time.toMSecsSinceEpoch();
        rows.append(row);
    }

    return rows;
}

bool TransferWindow::matchesFilter(const TransferRowData& row) const
{
    switch (filter_) {
    case FilterInProgress:
        if (!row.isInProgress())
            return false;
        break;
    case FilterUploaded:
        if (row.kind != TransferRowData::Uploaded)
            return false;
        break;
    case FilterDownloaded:
        if (row.kind != TransferRowData::Downloaded)
            return false;
        break;
    case FilterErrors:
        if (row.kind != TransferRowData::Error)
            return false;
        break;
    case FilterAll:
    default:
        break;
    }

    if (!search_text_.isEmpty()) {
        return row.name.contains(search_text_, Qt::CaseInsensitive) ||
               row.folder.contains(search_text_, Qt::CaseInsensitive);
    }
    return true;
}

void TransferWindow::rebuildRows()
{
    QList<TransferRowData> all = collectRows();
    QList<TransferRowData> rows;
    for (const TransferRowData& row : all) {
        if (matchesFilter(row))
            rows.append(row);
    }

    list_widget_->setUpdatesEnabled(false);

    // Reuse the existing row widgets, create or delete only the difference.
    while (rows_.size() > rows.size()) {
        TransferRow *row = rows_.takeLast();
        list_layout_->removeWidget(row);
        row->deleteLater();
    }
    while (rows_.size() < rows.size()) {
        TransferRow *row = new TransferRow(list_widget_);
        connect(row, &TransferRow::activated, this, &TransferWindow::onRowActivated);
        connect(row, &TransferRow::contextMenuRequested, this, &TransferWindow::onRowContextMenu);
        // Insert before the final stretch.
        list_layout_->insertWidget(list_layout_->count() - 1, row);
        rows_.append(row);
    }
    for (int i = 0; i < rows.size(); i++)
        rows_[i]->setData(rows[i]);

    list_widget_->setUpdatesEnabled(true);

    help_link_->setVisible(!errors_.isEmpty() &&
                           (filter_ == FilterErrors || filter_ == FilterAll));
    updateEmptyView();
}

void TransferWindow::updateFilterLabels()
{
    int errors = errors_.size();
    int in_progress = in_progress_.size();

    filter_buttons_[FilterAll]->setText(tr("All"));
    filter_buttons_[FilterInProgress]->setText(
        in_progress > 0 ? tr("In progress (%1)").arg(in_progress) : tr("In progress"));
    filter_buttons_[FilterUploaded]->setText(tr("Uploaded", "filter"));
    filter_buttons_[FilterDownloaded]->setText(tr("Downloaded", "filter"));
    filter_buttons_[FilterErrors]->setText(
        errors > 0 ? tr("Errors (%1)").arg(errors) : tr("Errors"));

    QPushButton *error_button = filter_buttons_[FilterErrors];
    bool warning = errors > 0;
    if (error_button->property("warning").toBool() != warning) {
        error_button->setProperty("warning", warning);
        error_button->style()->unpolish(error_button);
        error_button->style()->polish(error_button);
    }
}

void TransferWindow::updateEmptyView()
{
    if (!rows_.isEmpty()) {
        stack_->setCurrentIndex(INDEX_LIST_VIEW);
        return;
    }

    if (!search_text_.isEmpty()) {
        empty_title_->setText(tr("No files found"));
        empty_text_->setText(tr("No files match \"%1\".").arg(search_text_));
    } else if (filter_ == FilterErrors) {
        empty_title_->setText(tr("No errors"));
        empty_text_->setText(tr("All files are syncing correctly."));
    } else if (filter_ == FilterUploaded) {
        empty_title_->setText(tr("No uploaded files"));
        empty_text_->setText(tr("Files you upload to the server appear here, with the upload time."));
    } else if (filter_ == FilterDownloaded) {
        empty_title_->setText(tr("No downloaded files"));
        empty_text_->setText(tr("Files you download from the server appear here, with the download time."));
    } else {
        empty_title_->setText(tr("No transfers in progress"));
        empty_text_->setText(tr("When SeaDrive uploads or downloads a file, you see its progress here and, at the end, when it was completed."));
    }
    stack_->setCurrentIndex(INDEX_EMPTY_VIEW);
}

void TransferWindow::onFilterClicked(int id)
{
    if (id < FilterAll || id > FilterErrors)
        return;
    setFilter(static_cast<Filter>(id));
}

void TransferWindow::onSearchChanged(const QString& text)
{
    search_text_ = text.trimmed();
    rebuildRows();
}

QString TransferWindow::localPathForError(const TransferRowData& row, QString *root) const
{
    // Sync errors come in two shapes:
    //  - error inside a library: repo_id is set and `path` is relative to the
    //    library folder (e.g. "My Libraries/<library>" under the sync root);
    //  - error outside any library (e.g. a file dropped in the "My Libraries"
    //    root): repo_id is empty and `path` is relative to the sync root of
    //    the account (e.g. "My Libraries/Cloud CASA.png").
    const SyncError& error = row.error;
    const QString rel = row.file_path;

    // Candidate sync roots, best first.
    QStringList roots;
    auto addRoot = [&roots](const QString& r) {
        QString clean = QDir::cleanPath(r);
        if (!r.isEmpty() && !roots.contains(clean, Qt::CaseInsensitive))
            roots.append(clean);
    };

    QString repo_uname;
    if (!error.repo_id.isEmpty()) {
        QList<SeafileRpcClient *> clients;
        if (SeafileRpcClient *c = gui->rpcClient(error.domain_id))
            clients.append(c);
        if (SeafileRpcClient *c = gui->rpcClient(EMPTY_DOMAIN_ID)) {
            if (!clients.contains(c))
                clients.append(c);
        }
        for (SeafileRpcClient *rpc_client : clients) {
            json_t *ret_obj = nullptr;
            if (rpc_client->getAccountByRepoId(error.repo_id, &ret_obj) && ret_obj) {
                QScopedPointer<json_t, JsonPointerCustomDeleter> obj(ret_obj);
                Account account = gui->accountManager()->getAccountFromJson(obj.data());
                addRoot(account.syncRoot);
            }
            if (repo_uname.isEmpty())
                rpc_client->getRepoUnameById(error.repo_id, &repo_uname);
            if (!roots.isEmpty() && !repo_uname.isEmpty())
                break;
        }
    }

    // Accounts of the same server first, then every other account.
    const auto accounts = gui->accountManager()->activeAccounts();
    const QString server_host = QUrl(error.server).host();
    for (const Account& a : accounts) {
        if (!server_host.isEmpty() &&
            a.serverUrl.host().compare(server_host, Qt::CaseInsensitive) == 0)
            addRoot(a.syncRoot);
    }
    for (const Account& a : accounts)
        addRoot(a.syncRoot);
    addRoot(gui->seadriveRoot());

    if (roots.isEmpty())
        return QString();

    // Candidate paths, best first.
    QStringList candidates;
    auto addCandidate = [&candidates](const QString& base, const QString& sub) {
        QString p = sub.isEmpty() ? base : ::pathJoin(base, sub);
        p = QDir::cleanPath(p);
        if (!candidates.contains(p, Qt::CaseInsensitive))
            candidates.append(p);
    };
    for (const QString& r : roots) {
        if (!repo_uname.isEmpty())
            addCandidate(::pathJoin(r, repo_uname), rel);
        if (!error.repo_id.isEmpty() && !error.repo_name.isEmpty()) {
            // Library folder when the daemon could not give its display name.
            addCandidate(::pathJoin(::pathJoin(r, "My Libraries"), error.repo_name), rel);
            addCandidate(::pathJoin(r, error.repo_name), rel);
        }
        // Errors outside a library: the daemon may put the top folder (e.g.
        // "My Libraries") in repo_name, or leave it in the path.
        if (error.repo_id.isEmpty() && !error.repo_name.isEmpty())
            addCandidate(::pathJoin(r, error.repo_name), rel);
        // Path relative to the sync root.
        addCandidate(r, rel);
    }

    // A candidate that exists wins.
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) {
            if (root) {
                for (const QString& r : roots) {
                    if (c.startsWith(r, Qt::CaseInsensitive)) {
                        *root = r;
                        break;
                    }
                }
            }
            return c;
        }
    }

    // Nothing exists (file deleted or moved): the best guess. The caller
    // opens its nearest existing folder.
    if (root)
        *root = roots.first();
    return candidates.first();
}

QString TransferWindow::localPathForRow(const TransferRowData& row, QString *root) const
{
    if (row.kind == TransferRowData::Error)
        return localPathForError(row, root);

    if (row.file_path.isEmpty())
        return QString();
    if (QDir::isAbsolutePath(row.file_path))
        return row.file_path;

    // Paths from the daemon are relative to the SeaDrive root of the account.
    Account account;
    if (!row.server.isEmpty())
        account = gui->accountManager()->getAccountByUrlAndUsername(row.server, row.username);
    if (account.syncRoot.isEmpty()) {
        auto accounts = gui->accountManager()->activeAccounts();
        for (int i = 0; i < accounts.size(); i++) {
            const Account& a = accounts.at(i);
            if (a.syncRoot.isEmpty())
                continue;
            if (QFileInfo::exists(::pathJoin(a.syncRoot, row.file_path))) {
                account = a;
                break;
            }
            if (account.syncRoot.isEmpty())
                account = a;
        }
    }
    if (account.syncRoot.isEmpty())
        return QString();
    return ::pathJoin(account.syncRoot, row.file_path);
}

void TransferWindow::openFolderOfRow(const TransferRowData& row)
{
    QString root;
    QString path = localPathForRow(row, &root);
    if (path.isEmpty()) {
        qWarning("Transfer window: no local path for \"%s\"", toCStr(row.file_path));
        return;
    }

    if (QFileInfo::exists(path)) {
        // Explorer/Finder opens the parent folder with the item selected.
        showInGraphicalShell(path);
        return;
    }

    // The file is not there anymore: open the nearest folder that exists,
    // without climbing above the SeaDrive root of the account.
    QString dir = path;
    while (!dir.isEmpty() && !QFileInfo(dir).isDir()) {
        QString parent = QFileInfo(dir).path();
        if (parent == dir) {
            dir.clear();
            break;
        }
        dir = parent;
    }
    if (!root.isEmpty() &&
        (dir.isEmpty() || !QDir::cleanPath(dir).startsWith(root, Qt::CaseInsensitive)))
        dir = root;
    if (!dir.isEmpty() && QFileInfo(dir).isDir())
        openUrl(QUrl::fromLocalFile(dir));
}

void TransferWindow::onRowActivated(TransferRow *row)
{
    if (row)
        openFolderOfRow(row->data());
}

void TransferWindow::onRowContextMenu(TransferRow *row, const QPoint& global_pos)
{
    if (!row)
        return;
    const TransferRowData data = row->data();

    ModernMenu menu(this);
    QAction *open_action = menu.addIconAction(icon_folder_open, tr("Open folder"));
    QAction *delete_action = nullptr;
    if (data.kind == TransferRowData::Error)
        delete_action = menu.addIconAction(icon_trash, tr("Remove the error from the list"));

    QAction *chosen = menu.exec(global_pos);
    if (chosen == open_action) {
        openFolderOfRow(data);
    } else if (chosen && chosen == delete_action) {
        deleteSyncError(data.error);
    }
}

void TransferWindow::deleteSyncError(const SyncError& error)
{
    SeafileRpcClient *rpc_client = gui->rpcClient(error.domain_id);
    if (!rpc_client) {
        qWarning() << "Delete file sync error failed: rpc client not found";
        return;
    }
    if (!rpc_client->deleteFileSyncError(error.repo_id, error.path, error.error_id)) {
        gui->messageBox(tr("Delete file sync error failed"));
        return;
    }
    refresh();
}
