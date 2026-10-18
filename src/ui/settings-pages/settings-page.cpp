#include <QtWidgets>

#include "src/ui/design-tokens.h"
#include "settings-page.h"

namespace {

const int kPageMarginH = 32;
const int kPageMarginTop = 28;
const int kPageMarginBottom = 32;
const int kGroupSpacing = 20;
const int kRowControlSpacing = 24;

QLabel *wrappedLabel(const QString& text, const char *object_name)
{
    QLabel *label = new QLabel(text);
    label->setObjectName(object_name);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    return label;
}

} // namespace

SettingsPage::SettingsPage(const QString& title, const QString& subtitle, QWidget *parent)
    : QScrollArea(parent),
      current_group_(nullptr),
      group_layout_(nullptr)
{
    using namespace DesignTokens;

    setObjectName("settingsPage");
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    content_ = new QWidget;
    content_->setObjectName("pageContent");
    layout_ = new QVBoxLayout(content_);
    layout_->setContentsMargins(kPageMarginH, kPageMarginTop, kPageMarginH, kPageMarginBottom);
    layout_->setSpacing(4);

    QLabel *title_label = wrappedLabel(title, "pageTitle");
    title_label->setAccessibleName(title);
    layout_->addWidget(title_label);

    if (!subtitle.isEmpty()) {
        layout_->addWidget(wrappedLabel(subtitle, "pageSubtitle"));
    }
    layout_->addSpacing(kGroupSpacing - 4);

    setWidget(content_);
}

void SettingsPage::addGroup(const QString& heading)
{
    using namespace DesignTokens;

    if (current_group_) {
        layout_->addSpacing(kGroupSpacing);
    }

    QLabel *h = nullptr;
    if (!heading.isEmpty()) {
        h = wrappedLabel(heading, "groupHeading");
        layout_->addWidget(h);
        layout_->addSpacing(6);
    }

    current_group_ = new QFrame;
    current_group_->setObjectName("settingsGroup");
    current_group_->setProperty("heading", QVariant::fromValue<QObject *>(h));
    group_layout_ = new QVBoxLayout(current_group_);
    group_layout_->setContentsMargins(0, 0, 0, 0);
    group_layout_->setSpacing(0);
    layout_->addWidget(current_group_);
    groups_.append(current_group_);
}

static QWidget *createRowShell(QHBoxLayout **inner_out)
{
    using namespace DesignTokens;

    QWidget *row = new QWidget;
    row->setObjectName("settingsRow");
    QVBoxLayout *v = new QVBoxLayout(row);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    QFrame *sep = new QFrame;
    sep->setObjectName("rowSeparator");
    sep->setFrameShape(QFrame::NoFrame);
    sep->setFixedHeight(1);
    QHBoxLayout *sep_layout = new QHBoxLayout;
    sep_layout->setContentsMargins(kPadding, 0, 0, 0);
    sep_layout->addWidget(sep);
    v->addLayout(sep_layout);

    QHBoxLayout *inner = new QHBoxLayout;
    inner->setContentsMargins(kPadding, kRowPadding, kPadding, kRowPadding);
    inner->setSpacing(kRowControlSpacing);
    v->addLayout(inner);

    *inner_out = inner;
    return row;
}

QWidget *SettingsPage::addRow(const QString& title, const QString& description, QWidget *control)
{
    if (!current_group_) {
        addGroup();
    }

    QHBoxLayout *inner;
    QWidget *row = createRowShell(&inner);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    QLabel *title_label = wrappedLabel(title, "rowTitle");
    text->addWidget(title_label);
    QLabel *desc_label = wrappedLabel(description, "rowDesc");
    desc_label->setVisible(!description.isEmpty());
    text->addWidget(desc_label);
    inner->addLayout(text, 1);

    if (control) {
        inner->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
        if (control->accessibleName().isEmpty()) {
            control->setAccessibleName(title);
        }
        if (control->accessibleDescription().isEmpty() && !description.isEmpty()) {
            control->setAccessibleDescription(description);
        }
        title_label->setBuddy(control);
    }

    group_layout_->addWidget(row);
    refreshSeparators();
    return row;
}

QWidget *SettingsPage::addWideRow(QWidget *content)
{
    if (!current_group_) {
        addGroup();
    }

    QHBoxLayout *inner;
    QWidget *row = createRowShell(&inner);
    inner->addWidget(content, 1);
    group_layout_->addWidget(row);
    refreshSeparators();
    return row;
}

QLabel *SettingsPage::addNote(const QString& text)
{
    QLabel *note = wrappedLabel(text, "pageNote");
    layout_->addSpacing(8);
    layout_->addWidget(note);
    return note;
}

void SettingsPage::addPageWidget(QWidget *widget)
{
    layout_->addWidget(widget);
}

void SettingsPage::addStretch()
{
    layout_->addStretch(1);
}

QLabel *SettingsPage::rowDescription(QWidget *row)
{
    return row ? row->findChild<QLabel *>("rowDesc") : nullptr;
}

void SettingsPage::refreshSeparators()
{
    for (QFrame *group : groups_) {
        bool first_visible = true;
        const QList<QWidget *> rows =
            group->findChildren<QWidget *>("settingsRow", Qt::FindDirectChildrenOnly);
        for (QWidget *row : rows) {
            QFrame *sep = row->findChild<QFrame *>("rowSeparator");
            // Rows not shown yet are not "hidden": skip only rows hidden on purpose.
            if (row->testAttribute(Qt::WA_WState_ExplicitShowHide) && row->isHidden()) {
                continue;
            }
            if (sep) {
                sep->setVisible(!first_visible);
            }
            first_visible = false;
        }
        // A card with no visible rows is hidden too, with its heading.
        bool group_hidden = first_visible;
        if (group_hidden || group->testAttribute(Qt::WA_WState_ExplicitShowHide)) {
            group->setVisible(!group_hidden);
        }
        QWidget *heading = qobject_cast<QWidget *>(group->property("heading").value<QObject *>());
        if (heading && (group_hidden || heading->testAttribute(Qt::WA_WState_ExplicitShowHide))) {
            heading->setVisible(!group_hidden);
        }
    }
}

QPushButton *SettingsPage::makeButton(const QString& text, ButtonKind kind)
{
    QPushButton *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setAutoDefault(false);
    switch (kind) {
    case ButtonPrimary:
        btn->setObjectName("primaryButton");
        break;
    case ButtonDanger:
        btn->setObjectName("dangerButton");
        break;
    default:
        btn->setObjectName("secondaryButton");
        break;
    }
    return btn;
}

QLabel *SettingsPage::makePill(const QString& text, const QString& kind)
{
    QLabel *pill = new QLabel;
    pill->setObjectName("pill");
    pill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setPill(pill, text, kind);
    return pill;
}

void SettingsPage::setPill(QLabel *pill, const QString& text, const QString& kind)
{
    pill->setText(text);
    if (pill->property("pill").toString() != kind) {
        pill->setProperty("pill", kind);
        pill->style()->unpolish(pill);
        pill->style()->polish(pill);
    }
}

void SettingsPage::showEvent(QShowEvent *event)
{
    QScrollArea::showEvent(event);
    emit pageShown();
}

void SettingsPage::hideEvent(QHideEvent *event)
{
    QScrollArea::hideEvent(event);
    emit pageHidden();
}
