#include <QtWidgets>
#include <QSettings>
#include <QStyleHints>

#include "i18n.h"
#include "seadrive-gui.h"
#include "settings-mgr.h"
#include "utils/utils.h"
#if defined(_MSC_VER)
#include "utils/registry.h"
#endif

#include "toggle-switch.h"
#include "general-page.h"

namespace {

const char *kSettingsGroup = "SettingsWindow";
const char *kColorSchemeKey = "color_scheme";

} // namespace

GeneralPage::GeneralPage(QWidget *parent)
    : SettingsPage(tr("General"),
                   tr("Startup, notifications, language and appearance of %1.").arg(getBrand()),
                   parent),
      loading_(false),
      auto_start_(nullptr),
      notify_(nullptr),
      shell_ext_(nullptr),
      hide_incompat_(nullptr),
      folder_row_(nullptr)
{
    // --- Start and notifications
    addGroup(tr("Startup and notifications"));

#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS)
    auto_start_ = new ToggleSwitch;
    addRow(tr("Auto start %1 after login").arg(getBrand()),
           tr("Opens %1 automatically when you sign in to the computer.").arg(getBrand()),
           auto_start_);
    connect(auto_start_, SIGNAL(toggled(bool)), this, SLOT(onAutoStartToggled(bool)));
#endif

    notify_ = new ToggleSwitch;
    addRow(tr("Notifications"),
           tr("Notify when libraries are synchronized"),
           notify_);
    connect(notify_, SIGNAL(toggled(bool)), this, SLOT(onNotifyToggled(bool)));

#if defined(Q_OS_WIN32)
    shell_ext_ = new ToggleSwitch;
    addRow(tr("Enable Explorer Extension"),
           tr("Shows the file status and the %1 items in the right-click menu.").arg(getBrand()),
           shell_ext_);
    connect(shell_ext_, SIGNAL(toggled(bool)), this, SLOT(onShellExtToggled(bool)));
#endif

#if defined(Q_OS_MACOS)
    hide_incompat_ = new ToggleSwitch;
    addRow(tr("Hide Windows incompatibility paths Message"),
           tr("Don't show the message when a file name is not valid on Windows."),
           hide_incompat_);
    connect(hide_incompat_, SIGNAL(toggled(bool)), this, SLOT(onHideIncompatToggled(bool)));
#endif

    // --- Look and language
    addGroup(tr("Appearance and language"));

    language_ = new QComboBox;
    language_->addItems(I18NHelper::getInstance()->getLanguages());
    // The first entry is "-- System --" in the shared helper.
    if (language_->count() > 0) {
        language_->setItemText(0, tr("System language"));
    }
    language_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    addRow(tr("Language"),
           tr("%1 must be restarted to apply it.").arg(getBrand()),
           language_);
    connect(language_, SIGNAL(currentIndexChanged(int)), this, SLOT(onLanguageChanged(int)));

    theme_ = new QComboBox;
    theme_->insertItem(SchemeSystem, tr("Same as system"));
    theme_->insertItem(SchemeLight, tr("Light"));
    theme_->insertItem(SchemeDark, tr("Dark"));
    theme_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    addRow(tr("Theme"),
           tr("Light or dark colors for the %1 windows.").arg(getBrand()),
           theme_);
    connect(theme_, SIGNAL(currentIndexChanged(int)), this, SLOT(onThemeChanged(int)));

    // --- Folder
#if defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
    addGroup(tr("Folder"));

    QWidget *buttons = new QWidget;
    QHBoxLayout *bl = new QHBoxLayout(buttons);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);
    QPushButton *open_btn = makeButton(tr("Open"));
    QPushButton *change_btn = makeButton(tr("Change…"));
    bl->addWidget(open_btn);
    bl->addWidget(change_btn);
    connect(open_btn, SIGNAL(clicked()), this, SLOT(onOpenFolderClicked()));
    connect(change_btn, SIGNAL(clicked()), this, SLOT(onChangeFolderClicked()));

#if defined(Q_OS_WIN32)
    folder_row_ = addRow(tr("%1 folder").arg(getBrand()), QString(), buttons);
#else
    folder_row_ = addRow(tr("Cache folder"), QString(), buttons);
#endif
    change_btn->setAccessibleName(tr("Change folder"));
    open_btn->setAccessibleName(tr("Open folder"));
#endif

    addStretch();
}

