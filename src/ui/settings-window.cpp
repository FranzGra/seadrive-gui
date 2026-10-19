#include <QtWidgets>
#include <QStyleHints>

#include "QtAwesome.h"
#include "seadrive-gui.h"
#include "account-mgr.h"
#include "settings-mgr.h"
#include "utils/utils.h"
#if defined(_MSC_VER)
#include "utils/registry.h"
#endif

#include "src/ui/design-tokens.h"
#include "src/ui/settings-pages/settings-page.h"
#include "src/ui/settings-pages/general-page.h"
#include "src/ui/settings-pages/sync-page.h"
#include "src/ui/settings-pages/cache-page.h"
#include "src/ui/settings-pages/account-page.h"
#include "src/ui/settings-pages/encrypted-repos-page.h"
#include "src/ui/settings-pages/network-page.h"
#include "src/ui/settings-pages/info-pages.h"

#include "settings-window.h"

namespace {

const int kSidebarWidth = 200;
const int kHeaderAvatarSize = 36;
const int kNavIconSize = 16;
const int kHeaderRefreshMSecs = 3000;

} // namespace

SettingsWindow::SettingsWindow(QWidget *parent)
    : QWidget(parent, Qt::Window),
      stack_(nullptr),
      nav_group_(nullptr),
      quit_btn_(nullptr),
      avatar_(nullptr),
      account_name_(nullptr),
      account_status_(nullptr),
      header_timer_(nullptr),
      built_(false),
      applying_style_(false)
{
    setObjectName("settingsWindow");
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(tr("%1 Settings").arg(getBrand()));
    setWindowIcon(QIcon(":/images/seafile.png"));
    setMinimumSize(720, 500);
    resize(920, 660);

    // The content is built on first use (ensureBuilt): this object is created
    // inside the SeadriveGui constructor, before the global "gui" pointer and
    // the QtAwesome icon font exist.
}

void SettingsWindow::setVisible(bool visible)
{
    if (visible) {
        ensureBuilt();
    }
    QWidget::setVisible(visible);
}

void SettingsWindow::prebuild()
{
    if (built_ || isVisible()) {
        return;
    }
    ensureBuilt();
    // Apply the style sheet and compute the layout now instead of at the
    // first show.
    ensurePolished();
    // Fill the pages once: the first text layout (font fallback for long
    // paths) is slow and would otherwise happen while the user waits.
    for (SettingsPage *page : pages_) {
        page->load();
    }
    if (layout()) {
        layout()->activate();
    }
    // Create the native window too, still hidden.
    winId();
}

void SettingsWindow::ensureBuilt()
{
    if (built_) {
        return;
    }
    built_ = true;

    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(createSidebar());

    stack_ = new QStackedWidget;
    stack_->setObjectName("settingsStack");
    root->addWidget(stack_, 1);

    // Order must match enum Page.
    pages_ << new GeneralPage
           << new SyncPage
           << new CachePage
           << new AccountPage
           << new EncryptedReposPage
           << new NetworkPage
           << new LogsPage
           << new AboutPage;
    for (SettingsPage *page : pages_) {
        stack_->addWidget(page);
    }

    connect(pages_[PageAccount], SIGNAL(accountsChanged()), this, SLOT(refreshHeader()));
    connect(gui->accountManager(), SIGNAL(accountInfoUpdated(const Account&)),
            this, SLOT(refreshHeader()));

    header_timer_ = new QTimer(this);
    header_timer_->setInterval(kHeaderRefreshMSecs);
    connect(header_timer_, SIGNAL(timeout()), this, SLOT(refreshHeader()));

    // Esc closes (hides) the window, like a dialog.
    QAction *close_action = new QAction(this);
    close_action->setShortcut(QKeySequence::Cancel);
    connect(close_action, SIGNAL(triggered()), this, SLOT(close()));
    addAction(close_action);

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &SettingsWindow::applyStyle);

    applyStyle();
    setCurrentPage(PageGeneral);
}

