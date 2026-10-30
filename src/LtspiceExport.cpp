#include "LtspiceExport.h"

#include "Editors.h"
#include "SmithChartWidget.h"

#include <QSaveFile>
#include <QStringList>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace ltspice {
namespace {
QString number(double value)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("LTspice export exceeded the supported numeric range.");
    return QString::number(value, 'g', 17);
}

QString positive(double value)
{
    if (value <= 0)
        throw std::invalid_argument("An LTspice element value is too small to represent.");
    return number(value);
}

class Schematic
{
public:
    QStringList lines;

    void wire(int x1, int y1, int x2, int y2)
    {
        if (x1 != x2 || y1 != y2)
            lines << QString("WIRE %1 %2 %3 %4").arg(x1).arg(y1).arg(x2).arg(y2);
    }

    void flag(int x, int y, const QString& name = "0")
    {
        lines << QString("FLAG %1 %2 %3").arg(x).arg(y).arg(name);
    }

    void text(int x, int y, QString value, bool directive = false)
    {
        // Each ASC record is one line. User titles must not inject extra records;
        // ';' is an annotation, while '!' introduces a SPICE directive.
        value.replace('\r', ' ').replace('\n', ' ').replace('\t', ' ');
        lines << QString("TEXT %1 %2 Left 2 %3%4")
            .arg(x).arg(y).arg(directive ? "!" : ";", value);
    }

    void passive(const QString& symbol, const QString& name, double value,
                 int x1, int y1, int x2, int y2)
    {
        const bool horizontal = y1 == y2;
        // Standard LTspice symbol pin offsets, not bounding-box dimensions:
        // res/ind pins are (16,16)/(16,96); cap pins are (16,0)/(16,64).
        // R270 rotates the vertical symbol into a left-to-right series element.
        const int offset = symbol == "cap" ? 0 : 16;
        const int length = symbol == "cap" ? 64 : 80;
        lines << QString("SYMBOL %1 %2 %3 %4").arg(symbol)
            .arg(horizontal ? x1 - offset : x1 - 16)
            .arg(horizontal ? y1 + 16 : y1 - offset)
            .arg(horizontal ? "R270" : "R0");
        lines << "SYMATTR InstName " + name << "SYMATTR Value " + positive(value);
        // LTspice otherwise assigns a default series resistance to inductors.
        if (symbol == "ind") lines << "SYMATTR SpiceLine Rser=0";
        wire(horizontal ? x1 + length : x1, horizontal ? y1 : y1 + length, x2, y2);
    }

    void lumped(const rf::Component& c, int index, int x)
    {
        const bool shunt = c.connection == rf::Connection::Shunt;
        const bool compound = c.kind == rf::Kind::SeriesRlc || c.kind == rf::Kind::ParallelRlc;
        const int count = compound ? 3 : 1;
        const QStringList symbols = compound ? QStringList{"res", "ind", "cap"}
            : QStringList{c.kind == rf::Kind::Resistor ? "res"
                : c.kind == rf::Kind::Inductor ? "ind" : "cap"};
        const QStringList prefixes = compound ? QStringList{"R", "L", "C"}
            : QStringList{c.kind == rf::Kind::Resistor ? "R"
                : c.kind == rf::Kind::Inductor ? "L" : "C"};
        const auto value = [&](const QString& prefix) {
            return prefix == "R" ? c.resistance
                : prefix == "L" ? c.inductanceNh * 1e-9 : c.capacitancePf * 1e-12;
        };
        const bool parallel = c.kind == rf::Kind::ParallelRlc;
        // Connection and internal topology are independent: a parallel RLC can
        // be inserted in series, and a series RLC can be a shunt branch.
        // Reserve 480 horizontal units per document component for either case.
        wire(x, 256, x + 32, 256);
        if (shunt) wire(x + 32, 256, x + 480, 256);
        if (parallel && !shunt) {
            wire(x + 32, 128, x + 32, 256);
            wire(x + 32, 256, x + 32, 384);
            wire(x + 416, 128, x + 416, 256);
            wire(x + 416, 256, x + 416, 384);
            wire(x + 416, 256, x + 480, 256);
        }
        for (int i = 0; i < count; ++i) {
            const auto name = prefixes[i] + QString::number(index);
            if (shunt) {
                const int branchX = parallel ? x + 32 + i * 128 : x + 32;
                const int top = parallel ? 256 : 256 + i * 112;
                if (parallel) wire(x + 32, 256, branchX, 256);
                passive(symbols[i], name, value(prefixes[i]),
                        branchX, top, branchX, top + 112);
                if (parallel || i == count - 1) flag(branchX, top + 112);
            } else {
                const int left = parallel ? x + 32 : x + 32 + i * 128;
                const int y = parallel ? 128 + i * 128 : 256;
                passive(symbols[i], name, value(prefixes[i]), left, y,
                        parallel ? x + 416 : (i == count - 1 ? x + 480 : left + 128), y);
            }
        }
    }

