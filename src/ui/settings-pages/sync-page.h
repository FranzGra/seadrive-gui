#ifndef SEADRIVE_GUI_SYNC_PAGE_H
#define SEADRIVE_GUI_SYNC_PAGE_H

#include <functional>

#include "settings-page.h"

class QLabel;
class QSpinBox;
class QTimer;
class ToggleSwitch;

// "Sincronizzazione": current transfer and errors (from the old tray menu),
// speed limits and advanced sync options. The cache has its own page.
class SyncPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit SyncPage(QWidget *parent = nullptr);

    void load() override;

private slots:
    void refreshStatus();
    void onSyncTempToggled(bool checked);
    void onCertVerifyToggled(bool checked);

private:
    QSpinBox *makeSpin(int min, int max, const QString& suffix);
    // Save the value of a spin box a short time after the last change.
    void saveOnChange(QSpinBox *spin, std::function<void(int)> save);

    bool loading_;
    QTimer *status_timer_;

    QWidget *transfer_row_;
    QWidget *errors_row_;
    QLabel *errors_pill_;
    QWidget *search_row_;

    QSpinBox *download_limit_;
    QSpinBox *upload_limit_;
    QSpinBox *delete_threshold_;
    ToggleSwitch *sync_temp_;
    ToggleSwitch *cert_verify_disabled_;
};

#endif // SEADRIVE_GUI_SYNC_PAGE_H
