#include "SmithChartWidget.h"
#include "FrequencyResponse.h"
#include "Touchstone.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace {
QString numeric(double value)
{
    return QString::number(value, 'g', 7);
}
}

QString impedanceText(const rf::Impedance& z, bool polar)
{
    if (z.isOpen()) return QStringLiteral("Open circuit");
    const auto value = z.ohms();
    if (polar)
        return QString("%1 ohm @ %2 deg").arg(numeric(std::abs(value)),
            numeric(std::arg(value) * 180 / std::numbers::pi));
    return QString("%1 %2 j%3 ohm").arg(numeric(value.real()),
        value.imag() < 0 ? "-" : "+", numeric(std::abs(value.imag())));
}

SmithChartWidget::SmithChartWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("smithChart");
    setMinimumSize(380, 380);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    evaluation_ = rf::evaluate(document_);
    setToolTip(tr("Click a trajectory to select. Double-click to edit.\n"
                  "Wheel tunes; Ctrl+wheel changes frequency; Shift+wheel changes Z0.\n"
                  "Right-click inside the chart to inspect or set an impedance."));
}

void SmithChartWidget::setFocusBorderEnabled(bool enabled)
{
    if (focusBorderEnabled_ == enabled) return;
    focusBorderEnabled_ = enabled;
    update();
}

void SmithChartWidget::setNetwork(const rf::Document& d, const rf::Evaluation& e)
{
    document_ = d;
    evaluation_ = e;
    s11Trajectory_.clear();
    s22Trajectory_.clear();
    s11FrequencyMHz_.clear();
    s22FrequencyMHz_.clear();
    referenceS11_.clear();
    referenceS22_.clear();
    simpleComponentFrequencyMHz_.clear();
    simpleComponentTrajectories_.clear();
    trajectoryStartMHz_ = document_.trajectoryStartMHz;
    trajectoryStopMHz_ = document_.trajectoryStopMHz;
    trajectoryPoints_ = document_.trajectoryPoints;
    trajectoryLogarithmic_ = document_.trajectoryLogarithmic;
    if (document_.referenceOverlay && document_.referenceOverlayVisible) {
        for (const auto& sample : document_.referenceOverlay->samples) {
            const double oldReference = document_.referenceOverlay->referenceOhms;
            const double newReference = document_.referenceOhms;
            const double r = (newReference - oldReference) / (newReference + oldReference);
            if (r == 0) {
                referenceS11_.push_back(sample.s.s11);
                if (document_.referenceOverlay->ports == 2)
                    referenceS22_.push_back(sample.s.s22);
            } else if (document_.referenceOverlay->ports == 1) {
                const rf::Complex denominator = 1.0 - r * sample.s.s11;
                if (denominator != rf::Complex{}) {
                    referenceS11_.push_back((sample.s.s11 - r) / denominator);
                }
            } else {
                const rf::Complex a = 1.0 - r * sample.s.s11;
                const rf::Complex d = 1.0 - r * sample.s.s22;
                const rf::Complex determinant =
                    a * d - r * r * sample.s.s12 * sample.s.s21;
                if (determinant != rf::Complex{}) {
                    referenceS11_.push_back(
                        ((sample.s.s11 - r) * d + r * sample.s.s12 * sample.s.s21)
                        / determinant);
                    referenceS22_.push_back(
                        ((sample.s.s22 - r) * a + r * sample.s.s21 * sample.s.s12)
                        / determinant);
                }
            }
        }
    }
    if (document_.simpleTrajectories) {
        try {
            simpleComponentFrequencyMHz_ = rf::simpleTrajectoryFrequencies(document_);
            simpleComponentTrajectories_.resize(document_.components.size());
            for (const double frequency : simpleComponentFrequencyMHz_) {
                const auto snapshot = rf::evaluateAtFrequency(document_, frequency);
                for (std::size_t i = 0; i < snapshot.trajectories.size(); ++i)
                    simpleComponentTrajectories_[i].push_back(snapshot.trajectories[i]);
            }
            if (document_.s11Trajectory || document_.s22Trajectory) {
                for (const double frequency : simpleComponentFrequencyMHz_) {
                    const auto s = rf::sParameters(document_.components,
                                                   frequency,
                                                   document_.referenceOhms);
                    if (document_.s11Trajectory) {
                        s11Trajectory_.push_back(s.s11);
                        s11FrequencyMHz_.push_back(frequency);
                    }
                    if (document_.s22Trajectory) {
                        s22Trajectory_.push_back(s.s22);
                        s22FrequencyMHz_.push_back(frequency);
                    }
                }
            }
        } catch (const std::exception&) {
            s11Trajectory_.clear();
            s22Trajectory_.clear();
            s11FrequencyMHz_.clear();
            s22FrequencyMHz_.clear();
            simpleComponentFrequencyMHz_.clear();
            simpleComponentTrajectories_.clear();
        }
    } else if (document_.s11Trajectory || document_.s22Trajectory) {
        rf::Sweep sweep;
        sweep.startMHz = document_.trajectoryStartMHz;
        sweep.stopMHz = document_.trajectoryStopMHz;
        sweep.points = document_.trajectoryPoints;
        sweep.logarithmic = document_.trajectoryLogarithmic;
        sweep.portOhms = document_.referenceOhms;
        try {
            rf::validate(sweep);
            auto range = rf::measuredFrequencyRange(document_);
            if (sweep.stopMHz > range.second) sweep.stopMHz = range.second;
            if (sweep.startMHz < range.first) sweep.startMHz = range.first;
            if (sweep.stopMHz > sweep.startMHz) {
                for (const auto& sample : rf::frequencyResponse(document_.components, sweep)) {
                    if (document_.s11Trajectory) {
                        s11Trajectory_.push_back(sample.s.s11);
                        s11FrequencyMHz_.push_back(sample.frequencyMHz);
                    }
                    if (document_.s22Trajectory) {
                        s22Trajectory_.push_back(sample.s.s22);
                        s22FrequencyMHz_.push_back(sample.frequencyMHz);
                    }
                }
            }
        } catch (const std::exception&) {
            s11Trajectory_.clear();
            s22Trajectory_.clear();
        }
    }
    update();
}

