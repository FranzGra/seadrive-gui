#include <climits>

#include <QtWidgets>

#include <sqlite3.h>

#include "seadrive-gui.h"
#include "settings-mgr.h"
#include "utils/utils.h"
#include "src/ui/design-tokens.h"

#include "toggle-switch.h"
#include "cache-page.h"

using namespace DesignTokens;

namespace {

const int kSaveDelayMSecs = 600;
const int kUsageRefreshMSecs = 15 * 1000;
const int kSliderMaxGB = 200;
const int kLimitMaxGB = 10000;
const int kMaxIntervalMinutes = 24 * 60;
const int kMaxIntervalHours = 240;
const int kBarMax = 1000;
const char *kCacheDbName = "cache.db";

QSpinBox *makeSpin(int min, int max, const QString& suffix, int width)
{
    QSpinBox *spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setSuffix(suffix);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    spin->setKeyboardTracking(false);
    spin->setFixedWidth(width);
    return spin;
}

QWidget *hbox(std::initializer_list<QWidget *> widgets)
{
    QWidget *box = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    for (QWidget *w : widgets) {
        layout->addWidget(w);
    }
    return box;
}

} // namespace

CachePage::CachePage(QWidget *parent)
    : SettingsPage(tr("Cache"),
                   tr("Disk space for the files downloaded from the server."),
                   parent),
      loading_(false),
      used_bytes_(0),
      usage_known_(false),
      bar_over_(false)
{
    // --- Space used
    addGroup(tr("Space used"));

    QWidget *usage = new QWidget;
    QVBoxLayout *usage_layout = new QVBoxLayout(usage);
    usage_layout->setContentsMargins(0, 2, 0, 2);
    usage_layout->setSpacing(10);

    QHBoxLayout *numbers = new QHBoxLayout;
    numbers->setSpacing(8);
    used_label_ = new QLabel;
    used_label_->setObjectName("rowTitleStrong");
    used_label_->setStyleSheet(QString("font-size: %1px; font-weight: 600;").arg(kFontTitle + 4));
    of_label_ = new QLabel;
    of_label_->setObjectName("rowDesc");
    over_pill_ = makePill(tr("Over the limit"), "warning");
    over_pill_->hide();
    numbers->addWidget(used_label_, 0, Qt::AlignBaseline);
    numbers->addWidget(of_label_, 0, Qt::AlignBaseline);
    numbers->addWidget(over_pill_, 0, Qt::AlignVCenter);
    numbers->addStretch(1);
    usage_layout->addLayout(numbers);

    used_bar_ = new QProgressBar;
    used_bar_->setObjectName("cacheBar");
    used_bar_->setRange(0, kBarMax);
    used_bar_->setTextVisible(false);
    used_bar_->setFixedHeight(8);
    usage_layout->addWidget(used_bar_);

    files_label_ = new QLabel;
    files_label_->setObjectName("rowDesc");
    files_label_->setWordWrap(true);
    usage_layout->addWidget(files_label_);

    addWideRow(usage);

    QTimer *usage_timer = new QTimer(this);
    usage_timer->setInterval(kUsageRefreshMSecs);
    connect(usage_timer, &QTimer::timeout, this, &CachePage::refreshUsage);
    connect(this, &SettingsPage::pageShown, this, [this, usage_timer] {
        refreshUsage();
        usage_timer->start();
    });
    connect(this, &SettingsPage::pageHidden, usage_timer, &QTimer::stop);

    // --- Limits
    addGroup(tr("Limits"));

    limit_slider_ = new QSlider(Qt::Horizontal);
    limit_slider_->setObjectName("cacheSlider");
    limit_slider_->setRange(1, kSliderMaxGB);
    limit_slider_->setFixedWidth(180);
    limit_slider_->setCursor(Qt::PointingHandCursor);
    limit_slider_->setAccessibleName(tr("Maximum size"));
    limit_spin_ = makeSpin(1, kLimitMaxGB, tr(" GB"), 100);
    limit_spin_->setAccessibleName(tr("Maximum size"));
    addRow(tr("Maximum size"),
           tr("Above this limit, the least used downloaded files are freed."),
           hbox({limit_slider_, limit_spin_}));

    limit_timer_ = new QTimer(this);
    limit_timer_->setSingleShot(true);
    limit_timer_->setInterval(kSaveDelayMSecs);
    connect(limit_timer_, &QTimer::timeout, this, [this] {
        SettingsManager *mgr = gui->settingsManager();
        const int v = limit_spin_->value();
        if (mgr->getCacheSizeLimitGB() != v) {
            mgr->setCacheSizeLimitGB(v);
        }
    });
    connect(limit_slider_, &QSlider::valueChanged, this, [this](int v) {
        if (loading_) return;
        // The slider covers the usual sizes, the field goes further.
        QSignalBlocker block(limit_spin_);
        limit_spin_->setValue(v);
        onLimitChanged(v);
    });
    connect(limit_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (loading_) return;
        QSignalBlocker block(limit_slider_);
        limit_slider_->setValue(qMin(v, kSliderMaxGB));
        onLimitChanged(v);
    });

    // --- Automatic cleanup
    addGroup(tr("Automatic cleanup"));

    clean_toggle_ = new ToggleSwitch;
    clean_toggle_->setAccessibleName(tr("Check the cache automatically"));
    addRow(tr("Check the cache automatically"),
           tr("Frees space when the cache is over the limit."),
           clean_toggle_);
    connect(clean_toggle_, &ToggleSwitch::toggled, this, &CachePage::onCleanToggled);

    interval_spin_ = makeSpin(1, kMaxIntervalMinutes, QString(), 80);
    interval_spin_->setAccessibleName(tr("Every"));
    interval_unit_ = new QComboBox;
    interval_unit_->addItem(QString(), 1);
    interval_unit_->addItem(QString(), 60);
    interval_unit_->setFixedWidth(120);
    interval_row_ = addRow(tr("Every"),
                           tr("How often to check whether the cache is over the limit."),
                           hbox({interval_spin_, interval_unit_}));

    interval_timer_ = new QTimer(this);
    interval_timer_->setSingleShot(true);
    interval_timer_->setInterval(kSaveDelayMSecs);
    connect(interval_timer_, &QTimer::timeout, this, [this] {
        SettingsManager *mgr = gui->settingsManager();
        const int minutes = intervalMinutes();
        if (mgr->getCacheCleanIntervalMinutes() != minutes) {
            mgr->setCacheCleanIntervalMinutes(minutes);
        }
    });
    connect(interval_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CachePage::onIntervalChanged);
    connect(interval_unit_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (loading_) return;
                // Keep the same time: 120 minutes become 2 hours.
                QSignalBlocker block(interval_spin_);
                const int v = interval_spin_->value();
                if (index == 1) {
                    interval_spin_->setRange(1, kMaxIntervalHours);
                    interval_spin_->setValue(qMax(1, (v + 30) / 60));
                } else {
                    interval_spin_->setRange(1, kMaxIntervalMinutes);
                    interval_spin_->setValue(qMin(kMaxIntervalMinutes, v * 60));
                }
                onIntervalChanged();
            });

    addStretch();
    themeChanged();
}

