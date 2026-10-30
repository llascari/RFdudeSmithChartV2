#include "Editors.h"
#include "Touchstone.h"

#include <QComboBox>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
class ComponentDialog final : public QDialog
{
public:
    explicit ComponentDialog(QWidget* parent) : QDialog(parent)
    {
        qApp->installEventFilter(this);
    }

    ~ComponentDialog() override { qApp->removeEventFilter(this); }

    std::vector<QWidget*> tabControls;
    std::function<void()> acceptEdit;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget
            || (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride))
            return QDialog::eventFilter(watched, event);
        auto* owner = widget;
        while (owner && !qobject_cast<QDialog*>(owner)) owner = owner->parentWidget();
        if (owner != this) return QDialog::eventFilter(watched, event);
        auto* key = static_cast<QKeyEvent*>(event);
        const bool tab = key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab;
        if (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter
            && key->key() != Qt::Key_Escape && !tab)
            return QDialog::eventFilter(watched, event);
        QComboBox* popup = nullptr;
        for (auto* combo : findChildren<QComboBox*>())
            if (combo->view()->isVisible()) {
                popup = combo;
                break;
            }
        if (tab && !popup) return QDialog::eventFilter(watched, event);
        key->accept();
        if (event->type() == QEvent::ShortcutOverride) return true;
        // A popup normally consumes Enter before the dialog sees it.
        if (popup) {
            if (key->key() != Qt::Key_Escape) {
                const auto index = popup->view()->currentIndex();
                if (index.isValid()) popup->setCurrentIndex(index.row());
            }
            popup->hidePopup();
        }
        if (tab) {
            popup->setFocus();
            return focusNextPrevChild(key->key() == Qt::Key_Tab
                                      && !(key->modifiers() & Qt::ShiftModifier));
        }
        if (key->key() != Qt::Key_Escape && acceptEdit) acceptEdit();
        return true;
    }

    bool focusNextPrevChild(bool next) override
    {
        const int count = static_cast<int>(tabControls.size());
        int current = -1;
        for (int i = 0; i < count; ++i) {
            if (tabControls[i] == focusWidget() || tabControls[i]->isAncestorOf(focusWidget())) {
                current = i;
                break;
            }
        }
        if (current < 0) return QDialog::focusNextPrevChild(next);
        for (int step = 1; step <= count; ++step) {
            const int index = (current + (next ? step : count - step)) % count;
            auto* control = tabControls[index];
            if (control->isVisible() && control->isEnabled()
                && (control->focusPolicy() & Qt::TabFocus)) {
                control->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
                return true;
            }
        }
        return QDialog::focusNextPrevChild(next);
    }
};

QDoubleSpinBox* field(QFormLayout* form, const QString& label, double value,
                      const QString& name, double minimum = 1e-9, double maximum = 1e12)
{
    auto* result = new QDoubleSpinBox;
    result->setObjectName(name);
    result->setDecimals(9);
    result->setRange(minimum, maximum);
    result->setValue(value);
    result->setKeyboardTracking(false);
    form->addRow(label, result);
    return result;
}

QDialogButtonBox* buttons(QDialog& dialog, QVBoxLayout* layout)
{
    auto* result = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    result->button(QDialogButtonBox::Ok)->setDefault(true);
    layout->addWidget(result);
    QObject::connect(result, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return result;
}
}

QString measuredDescription(const rf::TouchstoneData& data)
{
    return QString("Touchstone %1, %2, %3 ohm, %4-%5 MHz (%6 samples)")
        .arg(QString::fromStdString(data.name),
             data.ports == 1 ? "one-port load" : "P1=input / P2=load",
             QString::number(data.referenceOhms, 'g', 7),
             QString::number(data.samples.front().frequencyMHz, 'g', 7),
             QString::number(data.samples.back().frequencyMHz, 'g', 7))
        .arg(data.samples.size());
}

