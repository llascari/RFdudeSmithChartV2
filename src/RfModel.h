#pragma once

#include <array>
#include <complex>
#include <optional>
#include <string>
#include <vector>

namespace rf {

using Complex = std::complex<double>;

struct SParameters
{
    Complex s11;
    Complex s21;
    Complex s12;
    Complex s22;
    bool operator==(const SParameters&) const = default;
};

struct ResponseSample
{
    double frequencyMHz;
    SParameters s;
    bool operator==(const ResponseSample&) const = default;
};

struct TouchstoneData
{
    std::string name;
    int ports = 2;
    double referenceOhms = 50;
    std::vector<ResponseSample> samples;
    bool operator==(const TouchstoneData&) const = default;
};

// Homogeneous impedance keeps ideal opens and shorts well-defined through a network.
struct Impedance
{
    Complex numerator;
    Complex denominator;

    static Impedance fromOhms(Complex value);
    static Impedance open();
    static Impedance ratio(Complex numerator, Complex denominator);
    Complex reflection(double referenceOhms) const;
    bool isOpen() const;
    Complex ohms() const;
};

enum class Kind { Resistor = 10, Capacitor, Inductor, Line, ParallelRlc, SeriesRlc,
                  Transformer, Coax, Microstrip, Measured };
enum class Connection { Series = 1, Shunt, ThroughLine, OpenStub, ShortStub };
enum class Tune { Resistance, Inductance, Capacitance };
enum class LengthMode { Electrical, Physical };
enum class VelocityMode { Factor, Permittivity };

struct Component
{
    Kind kind = Kind::Capacitor;
    Connection connection = Connection::Series;
    Tune tune = Tune::Capacitance;
    double resistance = 50;
    double capacitancePf = 3.183098861837907;
    double inductanceNh = 7.957747154594767;
    double lineOhms = 50;
    double lengthDegrees = 45;
    double referenceMHz = 1000;
    double turnsRatio = 1; // Source-side turns / load-side turns.
    LengthMode lengthMode = LengthMode::Electrical;
    VelocityMode velocityMode = VelocityMode::Factor;
    double physicalLengthMm = 10;
    double velocityFactor = 0.66;
    double dielectricConstant = 2.25;
    double substrateHeightMm = 1.5;
    double stripWidthMm = 3;
    double substrateDk = 4.2;
    double lossTangent = 0.02;
    TouchstoneData measured;
    bool operator==(const Component&) const = default;
};

struct Document
{
    std::string title = "Untitled network";
    double frequencyMHz = 1000;
    double referenceOhms = 50;
    Complex startOhms = {50, 0};
    Complex goalOhms = {50, 0};
    double tunePercent = 2.5;
    std::array<double, 3> frequencyPresets = {1000, 1000, 1000};
    bool admittance = false;
    bool polar = false;
    bool qCurves = false;
    bool markers = true;
    bool vswrCircle = false;
    int selected = -1;
    std::vector<Component> components;
    std::optional<TouchstoneData> measuredLoad;
    bool operator==(const Document&) const = default;
};

struct Evaluation
{
    std::vector<Impedance> nodes;
    std::vector<std::vector<Complex>> trajectories;
    double maxQ = 0;
    bool qDefined = true;
};

struct MicrostripParameters
{
    double impedanceOhms;
    double effectivePermittivity;
    double dielectricFilling;
};

struct LineParameters
{
    double impedanceOhms;
    double lengthDegrees;
    double attenuationNepers;
};

bool isTransmissionLine(Kind kind);
MicrostripParameters microstrip(double widthMm, double heightMm, double dk);
double microstripWidth(double impedanceOhms, double heightMm, double dk);
LineParameters lineParameters(const Component& component);
Impedance lumpedImpedance(const Component& component, double frequencyMHz);
void validate(const Component& component);
void validate(const Document& document);
Component defaultComponent(Kind kind, double frequencyMHz, double referenceOhms);
Impedance apply(const Impedance& input, const Component& component, double frequencyMHz,
                double fraction = 1);
Evaluation evaluate(const Document& document);
void tune(Component& component, double percent, bool increase);
std::string kindName(Kind kind);
std::string connectionName(Connection connection);

}
