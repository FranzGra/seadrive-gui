#ifndef SEADRIVE_GUI_MODERN_MENU_H
#define SEADRIVE_GUI_MODERN_MENU_H

// Context menus in the style of the redesigned screens (sync panel, transfer
// window, settings window): flat card with rounded corners, thin border,
// roomy items with a QtAwesome icon on the left, soft hover, thin separators.
// Colors and sizes come from design-tokens.h, so light and dark mode follow
// the system theme every time the menu opens.

#include <QMenu>

class QAction;
class QStyleOptionMenuItem;

class ModernMenu : public QMenu
{
public:
    explicit ModernMenu(QWidget *parent = nullptr);
    explicit ModernMenu(const QString& title, QWidget *parent = nullptr);

    // Adds an action with a QtAwesome icon (e.g. icon_folder_open). A
    // "danger" action (e.g. quit, delete) is drawn in red.
    QAction *addIconAction(int fa_icon, const QString& text, bool danger = false);

    // Same, for actions created elsewhere (e.g. the tray icon actions).
    static void decorateAction(QAction *action, int fa_icon, bool danger = false);

    // Re-reads the design tokens (light/dark) and repaints icons. Called
    // automatically before the menu is shown.
    void refreshStyle();

    // Style sheet shared by every modern menu, for the current theme.
    static QString styleSheetForCurrentTheme();

protected:
    void initStyleOption(QStyleOptionMenuItem *option, const QAction *action) const override;

private:
    void setup();
};

#endif // SEADRIVE_GUI_MODERN_MENU_H
