#ifndef SEADRIVE_GUI_GENERAL_PAGE_H
#define SEADRIVE_GUI_GENERAL_PAGE_H

#include "settings-page.h"

class QComboBox;
class QLabel;
class ToggleSwitch;

// "Generale": start at login, notifications, Explorer extension, language,
// theme and the SeaDrive / cache folder.
class GeneralPage : public SettingsPage
{
    Q_OBJECT

public:
    enum ColorSchemeChoice {
        SchemeSystem = 0,
        SchemeLight,
        SchemeDark,
    };

    explicit GeneralPage(QWidget *parent = nullptr);

    void load() override;

    // Read the saved theme choice and apply it to the whole application.
    // Call once at startup (after QApplication exists).
    static void applySavedColorScheme();
    static int savedColorScheme();

private slots:
    void onAutoStartToggled(bool checked);
    void onNotifyToggled(bool checked);
    void onShellExtToggled(bool checked);
    void onHideIncompatToggled(bool checked);
    void onLanguageChanged(int index);
    void onThemeChanged(int index);
    void onOpenFolderClicked();
    void onChangeFolderClicked();

private:
    static void applyColorScheme(int choice);
    QString currentFolder() const;

    bool loading_;

    ToggleSwitch *auto_start_;
    ToggleSwitch *notify_;
    ToggleSwitch *shell_ext_;
    ToggleSwitch *hide_incompat_;
    QComboBox *language_;
    QComboBox *theme_;
    QWidget *folder_row_;
    QString current_folder_;
};

#endif // SEADRIVE_GUI_GENERAL_PAGE_H
