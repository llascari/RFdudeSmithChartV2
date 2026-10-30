#include "SmithChartWidget.h"

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

void SmithChartWidget::setNetwork(const rf::Document& d, const rf::Evaluation& e)
{
    document_ = d;
    evaluation_ = e;
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
    p.restore();
    p.setPen(QPen(QColor("#873d3d"), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(center, r, r);
    p.setPen(QColor("#606060"));
    p.drawText(QRectF(center.x() - 40, center.y() + r + 8, 80, 20), Qt::AlignCenter, tr("Z0 = %1").arg(numeric(document_.referenceOhms)));
    p.drawText(QPointF(center.x() - r - 26, center.y() + 4), "0");
    p.drawText(QPointF(center.x() + r + 5, center.y() + 4), tr("Open"));
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
