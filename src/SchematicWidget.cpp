#include "SchematicWidget.h"

#include "Editors.h"
#include "SmithChartWidget.h"
#include "Touchstone.h"

#include <QFontMetrics>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
constexpr double sourceWidth = 110;
constexpr double loadWidth = 110;
constexpr double cellWidth = 140;
constexpr double circuitY = 60;
constexpr double sceneHeight = 200;

QString compactDescription(const rf::Component& c)
{
    if (c.kind == rf::Kind::Measured)
        return QString::fromStdString(c.measured.name);
    const auto n = [](double value) { return QString::number(value, 'g', 4); };
    if (rf::isTransmissionLine(c.kind)) {
        const auto p = rf::lineParameters(c);
        const auto length = c.kind != rf::Kind::Line && c.lengthMode == rf::LengthMode::Physical
            ? QString("%1 mm").arg(n(c.physicalLengthMm))
            : QString("%1 deg").arg(n(p.lengthDegrees));
        return QString("%1 ohm, %2\n@ %3 MHz")
            .arg(n(p.impedanceOhms), length, n(c.referenceMHz));
    }
    if (c.kind == rf::Kind::Transformer)
        return QString("%1 : 1\nsource : load").arg(n(c.turnsRatio));
    if (c.kind == rf::Kind::SeriesRlc || c.kind == rf::Kind::ParallelRlc)
        return QString("%1 ohm\n%2 nH, %3 pF")
            .arg(n(c.resistance), n(c.inductanceNh), n(c.capacitancePf));
    return componentDescription(c);
}

void ground(QPainter& painter, QPointF point)
{
    painter.drawLine(point, point + QPointF(0, 6));
    for (int i = 0; i < 3; ++i)
        painter.drawLine(point + QPointF(-10 + 3 * i, 6 + 5 * i),
                         point + QPointF(10 - 3 * i, 6 + 5 * i));
}

void passive(QPainter& painter, rf::Kind kind, QPointF from, QPointF to)
{
    painter.save();
    const QLineF axis(from, to);
    painter.translate(from);
    painter.rotate(-axis.angle());
    const double length = axis.length();
    const double symbolLength = std::min(40.0, length * 0.65);
    const double left = (length - symbolLength) / 2;
    const double right = length - left;
    painter.drawLine(QPointF(0, 0), QPointF(left, 0));
    painter.drawLine(QPointF(right, 0), QPointF(length, 0));
    if (kind == rf::Kind::Resistor) {
        painter.drawRect(QRectF(left, -7, symbolLength, 14));
    } else if (kind == rf::Kind::Capacitor) {
        painter.drawLine(QPointF(left, 0), QPointF(length / 2 - 3, 0));
        painter.drawLine(QPointF(length / 2 + 3, 0), QPointF(right, 0));
        painter.drawLine(QPointF(length / 2 - 3, -11), QPointF(length / 2 - 3, 11));
        painter.drawLine(QPointF(length / 2 + 3, -11), QPointF(length / 2 + 3, 11));
    } else {
        QPainterPath coils;
        coils.moveTo(left, 0);
        const double diameter = symbolLength / 4;
        for (int i = 0; i < 4; ++i)
            coils.arcTo(QRectF(left + i * diameter, -diameter / 2, diameter, diameter),
                        180, -180);
        painter.drawPath(coils);
    }
    painter.restore();
}

void lumped(QPainter& painter, rf::Kind kind, QPointF from, QPointF to)
{
    if (kind != rf::Kind::SeriesRlc && kind != rf::Kind::ParallelRlc) {
        passive(painter, kind, from, to);
        return;
    }
    const std::array<rf::Kind, 3> kinds{rf::Kind::Resistor, rf::Kind::Inductor,
                                       rf::Kind::Capacitor};
    painter.save();
    const QLineF axis(from, to);
    painter.translate(from);
    painter.rotate(-axis.angle());
    const double length = axis.length();
    if (kind == rf::Kind::SeriesRlc) {
        for (int i = 0; i < 3; ++i)
            passive(painter, kinds[i], {i * length / 3, 0}, {(i + 1) * length / 3, 0});
    } else {
        painter.drawLine(QPointF(0, -22), QPointF(0, 22));
        painter.drawLine(QPointF(length, -22), QPointF(length, 22));
        for (int i = 0; i < 3; ++i) {
            const double offset = (i - 1) * 22;
            passive(painter, kinds[i], {0, offset}, {length, offset});
        }
    }
    painter.restore();
}