void GeneralPage::load()
{
    SettingsManager *mgr = gui->settingsManager();
    loading_ = true;

    if (auto_start_) {
        auto_start_->setCheckedImmediate(mgr->autoStart());
    }
    notify_->setCheckedImmediate(mgr->notify());
#if defined(Q_OS_WIN32)
    shell_ext_->setCheckedImmediate(mgr->shellExtensionEnabled());
#endif
#if defined(Q_OS_MACOS)
    hide_incompat_->setCheckedImmediate(mgr->getHideWindowsIncompatibilityPathMsg());
#endif

    language_->blockSignals(true);
    language_->setCurrentIndex(I18NHelper::getInstance()->preferredLanguage());
    language_->blockSignals(false);

    theme_->blockSignals(true);
    theme_->setCurrentIndex(savedColorScheme());
    theme_->blockSignals(false);

    current_folder_ = currentFolder();
    if (folder_row_) {
        QLabel *desc = rowDescription(folder_row_);
        // Allow long paths to wrap after each separator.
        QString shown = QDir::toNativeSeparators(current_folder_);
        shown.replace(QDir::separator(), QString(QDir::separator()) + QChar(0x200B));
        desc->setText(shown);
        desc->setToolTip(QDir::toNativeSeparators(current_folder_));
        desc->setVisible(!current_folder_.isEmpty());
    }

    loading_ = false;
}

QString GeneralPage::currentFolder() const
{
    QString folder;
#if defined(Q_OS_WIN32)
    if (!gui->settingsManager()->getSeadriveRoot(&folder)) {
        folder = gui->seadriveRoot();
    }
#elif defined(Q_OS_LINUX)
    if (!gui->settingsManager()->getDataDir(&folder)) {
        folder = QDir(seadriveDataDir()).absolutePath();
    }
#endif
    return folder;
}

void GeneralPage::onAutoStartToggled(bool checked)
{
    if (loading_) return;
    gui->settingsManager()->setAutoStart(checked);
}

void GeneralPage::onNotifyToggled(bool checked)
{
    if (loading_) return;
    gui->settingsManager()->setNotify(checked);
}

void GeneralPage::onShellExtToggled(bool checked)
{
    if (loading_) return;
#if defined(Q_OS_WIN32)
    gui->settingsManager()->setShellExtensionEnabled(checked);
#else
    Q_UNUSED(checked);
#endif
}

void GeneralPage::onHideIncompatToggled(bool checked)
{
    if (loading_) return;
#if defined(Q_OS_MACOS)
    gui->settingsManager()->setHideWindowsIncompatibilityPathMsg(checked);
#else
    Q_UNUSED(checked);
#endif
}

void GeneralPage::onLanguageChanged(int index)
{
    if (loading_ || index < 0) return;
    if (index == I18NHelper::getInstance()->preferredLanguage()) {
        return;
    }
    I18NHelper::getInstance()->setPreferredLanguage(index);
    if (gui->yesOrNoBox(tr("You have changed language, Restart to apply it?"),
                        window(), true)) {
        gui->restartApp();
    }
}

void GeneralPage::onThemeChanged(int index)
{
    if (loading_ || index < 0) return;
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(kColorSchemeKey, index);
    settings.endGroup();
    applyColorScheme(index);
}

void GeneralPage::onOpenFolderClicked()
{
    if (current_folder_.isEmpty()) return;
    openUrl(QUrl::fromLocalFile(current_folder_));
}

void GeneralPage::onChangeFolderClicked()
{
#if defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
    QString dir = QFileDialog::getExistingDirectory(window(),
                                                    tr("Please choose the cache folder"),
                                                    current_folder_,
                                                    QFileDialog::ShowDirsOnly
                                                    | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty())
        return;
    if (dir.endsWith("/")) {
        dir.resize(dir.size() - 1);
    }
    if (dir == current_folder_) {
        return;
    }

    SettingsManager *mgr = gui->settingsManager();
#if defined(Q_OS_WIN32)
    mgr->setSeadriveRoot(dir);
    RegElement::removeIconRegItem();
    QString question = tr("You have changed %1 cache folder. Restart to apply it?").arg(getBrand());
#else
    mgr->setDataDir(dir);
    QString question = tr("You have changed cache directory. Restart to apply it?");
#endif
    load();
    if (gui->yesOrNoBox(question, window(), true)) {
        gui->restartApp();
    }
#endif
}

int GeneralPage::savedColorScheme()
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    int value = settings.value(kColorSchemeKey, SchemeSystem).toInt();
    settings.endGroup();
    if (value < SchemeSystem || value > SchemeDark) {
        value = SchemeSystem;
    }
    return value;
}

void GeneralPage::applySavedColorScheme()
{
    applyColorScheme(savedColorScheme());
}

void GeneralPage::applyColorScheme(int choice)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;  // Unknown = follow the system
    if (choice == SchemeLight) {
        scheme = Qt::ColorScheme::Light;
    } else if (choice == SchemeDark) {
        scheme = Qt::ColorScheme::Dark;
    }
    QGuiApplication::styleHints()->setColorScheme(scheme);
#else
    Q_UNUSED(choice);
#endif
}