double SmithChartWidget::radius() const
{
    return std::max(1.0, std::min(width(), height()) / 2.0 - 35);
}

QPointF SmithChartWidget::pointForReflection(rf::Complex gamma) const
{
    return {width() / 2.0 + radius() * gamma.real(),
            height() / 2.0 - radius() * gamma.imag()};
}

rf::Complex SmithChartWidget::reflectionAt(QPointF point) const
{
    return {(point.x() - width() / 2.0) / radius(),
            (height() / 2.0 - point.y()) / radius()};
}

void SmithChartWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);
    const auto center = pointForReflection(0);
    const double r = radius();
    QPainterPath clip;
    clip.addEllipse(center, r, r);
    p.save();
    p.setClipPath(clip);
    const auto grid = [&](bool admittance) {
        p.setPen(QPen(admittance ? QColor("#80a58c") : QColor("#d99494"), 0.8));
        const auto mapped = [&](rf::Complex z) {
            const auto gamma = (z - 1.0) / (z + 1.0);
            return pointForReflection(admittance ? -gamma : gamma);
        };
        for (double resistance : {0.1, 0.2, 0.333, 0.5, 0.75, 1.0, 1.333, 2.0, 3.0, 5.0, 10.0}) {
            const double sign = admittance ? -1 : 1;
            const auto c = pointForReflection(sign * resistance / (resistance + 1));
            const double cr = r / (resistance + 1);
            p.drawEllipse(c, cr, cr);
        }
        for (double reactance : {0.1, 0.2, 0.333, 0.5, 0.75, 1.0, 1.333, 2.0, 3.0, 5.0, 10.0}) {
            for (double sign : {-1.0, 1.0}) {
                QPainterPath path;
                path.moveTo(mapped({0, sign * reactance}));
                for (int i = 1; i <= 300; ++i) {
                    const double resistance = std::expm1(i * std::log(10001.0) / 300);
                    path.lineTo(mapped({resistance, sign * reactance}));
                }
                p.drawPath(path);
            }
        }
    };
    grid(false);
    if (document_.admittance) grid(true);
    p.setPen(QPen(QColor("#bf7777"), 1));
    p.drawLine(pointForReflection(-1), pointForReflection(1));

    if (document_.qCurves && evaluation_.qDefined && evaluation_.maxQ > 0) {
        const double q = std::min(evaluation_.maxQ, 10.0);
        p.setPen(QPen(QColor("#42a9b8"), 1.5, Qt::DashLine));
        for (double sign : {-1.0, 1.0}) {
            QPainterPath path;
            path.moveTo(pointForReflection(-1));
            for (int i = 0; i <= 500; ++i) {
                const double resistance = std::exp(-9 + i * 18.0 / 500);
                const auto z = rf::Impedance::fromOhms({resistance, sign * q * resistance});
                path.lineTo(pointForReflection(z.reflection(1)));
            }
            p.drawPath(path);
        }
    }
    if (document_.vswrCircle) {
        p.setPen(QPen(QColor("#bd8800"), 1.5, Qt::DashLine));
        const double cr = r * std::abs(evaluation_.nodes.back().reflection(document_.referenceOhms));
        p.drawEllipse(center, cr, cr);
    }
    if (document_.simpleTrajectories) {
        for (std::size_t i = 0; i < simpleComponentTrajectories_.size(); ++i) {
            const bool selected = static_cast<int>(i) == document_.selected;
            QColor color = selected ? QColor("#1f59b6") : QColor("#303030");
            p.setBrush(Qt::NoBrush);
            const auto& contours = simpleComponentTrajectories_[i];
            for (std::size_t j = 0; j < contours.size(); ++j) {
                const auto& samples = contours[j];
                if (samples.empty()) continue;
                const double frequency = j < simpleComponentFrequencyMHz_.size()
                    ? simpleComponentFrequencyMHz_[j] : document_.frequencyMHz;
                const bool current = std::abs(frequency - document_.frequencyMHz) < 1e-9;
                const Qt::PenStyle style = current ? Qt::SolidLine
                                             : frequency < document_.frequencyMHz
                                                 ? Qt::DashLine : Qt::DotLine;
                QPainterPath path;
                path.moveTo(pointForReflection(samples.front()));
                for (const auto gamma : samples)
                    path.lineTo(pointForReflection(gamma));
                p.setPen(QPen(color, selected && current ? 3 : 1.8, style));
                p.drawPath(path);
                p.setBrush(Qt::NoBrush);
            }
        }
    } else {
        for (std::size_t i = 0; i < evaluation_.trajectories.size(); ++i) {
            const auto& samples = evaluation_.trajectories[i];
            if (samples.empty()) continue;
            QPainterPath path;
            path.moveTo(pointForReflection(samples.front()));
            for (const auto gamma : samples) path.lineTo(pointForReflection(gamma));
            const bool selected = static_cast<int>(i) == document_.selected;
            p.setPen(QPen(selected ? QColor("#1f59b6") : QColor("#303030"), selected ? 3 : 1.8,
                          document_.components[i].kind == rf::Kind::Measured ? Qt::DashLine : Qt::SolidLine));
            p.drawPath(path);
        }
    }
    if (!s11Trajectory_.empty()) {
        QPainterPath path;
        path.moveTo(pointForReflection(s11Trajectory_.front()));
        for (const auto& gamma : s11Trajectory_)
            path.lineTo(pointForReflection(gamma));
        p.setPen(QPen(QColor("#9d409a"), 2.2, Qt::SolidLine));
        p.drawPath(path);
        p.setBrush(QColor("#9d409a"));
        p.drawEllipse(pointForReflection(s11Trajectory_.front()), 3.5, 3.5);
        p.drawEllipse(pointForReflection(s11Trajectory_.back()), 3.5, 3.5);
        std::size_t nearest = 0;
        if (!s11FrequencyMHz_.empty()) {
            double nearestDistance = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < s11FrequencyMHz_.size(); ++i) {
                const double distance = std::abs(s11FrequencyMHz_[i] - document_.frequencyMHz);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearest = i;
                }
            }
            p.setPen(QPen(QColor("#9d409a"), 1));
            p.setBrush(QColor("#9d409a"));
            p.drawEllipse(pointForReflection(s11Trajectory_[nearest]), 5.5, 5.5);
        }
    }
    if (!s22Trajectory_.empty()) {
        QPainterPath path;
        path.moveTo(pointForReflection(s22Trajectory_.front()));
        for (const auto& gamma : s22Trajectory_)
            path.lineTo(pointForReflection(gamma));
        p.setPen(QPen(QColor("#007f86"), 2.2, Qt::DashLine));
        p.drawPath(path);
        p.setBrush(QColor("#007f86"));
        p.drawEllipse(pointForReflection(s22Trajectory_.front()), 3.5, 3.5);
        p.drawEllipse(pointForReflection(s22Trajectory_.back()), 3.5, 3.5);
        std::size_t nearest = 0;
        if (!s22FrequencyMHz_.empty()) {
            double nearestDistance = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < s22FrequencyMHz_.size(); ++i) {
                const double distance = std::abs(s22FrequencyMHz_[i] - document_.frequencyMHz);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearest = i;
                }
            }
            p.setPen(QPen(QColor("#007f86"), 1));
            p.setBrush(QColor("#007f86"));
            p.drawEllipse(pointForReflection(s22Trajectory_[nearest]), 5.5, 5.5);
        }
    }
    if (!referenceS11_.empty()) {
        QPainterPath path;
        path.moveTo(pointForReflection(referenceS11_.front()));
        for (const auto& gamma : referenceS11_)
            path.lineTo(pointForReflection(gamma));
        p.setPen(QPen(QColor("#cc5e00"), 2.3, Qt::SolidLine));
        p.drawPath(path);
        p.setBrush(QColor("#cc5e00"));
        p.drawEllipse(pointForReflection(referenceS11_.front()), 3.5, 3.5);
        p.drawEllipse(pointForReflection(referenceS11_.back()), 3.5, 3.5);
    }
    if (!referenceS22_.empty()) {
        QPainterPath path;
        path.moveTo(pointForReflection(referenceS22_.front()));
        for (const auto& gamma : referenceS22_)
            path.lineTo(pointForReflection(gamma));
        p.setPen(QPen(QColor("#005fa3"), 2.3, Qt::DashLine));
        p.drawPath(path);
        p.setBrush(QColor("#005fa3"));
        p.drawEllipse(pointForReflection(referenceS22_.front()), 3.5, 3.5);
        p.drawEllipse(pointForReflection(referenceS22_.back()), 3.5, 3.5);
    }
    if (document_.stabilitySourceCircle || document_.stabilityLoadCircle
        || document_.stabilityUnstableShade) {
        try {
            const auto s = rf::sParameters(document_.components,
                                            document_.frequencyMHz,
                                            document_.referenceOhms);
            const auto circles = rf::stabilityCircles(s);
            const auto drawCircle = [&](const rf::StabilityCircle& circle,
                                        const QColor& color,
                                        bool shadeUnstable,
                                        const QString& label) {
                if (!circle.valid) return;
                const auto center = pointForReflection(circle.center);
                const double mappedRadius = r * circle.radius;
                if (document_.stabilityUnstableShade && shadeUnstable) {
                    p.save();
                    p.setPen(Qt::NoPen);
                    p.setBrush(QBrush(QColor(color.red(), color.green(), color.blue(), 55),
                                      Qt::Dense5Pattern));
                    p.drawEllipse(center, mappedRadius, mappedRadius);
                    p.restore();
                }
                p.setPen(QPen(color, 2, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(center, mappedRadius, mappedRadius);
                p.drawText(center + QPointF(mappedRadius * 0.25,
                                             mappedRadius * 0.25),
                           label);
            };
            if (document_.stabilitySourceCircle) {
                drawCircle(circles.source, QColor("#9d409a"),
                           circles.source.originStable ? circles.source.originOutside
                                                       : circles.source.originInside,
                           tr("Source"));
            }
            if (document_.stabilityLoadCircle) {
                drawCircle(circles.load, QColor("#007f86"),
                           circles.load.originStable ? circles.load.originOutside
                                                     : circles.load.originInside,
                           tr("Load"));
            }
        } catch (const std::exception&) {
        }
    }
    p.restore();
    p.setPen(QPen(QColor("#873d3d"), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(center, r, r);
    p.setPen(QColor("#606060"));
    p.drawText(QRectF(center.x() - 40, center.y() + r + 8, 80, 20), Qt::AlignCenter, tr("Z0 = %1").arg(numeric(document_.referenceOhms)));
    p.drawText(QPointF(center.x() - r - 26, center.y() + 4), "0");
    p.drawText(QPointF(center.x() + r + 5, center.y() + 4), tr("Open"));
    if (!s11Trajectory_.empty() || !s22Trajectory_.empty()) {
        int legendOffset = 0;
        if (!s11Trajectory_.empty()) {
            p.setPen(QPen(QColor("#9d409a"), 2.2));
            p.drawLine(QPointF(center.x() - r, center.y() - r - 18),
                       QPointF(center.x() - r + 24, center.y() - r - 18));
            p.setPen(QColor("#606060"));
            p.drawText(QPointF(center.x() - r + 30, center.y() - r - 14), tr("S11 trajectory"));
            legendOffset = 150;
        }
        if (!s22Trajectory_.empty()) {
            p.setPen(QPen(QColor("#007f86"), 2.2, Qt::DashLine));
            p.drawLine(QPointF(center.x() - r + legendOffset, center.y() - r - 18),
                       QPointF(center.x() - r + legendOffset + 24, center.y() - r - 18));
            p.setPen(QColor("#606060"));
            p.drawText(QPointF(center.x() - r + legendOffset + 30, center.y() - r - 14),
                       tr("S22 trajectory"));
        }
    }
    if (!referenceS11_.empty() || !referenceS22_.empty()) {
        int referenceLegendOffset = 0;
        if (!referenceS11_.empty()) {
            p.setPen(QPen(QColor("#cc5e00"), 2.3));
            p.drawLine(QPointF(center.x() - r, center.y() - r - 42),
                       QPointF(center.x() - r + 24, center.y() - r - 42));
            p.setPen(QColor("#606060"));
            p.drawText(QPointF(center.x() - r + 30, center.y() - r - 38),
                       tr("Reference S11"));
            referenceLegendOffset = 150;
        }
        if (!referenceS22_.empty()) {
            p.setPen(QPen(QColor("#005fa3"), 2.3, Qt::DashLine));
            p.drawLine(QPointF(center.x() - r + referenceLegendOffset, center.y() - r - 42),
                       QPointF(center.x() - r + referenceLegendOffset + 24, center.y() - r - 42));
            p.setPen(QColor("#606060"));
            p.drawText(QPointF(center.x() - r + referenceLegendOffset + 30, center.y() - r - 38),
                       tr("Reference S22"));
        }
    }
    if (document_.simpleTrajectories && !simpleComponentFrequencyMHz_.empty()) {
        const double legendY = center.y() + r + 28;
        double legendX = center.x() - r;
        for (std::size_t i = 0; i < simpleComponentFrequencyMHz_.size(); ++i) {
            const double frequency = simpleComponentFrequencyMHz_[i];
            const bool current = std::abs(frequency - document_.frequencyMHz) < 1e-9;
            const Qt::PenStyle style = current ? Qt::SolidLine
                                               : frequency < document_.frequencyMHz
                                                    ? Qt::DashLine : Qt::DotLine;
            p.setPen(QPen(QColor("#303030"), 1.6, style));
            p.drawLine(QPointF(legendX, legendY),
                       QPointF(legendX + 20, legendY));
            p.setPen(QColor("#606060"));
            p.drawText(QPointF(legendX + 26, legendY + 4),
                       tr("%1 MHz").arg(numeric(frequency)));
            legendX += 115;
        }
    }
    for (std::size_t i = 0; i < evaluation_.nodes.size(); ++i) {
        const auto point = pointForReflection(evaluation_.nodes[i].reflection(document_.referenceOhms));
        p.setPen(QPen(QColor("#19804a"), 1.5));
        p.setBrush(Qt::white);
        p.drawEllipse(point, 3.5, 3.5);
        if (document_.markers) {
            p.setPen(QColor("#215b37"));
            p.drawText(point + QPointF(6, -7), QString::number(i));
        }
    }
    const auto goal = pointForReflection(rf::Impedance::fromOhms(document_.goalOhms).reflection(document_.referenceOhms));
    p.setPen(QPen(QColor("#9d409a"), 2));
    p.drawLine(goal + QPointF(-5, -5), goal + QPointF(5, 5));
    p.drawLine(goal + QPointF(-5, 5), goal + QPointF(5, -5));

    if (focusBorderEnabled_ && hasFocus()) {
        p.setPen(QPen(palette().color(QPalette::Highlight), 3));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -2, -2));
    }
}

void SmithChartWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    update();
}

void SmithChartWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    update();
}

void SmithChartWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    const auto gamma = reflectionAt(event->position());
    if (event->button() == Qt::RightButton && std::abs(gamma) < 1 - 1e-9) {
        const auto z = document_.referenceOhms * (1.0 + gamma) / (1.0 - gamma);
        emit impedanceRequested(z.real(), z.imag(), event->globalPosition().toPoint());
    } else if (event->button() == Qt::LeftButton) {
        double nearest = std::numeric_limits<double>::infinity();
        int selected = -1;
        for (std::size_t i = 0; i < evaluation_.trajectories.size(); ++i) {
            for (const auto sample : evaluation_.trajectories[i]) {
                const auto delta = pointForReflection(sample) - event->position();
                const double distance = delta.x() * delta.x() + delta.y() * delta.y();
                if (distance < nearest) {
                    nearest = distance;
                    selected = static_cast<int>(i);
                }
            }
        }
        if (selected >= 0) emit componentSelected(selected);
    }
}

void SmithChartWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        mousePressEvent(event);
        emit editRequested();
    }
}

void SmithChartWidget::mouseMoveEvent(QMouseEvent* event)
{
    const auto gamma = reflectionAt(event->position());
    if (std::abs(gamma) < 1 - 1e-9) {
        const auto z = rf::Impedance::fromOhms(document_.referenceOhms * (1.0 + gamma) / (1.0 - gamma));
        emit hoverText(tr("Cursor: %1    |Gamma| = %2").arg(impedanceText(z, document_.polar), numeric(std::abs(gamma))));
    } else {
        emit hoverText(tr("Move inside the chart to inspect impedance."));
    }
}

void SmithChartWidget::wheelEvent(QWheelEvent* event)
{
    wheelRemainder_ += event->angleDelta().y();
    while (std::abs(wheelRemainder_) >= 120) {
        const int direction = wheelRemainder_ > 0 ? 1 : -1;
        wheelRemainder_ -= direction * 120;
        emit tuneRequested(direction, event->modifiers());
    }
    event->accept();
}