void component(QPainter& painter, const rf::Component& c)
{
    const QPointF left(0, circuitY);
    const QPointF right(cellWidth, circuitY);
    const QPointF junction(cellWidth / 2, circuitY);
    if (c.kind == rf::Kind::Measured) {
        painter.drawLine(left, {24, circuitY});
        painter.drawRect(QRectF(24, circuitY - 20, 92, 40));
        painter.drawText(QRectF(28, circuitY - 18, 84, 36), Qt::AlignCenter, "1   S2P   2");
        painter.drawLine(QPointF(116, circuitY), right);
        painter.drawEllipse(QPointF(24, circuitY), 2, 2);
        painter.drawEllipse(QPointF(116, circuitY), 2, 2);
    } else if (c.kind == rf::Kind::Transformer) {
        painter.drawLine(left, {48, circuitY});
        passive(painter, rf::Kind::Inductor, {48, circuitY}, {48, 120});
        passive(painter, rf::Kind::Inductor, {92, circuitY}, {92, 120});
        painter.drawLine(QPointF(92, circuitY), right);
        for (const double x : {67, 73})
            painter.drawLine(QPointF(x, 72), {x, 110});
        ground(painter, {48, 120});
        ground(painter, {92, 120});
        painter.drawEllipse(QPointF(56, 72), 2, 2);
        painter.drawEllipse(QPointF(84, 72), 2, 2);
    } else if (rf::isTransmissionLine(c.kind)) {
        const QString label = c.kind == rf::Kind::Coax ? "Coax"
            : c.kind == rf::Kind::Microstrip ? "MS" : "TL";
        if (c.connection == rf::Connection::ThroughLine) {
            painter.drawLine(left, {38, circuitY});
            painter.drawRect(QRectF(38, circuitY - 12, 64, 24));
            painter.drawText(QRectF(38, circuitY - 12, 64, 24), Qt::AlignCenter, label);
            painter.drawLine(QPointF(102, circuitY), right);
        } else {
            painter.drawLine(left, right);
            painter.drawEllipse(junction, 3, 3);
            painter.drawLine(junction, {cellWidth / 2, 78});
            painter.drawRect(QRectF(cellWidth / 2 - 18, 78, 36, 34));
            painter.drawText(QRectF(cellWidth / 2 - 18, 78, 36, 34), Qt::AlignCenter, label);
            painter.drawLine(QPointF(cellWidth / 2, 112), {cellWidth / 2, 124});
            if (c.connection == rf::Connection::ShortStub)
                ground(painter, {cellWidth / 2, 124});
            else
                painter.drawEllipse(QPointF(cellWidth / 2, 127), 3, 3);
        }
    } else if (c.connection == rf::Connection::Shunt) {
        painter.drawLine(left, right);
        painter.drawEllipse(junction, 3, 3);
        lumped(painter, c.kind, junction, {cellWidth / 2, 124});
        ground(painter, {cellWidth / 2, 124});
    } else {
        painter.drawLine(left, {16, circuitY});
        lumped(painter, c.kind, {16, circuitY}, {cellWidth - 16, circuitY});
        painter.drawLine(QPointF(cellWidth - 16, circuitY), right);
    }
}
}

SchematicWidget::SchematicWidget(QWidget* parent) : QGraphicsView(parent)
{
    setObjectName("schematic");
    setAccessibleName(tr("Network schematic"));
    setScene(new QGraphicsScene(this));
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setRenderHint(QPainter::Antialiasing);
    setMouseTracking(true);
    setMinimumHeight(150);
    setToolTip(tr("Click a component to select; double-click to edit. Hover for full values."));
    setNetwork(document_);
}