QWidget *SettingsWindow::createSidebar()
{
    using namespace DesignTokens;

    QWidget *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setFixedWidth(kSidebarWidth);

    QVBoxLayout *v = new QVBoxLayout(sidebar);
    v->setContentsMargins(12, kPadding + 4, 12, kPadding);
    v->setSpacing(2);

    // Account header: avatar, name, connection state
    QWidget *header = new QWidget;
    QHBoxLayout *hl = new QHBoxLayout(header);
    hl->setContentsMargins(4, 0, 4, 0);
    hl->setSpacing(10);
    avatar_ = new QLabel;
    avatar_->setFixedSize(kHeaderAvatarSize, kHeaderAvatarSize);
    hl->addWidget(avatar_, 0, Qt::AlignTop);
    QVBoxLayout *tl = new QVBoxLayout;
    tl->setSpacing(0);
    account_name_ = new QLabel;
    account_name_->setObjectName("sidebarName");
    account_name_->setWordWrap(true);
    account_name_->setTextFormat(Qt::PlainText);
    account_status_ = new QLabel;
    account_status_->setObjectName("sidebarStatus");
    account_status_->setWordWrap(true);
    tl->addWidget(account_name_);
    tl->addWidget(account_status_);
    hl->addLayout(tl, 1);
    v->addWidget(header);
    v->addSpacing(kPadding + 4);

    nav_group_ = new QButtonGroup(this);
    nav_group_->setExclusive(true);
    connect(nav_group_, SIGNAL(idClicked(int)), this, SLOT(onNavClicked(int)));

    v->addWidget(addNavItem(PageGeneral, tr("General"), icon_cog));
    v->addWidget(addNavItem(PageSync, tr("Sync"), icon_refresh));
    v->addWidget(addNavItem(PageCache, tr("Cache"), icon_hdd));
    v->addWidget(addNavItem(PageAccount, tr("Accounts"), icon_user));
    v->addWidget(addNavItem(PageEncrypted, tr("Encrypted Libraries"), icon_lock));
    v->addWidget(addNavItem(PageNetwork, tr("Network and proxy"), icon_globe));
    v->addWidget(addNavItem(PageLogs, tr("Logs and diagnostics"), icon_file_text));
    v->addWidget(addNavItem(PageAbout, tr("About"), icon_info_sign));

    v->addStretch(1);

    QFrame *sep = new QFrame;
    sep->setObjectName("rowSeparator");
    sep->setFixedHeight(1);
    v->addWidget(sep);
    v->addSpacing(8);

    quit_btn_ = new QPushButton(tr("Quit %1").arg(getBrand()));
    quit_btn_->setObjectName("navQuit");
    quit_btn_->setCursor(Qt::PointingHandCursor);
    quit_btn_->setIconSize(QSize(kNavIconSize, kNavIconSize));
    quit_btn_->setAutoDefault(false);
    connect(quit_btn_, SIGNAL(clicked()), this, SLOT(onQuitClicked()));
    v->addWidget(quit_btn_);

    return sidebar;
}

QPushButton *SettingsWindow::addNavItem(Page page, const QString& text, int glyph)
{
    QPushButton *btn = new QPushButton(text);
    btn->setObjectName("navItem");
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setIconSize(QSize(kNavIconSize, kNavIconSize));
    btn->setAutoDefault(false);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    nav_group_->addButton(btn, page);
    nav_buttons_.append(btn);
    nav_glyphs_.append(glyph);
    return btn;
}

void SettingsWindow::updateNavIcons()
{
    using namespace DesignTokens;
    if (!awesome) {
        return;
    }
    for (int i = 0; i < nav_buttons_.size(); i++) {
        QPushButton *btn = nav_buttons_[i];
        QColor color = btn->isChecked() ? accentText() : textSecondary();
        btn->setIcon(awesome->icon(nav_glyphs_[i], color));
    }
    quit_btn_->setIcon(awesome->icon(icon_off, dangerText()));
}

void SettingsWindow::setCurrentPage(Page page)
{
    ensureBuilt();
    if (page < 0 || page >= PageCount) {
        page = PageGeneral;
    }
    stack_->setCurrentIndex(page);
    if (QAbstractButton *btn = nav_group_->button(page)) {
        btn->setChecked(true);
    }
    updateNavIcons();
}

SettingsWindow::Page SettingsWindow::currentPage() const
{
    if (!stack_) {
        return PageGeneral;
    }
    return static_cast<Page>(stack_->currentIndex());
}

void SettingsWindow::showPage(Page page)
{
    setCurrentPage(page);
    show();
    if (isMinimized()) {
        showNormal();
    }
    raise();
    activateWindow();
}

void SettingsWindow::setCurrentTab(int index)
{
    switch (index) {
    case 1:  // Advanced: delete confirmation, certificate (cache: own page)
        setCurrentPage(PageSync);
        break;
    case 3:  // Network
        setCurrentPage(PageNetwork);
        break;
    case 0:  // Basic
    case 2:  // Language
    default:
        setCurrentPage(PageGeneral);
        break;
    }
}

void SettingsWindow::onNavClicked(int id)
{
    setCurrentPage(static_cast<Page>(id));
}

