#pragma once

#include "FrequencyResponse.h"

#include <QWidget>
class ResponsePlotWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ResponsePlotWidget(QWidget* parent = nullptr);
    void setFocusBorderEnabled(bool enabled);
    void setResponse(const rf::Sweep& sweep, std::vector<rf::ResponseSample> samples, bool onePort = false);
    void setStabilityTraces(bool gmax, bool k, bool b1);
    void setMagnitudeScale(bool automatic, double ceilingDb, double floorDb);
    void setMarker(double frequencyMHz);
    void clearMarker();
    const std::vector<rf::ResponseSample>& samples() const { return samples_; }
    QPointF pointForSample(double frequencyMHz, double decibels) const;

signals:
    void frequencySelected(double frequencyMHz);
    void frequencyTuneRequested(int direction);

protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRectF plotRect() const;
    double fraction(double frequencyMHz) const;
    QPointF pointForStability(double frequencyMHz, double value) const;
    rf::Sweep sweep_;
    std::vector<rf::ResponseSample> samples_;
    double markerMHz_ = 1000;
    bool markerVisible_ = true;
    bool onePort_ = false;
    double ceilingDb_ = 0;
    double magnitudeCeilingDb_ = 0;
    double floorDb_ = -100;
    double stabilityMinimum_ = -1;
    double stabilityMaximum_ = 5;
    bool magnitudeAutoScale_ = true;
    bool showGmax_ = false;
    bool showK_ = false;
    bool showB1_ = false;
    bool focusBorderEnabled_ = true;
    int wheelRemainder_ = 0;
};
