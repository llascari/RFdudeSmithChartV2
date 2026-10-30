#include "SynthesisDialogs.h"

#include "Editors.h"
#include "SmithChartWidget.h"
#include "Synthesis.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>

namespace {
QDoubleSpinBox* field(QFormLayout* form, const QString& label, const QString& name,
                      double value, double minimum = 1e-9, double maximum = 1e12)
{
    auto* spin = new QDoubleSpinBox;
    spin->setObjectName(name);
    spin->setDecimals(9);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    form->addRow(label, spin);
    return spin;
}

QComboBox* choices(QFormLayout* form, const QString& label, const QString& name,
                   const QStringList& values)
{
    auto* box = new QComboBox;
    box->setObjectName(name);
    box->addItems(values);
    form->addRow(label, box);
    return box;
}

std::optional<rf::Document> runWizard(QDialog& dialog, QVBoxLayout* layout,
                                     const rf::Document& original,
                                     const std::function<rf::Document()>& generate)
{
    auto* warning = new QLabel("Applying replaces the current network and its start/goal impedances. "
                              "Cancel leaves it unchanged. Replacement can be undone.");
    warning->setWordWrap(true);
    layout->addWidget(warning);
    auto* preview = new QPlainTextEdit;
    preview->setObjectName("synthesisPreview");
    preview->setReadOnly(true);
    preview->setMinimumSize(560, 220);
    layout->addWidget(preview);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Replace network");
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    layout->addWidget(buttons);
    std::optional<rf::Document> result;
    const auto update = [&] {
        try {
            auto generated = generate();
            // Synthesis owns the circuit/specification fields, not unrelated
            // preferences such as tuning step, frequency presets, or polar view.
            // Work on a copy: preview and Cancel must never mutate the document.
            auto next = original;
            next.components = std::move(generated.components);
            next.startOhms = generated.startOhms;
            next.measuredLoad.reset();
            next.goalOhms = generated.goalOhms;
            next.frequencyMHz = generated.frequencyMHz;
            next.referenceOhms = generated.referenceOhms;
            next.title = std::move(generated.title);
            next.selected = generated.selected;
            next.vswrCircle = generated.vswrCircle;
            next.qCurves = original.qCurves || generated.qCurves;
            const auto evaluation = rf::evaluate(next);
            QString text = QString::fromStdString(next.title)
                + QString("\nFrequency: %1 MHz; Z0: %2 ohm\nStart: %3\nGoal: %4\n\n")
                    .arg(next.frequencyMHz, 0, 'g', 9).arg(next.referenceOhms, 0, 'g', 9)
                    .arg(impedanceText(rf::Impedance::fromOhms(next.startOhms)),
                         impedanceText(rf::Impedance::fromOhms(next.goalOhms)));
            if (next.components.empty()) text += "No matching elements required.\n";
            for (std::size_t i = 0; i < next.components.size(); ++i) {
                const auto& c = next.components[i];
                text += QString("%1. %2: %3\n").arg(i + 1)
                    .arg(QString::fromStdString(rf::connectionName(c.connection)), componentDescription(c));
            }
            text += "\nEndpoint: " + impedanceText(evaluation.nodes.back());
            preview->setPlainText(text);
            result = std::move(next);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        } catch (const std::exception& e) {
            // Invalidate any previous successful preview so invalid controls
            // cannot accidentally apply a stale circuit.
            result.reset();
            preview->setPlainText(QString("Cannot synthesize: %1").arg(e.what()));
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        }
    };
    for (auto* spin : dialog.findChildren<QDoubleSpinBox*>())
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, update);
    for (auto* spin : dialog.findChildren<QSpinBox*>())
        QObject::connect(spin, &QSpinBox::valueChanged, &dialog, update);
    for (auto* box : dialog.findChildren<QComboBox*>())
        QObject::connect(box, &QComboBox::currentIndexChanged, &dialog, update);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        // Re-read values after the focused spin box has committed its edit.
        // MainWindow applies this returned document as a single undo operation.
        update();
        if (result) dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return result;
}
}

