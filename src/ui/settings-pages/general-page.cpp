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
    : SettingsPage(tr("Generale"),
                   tr("Avvio, notifiche, lingua e aspetto di %1.").arg(getBrand()),
                   parent),
      loading_(false),
      auto_start_(nullptr),
      notify_(nullptr),
      shell_ext_(nullptr),
      hide_incompat_(nullptr),
      folder_row_(nullptr)
{
    // --- Start and notifications
    addGroup(tr("Avvio e notifiche"));

#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS)
    auto_start_ = new ToggleSwitch;
    addRow(tr("Avvia %1 all'accesso").arg(getBrand()),
           tr("Apre %1 automaticamente quando accedi al computer.").arg(getBrand()),
           auto_start_);
    connect(auto_start_, SIGNAL(toggled(bool)), this, SLOT(onAutoStartToggled(bool)));
#endif

    notify_ = new ToggleSwitch;
    addRow(tr("Notifiche"),
           tr("Avvisa quando le librerie sono state sincronizzate."),
           notify_);
    connect(notify_, SIGNAL(toggled(bool)), this, SLOT(onNotifyToggled(bool)));

#if defined(Q_OS_WIN32)
    shell_ext_ = new ToggleSwitch;
    addRow(tr("Estensione di Esplora risorse"),
           tr("Mostra lo stato dei file e le voci di %1 nel menu del tasto destro.").arg(getBrand()),
           shell_ext_);
    connect(shell_ext_, SIGNAL(toggled(bool)), this, SLOT(onShellExtToggled(bool)));
#endif

#if defined(Q_OS_MACOS)
    hide_incompat_ = new ToggleSwitch;
    addRow(tr("Nascondi l'avviso sui percorsi non compatibili con Windows"),
           tr("Non mostrare il messaggio quando un nome di file non è valido su Windows."),
           hide_incompat_);
    connect(hide_incompat_, SIGNAL(toggled(bool)), this, SLOT(onHideIncompatToggled(bool)));
#endif

    // --- Look and language
    addGroup(tr("Aspetto e lingua"));

    language_ = new QComboBox;
    language_->addItems(I18NHelper::getInstance()->getLanguages());
    // The first entry is "-- System --" in the shared helper.
    if (language_->count() > 0) {
        language_->setItemText(0, tr("Lingua del sistema"));
    }
    language_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    addRow(tr("Lingua"),
           tr("Serve un riavvio di %1 per applicarla.").arg(getBrand()),
           language_);
    connect(language_, SIGNAL(currentIndexChanged(int)), this, SLOT(onLanguageChanged(int)));

    theme_ = new QComboBox;
    theme_->insertItem(SchemeSystem, tr("Come il sistema"));
    theme_->insertItem(SchemeLight, tr("Chiaro"));
    theme_->insertItem(SchemeDark, tr("Scuro"));
    theme_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    addRow(tr("Tema"),
           tr("Colori chiari o scuri per le finestre di %1.").arg(getBrand()),
           theme_);
    connect(theme_, SIGNAL(currentIndexChanged(int)), this, SLOT(onThemeChanged(int)));

    // --- Folder
#if defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
    addGroup(tr("Cartella"));

    QWidget *buttons = new QWidget;
    QHBoxLayout *bl = new QHBoxLayout(buttons);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);
    QPushButton *open_btn = makeButton(tr("Apri"));
    QPushButton *change_btn = makeButton(tr("Cambia…"));
    bl->addWidget(open_btn);
    bl->addWidget(change_btn);
    connect(open_btn, SIGNAL(clicked()), this, SLOT(onOpenFolderClicked()));
    connect(change_btn, SIGNAL(clicked()), this, SLOT(onChangeFolderClicked()));

#if defined(Q_OS_WIN32)
    folder_row_ = addRow(tr("Cartella di %1").arg(getBrand()), QString(), buttons);
#else
    folder_row_ = addRow(tr("Cartella della cache"), QString(), buttons);
#endif
    change_btn->setAccessibleName(tr("Cambia cartella"));
    open_btn->setAccessibleName(tr("Apri cartella"));
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
    if (gui->yesOrNoBox(tr("Hai cambiato la lingua. Vuoi riavviare %1 ora per applicarla?").arg(getBrand()),
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
                                                    tr("Scegli la cartella"),
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
    QString question = tr("Hai cambiato la cartella di %1. Vuoi riavviare ora per applicare la modifica?").arg(getBrand());
#else
    mgr->setDataDir(dir);
    QString question = tr("Hai cambiato la cartella della cache. Vuoi riavviare ora per applicare la modifica?");
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
