#include "FrequencyResponseDialog.h"

#include "ResponsePlotWidget.h"
#include "Touchstone.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
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
    spin->setDecimals(9);
    spin->setRange(1e-9, 1e12);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
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
    resize(1000, 720);
    setMinimumWidth(760);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    auto* explanation = new QLabel(tr(
        "Port 1 = source / input; port 2 = load end. Both ports use the reference below.\n"
        "Start/goal impedances and the schematic source resistor are external, not included. "
        "S11 is input match with port 2 matched; it may differ from the terminated Smith chart.\n"
        "Terminated-input mode instead includes the starting/measured load and plots S11 only."));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* controls = new QGridLayout;
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
    terminated_ = document.measuredLoad.has_value();
    start_ = field("sweepStart", sweep_.startMHz, this);
    stop_ = field("sweepStop", sweep_.stopMHz, this);
    ports_ = field("sweepPorts", sweep_.portOhms, this);
    ports_->setToolTip(tr("Equal real reference impedance at both ports; independent of chart Z0 after opening."));
    points_ = new QSpinBox;
    points_->setObjectName("sweepPoints");
    points_->setRange(2, 2001);
    points_->setValue(sweep_.points);
    scale_ = new QComboBox;
    scale_->setObjectName("sweepScale");
    scale_->addItems({tr("Linear"), tr("Logarithmic")});
    mode_ = new QComboBox;
    mode_->setObjectName("responseMode");
    mode_->addItems({tr("Two-port circuit: S11/S21/S22"), tr("Terminated input: S11 only")});
    mode_->setCurrentIndex(terminated_ ? 1 : 0);
    const auto add = [&](int row, int column, const QString& text, QWidget* widget) {
        controls->addWidget(new QLabel(text), row, column);
        controls->addWidget(widget, row, column + 1);
    };
    add(0, 0, tr("Start (MHz)"), start_);
    add(0, 2, tr("Stop (MHz)"), stop_);
    add(0, 4, tr("Points"), points_);
    add(1, 0, tr("Port reference (ohm)"), ports_);
    add(1, 2, tr("Frequency scale"), scale_);
    add(2, 0, tr("Response"), mode_);
    auto* apply = new QPushButton(tr("Apply sweep"));
    apply->setObjectName("applySweep");
    apply->setDefault(true);
    controls->addWidget(apply, 1, 5);
    connect(apply, &QPushButton::clicked, this, &FrequencyResponseDialog::applySweep);
    layout->addLayout(controls);
    status_ = new QLabel;
    status_->setObjectName("responseStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    plot_ = new ResponsePlotWidget;
    layout->addWidget(plot_, 1);
    marker_ = new QLabel;
    marker_->setObjectName("responseMarker");
    marker_->setTextFormat(Qt::PlainText);
    marker_->setWordWrap(true);
    marker_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(marker_);
    auto* hint = new QLabel(tr("Click a frequency in the plot to update the Smith chart (undoable). "
        "Marker values are calculated at the exact analysis frequency. Measured data uses complex linear interpolation. "
        "S22 is dashed; coincident traces may overlap. Values below -100 dB are clipped in the plot only."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* exportButton = buttons->addButton(tr("Export plot PNG..."), QDialogButtonBox::ActionRole);
    exportButton->setObjectName("exportResponse");
    exportButton->setAutoDefault(false);
    connect(exportButton, &QPushButton::clicked, this, &FrequencyResponseDialog::exportPlot);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    layout->addWidget(buttons);
    connect(plot_, &ResponsePlotWidget::frequencySelected, this, &FrequencyResponseDialog::frequencySelected);
    calculate();
}

void FrequencyResponseDialog::applySweep()
{
    sweep_ = {start_->value(), stop_->value(), points_->value(),
              scale_->currentIndex() == 1, ports_->value()};
    terminated_ = mode_->currentIndex() == 1;
    calculate();
}

void FrequencyResponseDialog::setNetwork(const rf::Document& document)
{
    const bool changed = document_.components != document.components
        || document_.measuredLoad != document.measuredLoad
        || (terminated_ && document_.startOhms != document.startOhms);
    const bool rangeChanged = rf::measuredFrequencyRange(document_) != rf::measuredFrequencyRange(document);
    const bool loadChanged = document_.measuredLoad.has_value() != document.measuredLoad.has_value();
    document_ = document;
    if (loadChanged) {
        terminated_ = document.measuredLoad.has_value();
        mode_->setCurrentIndex(terminated_ ? 1 : 0);
    }
    if (changed) {
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
    start_->setValue(sweep_.startMHz);
    stop_->setValue(sweep_.stopMHz);
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
        plot_->setMarker(document_.frequencyMHz);
        marker_->setText(tr("Analysis marker: %1 MHz%2\nS11: %3 dB     S21: %4 dB     S22: %5 dB")
            .arg(document_.frequencyMHz, 0, 'g', 9)
            .arg(document_.frequencyMHz < sweep_.startMHz || document_.frequencyMHz > sweep_.stopMHz
                     ? tr(" (outside sweep)") : QString{})
            .arg(magnitude(s.s11), magnitude(s.s21), magnitude(s.s22)));
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

QImage FrequencyResponseDialog::responseImage()
{
    if (plot_->samples().empty())
        throw std::invalid_argument(("Open Frequency response and apply a valid sweep before exporting.\n"
                                     + status_->text()).toStdString());
    layout()->activate();
    const auto plot = plot_->grab();
    const auto plotSize = plot_->size();
    const int margin = 20;
    const int width = std::max(900, plotSize.width() + 2 * margin);
    QString header = tr("%1\nFrequency response: %2 - %3 MHz, %4, %5 points; both ports %6 ohm\n"
        "Port 1: input; port 2: load end. External source/start/goal terminations excluded.\n"
        "Microstrip dielectric loss and measured blocks included; complex linear interpolation, no extrapolation; floor -100 dB.")
        .arg(QString::fromStdString(document_.title))
        .arg(sweep_.startMHz, 0, 'g', 8).arg(sweep_.stopMHz, 0, 'g', 8)
        .arg(sweep_.logarithmic ? tr("logarithmic") : tr("linear"))
        .arg(singlePoint_ ? 1 : sweep_.points).arg(sweep_.portOhms, 0, 'g', 8);
    if (terminated_)
        header = tr("%1\nTerminated input S11: %2 - %3 MHz, %4, %5 points; input %6 ohm\n"
            "Starting/measured load included; S21/S22 not defined. Complex linear interpolation; no extrapolation.\n"
            "Microstrip dielectric loss and measured blocks included; plot floor -100 dB.")
            .arg(QString::fromStdString(document_.title))
            .arg(sweep_.startMHz, 0, 'g', 8).arg(sweep_.stopMHz, 0, 'g', 8)
            .arg(sweep_.logarithmic ? tr("logarithmic") : tr("linear"))
            .arg(singlePoint_ ? 1 : sweep_.points).arg(sweep_.portOhms, 0, 'g', 8);
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
