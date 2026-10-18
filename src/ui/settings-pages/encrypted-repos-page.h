#ifndef SEADRIVE_GUI_ENCRYPTED_REPOS_PAGE_H
#define SEADRIVE_GUI_ENCRYPTED_REPOS_PAGE_H

#include "settings-page.h"

class QVBoxLayout;
class EncryptedRepoInfo;
class EncryptedReposTableModel;

// "Librerie cifrate": the list of encrypted libraries with unlock / lock,
// reusing the model (and its RPC calls) of EncryptedReposDialog.
class EncryptedReposPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit EncryptedReposPage(QWidget *parent = nullptr);

    void load() override;
    void themeChanged() override;

private slots:
    void rebuild();
    void onPageShown();
    void onPageHidden();

private:
    void ensureModel();
    QWidget *createItem(const EncryptedRepoInfo& info);
    void unlockRepo(const EncryptedRepoInfo& info);
    void lockRepo(const EncryptedRepoInfo& info);

    EncryptedReposTableModel *model_;
    // True while a password / confirmation dialog is open: the list is not
    // rebuilt then, because rebuilding deletes the button being handled.
    bool busy_;
    bool rebuild_pending_;
    QWidget *list_;
    QVBoxLayout *list_layout_;
};

#endif // SEADRIVE_GUI_ENCRYPTED_REPOS_PAGE_H
