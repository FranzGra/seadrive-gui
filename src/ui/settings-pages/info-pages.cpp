#include <QtWidgets>
#include <QClipboard>
#include <QSysInfo>

#include "seadrive-gui.h"
#include "account-mgr.h"
#include "utils/utils.h"
#include "src/ui/tray-icon.h"
#include "src/ui/design-tokens.h"

#include "info-pages.h"

// ---------------------------------------------------------------------------
// LogsPage
// ---------------------------------------------------------------------------

LogsPage::LogsPage(QWidget *parent)
    : SettingsPage(tr("Logs and diagnostics"),
                   tr("Useful information when something does not work."),
                   parent)
{
    addGroup(tr("Logs"));

    QWidget *buttons = new QWidget;
    QHBoxLayout *bl = new QHBoxLayout(buttons);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);
    QPushButton *open_btn = makeButton(tr("Open folder"));
    QPushButton *copy_btn = makeButton(tr("Copy path"));
    bl->addWidget(open_btn);
    bl->addWidget(copy_btn);
    connect(open_btn, SIGNAL(clicked()), this, SLOT(openLogFolder()));
    connect(copy_btn, SIGNAL(clicked()), this, SLOT(copyLogPath()));

    log_row_ = addRow(tr("Logs folder"), QString(), buttons);
    open_btn->setAccessibleName(tr("Open the logs folder"));
    copy_btn->setAccessibleName(tr("Copy the path of the logs folder"));

    addGroup(tr("Support"));

    QPushButton *diag_btn = makeButton(tr("Copy"));
    diag_btn->setAccessibleName(tr("Copy the support information"));
    addRow(tr("Support information"),
           tr("Copies the version, system and servers in use, to paste into a report."),
           diag_btn);
    connect(diag_btn, SIGNAL(clicked()), this, SLOT(copyDiagnostics()));

    QPushButton *errors_btn = makeButton(tr("Show"));
    errors_btn->setAccessibleName(tr("Show file sync errors"));
    addRow(tr("File Sync Errors"),
           tr("The list of files with upload or download problems."),
           errors_btn);
    connect(errors_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showSyncErrorsDialog();
    });

    copied_note_ = addNote(QString());
    copied_note_->setObjectName("rowSuccess");
    copied_note_->setVisible(false);

    addStretch();
}

QString LogsPage::logDir() const
{
    return QFileInfo(seadriveLogDir()).absoluteFilePath();
}

void LogsPage::load()
{
    QString path = QDir::toNativeSeparators(logDir());
    QString shown = path;
    shown.replace(QDir::separator(), QString(QDir::separator()) + QChar(0x200B));
    QLabel *desc = rowDescription(log_row_);
    desc->setText(shown);
    desc->setToolTip(path);
    desc->setVisible(true);
    copied_note_->setVisible(false);
}

// Same as SeafileTrayIcon::openLogDirectory().
void LogsPage::openLogFolder()
{
    openUrl(QUrl::fromLocalFile(logDir()));
}

void LogsPage::copyLogPath()
{
    QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(logDir()));
    copied_note_->setText(tr("Path copied to the clipboard."));
    copied_note_->setVisible(true);
}

void LogsPage::copyDiagnostics()
{
    QStringList lines;
    lines << QString("%1 %2").arg(getBrand(), AboutPage::versionText());
    lines << QString("OS: %1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
    lines << QString("Qt: %1").arg(qVersion());
    const auto accounts = gui->accountManager()->allAccounts();
    for (const Account& a : accounts) {
        lines << QString("Server: %1 (%2)")
                     .arg(a.serverUrl.host(), a.isValid() ? "logged in" : "not logged in");
    }
    lines << QString("Log: %1").arg(QDir::toNativeSeparators(logDir()));

    QGuiApplication::clipboard()->setText(lines.join('\n'));
    copied_note_->setText(tr("Information copied to the clipboard."));
    copied_note_->setVisible(true);
}

// ---------------------------------------------------------------------------
// AboutPage
// ---------------------------------------------------------------------------

QString AboutPage::versionText()
{
    QString version = QString(STRINGIZE(SEADRIVE_GUI_VERSION));
#ifdef SEAFILE_CLIENT_REVISION
    version += QString(" (REV %1)").arg(STRINGIZE(SEAFILE_CLIENT_REVISION));
#endif
    return version;
}

AboutPage::AboutPage(QWidget *parent)
    : SettingsPage(tr("About"),
                   tr("Version, help and licenses."),
                   parent)
{
    using namespace DesignTokens;

    // Product header card
    addGroup();
    QWidget *header = new QWidget;
    QHBoxLayout *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 4, 0, 4);
    hl->setSpacing(kPadding);
    QLabel *logo = new QLabel;
    logo->setFixedSize(48, 48);
    logo->setPixmap(QIcon(":/images/seafile.png").pixmap(48, 48));
    hl->addWidget(logo, 0, Qt::AlignTop);
    QVBoxLayout *tl = new QVBoxLayout;
    tl->setSpacing(2);
    QLabel *name = new QLabel(getBrand());
    name->setObjectName("productName");
    name->setWordWrap(true);
    QLabel *version = new QLabel(tr("Version %1").arg(versionText()));
    version->setObjectName("rowDesc");
    version->setWordWrap(true);
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tl->addWidget(name);
    tl->addWidget(version);
    hl->addLayout(tl, 1);
    addWideRow(header);

    addGroup(tr("Help"));

    QPushButton *help_btn = makeButton(tr("Open help"));
    addRow(tr("Online help"),
           tr("The %1 manual on the Seafile website.").arg(getBrand()),
           help_btn);
    connect(help_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->openHelp();
    });

    QPushButton *about_btn = makeButton(tr("Show"));
    addRow(tr("About %1").arg(getBrand()),
           tr("Full version, license and credits."),
           about_btn);
    connect(about_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showAboutDialog();
    });

    addStretch();
}
