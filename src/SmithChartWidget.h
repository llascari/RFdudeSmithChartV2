#pragma once

#include "RfModel.h"

#include <QWidget>

class SmithChartWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SmithChartWidget(QWidget* parent = nullptr);
    void setFocusBorderEnabled(bool enabled);
    void setNetwork(const rf::Document& document, const rf::Evaluation& evaluation);
    const std::vector<rf::Complex>& s11Trajectory() const { return s11Trajectory_; }
    const std::vector<rf::Complex>& s22Trajectory() const { return s22Trajectory_; }
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
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    double radius() const;
    rf::Document document_;
    rf::Evaluation evaluation_;
    std::vector<rf::Complex> s11Trajectory_;
    std::vector<rf::Complex> s22Trajectory_;
    std::vector<double> s11FrequencyMHz_;
    std::vector<double> s22FrequencyMHz_;
    std::vector<rf::Complex> referenceS11_;
    std::vector<rf::Complex> referenceS22_;
    std::vector<double> simpleComponentFrequencyMHz_;
    std::vector<std::vector<std::vector<rf::Complex>>> simpleComponentTrajectories_;
    double trajectoryStartMHz_ = 100;
    double trajectoryStopMHz_ = 2000;
    int trajectoryPoints_ = 401;
    bool trajectoryLogarithmic_ = false;
    bool focusBorderEnabled_ = true;
    int wheelRemainder_ = 0;
};

QString impedanceText(const rf::Impedance& impedance, bool polar = false);