void CachePage::load()
{
    SettingsManager *mgr = gui->settingsManager();
    loading_ = true;

    const int limit = mgr->getCacheSizeLimitGB();
    limit_spin_->setValue(limit);
    limit_slider_->setValue(qMin(limit, kSliderMaxGB));

    const bool enabled = mgr->getCacheCleanEnabled();
    clean_toggle_->setCheckedImmediate(enabled);
    interval_row_->setEnabled(enabled);
    showInterval(mgr->getCacheCleanIntervalMinutes());

    loading_ = false;
    refreshUsage();
}

void CachePage::themeChanged()
{
    applyBarStyle(bar_over_);
    limit_slider_->setStyleSheet(
        QString("QSlider#cacheSlider::groove:horizontal { height: 4px; background: %1;"
                " border-radius: 2px; }"
                "QSlider#cacheSlider::sub-page:horizontal { background: %2; border-radius: 2px; }"
                "QSlider#cacheSlider::handle:horizontal { width: 16px; height: 16px;"
                " margin: -7px 0; border-radius: 9px; background: %3; border: 2px solid %2; }"
                "QSlider#cacheSlider:focus { outline: none; }")
            .arg(css(border()), css(accentText()), css(surfaceCard())));
}

void CachePage::applyBarStyle(bool over)
{
    bar_over_ = over;
    used_bar_->setStyleSheet(
        QString("QProgressBar#cacheBar { background: %1; border: none; border-radius: 4px; }"
                "QProgressBar#cacheBar::chunk { background: %2; border-radius: 4px; }")
            .arg(css(surfaceHover()), css(over ? warningText() : accentText())));
}

