#include <QtWidgets>

#include "QtAwesome.h"
#include "seadrive-gui.h"
#include "utils/utils.h"
#include "src/ui/encrypted-repos-dialog.h"
#include "src/ui/design-tokens.h"

#include "encrypted-repos-page.h"

namespace {

const int kLockIconSize = 20;

QLabel *plainLabel(const QString& text, const char *object_name)
{
    QLabel *l = new QLabel(text);
    l->setObjectName(object_name);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    return l;
}

} // namespace

EncryptedReposPage::EncryptedReposPage(QWidget *parent)
    : SettingsPage(tr("Encrypted Libraries"),
                   tr("Encrypted libraries open in the virtual drive only after you unlock them with their password."),
                   parent),
      model_(nullptr),
      busy_(false),
      rebuild_pending_(false)
{
    addGroup();

    list_ = new QWidget;
    list_layout_ = new QVBoxLayout(list_);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(0);

    QWidget *row = addWideRow(list_);
    if (QLayout *l = row->layout()) {
        if (l->count() > 1) {
            if (QLayout *inner = l->itemAt(1)->layout()) {
                inner->setContentsMargins(0, 0, 0, 0);
            }
        }
    }

    addNote(tr("When you lock a library, the key saved on this computer is deleted."));
    addStretch();

    connect(this, SIGNAL(pageShown()), this, SLOT(onPageShown()));
    connect(this, SIGNAL(pageHidden()), this, SLOT(onPageHidden()));
}

void EncryptedReposPage::ensureModel()
{
    if (model_) {
        return;
    }
    // Created lazily: the model asks the daemon for the list right away, and
    // the settings window exists before the daemon is connected.
    model_ = new EncryptedReposTableModel(this);
    model_->getUpdateTimer()->stop();
    connect(model_, SIGNAL(modelReset()), this, SLOT(rebuild()));
    connect(model_, SIGNAL(dataChanged(const QModelIndex&, const QModelIndex&, const QList<int>&)),
            this, SLOT(rebuild()));
}

void EncryptedReposPage::onPageShown()
{
    ensureModel();
    model_->getUpdateTimer()->start();
    model_->updateEncryptRepoList();
    rebuild();
}

void EncryptedReposPage::onPageHidden()
{
    if (model_) {
        model_->getUpdateTimer()->stop();
    }
}

void EncryptedReposPage::load()
{
    if (isVisible()) {
        onPageShown();
    } else {
        rebuild();
    }
}

void EncryptedReposPage::themeChanged()
{
    rebuild();
}

void EncryptedReposPage::rebuild()
{
    if (busy_) {
        rebuild_pending_ = true;
        return;
    }
    rebuild_pending_ = false;

    while (QLayoutItem *item = list_layout_->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    int n = model_ ? model_->rowCount() : 0;
    if (n == 0) {
        QWidget *empty = new QWidget;
        QVBoxLayout *el = new QVBoxLayout(empty);
        el->setContentsMargins(DesignTokens::kPadding, 20, DesignTokens::kPadding, 20);
        el->addWidget(plainLabel(tr("No Encrypted Library."), "rowTitle"));
        el->addWidget(plainLabel(tr("Password-protected libraries of your accounts will appear here."),
                                 "rowDesc"));
        list_layout_->addWidget(empty);
        return;
    }

    for (int i = 0; i < n; i++) {
        if (i > 0) {
            QFrame *sep = new QFrame;
            sep->setObjectName("rowSeparator");
            sep->setFixedHeight(1);
            list_layout_->addWidget(sep);
        }
        list_layout_->addWidget(createItem(model_->encRepoInfoAt(i)));
    }
}

QWidget *EncryptedReposPage::createItem(const EncryptedRepoInfo& info)
{
    using namespace DesignTokens;

    QWidget *item = new QWidget;
    QHBoxLayout *h = new QHBoxLayout(item);
    h->setContentsMargins(kPadding, kRowPadding, kPadding, kRowPadding);
    h->setSpacing(kPadding - 4);

    QLabel *icon = new QLabel;
    icon->setFixedSize(kLockIconSize, kLockIconSize);
    QColor color = info.is_password_set ? successText() : textSecondary();
    if (awesome) {
        int glyph = info.is_password_set ? icon_unlock_alt : icon_lock;
        icon->setPixmap(awesome->icon(glyph, color).pixmap(kLockIconSize, kLockIconSize));
    }
    h->addWidget(icon, 0, Qt::AlignTop);

    QVBoxLayout *texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(plainLabel(info.repo_name, "rowTitleStrong"));
    QString detail = info.repo_server;
    if (!info.repo_username.isEmpty()) {
        detail = info.repo_username + QString::fromUtf8(" · ") + detail;
    }
    texts->addWidget(plainLabel(detail, "rowDesc"));
    h->addLayout(texts, 1);

    QLabel *pill = makePill(info.is_password_set ? tr("Unlocked") : tr("Locked"),
                            info.is_password_set ? "success" : "neutral");
    h->addWidget(pill, 0, Qt::AlignVCenter);

    QPushButton *btn;
    if (info.is_password_set) {
        btn = makeButton(tr("Lock"));
        btn->setAccessibleName(tr("Lock library %1").arg(info.repo_name));
        connect(btn, &QPushButton::clicked, this, [this, info] { lockRepo(info); });
    } else {
        btn = makeButton(tr("Unlock…"), ButtonPrimary);
        btn->setAccessibleName(tr("Unlock library %1").arg(info.repo_name));
        connect(btn, &QPushButton::clicked, this, [this, info] { unlockRepo(info); });
    }
    h->addWidget(btn, 0, Qt::AlignVCenter);

    return item;
}

// Same flow as EncryptedReposTableView::onClickSyncAction().
void EncryptedReposPage::unlockRepo(const EncryptedRepoInfo& info)
{
    busy_ = true;
    bool ok = false;
    QString password = QInputDialog::getText(window(),
                                             tr("Unlock library"),
                                             tr("Password for library \"%1\":").arg(info.repo_name),
                                             QLineEdit::Password, QString(), &ok);
    if (ok && !password.isEmpty()) {
        model_->slotSetEncRepoPassword(info.domain_id, info.repo_id, password);
    }
    busy_ = false;
    model_->updateEncryptRepoList();
    if (rebuild_pending_) {
        // Deferred: this runs inside the clicked() handler of a list button.
        QTimer::singleShot(0, this, SLOT(rebuild()));
    }
}

void EncryptedReposPage::lockRepo(const EncryptedRepoInfo& info)
{
    busy_ = true;
    bool ok = gui->yesOrCancelBox(
        tr("After locking, the local key of this library is deleted and the library can no longer be accessed in the virtual drive. Do you want to continue?"),
        window(), true);
    if (ok) {
        model_->slotClearEncRepoPassword(info.domain_id, info.repo_id);
    }
    busy_ = false;
    model_->updateEncryptRepoList();
    if (rebuild_pending_) {
        QTimer::singleShot(0, this, SLOT(rebuild()));
    }
}
