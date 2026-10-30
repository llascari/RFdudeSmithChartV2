#include "RfModel.h"
#include "Touchstone.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace rf {
namespace {
constexpr double pi = std::numbers::pi;

bool finite(Complex value)
{
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

void positive(double value, const char* name)
{
    if (!std::isfinite(value) || value <= 0 || value > 1e12)
        throw std::invalid_argument(std::string(name) + " must be finite, positive, and at most 1e12.");
}

Impedance parallel(Impedance a, Impedance b)
{
    if (a.numerator == Complex{} || b.numerator == Complex{})
        return Impedance::fromOhms(0);
    return Impedance::ratio(a.numerator * b.numerator,
                           a.numerator * b.denominator + b.numerator * a.denominator);
}

Impedance element(const Component& c, double frequency)
{
    const double omega = 2 * pi * frequency * 1e6;
    const Complex inductive(0, omega * c.inductanceNh * 1e-9);
    const Complex capacitive(0, -1 / (omega * c.capacitancePf * 1e-12));
    switch (c.kind) {
    case Kind::Resistor: return Impedance::fromOhms(c.resistance);
    case Kind::Capacitor: return Impedance::fromOhms(capacitive);
    case Kind::Inductor: return Impedance::fromOhms(inductive);
    case Kind::SeriesRlc: return Impedance::fromOhms(c.resistance + inductive + capacitive);
    case Kind::ParallelRlc:
        return Impedance::ratio(1, 1 / c.resistance + 1.0 / inductive + 1.0 / capacitive);
    case Kind::Line:
    case Kind::Transformer:
    case Kind::Coax:
    case Kind::Microstrip:
    case Kind::Measured: break;
    }
    throw std::invalid_argument("A transmission line is not a lumped element.");
}
}

bool isTransmissionLine(Kind kind)
{
    return kind == Kind::Line || kind == Kind::Coax || kind == Kind::Microstrip;
}

MicrostripParameters microstrip(double widthMm, double heightMm, double dk)
{
    positive(widthMm, "Strip width");
    positive(heightMm, "Substrate height");
    if (!std::isfinite(dk) || dk < 1 || dk > 128)
        throw std::invalid_argument("Substrate Dk must be between 1 and 128.");
    const double u = widthMm / heightMm;
    if (u < 0.01 || u > 100)
        throw std::invalid_argument("Microstrip width/height must be between 0.01 and 100.");
    // Hammerstad-Jensen quasi-static, zero-thickness strip over an infinite ground plane.
    // E. Hammerstad and O. Jensen, "Accurate Models for Microstrip Computer-Aided
    // Design," IEEE MTT-S International Microwave Symposium Digest, 1980, pp. 407-409.
    // https://doi.org/10.1109/MWSYM.1980.1124303
    // Equations reproduced in Qucs Technical Papers, Single microstrip line:
    // impedance (11.4)-(11.6), effective permittivity (11.15)-(11.18).
    // https://qucs.sourceforge.net/tech/node75.html
    // Thickness and dispersion corrections from that reference are not applied here.
    const double u4 = std::pow(u, 4);
    const double a = 1 + std::log((u4 + std::pow(u / 52, 2)) / (u4 + 0.432)) / 49
        + std::log(1 + std::pow(u / 18.1, 3)) / 18.7;
    const double b = 0.564 * std::pow((dk - 0.9) / (dk + 3), 0.053);
    const double filling = (1 + std::pow(1 + 10 / u, -a * b)) / 2;
    const double effective = 1 + (dk - 1) * filling;
    const double f = 6 + (2 * pi - 6) * std::exp(-std::pow(30.666 / u, 0.7528));
    const double impedance = 376.730313668 / (2 * pi * std::sqrt(effective))
        * std::log(f / u + std::sqrt(1 + 4 / (u * u)));
    return {impedance, effective, filling};
}

double microstripWidth(double impedanceOhms, double heightMm, double dk)
{
    positive(impedanceOhms, "Target impedance");
    const double narrow = microstrip(0.01 * heightMm, heightMm, dk).impedanceOhms;
    const double wide = microstrip(100 * heightMm, heightMm, dk).impedanceOhms;
    if (impedanceOhms < wide || impedanceOhms > narrow)
        throw std::invalid_argument("Target impedance is outside the supported width/height range.");
    double low = 0.01;
    double high = 100;
    for (int i = 0; i < 80; ++i) {
        const double middle = (low + high) / 2;
        if (microstrip(middle * heightMm, heightMm, dk).impedanceOhms > impedanceOhms)
            low = middle;
        else
            high = middle;
    }
    return (low + high) / 2 * heightMm;
}

LineParameters lineParameters(const Component& c)
{
    constexpr double lightMmPerSecond = 299792458000.0;
    if (!isTransmissionLine(c.kind))
        throw std::invalid_argument("Component is not a transmission line.");
    double impedance = c.lineOhms;
    double velocity = 1;
    double effective = 1;
    double filling = 0;
    if (c.kind == Kind::Coax) {
        velocity = c.velocityMode == VelocityMode::Factor ? c.velocityFactor
                                                         : 1 / std::sqrt(c.dielectricConstant);
    } else if (c.kind == Kind::Microstrip) {
        const auto parameters = microstrip(c.stripWidthMm, c.substrateHeightMm, c.substrateDk);
        impedance = parameters.impedanceOhms;
        effective = parameters.effectivePermittivity;
        filling = parameters.dielectricFilling;
        velocity = 1 / std::sqrt(effective);
    }
    const double degrees = c.kind != Kind::Line && c.lengthMode == LengthMode::Physical
        ? 360 * c.referenceMHz * 1e6 * c.physicalLengthMm / (lightMmPerSecond * velocity)
        : c.lengthDegrees;
    // Weak-loss quasi-TEM dielectric attenuation: alpha/beta = er*q*tan(delta)/(2*eeff).
    // q is the dielectric filling factor; this remains well-defined at er=1.
    // Qucs Technical Papers, Single microstrip line, dielectric losses, eq. (11.79):
    // alpha_d = (pi/lambda0) * er/sqrt(eeff) * (eeff-1)/(er-1) * tan(delta), Np/m.
    // https://qucs.sourceforge.net/tech/node75.html#SECTION001215100000000000000
    // Multiplying by length and substituting beta*l = degrees*pi/180 gives this
    // expression. It applies only to microstrip; coax remains ideal/lossless.
    const double attenuation = c.kind == Kind::Microstrip
        ? degrees * pi / 180 * c.substrateDk * filling * c.lossTangent / (2 * effective) : 0;
    return {impedance, degrees, attenuation};
}

Impedance Impedance::ratio(Complex n, Complex d)
{
    if (!finite(n) || !finite(d))
        throw std::invalid_argument("Impedance calculation exceeded the supported numeric range.");
    const double scale = std::max(std::abs(n), std::abs(d));
    if (scale == 0)
        throw std::invalid_argument("Indeterminate impedance (zero numerator and denominator).");
    return {n / scale, d / scale};
}

Impedance Impedance::fromOhms(Complex value) { return ratio(value, 1); }
Impedance Impedance::open() { return {1, 0}; }
bool Impedance::isOpen() const { return std::abs(denominator) < 1e-14; }
Complex Impedance::ohms() const
{
    if (isOpen())
        throw std::domain_error("An ideal open circuit has no finite impedance.");
    return numerator / denominator;
}

Complex Impedance::reflection(double referenceOhms) const
{
    positive(referenceOhms, "Reference impedance");
    const auto divisor = numerator + referenceOhms * denominator;
    if (divisor == Complex{})
        throw std::domain_error("Reflection coefficient is undefined at this impedance.");
    return (numerator - referenceOhms * denominator) / divisor;
}

void validate(const Component& c)
{
    if (c.kind < Kind::Resistor || c.kind > Kind::Measured)
        throw std::invalid_argument("Unknown component type.");
    if (c.tune < Tune::Resistance || c.tune > Tune::Capacitance)
        throw std::invalid_argument("Unknown tuning parameter.");
    const bool line = isTransmissionLine(c.kind);
    if ((line && (c.connection < Connection::ThroughLine || c.connection > Connection::ShortStub))
        || ((c.kind == Kind::Transformer || c.kind == Kind::Measured) && c.connection != Connection::ThroughLine)
        || (!line && c.kind != Kind::Transformer && c.kind != Kind::Measured
            && c.connection != Connection::Series && c.connection != Connection::Shunt))
        throw std::invalid_argument("Component type and connection are incompatible.");
    if (c.kind == Kind::Measured) {
        validate(c.measured);
        if (c.measured.ports != 2)
            throw std::invalid_argument("A measured circuit block requires two-port data.");
    }
    positive(c.resistance, "Resistance");
    positive(c.capacitancePf, "Capacitance");
    positive(c.inductanceNh, "Inductance");
    positive(c.lineOhms, "Line impedance");
    positive(c.referenceMHz, "Line reference frequency");
    if (!std::isfinite(c.lengthDegrees) || c.lengthDegrees < 0 || c.lengthDegrees > 36000)
        throw std::invalid_argument("Line length must be between 0 and 36000 degrees.");
    if (!std::isfinite(c.turnsRatio) || c.turnsRatio < 1e-6 || c.turnsRatio > 1e6)
        throw std::invalid_argument("Turns ratio must be between 1e-6 and 1e6.");
    if (c.lengthMode < LengthMode::Electrical || c.lengthMode > LengthMode::Physical
        || c.velocityMode < VelocityMode::Factor || c.velocityMode > VelocityMode::Permittivity)
        throw std::invalid_argument("Unknown line input mode.");
    if (!std::isfinite(c.physicalLengthMm) || c.physicalLengthMm < 0 || c.physicalLengthMm > 1e9)
        throw std::invalid_argument("Physical length must be between 0 and 1e9 mm.");
    if (!std::isfinite(c.velocityFactor) || c.velocityFactor <= 0 || c.velocityFactor > 1)
        throw std::invalid_argument("Velocity factor must be greater than zero and at most 1.");
    if (!std::isfinite(c.dielectricConstant) || c.dielectricConstant < 1 || c.dielectricConstant > 128)
        throw std::invalid_argument("Coax dielectric constant must be between 1 and 128.");
    if (!std::isfinite(c.lossTangent) || c.lossTangent < 0 || c.lossTangent > 0.1)
        throw std::invalid_argument("Microstrip loss tangent must be between 0 and 0.1.");
    positive(c.stripWidthMm, "Strip width");
    positive(c.substrateHeightMm, "Substrate height");
    if (!std::isfinite(c.substrateDk) || c.substrateDk < 1 || c.substrateDk > 128)
        throw std::invalid_argument("Substrate Dk must be between 1 and 128.");
    if (line) {
        const auto parameters = lineParameters(c);
        if (!std::isfinite(parameters.lengthDegrees) || parameters.lengthDegrees > 36000)
            throw std::invalid_argument("Derived line length must be at most 36000 degrees at its reference frequency.");
    }
}

void validate(const Document& d)
{
    positive(d.frequencyMHz, "Frequency");
    positive(d.referenceOhms, "Reference impedance");
    if (!finite(d.startOhms) || d.startOhms.real() < 0 || std::abs(d.startOhms) > 1e12
        || !finite(d.goalOhms) || d.goalOhms.real() <= 0 || std::abs(d.goalOhms) > 1e12)
        throw std::invalid_argument("Start must have nonnegative resistance; goal must have positive resistance. Impedances must be finite and at most 1e12 ohms.");
    if (!std::isfinite(d.tunePercent) || d.tunePercent <= 0 || d.tunePercent >= 100)
        throw std::invalid_argument("Tuning step must be greater than 0 and less than 100 percent.");
    for (const double preset : d.frequencyPresets)
        positive(preset, "Frequency preset");
    if (d.components.size() > 1000)
        throw std::invalid_argument("A network may contain at most 1000 components.");
    if (d.selected < -1 || d.selected >= static_cast<int>(d.components.size())
        || (!d.components.empty() && d.selected < 0))
        throw std::invalid_argument("Selected component index is invalid.");
    for (const auto& c : d.components)
        validate(c);
    if (d.measuredLoad) {
        validate(*d.measuredLoad);
        if (d.measuredLoad->ports != 1)
            throw std::invalid_argument("A measured load requires one-port data.");
    }
    const auto range = measuredFrequencyRange(d);
    if (d.frequencyMHz < range.first || d.frequencyMHz > range.second)
        throw std::invalid_argument("Analysis frequency is outside the common Touchstone frequency range.");
}

Component defaultComponent(Kind kind, double frequencyMHz, double referenceOhms)
{
    positive(frequencyMHz, "Frequency");
    positive(referenceOhms, "Reference impedance");
    Component c;
    c.kind = kind;
    c.resistance = c.lineOhms = referenceOhms;
    c.referenceMHz = frequencyMHz;
    c.capacitancePf = 1e12 / (2 * pi * frequencyMHz * 1e6 * referenceOhms);
    c.inductanceNh = referenceOhms * 1e9 / (2 * pi * frequencyMHz * 1e6);
    c.tune = kind == Kind::Inductor ? Tune::Inductance : Tune::Resistance;
    if (kind == Kind::Capacitor)
        c.tune = Tune::Capacitance;
    if (isTransmissionLine(kind) || kind == Kind::Transformer || kind == Kind::Measured)
        c.connection = Connection::ThroughLine;
    if (kind == Kind::Microstrip) {
        c.lengthMode = LengthMode::Physical;
    }
    return c;
}

Impedance lumpedImpedance(const Component& component, double frequencyMHz)
{
    validate(component);
    positive(frequencyMHz, "Frequency");
    return element(component, frequencyMHz);
}

Impedance apply(const Impedance& input, const Component& c, double frequency, double fraction)
{
    validate(c);
    positive(frequency, "Frequency");
    if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
        throw std::invalid_argument("Trajectory fraction must be between 0 and 1.");
    if (fraction == 0)
        return input;
    if (c.kind == Kind::Measured) {
        if (fraction != 1)
            throw std::invalid_argument("A measured block has endpoints only, not a fractional circuit trajectory.");
        const auto s = measuredParameters(c.measured, frequency, c.measured.referenceOhms);
        const auto gamma = input.reflection(c.measured.referenceOhms);
        const auto d = 1.0 - s.s22 * gamma;
        const auto n = s.s11 * d + s.s12 * s.s21 * gamma;
        if (d == Complex{} && s.s12 * s.s21 == Complex{})
            return Impedance::ratio(c.measured.referenceOhms * (1.0 + s.s11), 1.0 - s.s11);
        return Impedance::ratio(c.measured.referenceOhms * (d + n), d - n);
    }
    if (c.kind == Kind::Transformer) {
        const double ratio = std::pow(c.turnsRatio, 2 * fraction);
        return Impedance::ratio(input.numerator * ratio, input.denominator);
    }
    if (!isTransmissionLine(c.kind)) {
        auto z = element(c, frequency);
        if (c.connection == Connection::Series)
            return Impedance::ratio(input.numerator * z.denominator
                                        + fraction * z.numerator * input.denominator,
                                    input.denominator * z.denominator);
        z = Impedance::ratio(z.numerator, z.denominator * fraction);
        return parallel(input, z);
    }
    // A shorted stub starts at 90 degrees (open), approaching its specified length.
    const auto parameters = lineParameters(c);
    const double z0 = parameters.impedanceOhms;
    const double fullAngle = parameters.lengthDegrees * frequency / c.referenceMHz;
    const double degrees = c.connection == Connection::ShortStub
        ? 90 + fraction * (fullAngle - 90) : fraction * fullAngle;
    const double angle = std::remainder(degrees, 360.0) * pi / 180;
    const double attenuation = fraction * parameters.attenuationNepers * frequency / c.referenceMHz;
    if (!std::isfinite(attenuation) || !std::isfinite(fullAngle))
        throw std::invalid_argument("Line propagation exceeded the supported numeric range.");
    if (attenuation > 0) {
        // Uniform-line ABCD: A=D=cosh(gamma*l), B=Z0*sinh(gamma*l),
        // C=sinh(gamma*l)/Z0; Zin=(A*Zload+B)/(C*Zload+D).
        // D. M. Pozar, Microwave Engineering, 4th ed., Wiley, 2012,
        // Ch. 2 (transmission lines) and Sec. 4.4 (ABCD transmission matrix).
        // Divide ABCD by cosh(gamma*l), using tanh to avoid overflow on very long lossy lines.
        const auto t = std::tanh(Complex(attenuation, angle));
        if (c.connection == Connection::ThroughLine)
            return Impedance::ratio(input.numerator + z0 * t * input.denominator,
                                    input.denominator + t * input.numerator / z0);
        const auto stub = c.connection == Connection::OpenStub
            ? Impedance::ratio(z0, t) : Impedance::fromOhms(z0 * t);
        return parallel(input, stub);
    }
    double sine = std::sin(angle);
    double cosine = std::cos(angle);
    if (std::abs(sine) < 1e-14) sine = 0;
    if (std::abs(cosine) < 1e-14) cosine = 0;
    const Complex j(0, 1);
    if (c.connection == Connection::ThroughLine)
        return Impedance::ratio(z0 * (input.numerator * cosine
                                    + j * z0 * input.denominator * sine),
                                z0 * input.denominator * cosine
                                    + j * input.numerator * sine);
    const auto stub = c.connection == Connection::OpenStub
        ? Impedance::ratio(z0 * cosine, j * sine)
        : Impedance::ratio(j * z0 * sine, cosine);
    return parallel(input, stub);
}

Evaluation evaluate(const Document& d)
{
    validate(d);
    Evaluation result;
    auto input = startingImpedance(d);
    result.nodes.push_back(input);
    auto updateQ = [&result](const Impedance& z) {
        if (z.isOpen())
            result.maxQ = std::numeric_limits<double>::infinity();
        else {
            const auto value = z.ohms();
            if (value.real() < 0) result.qDefined = false;
            const double q = value.real() > 1e-12 ? std::abs(value.imag()) / value.real()
                : (std::abs(value.imag()) > 1e-12 ? std::numeric_limits<double>::infinity() : 0);
            result.maxQ = std::max(result.maxQ, q);
        }
    };
    updateQ(input);
    for (const auto& c : d.components) {
        auto& path = result.trajectories.emplace_back();
        const double angle = isTransmissionLine(c.kind)
            ? lineParameters(c).lengthDegrees * d.frequencyMHz / c.referenceMHz : 0;
        const int steps = c.kind == Kind::Measured ? 1
            : static_cast<int>(std::clamp(std::abs(angle) * 4, 256.0, 8192.0));
        path.reserve(static_cast<std::size_t>(steps + 1));
        for (int i = 0; i <= steps; ++i) {
            const auto z = apply(input, c, d.frequencyMHz, static_cast<double>(i) / steps);
            path.push_back(z.reflection(d.referenceOhms));
            updateQ(z);
        }
        input = apply(input, c, d.frequencyMHz);
        result.nodes.push_back(input);
    }
    return result;
}

void tune(Component& c, double percent, bool increase)
{
    if (c.kind == Kind::Measured)
        throw std::invalid_argument("Measured S parameters cannot be tuned. Tune frequency or adjacent components instead.");
    if (!std::isfinite(percent) || percent <= 0 || percent >= 100)
        throw std::invalid_argument("Invalid tuning step.");
    auto next = c;
    double* value = &next.resistance;
    if (c.kind == Kind::Capacitor) value = &next.capacitancePf;
    else if (c.kind == Kind::Inductor) value = &next.inductanceNh;
    else if (c.kind == Kind::Transformer) value = &next.turnsRatio;
    else if (isTransmissionLine(c.kind))
        value = c.kind != Kind::Line && c.lengthMode == LengthMode::Physical
            ? &next.physicalLengthMm : &next.lengthDegrees;
    else if (c.kind == Kind::SeriesRlc || c.kind == Kind::ParallelRlc) {
        if (c.tune == Tune::Inductance) value = &next.inductanceNh;
        if (c.tune == Tune::Capacitance) value = &next.capacitancePf;
    }
    *value *= 1 + (increase ? percent : -percent) / 100;
    validate(next);
    c = next;
}

std::string kindName(Kind kind)
{
    switch (kind) {
    case Kind::Resistor: return "Resistor";
    case Kind::Capacitor: return "Capacitor";
    case Kind::Inductor: return "Inductor";
    case Kind::Line: return "Transmission line";
    case Kind::ParallelRlc: return "Parallel RLC";
    case Kind::SeriesRlc: return "Series RLC";
    case Kind::Transformer: return "Ideal transformer";
    case Kind::Coax: return "Coaxial cable";
    case Kind::Microstrip: return "Microstrip";
    case Kind::Measured: return "Touchstone two-port";
    }
    throw std::invalid_argument("Unknown component type.");
}

std::string connectionName(Connection c)
{
    switch (c) {
    case Connection::Series: return "Series";
    case Connection::Shunt: return "Shunt";
    case Connection::ThroughLine: return "Through";
    case Connection::OpenStub: return "Open stub";
    case Connection::ShortStub: return "Shorted stub";
    }
    throw std::invalid_argument("Unknown connection.");
}
}
