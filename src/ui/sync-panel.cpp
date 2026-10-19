#include <QtGlobal>
#include <climits>

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#if defined(Q_OS_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "QtAwesome.h"
#include "utils/utils.h"
#include "recent-files-store.h"
#include "sync-pause.h"
#include "modern-menu.h"
#include "design-tokens.h"
#include "glyph-icon.h"
#include "settings-pages/account-page.h"

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
const char *kPropAccountId = "sp_account_id";
const char *kSettingsGroup = "SyncPanel";
const char *kAccountKey = "account";
const int kAccountAvatarSize = 22;
const int kMenuAvatarSize = 32;
const int kMaxAccountNameWidth = 150;

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
      syncing_(false),
      sent_rate_(0),
      recv_rate_(0),
      last_check_(QDateTime::currentDateTime()),
      hidden_at_msec_(0),
      status_kind_(StatusOk)
{
    setObjectName("syncPanel");
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(tr("SeaDrive"));
    setFixedSize(kPanelWidth, kPanelHeight);

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    current_id_ = settings.value(kAccountKey).toString();
    settings.endGroup();

    createUi();

    tick_timer_ = new QTimer(this);
    tick_timer_->setInterval(kTickIntervalMSecs);
    connect(tick_timer_, &QTimer::timeout, this, &SyncPanel::tick);

    connect(RecentFilesStore::instance(), &RecentFilesStore::changed,
            this, &SyncPanel::refreshList);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &SyncPanel::applyTheme);
    connect(SyncPauseManager::instance(), &SyncPauseManager::changed,
            this, &SyncPanel::refreshStatus);

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

    // "SeaDrive · name" with one account; "SeaDrive" + account selector
    // with more (see refreshAccountButton()).
    title_layout_ = new QHBoxLayout;
    title_layout_->setContentsMargins(0, 0, 0, 0);
    title_layout_->setSpacing(8);
    title_label_ = new QLabel(header);
    title_label_->setObjectName("spTitle");
    title_label_->setTextFormat(Qt::RichText);
    title_label_->setWordWrap(true);
    title_layout_->addWidget(title_label_, 1);

    account_button_ = new QToolButton(header);
    account_button_->setObjectName("spAccountButton");
    account_button_->setCursor(Qt::PointingHandCursor);
    account_button_->setFixedHeight(32);
    account_button_->setAccessibleName(tr("Switch account"));
    QHBoxLayout *chip = new QHBoxLayout(account_button_);
    chip->setContentsMargins(5, 0, 8, 0);
    chip->setSpacing(6);
    account_avatar_ = new QLabel(account_button_);
    account_avatar_->setFixedSize(kAccountAvatarSize, kAccountAvatarSize);
    account_name_label_ = new QLabel(account_button_);
    account_name_label_->setObjectName("spAccountName");
    account_chevron_ = new QLabel(account_button_);
    account_chevron_->setFixedSize(14, 14);
    account_chevron_->setAlignment(Qt::AlignCenter);
    for (QLabel *label : {account_avatar_, account_name_label_, account_chevron_}) {
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    chip->addWidget(account_avatar_);
    chip->addWidget(account_name_label_);
    chip->addWidget(account_chevron_);
    connect(account_button_, &QToolButton::clicked, this, &SyncPanel::onAccountButtonClicked);
    account_button_->hide();
    title_layout_->addWidget(account_button_, 0, Qt::AlignVCenter);
    title_layout_->addStretch(0);
    header_layout->addLayout(title_layout_, 1);

    // Pause / resume the sync (all accounts: the daemon has one switch).
    pause_button_ = new QToolButton(header);
    pause_button_->setObjectName("spIconButton");
    pause_button_->setCursor(Qt::PointingHandCursor);
    pause_button_->setFixedSize(36, 36);
    pause_button_->setIconSize(QSize(kIconSize, kIconSize));
    pause_button_->setAutoRaise(true);
    connect(pause_button_, &QToolButton::clicked, this, &SyncPanel::onPauseButtonClicked);
    header_layout->addWidget(pause_button_, 0, Qt::AlignTop);

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

    resume_button_ = new QPushButton(tr("Resume"), status_box_);
    resume_button_->setObjectName("spResumeButton");
    resume_button_->setCursor(Qt::PointingHandCursor);
    resume_button_->setIconSize(QSize(12, 12));
    resume_button_->setAccessibleName(tr("Resume sync"));
    resume_button_->hide();
    connect(resume_button_, &QPushButton::clicked, this, []() {
        SyncPauseManager::instance()->resume();
    });
    status_layout->addWidget(resume_button_, 0, Qt::AlignVCenter);

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

    style += QString("QToolButton#spAccountButton { background: %1; border: 1px solid transparent;"
                     " border-radius: %2; }"
                     "QToolButton#spAccountButton:hover { border: 1px solid %3; }"
                     "QToolButton#spAccountButton:focus { border: 1px solid %4; }"
                     "QWidget#syncPanel QLabel#spAccountName { font-size: %5; }")
                 .arg(css(surfaceHover()), px(kRadius), css(border()), css(accentText()),
                      px(kFontBody));

    style += QString("QToolButton#spIconButton { background: transparent; border: none;"
                     " border-radius: %1; }"
                     "QToolButton#spIconButton:hover { background: %2; }"
                     "QToolButton#spIconButton:focus { border: 1px solid %3; }")
                 .arg(px(kRadius), css(surfaceHover()), css(accentText()));

    style += QString("QPushButton#spResumeButton { background: %1; color: %2;"
                     " border: 1px solid %3; border-radius: %4; padding: 6px 12px;"
                     " font-size: %5; }"
                     "QPushButton#spResumeButton:hover { background: %6; }"
                     "QPushButton#spResumeButton:focus { border: 1px solid %7; }")
                 .arg(css(surfaceCard()), css(textPrimary()), css(border()), px(kRadius),
                      px(kFontSmall), css(surfaceHover()), css(accentText()));

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
    resume_button_->setIcon(faIcon(icon_play, textPrimary()));
    account_chevron_->setPixmap(faIcon(icon_chevron_down, textSecondary())
                                    .pixmap(QSize(12, 12)));
    refreshAccountButton();
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

void SyncPanel::setAccounts(const QList<SyncPanelAccount>& accounts)
{
    if (accounts == accounts_) {
        return;
    }
    accounts_ = accounts;
    refreshAccountButton();
    refreshStatus();
    refreshList();
}

SyncPanelAccount SyncPanel::currentAccount() const
{
    for (const SyncPanelAccount& account : accounts_) {
        if (account.id == current_id_) {
            return account;
        }
    }
    // Nothing chosen yet, or the chosen account was removed.
    return accounts_.isEmpty() ? SyncPanelAccount() : accounts_.first();
}

void SyncPanel::selectAccount(const QString& id)
{
    if (id == currentAccount().id) {
        return;
    }
    current_id_ = id;
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(kAccountKey, id);
    settings.endGroup();
    refreshAccountButton();
    refreshStatus();
    refreshList();
}

void SyncPanel::refreshAccountButton()
{
    const bool many = hasManyAccounts();
    account_button_->setVisible(many);
    // One account: "SeaDrive · name" fills the row and may wrap. More:
    // "SeaDrive" and the selector right after it, the space goes to the end.
    title_label_->setWordWrap(!many);
    title_layout_->setStretch(0, many ? 0 : 1);
    title_layout_->setStretch(2, many ? 1 : 0);
    if (!many) {
        return;
    }
    const SyncPanelAccount account = currentAccount();
    account_avatar_->setPixmap(
        AccountDisplay::avatar(account.name, kAccountAvatarSize, devicePixelRatioF()));
    account_name_label_->setText(account_name_label_->fontMetrics().elidedText(
        account.name, Qt::ElideRight, kMaxAccountNameWidth));
    account_button_->setToolTip(tr("Switch account"));
    account_button_->setFixedWidth(account_button_->layout()->sizeHint().width());
}

bool SyncPanel::belongsTo(const SyncPanelAccount& account,
                          const QString& server, const QString& username)
{
    // Unknown account (entries of older versions, some errors): shown for
    // every account.
    if (server.isEmpty()) {
        return true;
    }
    const QUrl url = QUrl::fromUserInput(server);
    auto port = [](const QUrl& u) {
        return u.port(u.scheme() == QLatin1String("https") ? 443 : 80);
    };
    if (url.host().compare(account.server.host(), Qt::CaseInsensitive) != 0 ||
        port(url) != port(account.server)) {
        return false;
    }
    return username.isEmpty() || username == account.username;
}

bool SyncPanel::belongsToCurrent(const QString& server, const QString& username) const
{
    // With a single account everything belongs to it.
    if (!hasManyAccounts()) {
        return true;
    }
    return belongsTo(currentAccount(), server, username);
}

int SyncPanel::errorCountFor(const SyncPanelAccount& account) const
{
    if (!hasManyAccounts()) {
        return errors_.size();
    }
    int count = 0;
    for (const SyncError& error : errors_) {
        if (belongsTo(account, error.server, QString())) {
            count++;
        }
    }
    return count;
}

bool SyncPanel::isConnected(const SyncPanelAccount& account) const
{
    if (!hasManyAccounts()) {
        return network_errors_.isEmpty();
    }
    for (const SyncError& error : network_errors_) {
        if (belongsTo(account, error.server, QString())) {
            return false;
        }
    }
    return true;
}

QString SyncPanel::rootPath() const
{
    return currentAccount().root;
}

void SyncPanel::setErrors(const QList<SyncError>& errors, const QList<SyncError>& networkErrors)
{
    errors_ = errors;
    network_errors_ = networkErrors;
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
    const SyncPanelAccount account = currentAccount();
    if (!hasManyAccounts() && !account.name.isEmpty()) {
        title += QString(" <span style=\"color:%1; font-size:%2px;\">&middot; %3</span>")
                     .arg(textSecondary().name(), QString::number(kFontBody),
                          account.name.toHtmlEscaped());
    }
    title_label_->setText(title);

    // Status box: state of the chosen account.
    const bool connected = isConnected(account);
    const int error_count = errorCountFor(account);
    int active = 0;
    quint64 done = 0, total = 0;
    for (const auto *list : {&transfers_.uploading_files, &transfers_.downloading_files}) {
        for (const TransferringInfo& info : *list) {
            if (!belongsToCurrent(info.server, info.username)) {
                continue;
            }
            active++;
            done += info.transferred_bytes;
            total += info.total_bytes;
        }
    }
    // The daemon reports one "syncing" flag for every account: with more
    // accounts it counts only when this one has files in progress, or when
    // no account has any (the daemon is busy without a file list).
    const int active_all =
        transfers_.uploading_files.size() + transfers_.downloading_files.size();
    const bool syncing = syncing_ && (!hasManyAccounts() || active > 0 || active_all == 0);

    QColor bg, fg;
    int icon_code;
    QString title_text, detail_text;
    bool show_progress = false;

    // Pause: the state comes from the daemon, the end time from the client.
    SyncPauseManager *pause = SyncPauseManager::instance();
    const bool paused = pause->isPaused();
    pause_button_->setIcon(faIcon(paused ? icon_play : icon_pause, textSecondary()));
    pause_button_->setToolTip(paused ? tr("Resume sync") : tr("Pause sync"));
    pause_button_->setAccessibleName(pause_button_->toolTip());
    pause_button_->setEnabled(pause->isStateKnown());
    pause_button_->setVisible(pause->isStateKnown());

    if (paused) {
        status_kind_ = StatusPaused;
        bg = surfaceHover();
        fg = textSecondary();
        icon_code = icon_pause;
        title_text = tr("Paused");
        const QDateTime until = pause->resumeTime();
        if (!until.isValid()) {
            detail_text = tr("Until you resume");
        } else {
            const qint64 days = QDate::currentDate().daysTo(until.date());
            const QString hhmm = until.toString("HH:mm");
            if (days <= 0) {
                detail_text = tr("Resumes at %1").arg(hhmm);
            } else if (days == 1) {
                detail_text = tr("Resumes tomorrow at %1").arg(hhmm);
            } else {
                detail_text = tr("Resumes on %1").arg(until.toString("dd-MM-yyyy HH:mm"));
            }
        }
    } else if (!connected) {
        status_kind_ = StatusOffline;
        bg = surfaceHover();
        fg = textSecondary();
        icon_code = icon_unlink;
        title_text = tr("Not connected");
        detail_text = tr("SeaDrive will retry on its own as soon as the server is reachable.");
    } else if (syncing || active > 0) {
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
    } else if (error_count > 0) {
        status_kind_ = StatusErrors;
        bg = warningBg();
        fg = warningText();
        icon_code = icon_warning_sign;
        title_text = tr("%n sync error(s)", nullptr, error_count);
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
    if (status_kind_ == StatusPaused) {
        // Title in the normal text color, as in the other boxes the detail
        // stays secondary.
        status_box_->setStyleSheet(status_box_->styleSheet() +
                                   QString("QWidget#syncPanel QLabel#spStatusTitle { color: %1; }")
                                       .arg(css(textPrimary())));
    }
    resume_button_->setVisible(status_kind_ == StatusPaused);

    // Files waiting while paused look dimmed.
    if (paused && !list_container_->graphicsEffect()) {
        QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(list_container_);
        effect->setOpacity(0.55);
        list_container_->setGraphicsEffect(effect);
    } else if (!paused && list_container_->graphicsEffect()) {
        list_container_->setGraphicsEffect(nullptr);
    }
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
            if (!belongsToCurrent(info.server, info.username)) {
                continue;
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
        if (!belongsToCurrent(entry.server, entry.username)) {
            continue;
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
            // A menu opened from the panel (pause, accounts) is not a
            // reason to close it.
            if (QApplication::activePopupWidget()) {
                return;
            }
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

    const QVariant account_id = w->property(kPropAccountId);
    if (account_id.isValid()) {
        // A row of the account menu: choose it and close the menu.
        selectAccount(account_id.toString());
        for (QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
            if (QMenu *menu = qobject_cast<QMenu *>(p)) {
                menu->close();
                break;
            }
        }
        return true;
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

void SyncPanel::addPauseActions(QMenu *menu)
{
    QAction *heading = menu->addAction(tr("Pause for"));
    heading->setEnabled(false);

    struct Choice {
        int minutes;
        QString text;
    };
    const QList<Choice> choices = {
        {30, tr("%n minute(s)", nullptr, 30)},
        {2 * 60, tr("%n hour(s)", nullptr, 2)},
        {8 * 60, tr("%n hour(s)", nullptr, 8)},
    };
    for (const Choice& choice : choices) {
        QAction *action = menu->addAction(choice.text);
        ModernMenu::decorateAction(action, icon_time);
        const int minutes = choice.minutes;
        QObject::connect(action, &QAction::triggered, menu, [minutes]() {
            SyncPauseManager::instance()->pauseFor(minutes);
        });
    }
    menu->addSeparator();
    QAction *until_resumed = menu->addAction(tr("Until I resume"));
    ModernMenu::decorateAction(until_resumed, icon_pause);
    QObject::connect(until_resumed, &QAction::triggered, menu, []() {
        SyncPauseManager::instance()->pauseFor(0);
    });
}

void SyncPanel::onPauseButtonClicked()
{
    SyncPauseManager *pause = SyncPauseManager::instance();
    if (pause->isPaused()) {
        pause->resume();
        return;
    }
    ModernMenu menu(this);
    addPauseActions(&menu);
    // Below the button, right edge aligned with it.
    menu.ensurePolished();
    const QSize size = menu.sizeHint();
    const QPoint pos = pause_button_->mapToGlobal(
        QPoint(pause_button_->width() - size.width(), pause_button_->height() + 4));
    QAction *chosen = menu.exec(pos);
    giveFocusBackAfterMenu(chosen != nullptr);
}

void SyncPanel::giveFocusBackAfterMenu(bool chosen)
{
    if (!isVisible()) {
        return;
    }
    // exec() takes the focus. Give it back when an entry was chosen or
    // the menu was closed inside the panel, so the panel keeps working as a
    // flyout; a click elsewhere closes the panel too.
    if (chosen || geometry().contains(QCursor::pos())) {
        activateWindow();
        setFocus();
    } else {
        hide();
    }
}

QWidget *SyncPanel::createAccountMenuRow(const SyncPanelAccount& account, bool current)
{
    QFrame *row = new QFrame;
    row->setObjectName("spAccountRow");
    row->setAttribute(Qt::WA_Hover);
    row->setCursor(Qt::PointingHandCursor);
    row->setFocusPolicy(Qt::StrongFocus);
    row->setProperty(kPropAccountId, account.id);
    row->installEventFilter(this);

    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(kRowPadding);

    QLabel *avatar = new QLabel(row);
    avatar->setFixedSize(kMenuAvatarSize, kMenuAvatarSize);
    avatar->setPixmap(AccountDisplay::avatar(account.name, kMenuAvatarSize, devicePixelRatioF()));
    layout->addWidget(avatar, 0, Qt::AlignVCenter);

    // Name, then a status dot with errors (if any) and the server.
    const int errors = errorCountFor(account);
    const bool connected = isConnected(account);
    QColor dot = successText();
    QString detail = account.server.host();
    if (!connected) {
        dot = textMuted();
        detail = tr("Not connected") + QStringLiteral(" \u00b7 ") + detail;
    } else if (errors > 0) {
        dot = warningText();
        detail = tr("%n error(s)", nullptr, errors) + QStringLiteral(" \u00b7 ") + detail;
    }

    QVBoxLayout *texts = new QVBoxLayout;
    texts->setSpacing(2);
    QLabel *name = new QLabel(account.name.toHtmlEscaped(), row);
    name->setObjectName("spAccountRowName");
    QLabel *desc = new QLabel(
        QString("<span style=\"color:%1;\">&#9679;</span>&nbsp;%2")
            .arg(dot.name(), detail.toHtmlEscaped()),
        row);
    desc->setObjectName("spAccountRowDetail");
    desc->setTextFormat(Qt::RichText);
    texts->addWidget(name);
    texts->addWidget(desc);
    layout->addLayout(texts, 1);

    QLabel *check = new QLabel(row);
    check->setFixedSize(kIconSize, kIconSize);
    if (current) {
        check->setPixmap(faIcon(icon_ok, accentText()).pixmap(QSize(16, 16)));
    }
    layout->addWidget(check, 0, Qt::AlignVCenter);

    for (QLabel *label : row->findChildren<QLabel *>()) {
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    row->setAccessibleName(account.name + QStringLiteral(", ") + detail);
    row->setStyleSheet(
        QString("QFrame#spAccountRow { background: %1; border: 1px solid transparent;"
                " border-radius: %2; }"
                "QFrame#spAccountRow:hover { background: %3; }"
                "QFrame#spAccountRow:focus { border: 1px solid %4; }"
                "QLabel { background: transparent; color: %5; }"
                "QLabel#spAccountRowName { font-size: %6; font-weight: 600; }"
                "QLabel#spAccountRowDetail { font-size: %7; color: %8; }")
            .arg(current ? css(accentBg()) : QStringLiteral("transparent"),
                 px(kRadius - 2), current ? css(accentBg()) : css(surfaceHover()),
                 css(accentText()), css(textPrimary()), px(kFontBody), px(kFontSmall),
                 css(textSecondary())));
    row->setMinimumWidth(260);
    return row;
}

void SyncPanel::onAccountButtonClicked()
{
    if (!hasManyAccounts()) {
        return;
    }
    ModernMenu menu(this);
    QAction *heading = menu.addAction(tr("Accounts"));
    heading->setEnabled(false);

    const QString current = currentAccount().id;
    for (const SyncPanelAccount& account : accounts_) {
        QWidgetAction *action = new QWidgetAction(&menu);
        action->setDefaultWidget(createAccountMenuRow(account, account.id == current));
        menu.addAction(action);
    }
    menu.addSeparator();
    QAction *add = menu.addIconAction(icon_plus, tr("Add account"));
    QAction *manage = menu.addIconAction(icon_user, tr("Manage accounts"));

    // Below the selector, left edge aligned with it.
    QAction *chosen = menu.exec(
        account_button_->mapToGlobal(QPoint(0, account_button_->height() + 4)));
    if (chosen == add) {
        hide();
        emit addAccountRequested();
        return;
    }
    if (chosen == manage) {
        hide();
        emit manageAccountsRequested();
        return;
    }
    giveFocusBackAfterMenu(chosen != nullptr);
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
    const QString root = rootPath();
    if (QDir::isAbsolutePath(path) || root.isEmpty()) {
        return QDir::cleanPath(path);
    }
    return QDir::cleanPath(QDir(root).filePath(path));
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
    const QString root = rootPath();
    if (!root.isEmpty() &&
        (dir.isEmpty() || !QDir::cleanPath(dir).startsWith(QDir::cleanPath(root),
                                                          Qt::CaseInsensitive))) {
        dir = root;
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
