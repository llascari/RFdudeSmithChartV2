#pragma once

#include "FrequencyResponse.h"

#include <QWidget>
class ResponsePlotWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ResponsePlotWidget(QWidget* parent = nullptr);
    void setResponse(const rf::Sweep& sweep, std::vector<rf::ResponseSample> samples, bool onePort = false);
    void setMarker(double frequencyMHz);
    void clearMarker();
    const std::vector<rf::ResponseSample>& samples() const { return samples_; }
    QPointF pointForSample(double frequencyMHz, double decibels) const;

signals:
    void frequencySelected(double frequencyMHz);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRectF plotRect() const;
    double fraction(double frequencyMHz) const;
    rf::Sweep sweep_;
    std::vector<rf::ResponseSample> samples_;
    double markerMHz_ = 1000;
    bool markerVisible_ = true;
    bool onePort_ = false;
    double ceilingDb_ = 0;
};
