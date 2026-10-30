#pragma once

#include "RfModel.h"

#include <QGraphicsView>

class SchematicWidget final : public QGraphicsView
{
    Q_OBJECT

public:
    explicit SchematicWidget(QWidget* parent = nullptr);
    void setNetwork(const rf::Document& document);
    QRectF componentRect(int index) const;
    int reportHeight(int width) const;
    void paintReport(QPainter& painter, QPointF position, int width) const;

signals:
    void componentSelected(int index);
    void editRequested();

protected:
    bool event(QEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    int componentAt(QPoint position) const;
    std::vector<QRectF> reportBands(int width) const;
    void paintCircuit(QPainter& painter, const QRectF& rect, bool report) const;
    rf::Document document_;
    bool selectingFromMouse_ = false;
};
