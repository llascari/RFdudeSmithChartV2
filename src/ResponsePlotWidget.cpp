#include "ResponsePlotWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
double db(rf::Complex value)
{
    const double magnitude = std::abs(value);
    return magnitude == 0 ? -300 : 20 * std::log10(magnitude);
}

double coarseCeilingDb(double maximum)
{
    if (!std::isfinite(maximum)) return 0;
    if (maximum <= 0) return 0;
    if (maximum <= 10) return 10;
    if (maximum <= 20) return 20;
    if (maximum <= 50) return 50;
    if (maximum <= 100) return 100;
    return std::ceil(maximum / 100) * 100;
}
}

ResponsePlotWidget::ResponsePlotWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("responsePlot");
    setAccessibleName(tr("S parameter magnitude and stability versus frequency"));
    setMinimumSize(520, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(tr("Click the plot to set the analysis frequency. "
                  "Up/Down or wheel while focused changes frequency."));
}

void ResponsePlotWidget::setFocusBorderEnabled(bool enabled)
{
    if (focusBorderEnabled_ == enabled) return;
    focusBorderEnabled_ = enabled;
    update();
}

void ResponsePlotWidget::setResponse(const rf::Sweep& sweep, std::vector<rf::ResponseSample> samples, bool onePort)
{
    sweep_ = sweep;
    samples_ = std::move(samples);
    onePort_ = onePort;
    double maximum = -300;
    for (const auto& sample : samples_) {
        maximum = std::max(maximum, db(sample.s.s11));
        if (!onePort_) maximum = std::max({maximum, db(sample.s.s21), db(sample.s.s22)});
        if (!onePort_ && showGmax_) {
            const auto metrics = rf::stabilityMetrics(sample.s);
            if (showGmax_ && metrics.gmaxDefined)
                maximum = std::max(maximum, metrics.gmaxDb);
            if (showGmax_ && metrics.msgDefined)
                maximum = std::max(maximum, metrics.msgDb);
        }
    }
    if (magnitudeAutoScale_)
        ceilingDb_ = coarseCeilingDb(maximum);
    else
        ceilingDb_ = std::max(magnitudeCeilingDb_, floorDb_ + 1);

    stabilityMinimum_ = -1;
    stabilityMaximum_ = 5;
    if (!onePort_) {
        for (const auto& sample : samples_) {
            const auto metrics = rf::stabilityMetrics(sample.s);
            if (showK_ && metrics.kDefined)
                stabilityMinimum_ = std::min(stabilityMinimum_, metrics.k);
            if (showB1_)
                stabilityMinimum_ = std::min(stabilityMinimum_, metrics.b1);
            if (showK_ && metrics.kDefined)
                stabilityMaximum_ = std::max(stabilityMaximum_, metrics.k);
            if (showB1_)
                stabilityMaximum_ = std::max(stabilityMaximum_, metrics.b1);
        }
        if (!std::isfinite(stabilityMinimum_)) stabilityMinimum_ = -1;
        if (!std::isfinite(stabilityMaximum_) || stabilityMaximum_ < 5)
            stabilityMaximum_ = 5;
        if (stabilityMaximum_ <= 10) stabilityMaximum_ = std::ceil(stabilityMaximum_);
        else if (stabilityMaximum_ <= 100) stabilityMaximum_ = std::ceil(stabilityMaximum_ / 10) * 10;
        else stabilityMaximum_ = std::ceil(stabilityMaximum_ / 100) * 100;
    }
    update();
}

void ResponsePlotWidget::setStabilityTraces(bool gmax, bool k, bool b1)
{
    showGmax_ = gmax;
    showK_ = k;
    showB1_ = b1;
    setResponse(sweep_, samples_, onePort_);
}

void ResponsePlotWidget::setMagnitudeScale(bool automatic, double ceilingDb, double floorDb)
{
    magnitudeAutoScale_ = automatic;
    magnitudeCeilingDb_ = std::max(0.0, ceilingDb);
    floorDb_ = automatic ? -100.0 : std::clamp(floorDb, -300.0, 0.0);
    setResponse(sweep_, samples_, onePort_);
}

