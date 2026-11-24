#include "FrequencyResponseDialog.h"

#include "FrequencySpinBox.h"
#include "ResponsePlotWidget.h"
#include "Touchstone.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
QDoubleSpinBox* field(const QString& name, double value, QWidget* parent)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setObjectName(name);
    spin->setDecimals(1);
    spin->setRange(1e-9, 1e12);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    return spin;
}

FrequencySpinBox* frequencyField(const QString& name, double valueMHz)
{
    auto* spin = new FrequencySpinBox;
    spin->setObjectName(name);
    spin->setValue(valueMHz);
    return spin;
}

QString magnitude(rf::Complex value)
{
    const double m = std::abs(value);
    return m == 0 ? QStringLiteral("-inf") : QString::number(20 * std::log10(m), 'f', 3);
}
}

FrequencyResponseDialog::FrequencyResponseDialog(const rf::Document& document, QWidget* parent)
    : QDialog(parent), document_(document)
{
    setObjectName("frequencyResponse");
    setWindowTitle(tr("Frequency response - S parameters"));
    resize(820, 820);
    setMinimumWidth(620);
    setMinimumHeight(760);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    auto* explanation = new QLabel(tr(
        "Port 1 = source / input; port 2 = load end. Both ports use the reference below.\n"
        "Start/goal impedances and the schematic source resistor are external, not included. "
        "S11 is input match with port 2 matched; it may differ from the terminated Smith chart.\n"
        "Terminated-input mode instead includes the starting/measured load and plots S11 only."));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* controls = new QGridLayout;
    if (document.frequencyResponseConfigured) {
        sweep_.startMHz = document.frequencyResponseStartMHz;
        sweep_.stopMHz = document.frequencyResponseStopMHz;
        sweep_.points = document.frequencyResponsePoints;
        sweep_.logarithmic = document.frequencyResponseLogarithmic;
        sweep_.portOhms = document.frequencyResponsePortOhms;
    } else {
        sweep_.startMHz = std::max(1e-9, document.frequencyMHz / 10);
        sweep_.stopMHz = std::min(1e12, document.frequencyMHz * 2);
        sweep_.portOhms = document.referenceOhms;
        const auto range = rf::measuredFrequencyRange(document);
        sweep_.startMHz = std::max(sweep_.startMHz, range.first);
        sweep_.stopMHz = std::min(sweep_.stopMHz, range.second);
        if (sweep_.startMHz >= sweep_.stopMHz && range.first > 0) {
            sweep_.startMHz = range.first;
            sweep_.stopMHz = range.second;
        }
    }
    terminated_ = document.frequencyResponseConfigured
        ? document.frequencyResponseTerminated
        : true; // S11 view is the default for projects without saved response settings.
    start_ = frequencyField("sweepStart", sweep_.startMHz);
    stop_ = frequencyField("sweepStop", sweep_.stopMHz);
    ports_ = field("sweepPorts", sweep_.portOhms, this);
    ports_->setToolTip(tr("Equal real reference impedance at both ports; independent of chart Z0 after opening."));
    points_ = new QSpinBox;
    points_->setObjectName("sweepPoints");
    points_->setRange(2, 2001);
    points_->setValue(sweep_.points);
    points_->setKeyboardTracking(false);
    scale_ = new QComboBox;
    scale_->setObjectName("sweepScale");
    scale_->addItems({tr("Linear"), tr("Logarithmic")});
    scale_->setCurrentIndex(sweep_.logarithmic ? 1 : 0);
    mode_ = new QComboBox;
    mode_->setObjectName("responseMode");
    mode_->addItems({tr("Two-port circuit: S11/S21/S22"), tr("Terminated input: S11 only")});
    mode_->setCurrentIndex(terminated_ ? 1 : 0);
    stabilityGmax_ = new QCheckBox(tr("Gmax / MSG"));
    stabilityGmax_->setObjectName("stabilityGmax");
    stabilityGmax_->setChecked(document_.stabilityGmax);
    stabilityK_ = new QCheckBox(tr("K"));
    stabilityK_->setObjectName("stabilityK");
    stabilityK_->setChecked(document_.stabilityK);
    stabilityB1_ = new QCheckBox(tr("B1"));
    stabilityB1_->setObjectName("stabilityB1");
    stabilityB1_->setChecked(document_.stabilityB1);
    magnitudeAuto_ = new QCheckBox(tr("Auto amplitude scale"));
    magnitudeAuto_->setObjectName("magnitudeAutoScale");
    magnitudeAuto_->setChecked(document_.magnitudeAutoScale);
    magnitudeCeiling_ = new QComboBox;
    magnitudeCeiling_->setObjectName("magnitudeCeiling");
    for (const double db : {0.0, 10.0, 20.0, 50.0, 100.0, 200.0, 300.0})
        magnitudeCeiling_->addItem(tr("%1 dB").arg(db, 0, 'g', 5), db);
    magnitudeCeiling_->setCurrentIndex(
        magnitudeCeiling_->findData(document_.magnitudeCeilingDb) < 0 ? 3
        : magnitudeCeiling_->findData(document_.magnitudeCeilingDb));
    magnitudeFloor_ = new QComboBox;
    magnitudeFloor_->setObjectName("magnitudeFloor");
    for (const double db : {0.0, -10.0, -20.0, -50.0, -100.0, -300.0})
        magnitudeFloor_->addItem(tr("%1 dB").arg(db, 0, 'g', 5), db);
    magnitudeFloor_->setCurrentIndex(
        magnitudeFloor_->findData(document_.magnitudeFloorDb) < 0 ? 4
        : magnitudeFloor_->findData(document_.magnitudeFloorDb));
    const auto stabilityEnabled = !terminated_;
    stabilityGmax_->setEnabled(stabilityEnabled);
    stabilityK_->setEnabled(stabilityEnabled);
    stabilityB1_->setEnabled(stabilityEnabled);
    const auto add = [&](int row, int column, const QString& text, QWidget* widget) {
        controls->addWidget(new QLabel(text), row, column);
        controls->addWidget(widget, row, column + 1);
    };
    add(0, 0, tr("Start"), frequencyEditor(start_));
    add(0, 2, tr("Stop"), frequencyEditor(stop_));
    add(1, 0, tr("Points"), points_);
    add(1, 2, tr("Port reference (ohm)"), ports_);
    add(2, 0, tr("Frequency scale"), scale_);
    controls->addWidget(new QLabel(tr("Response")), 3, 0);
    controls->addWidget(mode_, 3, 1, 1, 3);
    controls->addWidget(new QLabel(tr("Stability")), 4, 0);
    controls->addWidget(stabilityGmax_, 4, 1);
    controls->addWidget(stabilityK_, 4, 2);
    controls->addWidget(stabilityB1_, 4, 3);
    controls->addWidget(magnitudeAuto_, 5, 0, 1, 2);
    controls->addWidget(new QLabel(tr("Ceiling")), 5, 2);
    controls->addWidget(magnitudeCeiling_, 5, 3);
    controls->addWidget(new QLabel(tr("Floor")), 6, 2);
    controls->addWidget(magnitudeFloor_, 6, 3);

    for (auto* spin : {static_cast<QDoubleSpinBox*>(start_), static_cast<QDoubleSpinBox*>(stop_), ports_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { autoApply(); });
    connect(points_, &QSpinBox::valueChanged, this, [this] { autoApply(); });
    connect(scale_, &QComboBox::currentIndexChanged, this, [this] { autoApply(); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { autoApply(); });
    connect(stabilityGmax_, &QCheckBox::toggled, this, [this](bool) { applyDisplayOptions(); });
    connect(stabilityK_, &QCheckBox::toggled, this, [this](bool) { applyDisplayOptions(); });
    connect(stabilityB1_, &QCheckBox::toggled, this, [this](bool) { applyDisplayOptions(); });
    connect(magnitudeAuto_, &QCheckBox::toggled, this, [this](bool) { applyDisplayOptions(); });
    connect(magnitudeCeiling_, &QComboBox::currentIndexChanged, this, [this](int) { applyDisplayOptions(); });
    connect(magnitudeFloor_, &QComboBox::currentIndexChanged, this, [this](int) { applyDisplayOptions(); });

    layout->addLayout(controls);
    status_ = new QLabel;
    status_->setObjectName("responseStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    auto* hint = new QLabel(tr("Click a frequency in the plot to update the Smith chart (undoable). "
        "Marker values are calculated at the exact analysis frequency. Measured data uses complex linear interpolation. "
        "S22 is dashed; coincident traces may overlap. The amplitude floor is configurable."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    plot_ = new ResponsePlotWidget;
    layout->addWidget(plot_, 1);
    auto* markerPanel = new QFrame;
    markerPanel->setObjectName("responseMarkerPanel");
    markerPanel->setFrameShape(QFrame::StyledPanel);
    markerPanel->setFrameShadow(QFrame::Plain);
    auto* markerLayout = new QHBoxLayout(markerPanel);
    markerLayout->setContentsMargins(8, 6, 8, 6);
    markerLayout->setSpacing(10);
    markerLayout->addWidget(new QLabel(tr("Analysis frequency")));
    markerFrequency_ = frequencyField("responseMarkerFrequency", document_.frequencyMHz);
    markerLayout->addWidget(frequencyEditor(markerFrequency_));
    marker_ = new QLabel;
    marker_->setObjectName("responseMarker");
    marker_->setTextFormat(Qt::PlainText);
    marker_->setWordWrap(true);
    marker_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    markerLayout->addWidget(marker_, 1);
    connect(markerFrequency_, &QDoubleSpinBox::valueChanged, this,
            [this](double frequency) {
                emit frequencySelected(rf::nearestMeasuredFrequency(document_, frequency));
            });
    plot_->setMarkerOverlay(markerPanel);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* exportButton = buttons->addButton(tr("Export plot PNG..."), QDialogButtonBox::ActionRole);
    exportButton->setObjectName("exportResponse");
    exportButton->setAutoDefault(false);
    auto* touchstoneButton = buttons->addButton(tr("Export Touchstone..."), QDialogButtonBox::ActionRole);
    touchstoneButton->setObjectName("exportTouchstone");
    touchstoneButton->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
    connect(exportButton, &QPushButton::clicked, this, &FrequencyResponseDialog::exportPlot);
    connect(touchstoneButton, &QPushButton::clicked, this, &FrequencyResponseDialog::exportTouchstone);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(plot_, &ResponsePlotWidget::frequencySelected, this,
            [this](double frequency) {
                emit frequencySelected(rf::nearestMeasuredFrequency(document_, frequency));
            });
    connect(plot_, &ResponsePlotWidget::frequencyTuneRequested, this,
            [this](int direction) { emit frequencyTuneRequested(direction); });
    applyDisplayOptions();
    calculate();
}

void FrequencyResponseDialog::applySweep()
{
    sweep_ = {start_->value(), stop_->value(), points_->value(),
              scale_->currentIndex() == 1, ports_->value()};
    terminated_ = mode_->currentIndex() == 1;
    applyDisplayOptions();
    const bool stabilityEnabled = !terminated_;
    stabilityGmax_->setEnabled(stabilityEnabled);
    stabilityK_->setEnabled(stabilityEnabled);
    stabilityB1_->setEnabled(stabilityEnabled);
    calculate();
    if (sweep_.stopMHz > sweep_.startMHz)
        emit sweepChanged(sweep_.startMHz, sweep_.stopMHz, sweep_.points,
                          sweep_.logarithmic, sweep_.portOhms, terminated_);
}

void FrequencyResponseDialog::applyDisplayOptions()
{
    const bool gmaxPreference = stabilityGmax_->isChecked();
    const bool kPreference = stabilityK_->isChecked();
    const bool b1Preference = stabilityB1_->isChecked();
    const bool gmax = !terminated_ && gmaxPreference;
    const bool k = !terminated_ && kPreference;
    const bool b1 = !terminated_ && b1Preference;
    const bool automatic = magnitudeAuto_->isChecked();
    double ceiling = magnitudeCeiling_->currentData().toDouble();
    double floor = magnitudeFloor_->currentData().toDouble();
    if (!automatic && floor >= ceiling) {
        floor = std::max(-300.0, floor - 10.0);
        const int floorIndex = magnitudeFloor_->findData(floor);
        if (floorIndex >= 0) magnitudeFloor_->setCurrentIndex(floorIndex);
        else floor = magnitudeFloor_->currentData().toDouble();
    }
    magnitudeCeiling_->setEnabled(!automatic);
    magnitudeFloor_->setEnabled(!automatic);
    document_.stabilityGmax = gmaxPreference;
    document_.stabilityK = kPreference;
    document_.stabilityB1 = b1Preference;
    document_.magnitudeAutoScale = automatic;
    document_.magnitudeCeilingDb = ceiling;
    document_.magnitudeFloorDb = floor;
    plot_->setStabilityTraces(gmax, k, b1);
    plot_->setMagnitudeScale(automatic, ceiling, floor);
    emit displayOptionsChanged(gmaxPreference, kPreference, b1Preference,
                               automatic, ceiling, floor);
}

bool FrequencyResponseDialog::plotHasFocus() const
{
    return plot_ && plot_->hasFocus();
}

void FrequencyResponseDialog::focusPlot()
{
    if (plot_) plot_->setFocus(Qt::OtherFocusReason);
}

void FrequencyResponseDialog::autoApply()
{
    if (updatingControls_) return;
    // Mid-edit the range can be briefly inconsistent (e.g. start raised above stop before
    // stop is raised). That is not an error: keep the last good plot and wait.
    if (start_->value() > stop_->value()) {
        status_->setText(tr("Start must be below stop; the plot updates once the range is valid."));
        return;
    }
    applySweep();
}

void FrequencyResponseDialog::setNetwork(const rf::Document& document)
{
    const bool changed = document_.components != document.components
        || document_.measuredLoad != document.measuredLoad
        || (terminated_ && document_.startOhms != document.startOhms);
    const bool rangeChanged = rf::measuredFrequencyRange(document_) != rf::measuredFrequencyRange(document);
    const bool loadChanged = document_.measuredLoad.has_value() != document.measuredLoad.has_value();
    const bool responseChanged =
        document_.frequencyResponseConfigured != document.frequencyResponseConfigured
        || document_.frequencyResponseStartMHz != document.frequencyResponseStartMHz
        || document_.frequencyResponseStopMHz != document.frequencyResponseStopMHz
        || document_.frequencyResponsePoints != document.frequencyResponsePoints
        || document_.frequencyResponseLogarithmic != document.frequencyResponseLogarithmic
        || document_.frequencyResponsePortOhms != document.frequencyResponsePortOhms
        || document_.frequencyResponseTerminated != document.frequencyResponseTerminated;
    document_ = document;
    updatingControls_ = true;
    if (document_.frequencyResponseConfigured) {
        const QSignalBlocker startBlock(start_);
        const QSignalBlocker stopBlock(stop_);
        const QSignalBlocker pointsBlock(points_);
        const QSignalBlocker scaleBlock(scale_);
        const QSignalBlocker portsBlock(ports_);
        const QSignalBlocker modeBlock(mode_);
        start_->setValue(document_.frequencyResponseStartMHz);
        stop_->setValue(document_.frequencyResponseStopMHz);
        points_->setValue(document_.frequencyResponsePoints);
        scale_->setCurrentIndex(document_.frequencyResponseLogarithmic ? 1 : 0);
        ports_->setValue(document_.frequencyResponsePortOhms);
        mode_->setCurrentIndex(document_.frequencyResponseTerminated ? 1 : 0);
        sweep_ = {document_.frequencyResponseStartMHz, document_.frequencyResponseStopMHz,
                  document_.frequencyResponsePoints, document_.frequencyResponseLogarithmic,
                  document_.frequencyResponsePortOhms};
        terminated_ = document_.frequencyResponseTerminated;
    }
    {
        const QSignalBlocker gmaxBlock(stabilityGmax_);
        const QSignalBlocker kBlock(stabilityK_);
        const QSignalBlocker b1Block(stabilityB1_);
        const QSignalBlocker autoBlock(magnitudeAuto_);
        const QSignalBlocker ceilingBlock(magnitudeCeiling_);
        const QSignalBlocker floorBlock(magnitudeFloor_);
        stabilityGmax_->setChecked(document_.stabilityGmax);
        stabilityK_->setChecked(document_.stabilityK);
        stabilityB1_->setChecked(document_.stabilityB1);
        magnitudeAuto_->setChecked(document_.magnitudeAutoScale);
        magnitudeCeiling_->setCurrentIndex(
            magnitudeCeiling_->findData(document_.magnitudeCeilingDb) < 0 ? 3
            : magnitudeCeiling_->findData(document_.magnitudeCeilingDb));
        magnitudeFloor_->setCurrentIndex(
            magnitudeFloor_->findData(document_.magnitudeFloorDb) < 0 ? 4
            : magnitudeFloor_->findData(document_.magnitudeFloorDb));
        magnitudeCeiling_->setEnabled(!document_.magnitudeAutoScale);
        magnitudeFloor_->setEnabled(!document_.magnitudeAutoScale);
    }
    stabilityGmax_->setEnabled(!terminated_);
    stabilityK_->setEnabled(!terminated_);
    stabilityB1_->setEnabled(!terminated_);
    plot_->setStabilityTraces(!terminated_ && document_.stabilityGmax,
                              !terminated_ && document_.stabilityK,
                              !terminated_ && document_.stabilityB1);
    plot_->setMagnitudeScale(document_.magnitudeAutoScale,
                             document_.magnitudeCeilingDb,
                             document_.magnitudeFloorDb);
    if (loadChanged && document.measuredLoad) {
        // A measured load only exists in the terminated view; removing it leaves the mode as is.
        terminated_ = true;
        mode_->setCurrentIndex(1);
        stabilityGmax_->setEnabled(false);
        stabilityK_->setEnabled(false);
        stabilityB1_->setEnabled(false);
        plot_->setStabilityTraces(false, false, false);
    }
    updatingControls_ = false;
    if (changed || responseChanged) {
        if (rangeChanged) constrainSweep();
        calculate();
    } else updateMarker();
}

void FrequencyResponseDialog::constrainSweep()
{
    const auto range = rf::measuredFrequencyRange(document_);
    if (sweep_.startMHz >= range.first && sweep_.stopMHz <= range.second
        && (sweep_.startMHz < sweep_.stopMHz || range.first == range.second)) return;
    sweep_.startMHz = std::max(sweep_.startMHz, range.first);
    sweep_.stopMHz = std::min(sweep_.stopMHz, range.second);
    if (sweep_.startMHz >= sweep_.stopMHz) {
        sweep_.startMHz = std::max({1e-9, document_.frequencyMHz / 10, range.first});
        sweep_.stopMHz = std::min({1e12, document_.frequencyMHz * 2, range.second});
        if (sweep_.startMHz >= sweep_.stopMHz && range.first > 0) {
            sweep_.startMHz = range.first;
            sweep_.stopMHz = range.second;
        }
    }
    updatingControls_ = true;
    start_->setValue(sweep_.startMHz);
    stop_->setValue(sweep_.stopMHz);
    updatingControls_ = false;
}

void FrequencyResponseDialog::calculate()
{
    try {
        const auto range = rf::measuredFrequencyRange(document_);
        singlePoint_ = range.first == range.second && sweep_.startMHz == range.first
            && sweep_.stopMHz == range.second;
        auto plotSweep = sweep_;
        std::vector<rf::ResponseSample> samples;
        if (singlePoint_) {
            plotSweep.startMHz = range.first / 1.05;
            plotSweep.stopMHz = range.second * 1.05;
            samples.push_back({range.first, terminated_ ? rf::SParameters{}
                : rf::sParameters(document_.components, range.first, sweep_.portOhms)});
        } else {
            samples = rf::frequencyResponse(terminated_ ? std::vector<rf::Component>{} : document_.components, sweep_);
        }
        if (terminated_)
            for (auto& sample : samples)
                sample.s = {rf::inputReflection(document_, sample.frequencyMHz, sweep_.portOhms), 0, 0, 0};
        plot_->setResponse(plotSweep, std::move(samples), terminated_);
        status_->setText(tr("%1 points; both ports %2 ohm. Component network only; "
            "microstrip dielectric loss and measured blocks are included. Sweep controls take effect on Apply.")
            .arg(sweep_.points).arg(sweep_.portOhms, 0, 'g', 7));
        if (terminated_) status_->setText(tr("%1 points; input reference %2 ohm. Starting load included; S21/S22 not defined in this view.")
                                          .arg(sweep_.points).arg(sweep_.portOhms, 0, 'g', 7));
        if (singlePoint_) status_->setText(tr("Single measured frequency: %1 MHz. One point shown; no sweep or extrapolation.")
                                          .arg(range.first, 0, 'g', 9));
        updateMarker();
    } catch (const std::exception& e) {
        // Clear stale traces and readouts; the invalid sweep is not a successful result.
        plot_->setResponse(rf::Sweep{}, {});
        status_->setText(tr("Cannot calculate response: %1").arg(e.what()));
        marker_->clear();
    }
}

void FrequencyResponseDialog::updateMarker()
{
    if (plot_->samples().empty()) return;
    try {
        const auto s = terminated_
            ? rf::SParameters{rf::inputReflection(document_, document_.frequencyMHz, sweep_.portOhms), 0, 0, 0}
            : rf::sParameters(document_.components, document_.frequencyMHz, sweep_.portOhms);
        const QSignalBlocker markerBlock(markerFrequency_);
        markerFrequency_->setValue(document_.frequencyMHz);
        plot_->setMarker(document_.frequencyMHz);
        marker_->setText(tr("Analysis marker: %1 MHz%2\nS11: %3 dB     S21: %4 dB     S22: %5 dB")
            .arg(document_.frequencyMHz, 0, 'g', 9)
            .arg(document_.frequencyMHz < sweep_.startMHz || document_.frequencyMHz > sweep_.stopMHz
                     ? tr(" (outside sweep)") : QString{})
            .arg(magnitude(s.s11), magnitude(s.s21), magnitude(s.s22)));
        if (!terminated_
            && (document_.stabilityGmax || document_.stabilityK || document_.stabilityB1)) {
            const auto stability = rf::stabilityMetrics(s);
            QString gainText = tr("N/A");
            if (stability.gmaxDefined)
                gainText = tr("Gmax %1 dB").arg(stability.gmaxDb, 0, 'f', 3);
            else if (stability.msgDefined)
                gainText = tr("MSG %1 dB").arg(stability.msgDb, 0, 'f', 3);
            marker_->setText(marker_->text() + tr("\n%1     K: %2     |Delta|: %3     B1: %4")
                .arg(gainText)
                .arg(stability.kDefined ? QString::number(stability.k, 'g', 7) : tr("N/A"))
                .arg(QString::number(stability.deltaMagnitude, 'g', 7))
                .arg(QString::number(stability.b1, 'g', 7)));
        }
        if (terminated_) marker_->setText(tr("Analysis marker: %1 MHz%2\nTerminated input S11: %3 dB")
            .arg(document_.frequencyMHz, 0, 'g', 9)
            .arg(document_.frequencyMHz < sweep_.startMHz || document_.frequencyMHz > sweep_.stopMHz
                     ? tr(" (outside sweep)") : QString{}).arg(magnitude(s.s11)));
    } catch (const std::exception& e) {
        plot_->clearMarker();
        marker_->setText(tr("Cannot calculate marker: %1").arg(e.what()));
    }
}

void FrequencyResponseDialog::exportPlot()
{
    if (plot_->samples().empty()) {
        QMessageBox::warning(this, tr("Cannot export response"), tr("Apply a valid sweep first."));
        return;
    }
    auto path = QFileDialog::getSaveFileName(this, tr("Export frequency response"),
                                            "frequency-response.png", tr("PNG image (*.png)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    if (!responseImage().save(path, "PNG"))
        QMessageBox::critical(this, tr("Export failed"), tr("Could not write response image to %1.").arg(path));
}

void FrequencyResponseDialog::exportTouchstone()
{
    if (plot_->samples().empty()) {
        QMessageBox::warning(this, tr("Cannot export Touchstone"), tr("Apply a valid sweep first."));
        return;
    }
    const int ports = terminated_ ? 1 : 2;
    const QString filter = ports == 1
        ? tr("Touchstone 1-port (*.s1p)")
        : tr("Touchstone 2-port (*.s2p)");
    const QString defaultName = ports == 1
        ? "network-response.s1p" : "network-response.s2p";
    auto path = QFileDialog::getSaveFileName(this, tr("Export Touchstone response"),
                                             defaultName, filter);
    if (path.isEmpty()) return;
    if (!path.endsWith(ports == 1 ? ".s1p" : ".s2p", Qt::CaseInsensitive))
        path += ports == 1 ? ".s1p" : ".s2p";

    try {
        std::vector<rf::ResponseSample> exported = plot_->samples();
        if (ports == 1) {
            for (auto& sample : exported)
                sample.s = {sample.s.s11, 0, 0, 0};
        }
        const std::string text = rf::formatTouchstone(exported, ports, sweep_.portOhms);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw std::runtime_error(file.errorString().toStdString());
        const QByteArray bytes(text.data(), static_cast<qsizetype>(text.size()));
        if (file.write(bytes) != bytes.size())
            throw std::runtime_error(file.errorString().toStdString());
        if (file.error() != QFileDevice::NoError)
            throw std::runtime_error(file.errorString().toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Touchstone export failed"), e.what());
    }
}

QImage FrequencyResponseDialog::responseImage()
{
    if (plot_->samples().empty())
        throw std::invalid_argument(("Open Frequency response.\n"
                                     + status_->text()).toStdString());

    // Export at a fixed plot size so the PNG does not depend on the docked panel's size.
    const QSize plotSize(900, 460);
    const auto savedMinimum = plot_->minimumSize();
    const auto savedMaximum = plot_->maximumSize();
    plot_->setFixedSize(plotSize);
    plot_->setFocusBorderEnabled(false);
    const auto plot = plot_->grab();
    plot_->setFocusBorderEnabled(true);
    plot_->setMinimumSize(savedMinimum);
    plot_->setMaximumSize(savedMaximum);
    layout()->activate();  // put the on-screen plot back before returning to the event loop

    const int margin = 20;
    const int width = std::max(900, plotSize.width() + 2 * margin);
    QString header = tr("%1\nFrequency response: %2 - %3 MHz, %4, %5 points; both ports %6 ohm\n"
        "Port 1: input; port 2: load end. External source/start/goal terminations excluded.\n"
        "Microstrip dielectric loss and measured blocks included; complex linear interpolation, no extrapolation.\n"
        "Amplitude %7 dB to %8 dB.")
        .arg(QString::fromStdString(document_.title))
        .arg(sweep_.startMHz, 0, 'g', 8).arg(sweep_.stopMHz, 0, 'g', 8)
        .arg(sweep_.logarithmic ? tr("logarithmic") : tr("linear"))
        .arg(singlePoint_ ? 1 : sweep_.points).arg(sweep_.portOhms, 0, 'g', 8)
        .arg(document_.magnitudeAutoScale ? tr("auto ceiling")
                                          : QString::number(document_.magnitudeCeilingDb, 'g', 7))
        .arg(document_.magnitudeAutoScale ? -100.0 : document_.magnitudeFloorDb, 0, 'g', 7);
    if (terminated_)
        header = tr("%1\nTerminated input S11: %2 - %3 MHz, %4, %5 points; input %6 ohm\n"
            "Starting/measured load included; S21/S22 not defined. Complex linear interpolation; no extrapolation.\n"
            "Microstrip dielectric loss and measured blocks included.\n"
            "Amplitude %7 dB to %8 dB.")
            .arg(QString::fromStdString(document_.title))
            .arg(sweep_.startMHz, 0, 'g', 8).arg(sweep_.stopMHz, 0, 'g', 8)
            .arg(sweep_.logarithmic ? tr("logarithmic") : tr("linear"))
            .arg(singlePoint_ ? 1 : sweep_.points).arg(sweep_.portOhms, 0, 'g', 8)
            .arg(document_.magnitudeAutoScale ? tr("auto ceiling")
                                              : QString::number(document_.magnitudeCeilingDb, 'g', 7))
            .arg(document_.magnitudeAutoScale ? -100.0 : document_.magnitudeFloorDb, 0, 'g', 7);
    if (singlePoint_) header += tr("\nSingle measured frequency only; no sweep or extrapolation.");
    QImage image(width, plotSize.height() + 240, QImage::Format_RGB32);
    if (image.isNull()) return image;
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::black);
    painter.drawText(QRect(margin, margin, width - 2 * margin, 90),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, header);
    painter.drawPixmap(QRect(QPoint((width - plotSize.width()) / 2, 120), plotSize), plot);
    painter.drawText(QRect(margin, plotSize.height() + 140, width - 2 * margin, 80),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, marker_->text());
    return image;
}

void FrequencyResponseDialog::setTerminated(bool terminated){
    mode_->setCurrentIndex(terminated ? 1 : 0);  // recalculates through autoApply()
}