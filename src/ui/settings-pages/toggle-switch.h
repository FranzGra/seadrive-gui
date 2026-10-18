#ifndef SEADRIVE_GUI_TOGGLE_SWITCH_H
#define SEADRIVE_GUI_TOGGLE_SWITCH_H

#include <QAbstractButton>

class QPropertyAnimation;

// A flat on/off switch used instead of check boxes in the settings window.
// Colors come from DesignTokens, so it follows the light/dark theme.
//
// Use setCheckedImmediate() when loading a value from the settings: it does
// not animate and does not emit toggled(), so no "save" handler runs.
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal position READ position WRITE setPosition)

public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    qreal position() const { return position_; }
    void setPosition(qreal position);

    void setCheckedImmediate(bool checked);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool hitButton(const QPoint &pos) const override;

private slots:
    void animateTo(bool checked);

private:
    qreal position_;
    bool hover_;
    QPropertyAnimation *anim_;
};

#endif // SEADRIVE_GUI_TOGGLE_SWITCH_H
