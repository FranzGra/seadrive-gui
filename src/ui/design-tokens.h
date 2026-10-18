#ifndef SEADRIVE_GUI_DESIGN_TOKENS_H
#define SEADRIVE_GUI_DESIGN_TOKENS_H

// Shared design tokens for the redesigned UI (sync panel, transfer window,
// settings window). Every new widget reads colors and sizes from here so the
// three screens look like one product, in both light and dark mode.

#include <QColor>
#include <QGuiApplication>
#include <QStyleHints>
#include <QString>

namespace DesignTokens {

inline bool isDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

// Surfaces
inline QColor surfacePage()   { return isDark() ? QColor("#1f1f1e") : QColor("#f5f4ef"); }
inline QColor surfaceCard()   { return isDark() ? QColor("#2a2a28") : QColor("#ffffff"); }
inline QColor surfaceHover()  { return isDark() ? QColor("#343432") : QColor("#f0efe9"); }

// Text
inline QColor textPrimary()   { return isDark() ? QColor("#f1efe8") : QColor("#1f1e1d"); }
inline QColor textSecondary() { return isDark() ? QColor("#b4b2a9") : QColor("#5f5e5a"); }
inline QColor textMuted()     { return isDark() ? QColor("#888780") : QColor("#888780"); }

// Borders
inline QColor border()        { return isDark() ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 22); }

// Roles: accent (in progress), success (all synced), warning, danger
inline QColor accentText()    { return isDark() ? QColor("#85b7eb") : QColor("#185fa5"); }
inline QColor accentBg()      { return isDark() ? QColor("#0c447c") : QColor("#e6f1fb"); }
inline QColor successText()   { return isDark() ? QColor("#97c459") : QColor("#3b6d11"); }
inline QColor successBg()     { return isDark() ? QColor("#27500a") : QColor("#eaf3de"); }
inline QColor warningText()   { return isDark() ? QColor("#ef9f27") : QColor("#854f0b"); }
inline QColor warningBg()     { return isDark() ? QColor("#633806") : QColor("#faeeda"); }
inline QColor dangerText()    { return isDark() ? QColor("#f09595") : QColor("#a32d2d"); }

// Direction of a file: sent (local change -> server, blue) and received
// (change made elsewhere -> downloaded here, teal). Used for the arrow
// badge and the "Inviato" / "Ricevuto" pill, in progress and completed.
inline QColor sentText()      { return accentText(); }
inline QColor sentBg()        { return accentBg(); }
inline QColor receivedText()  { return isDark() ? QColor("#5dcaa5") : QColor("#0f6e56"); }
inline QColor receivedBg()    { return isDark() ? QColor("#085041") : QColor("#e1f5ee"); }

// Sizes (px). Text never below 13 px.
constexpr int kFontTitle   = 18;
constexpr int kFontBody    = 14;
constexpr int kFontSmall   = 13;
constexpr int kRadius      = 8;
constexpr int kRadiusCard  = 12;
constexpr int kPadding     = 16;
constexpr int kRowPadding  = 12;
constexpr int kIconSize    = 20;
// Status icons (panel status box, panel rows, transfer rows): the glyph is
// drawn optically centered in a square badge, same size everywhere.
constexpr int kStatusBadge = 32;
constexpr int kStatusGlyph = 16;
constexpr int kPillHeight  = 20;

inline QString css(const QColor &c) { return c.name(QColor::HexArgb); }

} // namespace DesignTokens

#endif // SEADRIVE_GUI_DESIGN_TOKENS_H