void SettingsWindow::showEvent(QShowEvent *event)
{
    // Like the old dialog: reload everything from the settings manager each
    // time the window is opened, so values changed elsewhere show up.
    gui->settingsManager()->loadSettings();
#if defined(_MSC_VER)
    // The old dialog cleaned the legacy Explorer namespace entries on every
    // save; doing it when the window opens keeps that behaviour.
    RegElement::removeIconRegItem();
#endif
    for (SettingsPage *page : pages_) {
        page->load();
    }
    refreshHeader();
    header_timer_->start();
    QWidget::showEvent(event);
}

void SettingsWindow::closeEvent(QCloseEvent *event)
{
    // Single instance for the life of the app: just hide.
    event->ignore();
    if (header_timer_) {
        header_timer_->stop();
    }
    hide();
}

void SettingsWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ThemeChange && built_ && !applying_style_) {
        applyStyle();
    }
    QWidget::changeEvent(event);
}

void SettingsWindow::refreshHeader()
{
    using namespace DesignTokens;

    const auto all = gui->accountManager()->allAccounts();
    const auto active = gui->accountManager()->activeAccounts();

    Account account;
    bool has_account = false;
    if (!active.isEmpty()) {
        account = active.first();
        has_account = true;
    } else if (!all.isEmpty()) {
        account = all.first();
        has_account = true;
    }

    QString name;
    QString status_text;
    bool connected = false;
    if (has_account) {
        name = AccountDisplay::displayName(account);
        AccountDisplay::Status st = AccountDisplay::status(account);
        connected = (st == AccountDisplay::StatusConnected);
        status_text = connected ? tr("Connected") : tr("Not connected");
        if (all.size() > 1) {
            status_text += QString::fromUtf8(" · ") + tr("%n account(s)", nullptr, int(all.size()));
        }
    } else {
        name = tr("No accounts");
        status_text = tr("Not connected");
    }

    header_name_ = name;
    account_name_->setText(name);
    account_name_->setToolTip(has_account ? account.username + "\n" + account.serverUrl.toString()
                                          : QString());
    account_status_->setText(QString::fromUtf8("● ") + status_text);
    if (account_status_->property("connected").toBool() != connected) {
        account_status_->setProperty("connected", connected);
        account_status_->style()->unpolish(account_status_);
        account_status_->style()->polish(account_status_);
    }
    avatar_->setPixmap(AccountDisplay::avatar(has_account ? name : QString("?"),
                                              kHeaderAvatarSize, devicePixelRatioF()));
}

void SettingsWindow::onQuitClicked()
{
    if (!gui->yesOrNoBox(tr("Do you want to quit %1? The virtual drive will not be available until you open it again.")
                             .arg(getBrand()),
                         this, false)) {
        return;
    }
    // Same as the "Quit" entry of the old tray menu.
    QCoreApplication::exit(0);
}

void SettingsWindow::applySavedTheme()
{
    GeneralPage::applySavedColorScheme();
}

QString SettingsWindow::caretImagePath() const
{
    using namespace DesignTokens;
    if (!awesome) {
        return QString();
    }
    // QSS needs a file for the combo box arrow: render the icon once per color.
    QColor color = textSecondary();
    QString path = QDir::temp().filePath(
        QString("seadrive-settings-caret-%1.png").arg(color.name().mid(1)));
    if (!QFileInfo::exists(path)) {
        QPixmap pix = awesome->icon(icon_chevron_down, color).pixmap(QSize(24, 24));
        pix.save(path, "PNG");
    }
    return QDir::fromNativeSeparators(path);
}

QString SettingsWindow::buildStyleSheet() const
{
    using namespace DesignTokens;

    const QString page = css(surfacePage());
    const QString card = css(surfaceCard());
    const QString hover = css(surfaceHover());
    const QString text = css(textPrimary());
    const QString text2 = css(textSecondary());
    const QString muted = css(textMuted());
    const QString line = css(border());
    const QString accent = css(accentText());
    const QString accent_bg = css(accentBg());
    const QString success = css(successText());
    const QString success_bg = css(successBg());
    const QString warning = css(warningText());
    const QString warning_bg = css(warningBg());
    const QString danger = css(dangerText());

    QString caret = caretImagePath();
    QString caret_rule = caret.isEmpty()
        ? QString()
        : QString("QComboBox::down-arrow { image: url(\"%1\"); width: 12px; height: 12px; }").arg(caret);

    QString s = QString(R"(
QWidget#settingsWindow, QWidget#settingsStack, QWidget#pageContent { background: %1; }
QWidget#settingsWindow QWidget { color: %4; font-size: %20px; }
QWidget#sidebar { background: %1; border-right: 1px solid %7; }
QScrollArea#settingsPage { background: %1; border: none; }

QLabel#sidebarName { font-size: %20px; font-weight: 600; color: %4; }
QLabel#sidebarStatus { font-size: %21px; color: %5; }
QLabel#sidebarStatus[connected="true"] { color: %10; }

QPushButton#navItem {
    text-align: left; padding: 8px 10px; border: none; border-radius: %22px;
    background: transparent; color: %4; font-size: %20px;
}
QPushButton#navItem:hover { background: %3; }
QPushButton#navItem:checked { background: %9; color: %8; font-weight: 600; }
QPushButton#navItem:focus { outline: none; }
QPushButton#navQuit {
    text-align: left; padding: 8px 10px; border: none; border-radius: %22px;
    background: transparent; color: %14; font-size: %20px;
}
QPushButton#navQuit:hover { background: %3; }

