#ifndef SEADRIVE_GUI_GLYPH_ICON_H
#define SEADRIVE_GUI_GLYPH_ICON_H

// Status icons drawn from the QtAwesome font, optically centered.
//
// QtAwesome paints a glyph with the font metrics, so the ink of each glyph
// sits at a different place in the box (the check sign looks big and low, the
// arrows thin and high). Here the glyph is turned into a path, scaled so its
// largest side is exactly `glyph` px and centered on its own ink box: every
// icon has the same size and sits in the middle of its badge.

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>

#include "QtAwesome.h"
#include "ui/design-tokens.h"

namespace GlyphIcon {

// Paints the glyph centered in `box` (the badge background is up to the
// caller).
inline void paint(QPainter *painter, const QRectF& box, int fa_icon,
                  const QColor& color, int glyph = DesignTokens::kStatusGlyph)
{
    if (!awesome || !painter)
        return;
    QPainterPath path;
    path.addText(0, 0, awesome->font(100), QString(QChar(fa_icon)));
    const QRectF ink = path.boundingRect();
    if (ink.isEmpty())
        return;
    const qreal scale = glyph / qMax(ink.width(), ink.height());

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(box.center());
    painter->scale(scale, scale);
    painter->translate(-ink.center());
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPath(path);
    painter->restore();
}

// Pixmap of `box` x `box` px (logical), with an optional rounded badge
// behind the glyph.
inline QPixmap pixmap(int fa_icon, const QColor& color, qreal dpr,
                      int box = DesignTokens::kStatusBadge,
                      int glyph = DesignTokens::kStatusGlyph,
                      const QColor& badge = QColor(Qt::transparent),
                      int radius = DesignTokens::kRadius)
{
    QPixmap pm(qRound(box * dpr), qRound(box * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(0, 0, box, box);
    if (badge.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(badge);
        painter.drawRoundedRect(r, radius, radius);
    }
    paint(&painter, r, fa_icon, color, glyph);
    return pm;
}

} // namespace GlyphIcon

#endif // SEADRIVE_GUI_GLYPH_ICON_H