QString componentDescription(const rf::Component& c)
{
    const auto n = [](double value) { return QString::number(value, 'g', 7); };
    switch (c.kind) {
    case rf::Kind::Measured:
        return measuredDescription(c.measured);
    case rf::Kind::Resistor: return QString("R %1 ohm").arg(n(c.resistance));
    case rf::Kind::Capacitor: return QString("C %1 pF").arg(n(c.capacitancePf));
    case rf::Kind::Inductor: return QString("L %1 nH").arg(n(c.inductanceNh));
    case rf::Kind::Line:
        return QString("TL %1 ohm, %2 deg @ %3 MHz")
            .arg(n(c.lineOhms), n(c.lengthDegrees), n(c.referenceMHz));
    case rf::Kind::Transformer:
        return QString("Ideal transformer %1:1 (source:load)").arg(n(c.turnsRatio));
    case rf::Kind::Coax:
    case rf::Kind::Microstrip: {
        const auto parameters = rf::lineParameters(c);
        QString result = QString("%1 %2 ohm, %3 deg @ %4 MHz")
            .arg(c.kind == rf::Kind::Coax ? "Coax" : "Microstrip",
                 n(parameters.impedanceOhms), n(parameters.lengthDegrees), n(c.referenceMHz));
        if (c.lengthMode == rf::LengthMode::Physical)
            result += QString(", %1 mm").arg(n(c.physicalLengthMm));
        if (c.kind == rf::Kind::Coax)
            result += c.velocityMode == rf::VelocityMode::Factor
                ? QString(", VF %1").arg(n(c.velocityFactor))
                : QString(", Dk %1").arg(n(c.dielectricConstant));
        else
            result += QString(", W/H %1/%2 mm, Dk %3, DF %4")
                .arg(n(c.stripWidthMm), n(c.substrateHeightMm), n(c.substrateDk), n(c.lossTangent));
        return result;
    }
    case rf::Kind::SeriesRlc:
    case rf::Kind::ParallelRlc:
        return QString("%1: %2 ohm, %3 nH, %4 pF (tune %5)")
            .arg(QString::fromStdString(rf::kindName(c.kind)), n(c.resistance),
                 n(c.inductanceNh), n(c.capacitancePf),
                 c.tune == rf::Tune::Resistance ? "R" : c.tune == rf::Tune::Inductance ? "L" : "C");
    }
    throw std::invalid_argument("Unknown component type.");
}

