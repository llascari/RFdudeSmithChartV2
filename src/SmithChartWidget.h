#pragma once

#include "RfModel.h"

#include <QWidget>

class SmithChartWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SmithChartWidget(QWidget* parent = nullptr);
    void setNetwork(const rf::Document& document, const rf::Evaluation& evaluation);
    QPointF pointForReflection(rf::Complex gamma) const;
    rf::Complex reflectionAt(QPointF point) const;

signals:
    void componentSelected(int index);
    void editRequested();
    void tuneRequested(int direction, Qt::KeyboardModifiers modifiers);
    void impedanceRequested(double real, double imaginary, QPoint globalPosition);
    void hoverText(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    double radius() const;
    rf::Document document_;
    rf::Evaluation evaluation_;
    int wheelRemainder_ = 0;
};

QString impedanceText(const rf::Impedance& impedance, bool polar = false);