std::optional<rf::Document> matchWizard(QWidget* parent, const rf::Document& document)
{
    QDialog dialog(parent);
    dialog.setObjectName("matchWizard");
    dialog.setWindowTitle("Simple MatchMe wizard");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* type = choices(form, "Topology", "matchType",
        {"Lumped low-pass", "Lumped high-pass", "Quarter-wave transformer"});
    auto* sections = new QSpinBox;
    sections->setObjectName("matchSections");
    sections->setRange(1, 10);
    sections->setValue(1);
    form->addRow("Sections (1-10)", sections);
    auto* startR = field(form, "Starting resistance (ohm)", "matchStartR", document.startOhms.real(), 0);
    auto* startX = field(form, "Starting reactance (ohm)", "matchStartX", document.startOhms.imag(), -1e12);
    auto* goalR = field(form, "Goal resistance (ohm)", "matchGoalR", document.goalOhms.real());
    auto* goalX = field(form, "Goal reactance (ohm)", "matchGoalX", document.goalOhms.imag(), -1e12);
    auto* frequency = field(form, "Design frequency (MHz)", "matchFrequency", document.frequencyMHz);
    auto* note = new QLabel("Legacy MatchMe: equal-ratio resistance steps, with series reactance "
                           "compensation at the ends. Uses start/goal settings, not the selected row. "
                           "Equal resistances are preserved; no example load is substituted.");
    note->setWordWrap(true);
    layout->addWidget(note);
    return runWizard(dialog, layout, document, [&] {
        rf::MatchSpecification s;
        s.type = static_cast<rf::MatchType>(type->currentIndex());
        s.sections = sections->value();
        s.startOhms = {startR->value(), startX->value()};
        s.goalOhms = {goalR->value(), goalX->value()};
        s.frequencyMHz = frequency->value();
        s.referenceOhms = document.referenceOhms;
        return rf::synthesizeMatch(s);
    });
}

std::optional<rf::Document> filterWizard(QWidget* parent, const rf::Document& document)
{
    QDialog dialog(parent);
    dialog.setObjectName("filterWizard");
    dialog.setWindowTitle("Simple HPF/LPF synthesis wizard");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* type = choices(form, "Chebyshev topology", "filterType", {"Low-pass", "High-pass"});
    auto* orientation = choices(form, "First element (from starting impedance)", "filterFirst",
                                {"Shunt", "Series"});
    auto* cutoff = field(form, "Ripple cutoff (MHz)", "filterCutoff", document.frequencyMHz);
    auto* ripple = field(form, "Passband ripple / loss at cutoff (dB)", "filterRipple", 0.1, 1e-4, 10);
    auto* order = new QSpinBox;
    order->setObjectName("filterOrder");
    order->setRange(3, 15);
    order->setValue(3);
    form->addRow("Order (3-15; even rounds up to odd)", order);
    auto* impedance = field(form, "Termination / chart impedance (ohm)", "filterImpedance",
                            document.referenceOhms);
    auto* note = new QLabel("Legacy simple filter synthesis: odd-order Chebyshev LC ladder, "
                           "equal real start/goal terminations. The preview shows the effective "
                           "odd order. Analysis frequency is set to the ripple cutoff.");
    note->setWordWrap(true);
    layout->addWidget(note);
    return runWizard(dialog, layout, document, [&] {
        rf::FilterSpecification s;
        s.type = static_cast<rf::FilterType>(type->currentIndex());
        s.order = order->value();
        s.cutoffMHz = cutoff->value();
        s.impedanceOhms = impedance->value();
        s.rippleDb = ripple->value();
        s.shuntFirst = orientation->currentIndex() == 0;
        return rf::synthesizeFilter(s);
    });
}