QRectF SchematicWidget::componentRect(int index) const
{
    if (index < 0 || index >= static_cast<int>(document_.components.size())) return {};
    // Impedance transformations run from the load toward the source.
    const int position = static_cast<int>(document_.components.size()) - 1 - index;
    return {sourceWidth + position * cellWidth, 0, cellWidth, sceneHeight};
}

void SchematicWidget::setNetwork(const rf::Document& document)
{
    const bool reveal = document.selected != document_.selected
        || document.components.size() != document_.components.size();
    document_ = document;
    setSceneRect(0, 0, sourceWidth + cellWidth * document_.components.size() + loadWidth, sceneHeight);
    // Keep clicks stationary so revealing a partial cell cannot retarget a double-click.
    if (reveal && !selectingFromMouse_ && document_.selected >= 0) {
        const auto bounds = componentRect(document_.selected);
        ensureVisible(QRectF(bounds.left(), 0, bounds.width(), 1), 12, 0);
    }
    viewport()->update();
}

bool SchematicWidget::event(QEvent* event)
{
    // Application editing shortcuts take precedence over the view's arrow-key scrolling.
    if (event->type() == QEvent::ShortcutOverride) {
        const auto key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up
            || key == Qt::Key_Down || key == Qt::Key_Delete) {
            event->ignore();
            return true;
        }
    }
    return QGraphicsView::event(event);
}

void SchematicWidget::drawBackground(QPainter* painter, const QRectF& rect)
{
    paintCircuit(*painter, rect, false);
}

std::vector<QRectF> SchematicWidget::reportBands(int width) const
{
    if (width < sourceWidth + cellWidth)
        throw std::invalid_argument("Schematic report width must be at least 250 pixels.");
    const double end = sourceWidth + cellWidth * document_.components.size() + loadWidth;
    std::vector<QRectF> bands;
    double left = 0;
    while (left < end) {
        double right = std::min(end, left + width);
        if (right < end) {
            right = sourceWidth + std::floor((right - sourceWidth) / cellWidth) * cellWidth;
            // The final load is kept intact, just like a component cell.
            right = std::min(right, end - loadWidth);
        }
        bands.emplace_back(left, 0, right - left, sceneHeight);
        left = right;
    }
    return bands;
}

int SchematicWidget::reportHeight(int width) const
{
    return static_cast<int>(reportBands(width).size()) * (static_cast<int>(sceneHeight) + 32);
}

void SchematicWidget::paintReport(QPainter& painter, QPointF position, int width) const
{
    const auto bands = reportBands(width);
    for (std::size_t i = 0; i < bands.size(); ++i) {
        painter.save();
        painter.translate(position + QPointF(0, i * (sceneHeight + 32)));
        painter.setPen(Qt::black);
        painter.drawText(QRectF(0, 0, width, 28), Qt::AlignLeft | Qt::AlignVCenter,
            bands.size() == 1 ? tr("Network schematic")
                             : tr("Network schematic - section %1 of %2 (continues left to right)")
                                   .arg(i + 1).arg(bands.size()));
        painter.translate(0, 32);
        painter.setClipRect(QRectF(0, 0, bands[i].width(), sceneHeight), Qt::IntersectClip);
        painter.translate(-bands[i].left(), 0);
        paintCircuit(painter, bands[i], true);
        painter.restore();
    }
}

