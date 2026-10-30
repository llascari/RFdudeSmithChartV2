#pragma once

#include "FrequencyResponse.h"

#include <QDialog>
#include <QImage>

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QLabel;
class ResponsePlotWidget;

class FrequencyResponseDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit FrequencyResponseDialog(const rf::Document& document, QWidget* parent = nullptr);
    void setNetwork(const rf::Document& document);
    QImage responseImage();

signals:
    void frequencySelected(double frequencyMHz);

private:
    void applySweep();
    void calculate();
    void updateMarker();
    void exportPlot();
    void constrainSweep();
    rf::Document document_;
    rf::Sweep sweep_;
    QDoubleSpinBox* start_;
    QDoubleSpinBox* stop_;
    QDoubleSpinBox* ports_;
    QSpinBox* points_;
    QComboBox* scale_;
    QComboBox* mode_;
    QLabel* status_;
    QLabel* marker_;
    ResponsePlotWidget* plot_;
    bool terminated_ = false;
    bool singlePoint_ = false;
};