QLabel#pageTitle { font-size: %23px; font-weight: 600; color: %4; }
QLabel#pageSubtitle { font-size: %21px; color: %5; }
QLabel#groupHeading { font-size: %21px; font-weight: 600; color: %5; }
QLabel#productName { font-size: %23px; font-weight: 600; color: %4; }
QLabel#pageNote { font-size: %21px; color: %5; }

QFrame#settingsGroup { background: %2; border: 1px solid %7; border-radius: %24px; }
QFrame#rowSeparator { background: %7; border: none; }
QLabel#rowTitle { font-size: %20px; color: %4; }
QLabel#rowTitleStrong { font-size: %20px; font-weight: 600; color: %4; }
QLabel#rowDesc { font-size: %21px; color: %5; }
QLabel#rowError { font-size: %21px; color: %14; }
QLabel#rowSuccess { font-size: %21px; color: %10; }

QLabel#pill { font-size: %21px; border-radius: 10px; padding: 2px 10px; }
QLabel#pill[pill="success"] { background: %11; color: %10; }
QLabel#pill[pill="warning"] { background: %13; color: %12; }
QLabel#pill[pill="danger"]  { background: %3; color: %14; }
QLabel#pill[pill="accent"]  { background: %9; color: %8; }
QLabel#pill[pill="neutral"] { background: %3; color: %5; }

QPushButton#secondaryButton, QPushButton#dangerButton {
    background: %2; color: %4; border: 1px solid %7; border-radius: %22px;
    padding: 6px 14px; font-size: %20px;
}
QPushButton#dangerButton { color: %14; }
QPushButton#secondaryButton:hover, QPushButton#dangerButton:hover { background: %3; }
QPushButton#secondaryButton:focus, QPushButton#dangerButton:focus { border: 1px solid %8; }
QPushButton#primaryButton {
    background: %8; color: %2; border: 1px solid %8; border-radius: %22px;
    padding: 6px 14px; font-size: %20px; font-weight: 600;
}
QPushButton#primaryButton:hover { background: %8; border: 1px solid %4; }
QPushButton:disabled { color: %6; }

QComboBox, QSpinBox, QLineEdit {
    background: %2; color: %4; border: 1px solid %7; border-radius: %22px;
    padding: 5px 10px; min-height: 20px; font-size: %20px;
    selection-background-color: %9; selection-color: %4;
}
QComboBox:hover, QSpinBox:hover, QLineEdit:hover { border: 1px solid %6; }
QComboBox:focus, QSpinBox:focus, QLineEdit:focus { border: 1px solid %8; }
QComboBox { padding-right: 30px; }
QComboBox::drop-down { border: none; width: 26px; subcontrol-origin: padding; subcontrol-position: center right; }
%25
QComboBox QAbstractItemView {
    background: %2; color: %4; border: 1px solid %7; outline: none;
    selection-background-color: %9; selection-color: %8; padding: 4px;
}
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %7; border-radius: 4px; min-height: 32px; }
QScrollBar::handle:vertical:hover { background: %6; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QToolTip { background: %2; color: %4; border: 1px solid %7; padding: 4px 8px; font-size: %21px; }
)")
        .arg(page, card, hover, text, text2, muted, line, accent, accent_bg)   // 1-9
        .arg(success, success_bg, warning, warning_bg, danger)                  // 10-14
        .arg(kFontBody).arg(kFontSmall).arg(kRadius).arg(kFontTitle).arg(kRadiusCard)  // 20-24
        .arg(caret_rule);                                                        // 25
    return s;
}

void SettingsWindow::applyStyle()
{
    if (applying_style_) {
        return;
    }
    applying_style_ = true;
    setStyleSheet(buildStyleSheet());
    updateNavIcons();
    refreshHeader();
    for (SettingsPage *page : pages_) {
        page->themeChanged();
    }
    applying_style_ = false;
}
