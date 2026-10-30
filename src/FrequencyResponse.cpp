#include "FrequencyResponse.h"
#include "Touchstone.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace rf {
namespace {
void positive(double value, const char* name)
{
    if (!std::isfinite(value) || value <= 0 || value > 1e12)
        throw std::invalid_argument(std::string(name) + " must be positive, finite, and at most 1e12.");
}

SParameters branch(const Impedance& z, bool shunt, double reference)
{
    const Complex n = shunt ? reference * z.denominator : z.numerator / reference;
    const Complex d = shunt ? z.numerator : z.denominator;
    const Complex divisor = 2.0 * d + n;
    if (divisor == Complex{})
        throw std::domain_error("Two-port branch has an undefined scattering matrix.");
    const auto reflection = (shunt ? -n : n) / divisor;
    const auto transmission = 2.0 * d / divisor;
    return {reflection, transmission, transmission, reflection};
}

SParameters componentResponse(const Component& c, double frequency, double reference)
{
    if (c.kind == Kind::Measured)
        return measuredParameters(c.measured, frequency, reference);
    if (c.kind == Kind::Transformer) {
        const double n = c.turnsRatio;
        const double divisor = n + 1 / n;
        const double reflection = (n - 1 / n) / divisor;
        return {reflection, 2 / divisor, 2 / divisor, -reflection};
    }
    if (!isTransmissionLine(c.kind))
        return branch(lumpedImpedance(c, frequency), c.connection == Connection::Shunt, reference);
    if (c.connection != Connection::ThroughLine) {
        auto through = c;
        through.connection = Connection::ThroughLine;
        const auto termination = c.connection == Connection::OpenStub
            ? Impedance::open() : Impedance::fromOhms(0);
        return branch(apply(termination, through, frequency), true, reference);
    }
    const auto p = lineParameters(c);
    const double degrees = p.lengthDegrees * frequency / c.referenceMHz;
    const double alpha = p.attenuationNepers * frequency / c.referenceMHz;
    if (!std::isfinite(degrees) || !std::isfinite(alpha))
        throw std::invalid_argument("Two-port line propagation exceeded the supported numeric range.");
    const double angle = std::remainder(degrees, 360.0) * std::numbers::pi / 180;
    double sine = std::sin(angle);
    double cosine = std::cos(angle);
    if (std::abs(sine) < 1e-14) sine = 0;
    if (std::abs(cosine) < 1e-14) cosine = 0;
    const Complex e = std::exp(-alpha) * Complex(cosine, -sine);
    // ABCD -> S for equal real references, with cosh/sinh multiplied by exp(-gamma*l).
    // This avoids overflow for highly attenuating lines and preserves S21's absolute scale.
    // D. M. Pozar, Microwave Engineering, 4th ed., Wiley, 2012, Sec. 4.4.
    const Complex a = (1.0 + e * e) / 2.0;
    const Complex b = (1.0 - e * e) / 2.0;
    const double ratio = p.impedanceOhms / reference;
    const Complex divisor = 2.0 * a + (ratio + 1 / ratio) * b;
    const Complex reflection = (ratio - 1 / ratio) * b / divisor;
    const Complex transmission = 2.0 * e / divisor;
    return {reflection, transmission, transmission, reflection};
}

SParameters cascade(const SParameters& left, const SParameters& right)
{
    // Scalar Redheffer star product: sum the internal multiple-reflection series.
    // See Pozar, Microwave Engineering, 4th ed., Sec. 4.3 (scattering parameters)
    // and Sec. 4.4 (cascaded networks). Unlike raw ABCD products, this also handles
    // exactly blocking stubs without infinite matrix entries or transmission overflow.
    const Complex divisor = 1.0 - left.s22 * right.s11;
    if (divisor == Complex{}) {
        if (left.s21 == Complex{} && left.s12 == Complex{}
            && right.s21 == Complex{} && right.s12 == Complex{})
            return {left.s11, 0, 0, right.s22};
        throw std::domain_error("Two-port cascade has an indeterminate internal resonance.");
    }
    return {left.s11 + left.s12 * right.s11 * left.s21 / divisor,
            right.s21 * left.s21 / divisor,
            left.s12 * right.s12 / divisor,
            right.s22 + right.s21 * left.s22 * right.s12 / divisor};
}
}

SParameters sParameters(const std::vector<Component>& components, double frequencyMHz,
                        double portOhms)
{
    positive(frequencyMHz, "Frequency");
    positive(portOhms, "Port reference impedance");
    if (components.size() > 1000)
        throw std::invalid_argument("Two-port network may contain at most 1000 components.");
    SParameters result{0, 1, 1, 0};
    for (const auto& c : components) {
        validate(c);
        // Document order is load -> input, so each new element prepends on port 1.
        result = cascade(componentResponse(c, frequencyMHz, portOhms), result);
    }
    for (const auto value : {result.s11, result.s21, result.s12, result.s22})
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
            throw std::domain_error("Two-port response exceeded the supported numeric range.");
    return result;
}

Complex inputReflection(const Document& document, double frequencyMHz, double portOhms)
{
    positive(frequencyMHz, "Frequency");
    auto z = startingImpedance(document, frequencyMHz);
    for (const auto& c : document.components) z = apply(z, c, frequencyMHz);
    return z.reflection(portOhms);
}

void validate(const Sweep& sweep)
{
    positive(sweep.startMHz, "Sweep start");
    positive(sweep.stopMHz, "Sweep stop");
    positive(sweep.portOhms, "Port reference impedance");
    if (sweep.stopMHz <= sweep.startMHz)
        throw std::invalid_argument("Sweep stop must exceed sweep start.");
    if (sweep.points < 2 || sweep.points > 2001)
        throw std::invalid_argument("Sweep must contain between 2 and 2001 points.");
}

std::vector<ResponseSample> frequencyResponse(const std::vector<Component>& components,
                                              const Sweep& sweep)
{
    validate(sweep);
    std::vector<ResponseSample> result;
    result.reserve(static_cast<std::size_t>(sweep.points));
    for (int i = 0; i < sweep.points; ++i) {
        const double fraction = static_cast<double>(i) / (sweep.points - 1);
        const double frequency = i == 0 ? sweep.startMHz : i == sweep.points - 1 ? sweep.stopMHz
            : sweep.logarithmic
                ? std::exp(std::lerp(std::log(sweep.startMHz), std::log(sweep.stopMHz), fraction))
                : std::lerp(sweep.startMHz, sweep.stopMHz, fraction);
        result.push_back({frequency, sParameters(components, frequency, sweep.portOhms)});
    }
    return result;
}

}