    void transformer(const rf::Component& c, int index, int x)
    {
        // E enforces Vprimary=N*Vsecondary; F enforces Isecondary=-N*Iprimary.
        // Dependent sources implement an ideal transformer without magnetizing inductance.
        const auto primary = QString("Eideal%1").arg(index);
        const auto secondary = QString("XFMR%1_OUT").arg(index);
        wire(x, 256, x + 96, 256);
        lines << QString("SYMBOL e %1 240 R0").arg(x + 96)
              << "SYMATTR InstName " + primary
              << "SYMATTR Value " + positive(c.turnsRatio);
        flag(x + 96, 336);
        flag(x + 48, 272, secondary);
        flag(x + 48, 320);
        lines << QString("SYMBOL f %1 256 R0").arg(x + 336)
              << QString("SYMATTR InstName Fideal%1").arg(index)
              << "SYMATTR Value " + primary + " " + number(-c.turnsRatio);
        flag(x + 336, 336);
        flag(x + 336, 256, secondary);
        wire(x + 336, 256, x + 480, 256);
        text(x + 16, 400, "Ideal transformer: dependent-source equivalent, source:load = "
             + number(c.turnsRatio) + ":1.");
    }

    void transmissionLine(const rf::Component& c, int index, int x, double frequencyMHz)
    {
        const auto p = rf::lineParameters(c);
        const bool through = c.connection == rf::Connection::ThroughLine;
        const bool open = c.connection == rf::Connection::OpenStub;
        if (!through) wire(x, 256, x + 480, 256);
        if (p.lengthDegrees == 0) {
            // TD=0 is not a valid ideal-line model. Use the exact limiting
            // circuit: through = wire, open stub = absent, short stub = ground.
            if (through) wire(x, 256, x + 480, 256);
            else if (!open) flag(x, 256);
            return;
        }
        const int y = through ? 256 : 416;
        wire(x, 256, x, y);
        int offset = 0;
        const double alpha = p.attenuationNepers * frequencyMHz / c.referenceMHz;
        if (alpha > 0) {
            // A matched symmetric T pad followed by the ideal line has exactly the
            // model's ABCD at this frequency. The pad is NOT a broadband loss model.
            // Qucs Technical Papers, Attenuators, eqs. (14.6)-(14.9), equal port Z0:
            // https://qucs.sourceforge.net/tech/node95.html
            // With power ratio L=exp(2*alpha), these reduce to series=Z0*tanh(alpha/2)
            // on each side and shunt=Z0/sinh(alpha), where alpha is total Np.
            const double series = p.impedanceOhms * std::tanh(alpha / 2);
            const double shunt = p.impedanceOhms / std::sinh(alpha);
            passive("res", QString("Rloss%1a").arg(index), series, x, y, x + 128, y);
            passive("res", QString("Rloss%1b").arg(index), series, x + 128, y, x + 256, y);
            passive("res", QString("Rloss%1g").arg(index), shunt, x + 128, y, x + 128, y + 112);
            flag(x + 128, y + 112);
            text(x + 16, 600, "Dielectric-loss pad: exact only at exported analysis frequency.");
            offset = 160;
            wire(x + 256, y, x + 288, y);
        } else {
            wire(x, y, x + 128, y);
        }
        lines << QString("SYMBOL tline %1 %2 R0").arg(x + 176 + offset).arg(y + 16);
        // T has two differential ports: ground each return pin. Leave only the
        // far signal pin floating for an open stub; ground it for a short stub.
        // One full cycle takes 1/f, hence delay = (degrees/360)/referenceHz.
        lines << "SYMATTR InstName T" + QString::number(index);
        lines << QString("SYMATTR Value Z0=%1 TD=%2")
            .arg(positive(p.impedanceOhms),
                 positive((p.lengthDegrees / 360.0) / (c.referenceMHz * 1e6)));
        wire(x + 128 + offset, y + 32, x + 128 + offset, y + 64);
        flag(x + 128 + offset, y + 64);
        wire(x + 224 + offset, y + 32, x + 224 + offset, y + 64);
        flag(x + 224 + offset, y + 64);
        if (through) wire(x + 224 + offset, y, x + 480, y);
        else if (!open) flag(x + 224 + offset, y);
    }
};
}

