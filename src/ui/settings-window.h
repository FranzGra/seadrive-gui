#ifndef SEADRIVE_GUI_SETTINGS_WINDOW_H
#define SEADRIVE_GUI_SETTINGS_WINDOW_H

#include <QWidget>
#include <QList>

class QLabel;
class QPushButton;
class QButtonGroup;
class QStackedWidget;
class QTimer;
class SettingsPage;

// The settings window. It replaces both SettingsDialog and the old tray
// menu: a fixed side menu (account header, sections, quit) and a stack of
// pages on the right. Every change is saved right away, there is no OK
// button.
//
// Like the old dialog there is one instance for the whole life of the app:
// closing the window only hides it.
class SettingsWindow : public QWidget
{
    Q_OBJECT

public:
    enum Page {
        PageGeneral = 0,
        PageSync,
        PageAccount,
        PageEncrypted,
        PageNetwork,
        PageLogs,
        PageAbout,
        PageCount
    };

    explicit SettingsWindow(QWidget *parent = nullptr);

    void setCurrentPage(Page page);
    Page currentPage() const;

    // Show, raise and focus the window on the given page.
    void showPage(Page page);

    // Compatibility with SettingsDialog::setCurrentTab(): old tab indexes
    // were 0 Basic, 1 Advanced, 2 Language, 3 Network.
    void setCurrentTab(int index);

    // Apply the theme saved in "Generale" (call once at startup).
    static void applySavedTheme();

    // Builds the content on first show.
    void setVisible(bool visible) override;

    // Build and polish the content without showing it, so the first open
    // is instant. Called once when the app is idle after startup.
    void prebuild();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onNavClicked(int id);
    void refreshHeader();
    void applyStyle();
    void onQuitClicked();

private:
    // Build the sidebar and pages (first show / first page change).
    void ensureBuilt();
    QWidget *createSidebar();
    QPushButton *addNavItem(Page page, const QString& text, int glyph);
    void updateNavIcons();
    QString buildStyleSheet() const;
    QString caretImagePath() const;

    QStackedWidget *stack_;
    QList<SettingsPage *> pages_;
    QButtonGroup *nav_group_;
    QList<QPushButton *> nav_buttons_;
    QList<int> nav_glyphs_;
    QPushButton *quit_btn_;

    QLabel *avatar_;
    QLabel *account_name_;
    QLabel *account_status_;
    QString header_name_;
    QTimer *header_timer_;

    bool built_;
    bool applying_style_;
};

#endif // SEADRIVE_GUI_SETTINGS_WINDOW_H