void ResponsePlotWidget::setMarker(double frequencyMHz)
{
    markerMHz_ = frequencyMHz;
    markerVisible_ = true;
    update();
}

void ResponsePlotWidget::clearMarker()
{
    markerVisible_ = false;
    update();
}

void ResponsePlotWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    update();
}

void ResponsePlotWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    update();
}

bool ResponsePlotWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        const auto key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Left || key == Qt::Key_Right
            || key == Qt::Key_Up || key == Qt::Key_Down) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

QRectF ResponsePlotWidget::plotRect() const
{
    return QRectF(64, 52, width() - 88, height() - 112);
}

double ResponsePlotWidget::fraction(double frequencyMHz) const
{
    if (sweep_.logarithmic)
        return (std::log(frequencyMHz) - std::log(sweep_.startMHz))
            / (std::log(sweep_.stopMHz) - std::log(sweep_.startMHz));
    return (frequencyMHz - sweep_.startMHz) / (sweep_.stopMHz - sweep_.startMHz);
}

QPointF ResponsePlotWidget::pointForSample(double frequencyMHz, double decibels) const
{
    const auto rect = plotRect();
    const double clipped = std::clamp(decibels, floorDb_, ceilingDb_);
    const double denominator = ceilingDb_ - floorDb_;
    return {rect.left() + fraction(frequencyMHz) * rect.width(),
            rect.top() + std::clamp((ceilingDb_ - clipped) / std::max(denominator, 1.0),
                                    0.0, 1.0) * rect.height()};
}

QPointF ResponsePlotWidget::pointForStability(double frequencyMHz, double value) const
{
    const auto rect = plotRect();
    const double fractionValue =
        std::clamp((value - stabilityMinimum_) / (stabilityMaximum_ - stabilityMinimum_), 0.0, 1.0);
    return {rect.left() + fraction(frequencyMHz) * rect.width(),
            rect.top() + (1.0 - fractionValue) * rect.height()};
}

void ResponsePlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);
    const auto area = plotRect();
    const std::array<QColor, 3> colors{QColor(190, 45, 40), QColor(30, 95, 200), QColor(15, 120, 65)};
    const std::array<QString, 3> names{"S11", "S21", "S22"};
    struct LegendEntry
    {
        QString name;
        QColor color;
        Qt::PenStyle style;
    };
    std::vector<LegendEntry> legendEntries;
    for (int i = 0; i < (onePort_ ? 1 : 3); ++i) {
        legendEntries.push_back({names[i], colors[i],
                                 i == 2 ? Qt::DashLine : Qt::SolidLine});
    }
    const std::array<std::pair<bool, QColor>, 3> stabilityLegend{
        std::pair{showGmax_, QColor("#8a3fb4")},
        std::pair{showK_, QColor("#d06b18")},
        std::pair{showB1_, QColor("#008b8b")}
    };
    const std::array<QString, 3> stabilityNames{"Gmax/MSG", "K", "B1"};
    for (int i = 0; i < 3; ++i) {
        if (!stabilityLegend[static_cast<std::size_t>(i)].first || onePort_) continue;
        legendEntries.push_back({stabilityNames[i],
                                 stabilityLegend[static_cast<std::size_t>(i)].second,
                                 i == 0 ? Qt::SolidLine : Qt::DashLine});
    }
    int legendX = 12;
    int legendY = 17;
    constexpr int legendRowHeight = 24;
    constexpr int legendLineWidth = 24;
    constexpr int legendGap = 14;
    for (const auto& entry : legendEntries) {
        const int textWidth = painter.fontMetrics().horizontalAdvance(entry.name);
        const int itemWidth = legendLineWidth + 6 + textWidth + legendGap;
        if (legendX > 12 && legendX + itemWidth > width() - 12) {
            legendX = 12;
            legendY += legendRowHeight;
        }
        painter.setPen(QPen(entry.color, 2, entry.style));
        painter.drawLine(legendX, legendY, legendX + legendLineWidth, legendY);
        painter.setPen(Qt::black);
        painter.drawText(legendX + legendLineWidth + 6, legendY + 5, entry.name);
        legendX += itemWidth;
    }
    for (int i = 0; i <= 5; ++i) {
        const double y = area.top() + i * area.height() / 5;
        painter.setPen(QColor(220, 220, 220));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(Qt::black);
        painter.drawText(QRectF(2, y - 10, 52, 20), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(std::lerp(ceilingDb_, floorDb_, i / 5.0), 'g', 4));
        if (showK_ || showB1_) {
            if (!onePort_) {
                painter.setPen(Qt::black);
                painter.drawText(QRectF(area.right() + 6, y - 10, 68, 20),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QString::number(std::lerp(stabilityMaximum_, stabilityMinimum_,
                                                           i / 5.0), 'g', 4));
            }
        }
        const double x = area.left() + i * area.width() / 5;
        painter.setPen(QColor(220, 220, 220));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        const double f = sweep_.logarithmic
            ? std::exp(std::lerp(std::log(sweep_.startMHz), std::log(sweep_.stopMHz), i / 5.0))
            : std::lerp(sweep_.startMHz, sweep_.stopMHz, i / 5.0);
        painter.setPen(Qt::black);
        painter.drawText(QRectF(x - 45, area.bottom() + 6, 90, 22), Qt::AlignCenter,
                         QString::number(f, 'g', 5));
    }
    painter.drawRect(area);
    painter.drawText(QRectF(area.left(), height() - 52, area.width(), 22), Qt::AlignCenter,
                     sweep_.logarithmic ? tr("Frequency (MHz, logarithmic)") : tr("Frequency (MHz, linear)"));
    painter.drawText(QRectF(2, height() - 28, 210, 22), Qt::AlignLeft,
                     tr("Magnitude (dB), floor %1").arg(floorDb_, 0, 'g', 5));
    if (!onePort_ && (showK_ || showB1_)) {
        painter.drawText(QRectF(area.right() - 210, height() - 28, 210, 22), Qt::AlignRight,
                         tr("Right axis: stability"));
    }
    if (samples_.empty()) {
        painter.drawText(area, Qt::AlignCenter, tr("No response - apply a valid sweep."));
        return;
    }
    painter.save();
    painter.setClipRect(area.adjusted(-1, -1, 1, 1));
    for (int trace = 0; trace < (onePort_ ? 1 : 3); ++trace) {
        QPainterPath path;
        bool first = true;
        for (const auto& sample : samples_) {
            const auto value = trace == 0 ? sample.s.s11 : trace == 1 ? sample.s.s21 : sample.s.s22;
            const auto point = pointForSample(sample.frequencyMHz, db(value));
            if (first) path.moveTo(point);
            else path.lineTo(point);
            first = false;
        }
        painter.setPen(QPen(colors[trace], 1.8,
                            trace == 2 ? Qt::DashLine : Qt::SolidLine));
        painter.drawPath(path);
        if (samples_.size() == 1) {
            const auto& s = samples_.front().s;
            const auto value = trace == 0 ? s.s11 : trace == 1 ? s.s21 : s.s22;
            painter.drawEllipse(pointForSample(samples_.front().frequencyMHz, db(value)), 3, 3);
        }
    }
    if (!onePort_) {
        if (showGmax_) {
            QPainterPath gmaxPath;
            QPainterPath msgPath;
            bool gmaxFirst = true;
            bool msgFirst = true;
            for (const auto& sample : samples_) {
                const auto metrics = rf::stabilityMetrics(sample.s);
                if (metrics.gmaxDefined) {
                    const auto point = pointForSample(sample.frequencyMHz, metrics.gmaxDb);
                    if (gmaxFirst) gmaxPath.moveTo(point);
                    else gmaxPath.lineTo(point);
                    gmaxFirst = false;
                }
                if (metrics.msgDefined) {
                    const auto point = pointForSample(sample.frequencyMHz, metrics.msgDb);
                    if (msgFirst) msgPath.moveTo(point);
                    else msgPath.lineTo(point);
                    msgFirst = false;
                }
            }
            painter.setPen(QPen(QColor("#8a3fb4"), 2));
            painter.drawPath(gmaxPath);
            painter.setPen(QPen(QColor("#8a3fb4"), 2, Qt::DotLine));
            painter.drawPath(msgPath);
        }
        if (showK_) {
            QPainterPath path;
            bool first = true;
            for (const auto& sample : samples_) {
                const auto metrics = rf::stabilityMetrics(sample.s);
                if (!metrics.kDefined) continue;
                const auto point = pointForStability(sample.frequencyMHz, metrics.k);
                if (first) path.moveTo(point);
                else path.lineTo(point);
                first = false;
            }
            painter.setPen(QPen(QColor("#d06b18"), 1.8, Qt::DashLine));
            painter.drawPath(path);
            painter.setPen(QPen(QColor("#d06b18"), 1, Qt::DotLine));
            painter.drawLine(area.left(), pointForStability(sweep_.startMHz, 1).y(),
                             area.right(), pointForStability(sweep_.stopMHz, 1).y());
        }
        if (showB1_) {
            QPainterPath path;
            bool first = true;
            for (const auto& sample : samples_) {
                const auto metrics = rf::stabilityMetrics(sample.s);
                const auto point = pointForStability(sample.frequencyMHz, metrics.b1);
                if (first) path.moveTo(point);
                else path.lineTo(point);
                first = false;
            }
            painter.setPen(QPen(QColor("#008b8b"), 1.8, Qt::DashLine));
            painter.drawPath(path);
            painter.setPen(QPen(QColor("#008b8b"), 1, Qt::DotLine));
            painter.drawLine(area.left(), pointForStability(sweep_.startMHz, 0).y(),
                             area.right(), pointForStability(sweep_.stopMHz, 0).y());
        }
    }
    if (markerVisible_ && markerMHz_ >= sweep_.startMHz && markerMHz_ <= sweep_.stopMHz) {
        const double x = pointForSample(markerMHz_, 0).x();
        painter.setPen(QPen(Qt::black, 1, Qt::DashLine));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    painter.restore();

    if (focusBorderEnabled_ && hasFocus()) {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(1, 1, -2, -2));
    }
}

void ResponsePlotWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !samples_.empty()
        && plotRect().contains(event->position())) {
        setFocus(Qt::MouseFocusReason);
        const double position = (event->position().x() - plotRect().left()) / plotRect().width();
        const double frequency = sweep_.logarithmic
            ? std::exp(std::lerp(std::log(sweep_.startMHz), std::log(sweep_.stopMHz), position))
            : std::lerp(sweep_.startMHz, sweep_.stopMHz, position);
        emit frequencySelected(samples_.size() == 1 ? samples_.front().frequencyMHz : frequency);
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void ResponsePlotWidget::wheelEvent(QWheelEvent* event)
{
    wheelRemainder_ += event->angleDelta().y();
    while (std::abs(wheelRemainder_) >= 120) {
        const int direction = wheelRemainder_ > 0 ? 1 : -1;
        wheelRemainder_ -= direction * 120;
        emit frequencyTuneRequested(direction);
    }
    event->accept();
}

void ResponsePlotWidget::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        emit frequencyTuneRequested(1);
        event->accept();
        break;
    case Qt::Key_Down:
        emit frequencyTuneRequested(-1);
        event->accept();
        break;
    case Qt::Key_Right:
        emit frequencyTuneRequested(1);
        event->accept();
        break;
    case Qt::Key_Left:
        emit frequencyTuneRequested(-1);
        event->accept();
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}
