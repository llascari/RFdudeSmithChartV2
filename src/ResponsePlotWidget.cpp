#include "ResponsePlotWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {
constexpr double floorDb = -100;
double db(rf::Complex value)
{
    const double magnitude = std::abs(value);
    return magnitude == 0 ? floorDb : std::max(floorDb, 20 * std::log10(magnitude));
}
}

ResponsePlotWidget::ResponsePlotWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("responsePlot");
    setAccessibleName(tr("S parameter magnitude versus frequency"));
    setMinimumSize(520, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(tr("Click the plot to set the analysis frequency. Values below -100 dB are clipped."));
}

void ResponsePlotWidget::setResponse(const rf::Sweep& sweep, std::vector<rf::ResponseSample> samples, bool onePort)
{
    sweep_ = sweep;
    samples_ = std::move(samples);
    onePort_ = onePort;
    ceilingDb_ = 0;
    for (const auto& sample : samples_) {
        ceilingDb_ = std::max(ceilingDb_, db(sample.s.s11));
        if (!onePort_) ceilingDb_ = std::max({ceilingDb_, db(sample.s.s21), db(sample.s.s22)});
    }
    ceilingDb_ = std::ceil(ceilingDb_ / 20) * 20;
    update();
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

QRectF ResponsePlotWidget::plotRect() const
{
    return QRectF(64, 42, width() - 88, height() - 102);
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
    return {rect.left() + fraction(frequencyMHz) * rect.width(),
            rect.top() + std::clamp((ceilingDb_ - decibels) / (ceilingDb_ - floorDb), 0.0, 1.0) * rect.height()};
}

void ResponsePlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);
    const auto area = plotRect();
    const std::array<QColor, 3> colors{QColor(190, 45, 40), QColor(30, 95, 200), QColor(15, 120, 65)};
    const std::array<QString, 3> names{"S11", "S21", "S22"};
    for (int i = 0; i < (onePort_ ? 1 : 3); ++i) {
        painter.setPen(QPen(colors[i], 2, i == 2 ? Qt::DashLine : Qt::SolidLine));
        const int x = 70 + i * 95;
        painter.drawLine(x, 20, x + 24, 20);
        painter.drawText(x + 30, 25, names[i]);
    }
    painter.setPen(Qt::black);
    painter.drawText(QRectF(area.right() - 190, 8, 190, 24), Qt::AlignRight,
                     tr("Magnitude (dB), floor -100"));
    for (int i = 0; i <= 5; ++i) {
        const double y = area.top() + i * area.height() / 5;
        painter.setPen(QColor(220, 220, 220));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(Qt::black);
        painter.drawText(QRectF(2, y - 10, 52, 20), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(std::lerp(ceilingDb_, floorDb, i / 5.0), 'g', 4));
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
    painter.drawText(QRectF(area.left(), height() - 28, area.width(), 22), Qt::AlignCenter,
                     sweep_.logarithmic ? tr("Frequency (MHz, logarithmic)") : tr("Frequency (MHz, linear)"));
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
    if (markerVisible_ && markerMHz_ >= sweep_.startMHz && markerMHz_ <= sweep_.stopMHz) {
        const double x = pointForSample(markerMHz_, 0).x();
        painter.setPen(QPen(Qt::black, 1, Qt::DashLine));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    painter.restore();
}

void ResponsePlotWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !samples_.empty()
        && plotRect().contains(event->position())) {
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
