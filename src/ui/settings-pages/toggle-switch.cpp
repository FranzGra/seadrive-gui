#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QEnterEvent>

#include "src/ui/design-tokens.h"
#include "toggle-switch.h"

namespace {

const int kTrackWidth = 40;
const int kTrackHeight = 22;
const int kKnobMargin = 3;
const int kFocusRing = 2;
const int kAnimMsecs = 140;

} // namespace

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent),
      position_(0.0),
      hover_(false)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    anim_ = new QPropertyAnimation(this, "position", this);
    anim_->setDuration(kAnimMsecs);
    anim_->setEasingCurve(QEasingCurve::OutCubic);

    connect(this, SIGNAL(toggled(bool)), this, SLOT(animateTo(bool)));
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(kTrackWidth + 2 * kFocusRing, kTrackHeight + 2 * kFocusRing);
}

QSize ToggleSwitch::minimumSizeHint() const
{
    return sizeHint();
}

void ToggleSwitch::setPosition(qreal position)
{
    position_ = position;
    update();
}

void ToggleSwitch::setCheckedImmediate(bool checked)
{
    anim_->stop();
    bool blocked = blockSignals(true);
    setChecked(checked);
    blockSignals(blocked);
    position_ = checked ? 1.0 : 0.0;
    update();
}

void ToggleSwitch::animateTo(bool checked)
{
    anim_->stop();
    anim_->setStartValue(position_);
    anim_->setEndValue(checked ? 1.0 : 0.0);
    anim_->start();
}

bool ToggleSwitch::hitButton(const QPoint &pos) const
{
    return rect().contains(pos);
}

void ToggleSwitch::enterEvent(QEnterEvent *event)
{
    hover_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ToggleSwitch::leaveEvent(QEvent *event)
{
    hover_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

static QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    using namespace DesignTokens;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) {
        p.setOpacity(0.4);
    }

    QRectF track((width() - kTrackWidth) / 2.0, (height() - kTrackHeight) / 2.0,
                 kTrackWidth, kTrackHeight);
    qreal radius = kTrackHeight / 2.0;

    // Track: muted when off, accent when on.
    QColor off = textMuted();
    off.setAlphaF(isDark() ? 0.55 : 0.45);
    if (hover_ && isEnabled()) {
        off.setAlphaF(off.alphaF() + 0.15);
    }
    QColor on = accentText();
    p.setPen(Qt::NoPen);
    p.setBrush(mix(off, on, position_));
    p.drawRoundedRect(track, radius, radius);

    // Knob
    qreal knob = kTrackHeight - 2 * kKnobMargin;
    qreal x0 = track.left() + kKnobMargin;
    qreal x1 = track.right() - kKnobMargin - knob;
    QRectF knob_rect(x0 + (x1 - x0) * position_, track.top() + kKnobMargin, knob, knob);
    p.setBrush(isDark() ? textPrimary() : surfaceCard());
    p.drawEllipse(knob_rect);

    // Keyboard focus ring
    if (hasFocus()) {
        QPen pen(accentText());
        pen.setWidthF(kFocusRing);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QRectF ring = track.adjusted(-kFocusRing / 2.0 - 1, -kFocusRing / 2.0 - 1,
                                     kFocusRing / 2.0 + 1, kFocusRing / 2.0 + 1);
        p.drawRoundedRect(ring, radius + 2, radius + 2);
    }
}
