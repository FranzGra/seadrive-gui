#ifndef SEADRIVE_GUI_NETWORK_PAGE_H
#define SEADRIVE_GUI_NETWORK_PAGE_H

#include "settings-page.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QTimer;
class ToggleSwitch;

// "Rete e proxy": the proxy settings of the old "Network" tab.
// Changes are saved automatically once the fields are valid; problems are
// shown inline below the card instead of in message boxes.
class NetworkPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit NetworkPage(QWidget *parent = nullptr);

    void load() override;

private slots:
    void onTypeChanged(int index);
    void onFieldChanged();
    void onRequirePasswordToggled(bool checked);
    void applyProxy();

private:
    void updateVisibility();
    void prefillRemembered();
    // Return an empty string when valid, or the problem to show.
    QString validate() const;
    void rememberHostPort();
    void showStatus(const QString& text, bool error);

    bool loading_;
    QTimer *save_timer_;

    QComboBox *type_;
    QLineEdit *host_;
    QSpinBox *port_;
    ToggleSwitch *require_password_;
    QLineEdit *username_;
    QLineEdit *password_;

    QWidget *host_row_;
    QWidget *port_row_;
    QWidget *require_row_;
    QWidget *username_row_;
    QWidget *password_row_;

    QLabel *status_;
};

#endif // SEADRIVE_GUI_NETWORK_PAGE_H