std::optional<rf::Component> editComponent(QWidget* parent, const rf::Component& original)
{
    if (original.kind == rf::Kind::Measured) {
        QMessageBox::information(parent, "Measured two-port",
            componentDescription(original) + "\n\nMeasured data is read-only. Delete this block and import another file to replace it.");
        return std::nullopt;
    }
    ComponentDialog dialog(parent);
    dialog.setObjectName("componentEditor");
    dialog.setWindowTitle("Component");
    dialog.setStyleSheet(
        "QComboBox, QDoubleSpinBox { border: 3px solid palette(mid); padding: 3px; }"
        "QComboBox:focus, QDoubleSpinBox:focus { border-color: palette(highlight); }"
        "QComboBox:focus, QDoubleSpinBox:focus QLineEdit {"
        " background-color: palette(highlight); color: palette(highlighted-text); }");
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    auto* kind = new QComboBox;
    kind->setObjectName("componentKind");
    for (int i = 10; i <= static_cast<int>(rf::Kind::Microstrip); ++i)
        kind->addItem(QString::fromStdString(rf::kindName(static_cast<rf::Kind>(i))), i);
    kind->setCurrentIndex(kind->findData(static_cast<int>(original.kind)));
    form->addRow("Type", kind);
    auto* connection = new QComboBox;
    connection->setObjectName("componentConnection");
    form->addRow("Connection", connection);
    auto* resistance = field(form, "Resistance (ohm)", original.resistance, "resistance");
    auto* inductance = field(form, "Inductance (nH)", original.inductanceNh, "inductance");
    auto* capacitance = field(form, "Capacitance (pF)", original.capacitancePf, "capacitance");
    auto* lineOhms = field(form, "Line impedance (ohm)", original.lineOhms, "lineOhms");
    auto* length = field(form, "Electrical length (deg)", original.lengthDegrees, "length", 0, 36000);
    auto* reference = field(form, "Length reference (MHz)", original.referenceMHz, "lineFrequency");
    auto* ratio = field(form, "Turns ratio (source : load = N : 1)", original.turnsRatio,
                        "turnsRatio", 1e-6, 1e6);
    auto* lengthMode = new QComboBox;
    lengthMode->setObjectName("lengthMode");
    lengthMode->addItems({"Electrical length", "Physical length (mm)"});
    lengthMode->setCurrentIndex(static_cast<int>(original.lengthMode));
    form->addRow("Length input", lengthMode);
    auto* physical = field(form, "Physical length (mm)", original.physicalLengthMm, "physicalLength", 0, 1e9);
    auto* velocityMode = new QComboBox;
    velocityMode->setObjectName("velocityMode");
    velocityMode->addItems({"Velocity factor", "Dielectric constant"});
    velocityMode->setCurrentIndex(static_cast<int>(original.velocityMode));
    form->addRow("Coax propagation", velocityMode);
    auto* velocity = field(form, "Velocity factor (v/c)", original.velocityFactor, "velocityFactor", 1e-9, 1);
    auto* dielectric = field(form, "Coax dielectric constant", original.dielectricConstant, "dielectricConstant", 1, 128);
    auto* height = field(form, "Substrate height (mm)", original.substrateHeightMm, "substrateHeight", 1e-6, 1e6);
    auto* width = field(form, "Strip width (mm)", original.stripWidthMm, "stripWidth", 1e-6, 1e8);
    auto* dk = field(form, "Substrate Dk", original.substrateDk, "substrateDk", 1, 128);
    auto* df = field(form, "Loss tangent / DF", original.lossTangent, "lossTangent", 0, 0.1);
    auto* target = field(form, "Target impedance (ohm)", original.lineOhms, "targetImpedance");
    auto* solve = new QPushButton("Calculate width from target impedance");
    solve->setObjectName("calculateWidth");
    solve->setAutoDefault(false);
    form->addRow(solve);
    auto* preview = new QLabel;
    preview->setObjectName("linePreview");
    preview->setWordWrap(true);
    preview->setTextFormat(Qt::PlainText);
    preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* results = new QGroupBox("Calculated results");
    auto* resultsLayout = new QVBoxLayout(results);
    resultsLayout->addWidget(preview);
    layout->addWidget(results);
    auto* modelNotes = new QGroupBox("Microstrip model / limits");
    modelNotes->setObjectName("microstripModelNotes");
    auto* notesLayout = new QVBoxLayout(modelNotes);
    auto* notes = new QLabel(
        "Hammerstad-Jensen: quasi-static, zero copper thickness, infinite ground.\n"
        "Dielectric loss only. Excludes copper/roughness loss, solder mask, radiation, "
        "discontinuities, dispersion, and open-end correction.\n"
        "Supported: W/H 0.01-100; Dk 1-128; DF 0-0.1.");
    notes->setWordWrap(true);
    notesLayout->addWidget(notes);
    layout->addWidget(modelNotes);
    auto* tuning = new QComboBox;
    tuning->setObjectName("tuningParameter");
    tuning->addItems({"Resistance", "Inductance", "Capacitance"});
    tuning->setCurrentIndex(static_cast<int>(original.tune));
    form->addRow("Keyboard tuning parameter", tuning);
    for (auto* control : dialog.findChildren<QComboBox*>())
        control->setFocusPolicy(Qt::StrongFocus);
    QDialogButtonBox* buttonBox = nullptr;
    const auto updateTabOrder = [&] {
        std::vector<QWidget*> controls;
        const auto append = [&](QWidget* control) {
            if (control) controls.push_back(control);
        };
        for (int row = 0; row < form->rowCount(); ++row) {
            if (auto* item = form->itemAt(row, QFormLayout::FieldRole)) {
                auto* widget = item->widget();
                if (qobject_cast<QComboBox*>(widget) || qobject_cast<QDoubleSpinBox*>(widget))
                    append(widget);
            }
        }
        dialog.tabControls = std::move(controls);
    };
    const auto update = [&] {
        const auto k = static_cast<rf::Kind>(kind->currentData().toInt());
        const bool line = rf::isTransmissionLine(k);
        const bool coax = k == rf::Kind::Coax;
        const bool strip = k == rf::Kind::Microstrip;
        const bool transformer = k == rf::Kind::Transformer;
        const bool compound = k == rf::Kind::SeriesRlc || k == rf::Kind::ParallelRlc;
        const int previous = connection->currentData().toInt();
        connection->clear();
        for (int i = line || transformer ? 3 : 1; i <= (line ? 5 : transformer ? 3 : 2); ++i)
            connection->addItem(QString::fromStdString(rf::connectionName(static_cast<rf::Connection>(i))), i);
        const int index = connection->findData(previous);
        connection->setCurrentIndex(index >= 0 ? index : 0);
        form->setRowVisible(resistance, compound || k == rf::Kind::Resistor);
        form->setRowVisible(inductance, compound || k == rf::Kind::Inductor);
        form->setRowVisible(capacitance, compound || k == rf::Kind::Capacitor);
        form->setRowVisible(lineOhms, line && !strip);
        form->setRowVisible(lengthMode, coax || strip);
        form->setRowVisible(length, line && (k == rf::Kind::Line || lengthMode->currentIndex() == 0));
        form->setRowVisible(physical, (coax || strip) && lengthMode->currentIndex() == 1);
        form->setRowVisible(reference, line);
        form->setRowVisible(ratio, transformer);
        form->setRowVisible(velocityMode, coax);
        form->setRowVisible(velocity, coax && velocityMode->currentIndex() == 0);
        form->setRowVisible(dielectric, coax && velocityMode->currentIndex() == 1);
        for (auto* control : {height, width, dk, df, target}) form->setRowVisible(control, strip);
        form->setRowVisible(solve, strip);
        form->setRowVisible(tuning, compound);
        dialog.setWindowTitle(strip ? "Microstrip calculator / element" : "Component");
        dialog.setMinimumWidth(strip ? 640 : 480);
        results->setVisible(line || transformer);
        modelNotes->setVisible(strip);
        updateTabOrder();
    };
    QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, update);
    update();
    connection->setCurrentIndex(connection->findData(static_cast<int>(original.connection)));
    const auto focusValue = [&] {
        switch (static_cast<rf::Kind>(kind->currentData().toInt())) {
        case rf::Kind::Resistor: return resistance;
        case rf::Kind::Inductor: return inductance;
        case rf::Kind::Capacitor: return capacitance;
        case rf::Kind::Line: return length;
        case rf::Kind::Transformer: return ratio;
        case rf::Kind::Coax:
        case rf::Kind::Microstrip:
            return lengthMode->currentIndex() == 1 ? physical : length;
        case rf::Kind::ParallelRlc:
        case rf::Kind::SeriesRlc:
            return original.tune == rf::Tune::Resistance ? resistance
                : original.tune == rf::Tune::Inductance ? inductance : capacitance;
        case rf::Kind::Measured: break;
        }
        throw std::invalid_argument("Unknown component type.");
    };
    auto* initialValue = focusValue();
    initialValue->setFocus(Qt::TabFocusReason);
    initialValue->selectAll();
    auto result = original;
    buttonBox = buttons(dialog, layout);
    auto* box = buttonBox;
    for (auto* button : box->buttons()) {
        button->setFocusPolicy(Qt::NoFocus);
        if (auto* push = qobject_cast<QPushButton*>(button)) push->setAutoDefault(false);
    }
    solve->setFocusPolicy(Qt::NoFocus);
    dialog.acceptEdit = [&] {
        for (auto* spin : dialog.findChildren<QDoubleSpinBox*>())
            if (spin->isVisible()) spin->interpretText();
        box->button(QDialogButtonBox::Ok)->click();
    };
    updateTabOrder();
    const auto read = [&] {
        auto value = original;
        value.kind = static_cast<rf::Kind>(kind->currentData().toInt());
        value.connection = static_cast<rf::Connection>(connection->currentData().toInt());
        value.tune = static_cast<rf::Tune>(tuning->currentIndex());
        value.resistance = resistance->value();
        value.inductanceNh = inductance->value();
        value.capacitancePf = capacitance->value();
        value.lineOhms = lineOhms->value();
        value.lengthDegrees = length->value();
        value.referenceMHz = reference->value();
        value.turnsRatio = ratio->value();
        value.lengthMode = static_cast<rf::LengthMode>(lengthMode->currentIndex());
        value.velocityMode = static_cast<rf::VelocityMode>(velocityMode->currentIndex());
        value.physicalLengthMm = physical->value();
        value.velocityFactor = velocity->value();
        value.dielectricConstant = dielectric->value();
        value.substrateHeightMm = height->value();
        value.stripWidthMm = width->value();
        value.substrateDk = dk->value();
        value.lossTangent = df->value();
        return value;
    };
    const auto showPreview = [&] {
        if (connection->currentIndex() < 0) return;
        try {
            const auto value = read();
            rf::validate(value);
            box->button(QDialogButtonBox::Ok)->setEnabled(true);
            if (value.kind == rf::Kind::Transformer) {
                preview->setText(QString("Z(source) = %1 x Z(load). Ideal, lossless, frequency-independent.")
                                    .arg(value.turnsRatio * value.turnsRatio, 0, 'g', 8));
            } else if (rf::isTransmissionLine(value.kind)) {
                const auto p = rf::lineParameters(value);
                QString message = QString("Z0: %1 ohm\nElectrical length: %2 deg @ %3 MHz")
                    .arg(p.impedanceOhms, 0, 'g', 6).arg(p.lengthDegrees, 0, 'g', 6)
                    .arg(value.referenceMHz, 0, 'g', 6);
                if (value.kind == rf::Kind::Microstrip) {
                    const auto m = rf::microstrip(value.stripWidthMm, value.substrateHeightMm, value.substrateDk);
                    message += QString("\nEffective Dk: %1\nMatched dielectric loss: %2 dB @ %3 MHz")
                        .arg(m.effectivePermittivity, 0, 'g', 6)
                        .arg(p.attenuationNepers * 20 / std::log(10.0), 0, 'g', 6)
                        .arg(value.referenceMHz, 0, 'g', 6);
                } else {
                    message += "\nIdeal lossless line.";
                }
                preview->setText(message);
            }
        } catch (const std::exception& e) {
            box->button(QDialogButtonBox::Ok)->setEnabled(false);
            results->show();
            preview->setText(QString("Invalid component: %1").arg(e.what()));
        }
    };
    for (auto* spin : dialog.findChildren<QDoubleSpinBox*>())
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, showPreview);
    QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, showPreview);
    QObject::connect(connection, &QComboBox::currentIndexChanged, &dialog, showPreview);
    QObject::connect(lengthMode, &QComboBox::currentIndexChanged, &dialog, [&] { update(); showPreview(); });
    QObject::connect(velocityMode, &QComboBox::currentIndexChanged, &dialog, [&] { update(); showPreview(); });
    QObject::connect(solve, &QPushButton::clicked, &dialog, [&] {
        try {
            const double calculated = rf::microstripWidth(target->value(), height->value(), dk->value());
            if (calculated < width->minimum() || calculated > width->maximum())
                throw std::invalid_argument("Calculated width is outside the editor's supported dimensions.");
            width->setValue(calculated);
            showPreview();
        } catch (const std::exception& e) {
            QMessageBox::warning(&dialog, "Cannot calculate microstrip width", e.what());
        }
    });
    showPreview();
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            result = read();
            rf::validate(result);
            dialog.accept();
        } catch (const std::exception& e) {
            QMessageBox::warning(&dialog, "Invalid component", e.what());
        }
    });
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return result;
}

