#ifndef SEADRIVE_GUI_CACHE_PAGE_H
#define SEADRIVE_GUI_CACHE_PAGE_H

#include "settings-page.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QSlider;
class QSpinBox;
class QTimer;
class QWidget;
class ToggleSwitch;

// "Cache": space used by the downloaded files (read from the daemon's
// cache.db, read-only), size limit and automatic cleanup.
class CachePage : public SettingsPage
{
    Q_OBJECT

public:
    explicit CachePage(QWidget *parent = nullptr);

    void load() override;
    void themeChanged() override;

private slots:
    void refreshUsage();
    void onLimitChanged(int gb);
    void onCleanToggled(bool checked);
    void onIntervalChanged();

private:
    // Space and number of files in cache.db. False when the file is
    // missing or cannot be read right now.
    static bool readCacheUsage(qint64 *bytes, qint64 *files);
    static QString formatGB(double gb);

    void applyBarStyle(bool over);
    void showInterval(int minutes);
    int intervalMinutes() const;

    bool loading_;

    QLabel *used_label_;
    QLabel *of_label_;
    QLabel *over_pill_;
    QProgressBar *used_bar_;
    QLabel *files_label_;

    QSlider *limit_slider_;
    QSpinBox *limit_spin_;
    QTimer *limit_timer_;

    ToggleSwitch *clean_toggle_;
    QWidget *interval_row_;
    QSpinBox *interval_spin_;
    QComboBox *interval_unit_;
    QTimer *interval_timer_;

    qint64 used_bytes_;
    bool usage_known_;
    bool bar_over_;
};

#endif // SEADRIVE_GUI_CACHE_PAGE_H
