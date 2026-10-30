#include "Synthesis.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace rf {
namespace {
constexpr double pi = std::numbers::pi;

void addSeriesReactance(Document& document, double reactance)
{
    if (reactance == 0) return;
    auto c = defaultComponent(reactance > 0 ? Kind::Inductor : Kind::Capacitor,
                              document.frequencyMHz, document.referenceOhms);
    const double omega = 2 * pi * document.frequencyMHz * 1e6;
    if (reactance > 0) c.inductanceNh = reactance / omega * 1e9;
    else c.capacitancePf = -1 / (omega * reactance) * 1e12;
    validate(c);
    document.components.push_back(c);
}

void finish(Document& document)
{
    document.selected = document.components.empty() ? -1 : 0;
    document.vswrCircle = true;
    validate(document);
}
}

Document synthesizeMatch(const MatchSpecification& s)
{
    // Behavioral reference: legacy smith1View.cpp::OnMatchme (revision a6340fb).
    // Document order is the order of impedance transformations from start to goal,
    // not the left-to-right source-to-load order used in exported schematics.
    if (s.type != MatchType::LowPass && s.type != MatchType::HighPass
        && s.type != MatchType::QuarterWave)
        throw std::invalid_argument("Unknown matching topology.");
    if (s.sections < 1 || s.sections > 10)
        throw std::invalid_argument("MatchMe requires between 1 and 10 sections.");
    Document document;
    document.startOhms = s.startOhms;
    document.goalOhms = s.goalOhms;
    document.frequencyMHz = s.frequencyMHz;
    document.referenceOhms = s.referenceOhms;
    validate(document);
    if (s.startOhms.real() <= 0)
        throw std::invalid_argument("MatchMe requires positive starting resistance; an ideal short or purely reactive impedance cannot be losslessly matched to positive resistance.");
    document.title = std::string("MatchMe: ")
        + (s.type == MatchType::LowPass ? "lumped low-pass"
            : s.type == MatchType::HighPass ? "lumped high-pass" : "quarter-wave transformer")
        + ", " + std::to_string(s.sections) + " section(s)";
    document.qCurves = true;
    if (s.startOhms == s.goalOhms) {
        // The legacy wizard substituted a different load to demonstrate a match.
        // Preserve the user's impedances instead of manufacturing an example.
        finish(document);
        return document;
    }
    // Reduce the problem to real terminations, then restore the goal reactance.
    // Unlike the legacy 2%-of-R threshold, compensate every nonzero reactance.
    addSeriesReactance(document, -s.startOhms.imag());
    const double low = std::min(s.startOhms.real(), s.goalOhms.real());
    const double high = std::max(s.startOhms.real(), s.goalOhms.real());
    std::vector<Component> ladder;
    const double logRatio = std::log(high / low);
    // Equal resistance ratios give every L section the same loaded Q:
    // Q = sqrt(Rhigh/Rlow - 1). expm1 avoids cancellation for nearly equal R.
    const double stepQ = std::sqrt(std::expm1(logRatio / s.sections));
    const double omega = 2 * pi * s.frequencyMHz * 1e6;
    if (high != low) {
        for (int i = 0; i < s.sections; ++i) {
            const double r1 = low * std::exp(logRatio * i / s.sections);
            const double r2 = i == s.sections - 1 ? high
                : low * std::exp(logRatio * (i + 1) / s.sections);
            if (s.type == MatchType::QuarterWave) {
                // At 90 degrees, Zin = Zline^2 / Rload. Each section transforms
                // r1 into r2; its electrical length is referenced to design MHz.
                auto c = defaultComponent(Kind::Line, s.frequencyMHz, s.referenceOhms);
                c.lengthDegrees = 90;
                c.lineOhms = std::sqrt(r1) * std::sqrt(r2);
                ladder.push_back(c);
            } else {
                const bool lowPass = s.type == MatchType::LowPass;
                auto series = defaultComponent(lowPass ? Kind::Inductor : Kind::Capacitor,
                                               s.frequencyMHz, s.referenceOhms);
                auto shunt = defaultComponent(lowPass ? Kind::Capacitor : Kind::Inductor,
                                              s.frequencyMHz, s.referenceOhms);
                shunt.connection = Connection::Shunt;
                // Low-pass: Xseries = Q*r1, Bshunt = Q/r2.
                // High-pass uses the opposite signs with the same magnitudes.
                // Calculations use Hz/H/F; the document stores MHz/nH/pF.
                if (lowPass) {
                    series.inductanceNh = stepQ * r1 / omega * 1e9;
                    shunt.capacitancePf = stepQ / (omega * r2) * 1e12;
                } else {
                    series.capacitancePf = 1 / (omega * stepQ * r1) * 1e12;
                    shunt.inductanceNh = r2 / (omega * stepQ) * 1e9;
                }
                ladder.push_back(series);
                ladder.push_back(shunt);
            }
        }
        // A reciprocal lossless ladder matches in the other direction when its
        // element order is reversed. Reverse only the real-resistance ladder;
        // the start/goal compensating reactances must stay at their own ends.
        if (s.startOhms.real() > s.goalOhms.real())
            std::reverse(ladder.begin(), ladder.end());
    }
    document.components.insert(document.components.end(), ladder.begin(), ladder.end());
    addSeriesReactance(document, s.goalOhms.imag());
    finish(document);
    // Validate the synthesized endpoint, not just individual component ranges:
    // extreme ratios can lose precision even when each element is representable.
    auto endpoint = Impedance::fromOhms(document.startOhms);
    for (const auto& c : document.components)
        endpoint = apply(endpoint, c, document.frequencyMHz);
    if (endpoint.isOpen() || std::abs(endpoint.ohms() - document.goalOhms)
        > 1e-7 * std::max(1.0, std::abs(document.goalOhms)))
        throw std::invalid_argument("The matching network exceeds the supported numerical precision.");
    return document;
}

