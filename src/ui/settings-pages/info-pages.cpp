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
    : SettingsPage(tr("Log e diagnostica"),
                   tr("Informazioni utili quando qualcosa non funziona."),
                   parent)
{
    addGroup(tr("Log"));

    QWidget *buttons = new QWidget;
    QHBoxLayout *bl = new QHBoxLayout(buttons);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);
    QPushButton *open_btn = makeButton(tr("Apri cartella"));
    QPushButton *copy_btn = makeButton(tr("Copia percorso"));
    bl->addWidget(open_btn);
    bl->addWidget(copy_btn);
    connect(open_btn, SIGNAL(clicked()), this, SLOT(openLogFolder()));
    connect(copy_btn, SIGNAL(clicked()), this, SLOT(copyLogPath()));

    log_row_ = addRow(tr("Cartella dei log"), QString(), buttons);
    open_btn->setAccessibleName(tr("Apri la cartella dei log"));
    copy_btn->setAccessibleName(tr("Copia il percorso della cartella dei log"));

    addGroup(tr("Assistenza"));

    QPushButton *diag_btn = makeButton(tr("Copia"));
    diag_btn->setAccessibleName(tr("Copia le informazioni per l'assistenza"));
    addRow(tr("Informazioni per l'assistenza"),
           tr("Copia versione, sistema e server in uso, da incollare in una segnalazione."),
           diag_btn);
    connect(diag_btn, SIGNAL(clicked()), this, SLOT(copyDiagnostics()));

    QPushButton *errors_btn = makeButton(tr("Mostra"));
    errors_btn->setAccessibleName(tr("Mostra gli errori di sincronizzazione"));
    addRow(tr("Errori di sincronizzazione"),
           tr("L'elenco dei file con problemi di caricamento o download."),
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
    copied_note_->setText(tr("Percorso copiato negli appunti."));
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
    copied_note_->setText(tr("Informazioni copiate negli appunti."));
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
    : SettingsPage(tr("Informazioni"),
                   tr("Versione, guida e licenze."),
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
    QLabel *version = new QLabel(tr("Versione %1").arg(versionText()));
    version->setObjectName("rowDesc");
    version->setWordWrap(true);
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tl->addWidget(name);
    tl->addWidget(version);
    hl->addLayout(tl, 1);
    addWideRow(header);

    addGroup(tr("Aiuto"));

    QPushButton *help_btn = makeButton(tr("Apri guida"));
    addRow(tr("Guida online"),
           tr("Il manuale di %1 sul sito di Seafile.").arg(getBrand()),
           help_btn);
    connect(help_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->openHelp();
    });

    QPushButton *about_btn = makeButton(tr("Mostra"));
    addRow(tr("Informazioni su %1").arg(getBrand()),
           tr("Versione completa, licenza e crediti."),
           about_btn);
    connect(about_btn, &QPushButton::clicked, this, [] {
        gui->trayIcon()->showAboutDialog();
    });

    addStretch();
}