void SchematicWidget::paintCircuit(QPainter& target, const QRectF& rect, bool report) const
{
    auto* painter = &target;
    painter->fillRect(rect, report ? QBrush(Qt::white) : palette().base());
    painter->setPen(QPen(report ? QColor(Qt::black) : palette().text().color(), 1.6));
    painter->setBrush(Qt::NoBrush);
    painter->drawText(QRectF(4, 4, sourceWidth - 8, 28), Qt::AlignCenter, tr("Source / input"));
    painter->drawEllipse(QPointF(18, 100), 12, 12);
    painter->drawText(QRectF(6, 88, 24, 24), Qt::AlignCenter, "~");
    painter->drawLine(QPointF(18, 88), {18, circuitY});
    passive(*painter, rf::Kind::Resistor, {18, circuitY}, {sourceWidth, circuitY});
    painter->drawLine(QPointF(18, 112), {18, 124});
    ground(*painter, {18, 124});
    painter->drawText(QRectF(4, 148, sourceWidth - 8, 48), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                      tr("Rs = %1 ohm\n(chart Z0)").arg(document_.referenceOhms, 0, 'g', 4));

    for (int i = 0; i < static_cast<int>(document_.components.size()); ++i) {
        const auto bounds = componentRect(i);
        if (!bounds.intersects(rect)) continue;
        painter->save();
        painter->translate(bounds.topLeft());
        if (!report && i == document_.selected) {
            painter->fillRect(QRectF(0, 4, cellWidth, sceneHeight - 8), palette().highlight());
            painter->setPen(QPen(palette().highlightedText().color(), 1.6));
        }
        const auto& c = document_.components[static_cast<std::size_t>(i)];
        painter->drawText(QRectF(4, 4, cellWidth - 8, 28), Qt::AlignCenter | Qt::TextWordWrap,
                          tr("#%1 %2").arg(i + 1).arg(QString::fromStdString(rf::connectionName(c.connection))));
        component(*painter, c);
        const auto caption = c.kind == rf::Kind::Measured
            ? painter->fontMetrics().elidedText(compactDescription(c), Qt::ElideMiddle, cellWidth - 8)
            : compactDescription(c);
        painter->drawText(QRectF(4, 148, cellWidth - 8, 48),
                          Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, caption);
        painter->restore();
    }

    const double end = sourceWidth + cellWidth * document_.components.size();
    painter->save();
    painter->translate(end, 0);
    painter->drawText(QRectF(4, 4, loadWidth - 8, 28), Qt::AlignCenter, tr("Load / start"));
    painter->drawLine(QPointF(0, circuitY), {55, circuitY});
    painter->drawLine(QPointF(55, circuitY), {55, 78});
    painter->drawRect(QRectF(41, 78, 28, 34));
    painter->drawText(QRectF(41, 78, 28, 34), Qt::AlignCenter, document_.measuredLoad ? "S1P" : "ZL");
    painter->drawLine(QPointF(55, 112), {55, 124});
    ground(*painter, {55, 124});
    painter->drawText(QRectF(4, 148, loadWidth - 8, 48), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                      document_.measuredLoad
                          ? painter->fontMetrics().elidedText(QString::fromStdString(document_.measuredLoad->name),
                                                              Qt::ElideMiddle, loadWidth - 8)
                          : impedanceText(rf::startingImpedance(document_)));
    painter->restore();
}

int SchematicWidget::componentAt(QPoint position) const
{
    const auto point = mapToScene(position);
    const int count = static_cast<int>(document_.components.size());
    if (point.x() < sourceWidth || point.x() >= sourceWidth + count * cellWidth
        || point.y() < 0 || point.y() >= sceneHeight)
        return -1;
    return count - 1 - static_cast<int>((point.x() - sourceWidth) / cellWidth);
}

void SchematicWidget::mousePressEvent(QMouseEvent* event)
{
    const int index = componentAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index >= 0) {
        selectingFromMouse_ = true;
        emit componentSelected(index);
        selectingFromMouse_ = false;
        event->accept();
    } else {
        QGraphicsView::mousePressEvent(event);
    }
}

void SchematicWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int index = componentAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index >= 0) {
        selectingFromMouse_ = true;
        emit componentSelected(index);
        selectingFromMouse_ = false;
        emit editRequested();
        event->accept();
    } else {
        QGraphicsView::mouseDoubleClickEvent(event);
    }
}

void SchematicWidget::mouseMoveEvent(QMouseEvent* event)
{
    const int index = componentAt(event->position().toPoint());
    setToolTip(index < 0 ? (document_.measuredLoad
        ? tr("Measured load: %1\n%2").arg(measuredDescription(*document_.measuredLoad),
                                          impedanceText(rf::startingImpedance(document_))) : QString{})
        : tr("#%1 %2\n%3").arg(index + 1)
        .arg(QString::fromStdString(rf::connectionName(document_.components[static_cast<std::size_t>(index)].connection)),
             componentDescription(document_.components[static_cast<std::size_t>(index)])));
    QGraphicsView::mouseMoveEvent(event);
}