Document synthesizeFilter(const FilterSpecification& s)
{
    // Behavioral reference: legacy smith1View.cpp::OnSimplefilterwizard.
    // This is its odd-order, equal-termination Chebyshev ladder, not a filter
    // optimizer. Even-order prototypes generally require unequal terminations.
    if (s.type != FilterType::LowPass && s.type != FilterType::HighPass)
        throw std::invalid_argument("Unknown filter type.");
    if (s.order < 3 || s.order > 15)
        throw std::invalid_argument("Filter order must be between 3 and 15 (even orders round up to odd).");
    if (!std::isfinite(s.rippleDb) || s.rippleDb < 1e-4 || s.rippleDb > 10)
        throw std::invalid_argument("Chebyshev passband ripple must be between 0.0001 and 10 dB.");
    Document document;
    document.frequencyMHz = s.cutoffMHz;
    document.referenceOhms = s.impedanceOhms;
    document.startOhms = document.goalOhms = {s.impedanceOhms, 0};
    validate(document);
    const int order = s.order % 2 == 0 ? s.order + 1 : s.order;
    std::vector<double> g(static_cast<std::size_t>(order + 1), 1);
    const double epsilon = std::sqrt(std::expm1(s.rippleDb * std::log(10.0) / 10));
    // |S21|^-2 = 1 + epsilon^2*T_n(Omega)^2. Ripple is insertion loss in dB,
    // not the chart's mismatch-loss metric. gamma and g below are normalized
    // low-pass prototype coefficients (1-ohm terminations, 1-rad/s cutoff).
    // Exact dB conversion replaces the legacy approximation using 17.37.
    const double gamma = std::sinh(std::asinh(1 / epsilon) / order);
    const auto a = [&](int i) { return std::sin((2 * i - 1) * pi / (2 * order)); };
    g[1] = 2 * a(1) / gamma;
    for (int i = 2; i <= order; ++i) {
        const double sine = std::sin((i - 1) * pi / order);
        g[static_cast<std::size_t>(i)] = 4 * a(i - 1) * a(i)
            / ((gamma * gamma + sine * sine) * g[static_cast<std::size_t>(i - 1)]);
    }
    document.title = std::string("Chebyshev ")
        + (s.type == FilterType::LowPass ? "low-pass" : "high-pass")
        + ", order " + std::to_string(order) + ", ripple " + std::to_string(s.rippleDb)
        + " dB, cutoff " + std::to_string(s.cutoffMHz) + " MHz";
    const double omega = 2 * pi * s.cutoffMHz * 1e6;
    for (int i = 1; i <= order; ++i) {
        // "First" means nearest the starting impedance, as in the legacy list.
        // Odd-order equal-termination prototypes are symmetric, so traversal
        // from the load gives the same coefficient sequence as from the source.
        const bool series = (i % 2 == 1) != s.shuntFirst;
        const bool inductive = s.type == FilterType::LowPass ? series : !series;
        auto c = defaultComponent(inductive ? Kind::Inductor : Kind::Capacitor,
                                  s.cutoffMHz, s.impedanceOhms);
        c.connection = series ? Connection::Series : Connection::Shunt;
        const double coefficient = g[static_cast<std::size_t>(i)];
        // LP scaling: L = R0*g/wc, C = g/(R0*wc). The HP transformation
        // interchanges L/C and replaces g with 1/g; connection stays unchanged.
        if (inductive)
            c.inductanceNh = s.impedanceOhms / omega
                * (s.type == FilterType::LowPass ? coefficient : 1 / coefficient) * 1e9;
        else
            c.capacitancePf = 1 / (s.impedanceOhms * omega)
                * (s.type == FilterType::LowPass ? coefficient : 1 / coefficient) * 1e12;
        document.components.push_back(c);
    }
    finish(document);
    return document;
}
}