QString CachePage::formatGB(double gb)
{
    // Same unit as the limit (1 GB = 10^9 bytes, as the daemon counts it).
    return QLocale().toString(gb, 'f', gb < 10 ? 2 : 1) + QStringLiteral(" GB");
}

bool CachePage::readCacheUsage(qint64 *bytes, qint64 *files)
{
    const QString path = QDir(seadriveDataDir()).filePath(kCacheDbName);
    if (!QFileInfo::exists(path)) {
        return false;
    }

    // Read only: the daemon owns this file, nothing is ever written here.
    const QByteArray uri = QUrl::fromLocalFile(path).toEncoded() + "?mode=ro";
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(uri.constData(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI,
                        nullptr) != SQLITE_OK) {
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }
    sqlite3_busy_timeout(db, 200);

    bool ok = false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*), COALESCE(SUM(size), 0) FROM CachedFiles",
                           -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        *files = sqlite3_column_int64(stmt, 0);
        *bytes = sqlite3_column_int64(stmt, 1);
        ok = true;
    }
    if (stmt) {
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return ok;
}

void CachePage::refreshUsage()
{
    qint64 bytes = 0, files = 0;
    const bool known = readCacheUsage(&bytes, &files);
    if (known != usage_known_) {
        if (!known) {
            qWarning("cache page: unable to read %s", kCacheDbName);
        }
    }
    usage_known_ = known;
    used_bytes_ = bytes;

    const int limit_gb = limit_spin_->value();
    const double limit_bytes = double(limit_gb) * 1e9;

    if (!known) {
        used_label_->setText(QStringLiteral("—"));
        of_label_->setText(tr("of %1").arg(formatGB(limit_gb)));
        files_label_->setText(tr("Unable to read the space used right now."));
        over_pill_->hide();
        used_bar_->setValue(0);
        applyBarStyle(false);
        return;
    }

    const double ratio = limit_bytes > 0 ? double(bytes) / limit_bytes : 0;
    const bool over = ratio > 1.0;
    used_label_->setText(formatGB(double(bytes) / 1e9));
    of_label_->setText(tr("of %1 · %2%")
                           .arg(formatGB(limit_gb))
                           .arg(qRound(ratio * 100)));
    files_label_->setText(tr("%n downloaded file(s)", nullptr, int(qMin<qint64>(files, INT_MAX))));
    over_pill_->setVisible(over);
    // Over the limit: the bar is full.
    used_bar_->setValue(over ? kBarMax : qRound(ratio * kBarMax));
    if (over != bar_over_) {
        applyBarStyle(over);
    }
}

void CachePage::onLimitChanged(int)
{
    limit_timer_->start();
    refreshUsage();
}

void CachePage::onCleanToggled(bool checked)
{
    interval_row_->setEnabled(checked);
    if (loading_) return;
    gui->settingsManager()->setCacheCleanEnabled(checked);
}

void CachePage::showInterval(int minutes)
{
    QSignalBlocker block_spin(interval_spin_);
    QSignalBlocker block_unit(interval_unit_);
    minutes = qMax(1, minutes);
    if (minutes >= 60 && (minutes % 60 == 0 || minutes > kMaxIntervalMinutes)) {
        interval_unit_->setCurrentIndex(1);
        interval_spin_->setRange(1, kMaxIntervalHours);
        interval_spin_->setValue(qMin(kMaxIntervalHours, (minutes + 30) / 60));
    } else {
        interval_unit_->setCurrentIndex(0);
        interval_spin_->setRange(1, kMaxIntervalMinutes);
        interval_spin_->setValue(minutes);
    }
    const int n = interval_spin_->value();
    interval_unit_->setItemText(0, tr("minute(s)", nullptr, n));
    interval_unit_->setItemText(1, tr("hour(s)", nullptr, n));
}

int CachePage::intervalMinutes() const
{
    return interval_spin_->value() * interval_unit_->currentData().toInt();
}

void CachePage::onIntervalChanged()
{
    const int n = interval_spin_->value();
    interval_unit_->setItemText(0, tr("minute(s)", nullptr, n));
    interval_unit_->setItemText(1, tr("hour(s)", nullptr, n));
    if (loading_) return;
    interval_timer_->start();
}
