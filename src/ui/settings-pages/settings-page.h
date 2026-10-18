#ifndef SEADRIVE_GUI_SETTINGS_PAGE_H
#define SEADRIVE_GUI_SETTINGS_PAGE_H

#include <QScrollArea>
#include <QList>

class QLabel;
class QFrame;
class QPushButton;
class QVBoxLayout;

// Base class for one page of the settings window.
//
// Layout of a page:
//   title (18 px) + subtitle (13 px)
//   group heading (optional)
//   [ card: row | row | row ]   rows are "label + description" on the left
//                               and a control on the right, divided by thin
//                               lines.
//
// Labels always wrap, nothing is elided. Pages reload their values in load(),
// which the window calls every time it is shown.
class SettingsPage : public QScrollArea
{
    Q_OBJECT

public:
    enum ButtonKind {
        ButtonSecondary = 0,
        ButtonPrimary,
        ButtonDanger,
    };

    SettingsPage(const QString& title, const QString& subtitle, QWidget *parent = nullptr);

    // Re-read the current values from the settings manager / daemon.
    virtual void load() {}
    // Called when the light/dark scheme changes (icons need recoloring).
    virtual void themeChanged() {}

    // Helpers shared by all pages.
    static QPushButton *makeButton(const QString& text, ButtonKind kind = ButtonSecondary);
    // A small rounded label. kind: "success", "warning", "danger", "accent", "neutral".
    static QLabel *makePill(const QString& text, const QString& kind);
    static void setPill(QLabel *pill, const QString& text, const QString& kind);

protected:
    // Start a new card. heading may be empty.
    void addGroup(const QString& heading = QString());
    // Add a row to the current card and return it (hide it with setVisible,
    // then call refreshSeparators()).
    QWidget *addRow(const QString& title, const QString& description, QWidget *control);
    // A row whose content spans the whole width of the card.
    QWidget *addWideRow(QWidget *content);
    // Free text below the cards (for hints / errors).
    QLabel *addNote(const QString& text);
    // Add any widget directly in the page column (outside cards).
    void addPageWidget(QWidget *widget);
    void addStretch();

    // Show the divider line only between visible rows.
    void refreshSeparators();

    // The description label of a row created with addRow().
    static QLabel *rowDescription(QWidget *row);

    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

signals:
    void pageShown();
    void pageHidden();

private:
    QWidget *content_;
    QVBoxLayout *layout_;
    QFrame *current_group_;
    QVBoxLayout *group_layout_;
    QList<QFrame *> groups_;
};

#endif // SEADRIVE_GUI_SETTINGS_PAGE_H
