#pragma once

#include "FrequencyResponse.h"

#include <QDialog>
#include <QImage>

class QDoubleSpinBox;
class QSpinBox;
class FrequencySpinBox;
class QComboBox;
class QLabel;
class ResponsePlotWidget;
class QCheckBox;

class FrequencyResponseDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit FrequencyResponseDialog(const rf::Document& document, QWidget* parent = nullptr);

    /// @brief Select the type of termination
    /// @param terminated: True for S11 only
    ///                    False for S11/S21/S22
    void setTerminated(bool terminated);
    void setNetwork(const rf::Document& document);
    QImage responseImage();
    bool plotHasFocus() const;
    void focusPlot();

signals:
    void frequencySelected(double frequencyMHz);
    void displayOptionsChanged(bool gmax, bool k, bool b1,
                               bool automaticScale, double ceilingDb, double floorDb);
    void sweepChanged(double startMHz, double stopMHz, int points,
                      bool logarithmic, double portOhms, bool terminated);
    void frequencyTuneRequested(int direction);

private:
    void applySweep();
    void autoApply();
    void calculate();
    void updateMarker();
    void exportPlot();
    void exportTouchstone();
    void constrainSweep();
    void applyDisplayOptions();
    rf::Document document_;
    rf::Sweep sweep_;
    FrequencySpinBox* start_;
    FrequencySpinBox* stop_;
    QDoubleSpinBox* ports_;
    QSpinBox* points_;
    QComboBox* scale_;
    QComboBox* mode_;
    QCheckBox* stabilityGmax_;
    QCheckBox* stabilityK_;
    QCheckBox* stabilityB1_;
    QCheckBox* magnitudeAuto_;
    QComboBox* magnitudeCeiling_;
    QComboBox* magnitudeFloor_;
    QLabel* status_;
    QLabel* marker_;
    FrequencySpinBox* markerFrequency_;
    ResponsePlotWidget* plot_;
    bool terminated_ = false;
    bool singlePoint_ = false;
    bool updatingControls_ = false;
};
