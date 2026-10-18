#include "modern-menu.h"

#include <QAction>
#include <QApplication>
#include <QPointer>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>

#include "QtAwesome.h"
#include "ui/design-tokens.h"

namespace DT = DesignTokens;

namespace {

const char *kPropIcon = "modernMenuIcon";
const char *kPropDanger = "modernMenuDanger";

const int kMenuPadding = 6;          // space between the card border and the items
const int kItemPadV = 8;             // 8 + text + 8 -> items about 34 px high
const int kItemPadRight = 20;
const int kIconLeft = 12;
// Qt adds the icon column (icon + its margins) after the left padding of
// the item: with 14 px the text starts ~10 px after the icon.
const int kItemPadLeft = 14;

// The Windows 11 style paints its own shadow next to menus, which shows as a
// dark square corner around our rounded card. The plain Fusion style under
// the style sheet does not.
QStyle *menuBaseStyle()
{
    static QPointer<QStyle> style;
    if (!style) {
        style = QStyleFactory::create(QStringLiteral("Fusion"));
        if (style)
            style->setParent(qApp);
    }
    return style;
}

QIcon iconFor(int fa_icon, const QColor& color)
{
    if (!awesome || fa_icon < 0)
        return QIcon();
    return awesome->icon(fa_icon, color);
}

} // namespace

ModernMenu::ModernMenu(QWidget *parent)
    : QMenu(parent)
{
    setup();
}

ModernMenu::ModernMenu(const QString& title, QWidget *parent)
    : QMenu(title, parent)
{
    setup();
}

void ModernMenu::setup()
{
    // Real rounded corners: the window itself is transparent and frameless,
    // the card is painted by the style sheet. No system shadow (Windows 11
    // would draw a square one around the rounded card).
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setSeparatorsCollapsible(true);
    if (QStyle *style = menuBaseStyle())
        setStyle(style);

    connect(this, &QMenu::aboutToShow, this, [this] { refreshStyle(); });
    refreshStyle();
}

QString ModernMenu::styleSheetForCurrentTheme()
{
    return QString(
        "QMenu {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  padding: %4px;"
        "  font-size: %5px;"
        "}"
        "QMenu::item {"
        "  background: transparent;"
        "  padding: %6px %7px %6px %8px;"
        "  border-radius: %9px;"
        "  min-width: 160px;"
        "}"
        "QMenu::item:selected { background: %10; }"
        "QMenu::item:disabled { color: %11; }"
        "QMenu::icon { left: %12px; }"
        "QMenu::separator {"
        "  height: 1px;"
        "  background: %2;"
        "  margin: %4px %4px;"
        "}")
        .arg(DT::css(DT::surfaceCard()))            // 1
        .arg(DT::css(DT::border()))                 // 2
        .arg(DT::kRadiusCard - 2)                   // 3: 10 px
        .arg(kMenuPadding)                          // 4
        .arg(DT::kFontBody)                         // 5
        .arg(kItemPadV)                             // 6
        .arg(kItemPadRight)                         // 7
        .arg(kItemPadLeft)                          // 8
        .arg(DT::kRadius - 2)                       // 9: 6 px, inside the 10 px card
        .arg(DT::css(DT::surfaceHover()))           // 10
        .arg(DT::css(DT::textMuted()))              // 11
        .arg(kIconLeft);                            // 12
}

void ModernMenu::refreshStyle()
{
    // The text color goes in the palette, not in the style sheet: a `color`
    // rule would win over the red of the "danger" items set in
    // initStyleOption().
    QPalette pal = palette();
    for (QPalette::ColorRole role : {QPalette::Text, QPalette::WindowText,
                                     QPalette::ButtonText, QPalette::HighlightedText}) {
        pal.setColor(QPalette::Active, role, DT::textPrimary());
        pal.setColor(QPalette::Inactive, role, DT::textPrimary());
        pal.setColor(QPalette::Disabled, role, DT::textMuted());
    }
    setPalette(pal);
    setStyleSheet(styleSheetForCurrentTheme());

    // Icons follow the theme too.
    for (QAction *action : actions()) {
        QVariant icon = action->property(kPropIcon);
        if (!icon.isValid())
            continue;
        bool danger = action->property(kPropDanger).toBool();
        action->setIcon(iconFor(icon.toInt(),
                                danger ? DT::dangerText() : DT::textSecondary()));
        action->setIconVisibleInMenu(true);
    }
}

QAction *ModernMenu::addIconAction(int fa_icon, const QString& text, bool danger)
{
    QAction *action = addAction(text);
    decorateAction(action, fa_icon, danger);
    action->setIcon(iconFor(fa_icon, danger ? DT::dangerText() : DT::textSecondary()));
    return action;
}

void ModernMenu::decorateAction(QAction *action, int fa_icon, bool danger)
{
    if (!action)
        return;
    action->setProperty(kPropIcon, fa_icon);
    action->setProperty(kPropDanger, danger);
    action->setIconVisibleInMenu(true);
    action->setIcon(iconFor(fa_icon, danger ? DT::dangerText() : DT::textSecondary()));
}

void ModernMenu::initStyleOption(QStyleOptionMenuItem *option, const QAction *action) const
{
    QMenu::initStyleOption(option, action);
    if (action && action->property(kPropDanger).toBool()) {
        const QColor red = DT::dangerText();
        for (QPalette::ColorRole role : {QPalette::Text, QPalette::WindowText,
                                         QPalette::ButtonText, QPalette::HighlightedText}) {
            option->palette.setColor(QPalette::Active, role, red);
            option->palette.setColor(QPalette::Inactive, role, red);
        }
    }
}