std::optional<rf::Document> editSettings(QWidget* parent, const rf::Document& original,
                                       const QString& focusField)
{
    QDialog dialog(parent);
    dialog.setObjectName("networkSettings");
    dialog.setWindowTitle("Network setup");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* title = new QLineEdit(QString::fromStdString(original.title));
    title->setObjectName("networkTitle");
    form->addRow("Title", title);
    auto* frequency = field(form, "Frequency (MHz)", original.frequencyMHz, "frequency");
    auto* z0 = field(form, "Chart reference Z0 (ohm)", original.referenceOhms, "z0");
    auto* startR = field(form, "Starting resistance (ohm)", original.startOhms.real(), "startR", 0);
    auto* startX = field(form, "Starting reactance (ohm)", original.startOhms.imag(), "startX", -1e12);
    auto* measuredLoad = new QCheckBox("Use imported one-port load");
    measuredLoad->setObjectName("useMeasuredLoad");
    measuredLoad->setChecked(original.measuredLoad.has_value());
    measuredLoad->setEnabled(original.measuredLoad.has_value());
    form->addRow(measuredLoad);
    const auto loadState = [=] {
        startR->setEnabled(!measuredLoad->isChecked());
        startX->setEnabled(!measuredLoad->isChecked());
    };
    QObject::connect(measuredLoad, &QCheckBox::toggled, &dialog, loadState);
    loadState();
    auto* goalR = field(form, "Goal resistance (ohm)", original.goalOhms.real(), "goalR");
    auto* goalX = field(form, "Goal reactance (ohm)", original.goalOhms.imag(), "goalX", -1e12);
    auto* step = field(form, "Tuning step (%)", original.tunePercent, "step", 0.000001, 99.999);
    std::array<QDoubleSpinBox*, 3> presets{};
    for (int i = 0; i < 3; ++i)
        presets[static_cast<std::size_t>(i)] = field(form, QString("F%1 preset (MHz)").arg(i + 1),
            original.frequencyPresets[static_cast<std::size_t>(i)], QString("preset%1").arg(i + 1));
    auto* box = buttons(dialog, layout);
    auto result = original;
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
        result.title = title->text().toStdString();
        result.frequencyMHz = frequency->value();
        result.referenceOhms = z0->value();
        result.startOhms = {startR->value(), startX->value()};
        if (!measuredLoad->isChecked()) result.measuredLoad.reset();
        result.goalOhms = {goalR->value(), goalX->value()};
        result.tunePercent = step->value();
        for (std::size_t i = 0; i < 3; ++i) result.frequencyPresets[i] = presets[i]->value();
        try {
            rf::validate(result);
            dialog.accept();
        } catch (const std::exception& e) {
            QMessageBox::warning(&dialog, "Invalid network", e.what());
        }
    });
    if (auto* target = dialog.findChild<QDoubleSpinBox*>(focusField); target && !focusField.isEmpty()) {
        target->setFocus();
        target->selectAll();
    }
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return result;
}