QByteArray schematic(const rf::Document& document)
{
    rf::validate(document);
    if (document.measuredLoad || std::any_of(document.components.begin(), document.components.end(),
        [](const rf::Component& c) { return c.kind == rf::Kind::Measured; }))
        throw std::invalid_argument("LTspice export of measured Touchstone blocks/loads is not supported. Use PNG export instead.");
    Schematic s;
    const int end = 320 + static_cast<int>(document.components.size()) * 480;
    s.lines << "Version 4" << QString("SHEET 1 %1 1160").arg(end + 640);
    s.text(32, 16, QString::fromStdString(document.title));
    s.text(32, 48, QString("RFdude: %1 MHz; chart Z0 = %2 ohm; goal = %3 (annotation only)")
        .arg(number(document.frequencyMHz), number(document.referenceOhms),
             impedanceText(rf::Impedance::fromOhms(document.goalOhms))));
    s.text(32, 80, "Source -> reversed component order -> starting impedance load.");
    s.lines << "SYMBOL voltage 64 240 R0" << "SYMATTR InstName Vdrive"
            << "SYMATTR Value AC 1";
    s.flag(64, 336);
    s.flag(64, 256, "SRC");
    s.wire(64, 256, 128, 256);
    s.passive("res", "Rsource", document.referenceOhms, 128, 256, 320, 256);
    s.flag(320, 256, "IN");
    int x = 320;
    // The RF model starts at its termination and adds elements toward the input.
    // Draw source -> input -> reversed elements -> termination; retaining the
    // original instance numbers keeps the schematic traceable to the chart.
    for (int i = static_cast<int>(document.components.size()); i > 0; --i, x += 480) {
        const auto& c = document.components[static_cast<std::size_t>(i - 1)];
        s.text(x + 16, 656, QString("#%1 %2").arg(i)
            .arg(QString::fromStdString(rf::connectionName(c.connection))));
        QString line;
        int textY = 688;
        for (const auto& word : componentDescription(c).split(' ')) {
            if (!line.isEmpty() && line.size() + word.size() + 1 > 44) {
                s.text(x + 16, textY, line);
                textY += 32;
                line.clear();
            }
            if (!line.isEmpty()) line += ' ';
            line += word;
        }
        s.text(x + 16, textY, line);
        if (rf::isTransmissionLine(c.kind)) s.transmissionLine(c, i, x, document.frequencyMHz);
        else if (c.kind == rf::Kind::Transformer) s.transformer(c, i, x);
        else s.lumped(c, i, x);
    }
    int y = 256;
    if (document.startOhms.real() > 0) {
        s.passive("res", "Rload", document.startOhms.real(), end, y, end, y + 128);
        y += 128;
    }
    const double reactance = document.startOhms.imag();
    if (reactance != 0) {
        // A constant complex impedance is not a physical broadband termination.
        // Realize its reactance with one L or C, exact only at this frequency.
        const double omega = 2 * std::numbers::pi * document.frequencyMHz * 1e6;
        s.passive(reactance > 0 ? "ind" : "cap", reactance > 0 ? "Lload" : "Cload",
                  reactance > 0 ? reactance / omega : -1 / (omega * reactance),
                  end, y, end, y + 128);
        y += 128;
    }
    s.flag(end, y);
    s.text(end + 32, 560, "Starting impedance: "
        + impedanceText(rf::Impedance::fromOhms(document.startOhms)));
    s.text(32, 800, "Reactive load and microstrip loss pads are equivalent only at the exported frequency. No finite-Q component losses.");
    // LTspice defaults complex AC measurements to dB/phase, even for re()/im().
    // Cartesian formatting makes the log report actual ohms and linear Gamma.
    s.text(32, 832, ".options meascplxfmt=cartesian", true);
    const auto frequency = positive(document.frequencyMHz * 1e6);
    s.text(32, 864, ".ac lin 1 " + frequency + " " + frequency, true);
    auto endpoint = rf::Impedance::fromOhms(document.startOhms);
    for (const auto& c : document.components)
        endpoint = rf::apply(endpoint, c, document.frequencyMHz);
    // LTspice defines source current into its positive terminal, so -Vsrc/Isrc
    // is the total impedance. Subtract Rsource to obtain the network impedance.
    // Use SRC because IN can be aliased to ground for an ideal short.
    const auto resistance = number(document.referenceOhms);
    const auto impedance = "-V(SRC)/I(Vdrive)-" + resistance;
    if (endpoint.isOpen()) {
        // Avoid dividing by zero source current for an ideal open. Reflection
        // remains defined, so omit only the finite-impedance measurements.
        s.text(32, 896, "Input is an ideal open at this frequency: no finite Zin. Use GammaRe/GammaIm.");
    } else {
        s.text(32, 896, ".meas AC ZinRe FIND re(" + impedance + ") AT=" + frequency, true);
        s.text(32, 928, ".meas AC ZinIm FIND im(" + impedance + ") AT=" + frequency, true);
    }
    // With Rsource = chart Z0, Gamma = 1 + 2*Isrc*Z0/Vsrc. This expression
    // handles both an open (Isrc=0) and a short without dividing by network Z.
    const auto gamma = "(V(SRC)+2*I(Vdrive)*" + resistance + ")/V(SRC)";
    s.text(32, 960, ".meas AC GammaRe FIND re(" + gamma + ") AT=" + frequency, true);
    s.text(32, 992, ".meas AC GammaIm FIND im(" + gamma + ") AT=" + frequency, true);
    s.text(32, 1040, "Run AC analysis; view impedance (ohms) and reflection coefficient in the SPICE error log.");
    return (s.lines.join("\r\n") + "\r\n").toUtf8();
}

void save(const QString& path, const rf::Document& document)
{
    const auto data = schematic(document);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        throw std::runtime_error(file.errorString().toStdString());
}
}
