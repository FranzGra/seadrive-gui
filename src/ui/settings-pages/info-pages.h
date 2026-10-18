#ifndef SEADRIVE_GUI_INFO_PAGES_H
#define SEADRIVE_GUI_INFO_PAGES_H

#include "settings-page.h"

class QLabel;

// "Log e diagnostica": log folder and a copy-paste summary for support.
class LogsPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit LogsPage(QWidget *parent = nullptr);

    void load() override;

private slots:
    void openLogFolder();
    void copyLogPath();
    void copyDiagnostics();

private:
    QString logDir() const;

    QWidget *log_row_;
    QLabel *copied_note_;
};

// "Informazioni": version, online help, about box.
class AboutPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit AboutPage(QWidget *parent = nullptr);

    static QString versionText();
};

#endif // SEADRIVE_GUI_INFO_PAGES_H
