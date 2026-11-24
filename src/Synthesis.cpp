#include "Synthesis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

/// @brief Appends a shunt capacitor or inductor with a given susceptance.
/// @details A positive susceptance is a capacitor (C = B/w) and a negative one is an
/// inductor (L = -1/(w B)), both evaluated at the document frequency. Nothing is
/// added for a zero susceptance.
/// @param document Document that receives the component.
/// @param susceptance Susceptance in siemens.
/// @throws std::invalid_argument If the resulting value is outside the range accepted
/// by validate().
void addShuntSusceptance(Document& document, double susceptance)
{
    if (susceptance == 0) return;
    auto c = defaultComponent(susceptance > 0 ? Kind::Capacitor : Kind::Inductor,
                              document.frequencyMHz, document.referenceOhms);
    c.connection = Connection::Shunt;
    const double omega = 2 * pi * document.frequencyMHz * 1e6;
    if (susceptance > 0) c.capacitancePf = susceptance / omega * 1e12;
    else c.inductanceNh = -1 / (omega * susceptance) * 1e9;
    validate(c);
    document.components.push_back(c);
}

void finish(Document& document)
{
    document.selected = document.components.empty() ? -1 : 0;
    document.vswrCircle = true;
    validate(document);
}

/// @brief Tells whether a topology is built from MatchSpecification::sections.
/// @param type Matching topology.
/// @return True for the lumped low-pass, lumped high-pass and quarter-wave
/// topologies; false for the stub, Pi, Tee and tapped topologies, which ignore the
/// section count.
bool usesSections(MatchType type)
{
    return type == MatchType::LowPass || type == MatchType::HighPass
           || type == MatchType::QuarterWave;
}

/// @brief Tells whether a topology is a stub match.
/// @param type Matching topology.
/// @return True for MatchType::SingleStub and MatchType::DoubleStub.
bool isStub(MatchType type)
{
    return type == MatchType::SingleStub || type == MatchType::DoubleStub;
}

/// @brief Characteristic impedance shared by every line and stub of a stub match.
/// @details It is the goal resistance, so that the admittance at the stub junction
/// becomes 1/goal.
/// @param s Match specification; its goal must be purely resistive.
/// @return Goal resistance in ohms.
/// @throws std::invalid_argument If the goal has a nonzero reactance.
double stubLineOhms(const MatchSpecification& s)
{
    if (s.goalOhms.imag() != 0)
        throw std::invalid_argument("Stub matching requires a purely resistive goal; "
                                    "the lines and stubs use the goal resistance as their impedance.");
    return s.goalOhms.real();
}

/// @brief Builds an ideal through transmission line.
/// @param s Match specification, for the design frequency and reference impedance.
/// @param z0 Characteristic impedance in ohms.
/// @param degrees Electrical length in degrees at the design frequency.
/// @return The line component, in series with the signal path.
Component matchLine(const MatchSpecification& s, double z0, double degrees)
{
    auto line = defaultComponent(Kind::Line, s.frequencyMHz, s.referenceOhms);
    line.lineOhms = z0;
    line.lengthDegrees = degrees;
    return line;
}

/// @brief Builds a shunt stub with a given input susceptance.
/// @details The termination comes from MatchSpecification::stub. An open stub has
/// Y = j tan(l)/z0 and a shorted stub has Y = -j cot(l)/z0. The length is folded into
/// [0, 180) degrees because a negative length is a half wavelength longer.
/// @param s Match specification, for the termination, frequency and reference.
/// @param z0 Characteristic impedance of the stub in ohms.
/// @param susceptance Required input susceptance of the stub in siemens.
/// @return The stub component, as a line with an open or shorted shunt connection.
Component matchStub(const MatchSpecification& s, double z0, double susceptance)
{
    const bool open = s.stub == StubTermination::Open;
    double angle = open ? std::atan(susceptance * z0) : std::atan2(-1.0, susceptance * z0);
    if (angle < 0) angle += pi;
    auto stub = matchLine(s, z0, angle * 180 / pi);
    stub.connection = open ? Connection::OpenStub : Connection::ShortStub;
    return stub;
}

/// @brief Appends a single-stub match: a series line, then a shunt stub.
/// @details The line of length d runs from the (possibly complex) start impedance to
/// the stub. The stub then cancels the susceptance left at that point, so the
/// admittance becomes 1/goal. With t = tan(beta d) and the load Zl = Rl + jXl:
/// t = -Xl/(2 Z0) if Rl = Z0, otherwise t = (Xl + sqrt(Rl ((Z0 - Rl)^2 + Xl^2)/Z0))
/// / (Rl - Z0). It uses the first of the two solutions, as the Qucs-S tool does.
/// @par Reference
/// D. M. Pozar, Microwave Engineering, 4th ed., Wiley, 2012, pp. 234-241; ported from
/// the SingleStub tool of Qucs-S (https://github.com/andresmmera/qucs-s-spar-viewer).
/// @param document Document that receives the line and the stub, in start-to-goal order.
/// @param s Match specification. The goal must be purely resistive.
/// @throws std::invalid_argument If the goal has a nonzero reactance.
void addSingleStub(Document& document, const MatchSpecification& s)
{
    const double z0 = stubLineOhms(s);
    const double rl = s.startOhms.real();
    const double xl = s.startOhms.imag();
    // t = tan(beta*d) makes Re(Y) at the stub equal to 1/z0.
    double t;
    if (rl == z0) {
        t = -xl / (2 * z0);
    } else {
        const double root = std::sqrt(rl / z0 * ((z0 - rl) * (z0 - rl) + xl * xl));
        t = (xl + root) / (rl - z0);
    }
    // Susceptance of the load seen through the line; the stub must cancel it.
    const double b = (rl * rl * t - (z0 - xl * t) * (z0 * t + xl))
                     / (z0 * (rl * rl + (z0 * t + xl) * (z0 * t + xl)));
    double lineAngle = std::atan(t);
    if (lineAngle < 0) lineAngle += pi;
    document.components.push_back(matchLine(s, z0, lineAngle * 180 / pi));
    document.components.push_back(matchStub(s, z0, -b));
}

/// @brief Appends a double-stub match: a shunt stub, a lambda/8 line, a second stub.
/// @details The first stub sits across the load and the second one lambda/8 (45
/// degrees) away. With t = tan(45 deg) = 1, the stub susceptances for a load
/// admittance Gl + jBl are B1 = -Bl + Y0 + sqrt(Gl (2 Y0 - Gl)) and
/// B2 = (Y0 sqrt(Gl (2 Y0 - Gl)) + Gl Y0)/Gl. It uses the first of the two solutions,
/// as the Qucs-S tool does. The radicand is negative, and the load cannot be matched,
/// when Gl exceeds 2 Y0.
/// @par Reference
/// D. M. Pozar, Microwave Engineering, 4th ed., Wiley, 2012, pp. 241-245; ported from
/// the DoubleStub tool of Qucs-S (https://github.com/andresmmera/qucs-s-spar-viewer).
/// @param document Document that receives the two stubs and the line, in
/// start-to-goal order.
/// @param s Match specification. The goal must be purely resistive.
/// @throws std::invalid_argument If the goal has a nonzero reactance, or if the load
/// conductance exceeds twice the goal conductance.
void addDoubleStub(Document& document, const MatchSpecification& s)
{
    const double z0 = stubLineOhms(s);
    const double y0 = 1 / z0;
    const double magnitude = std::norm(s.startOhms);
    const double gl = s.startOhms.real() / magnitude;
    const double bl = -s.startOhms.imag() / magnitude;
    // The lambda/8 spacing gives tan(beta*d) = 1. The radicand is negative when
    // Re(Y_load) exceeds Y0 / sin^2(beta*d) = 2*Y0: the load cannot be matched.
    const double radicand = 2 * gl * y0 - gl * gl;
    if (radicand < 0)
        throw std::invalid_argument("A lambda/8 double stub cannot match this load: its conductance "
                                    "exceeds twice the goal conductance.");
    const double root = std::sqrt(radicand);
    document.components.push_back(matchStub(s, z0, -bl + y0 + root));
    document.components.push_back(matchLine(s, z0, 45));
    document.components.push_back(matchStub(s, z0, (y0 * root + gl * y0) / gl));
}

/// @brief Checks that the loaded Q is high enough for the Pi, Tee and tapped networks.
/// @details These networks are two back-to-back L sections that meet at a virtual
/// resistance Rv. Rv is below both terminations for Pi and above both for Tee, and
/// the tapped networks need the same minimum Q: Q^2 + 1 must exceed Rmax/Rmin.
/// @param s Match specification; its loaded Q is checked.
/// @param rs Goal (source) resistance in ohms.
/// @param rl Start (load) resistance in ohms.
/// @throws std::invalid_argument If Q is not finite or does not exceed
/// sqrt(Rmax/Rmin - 1). The message reports that minimum.
void requireLoadedQ(const MatchSpecification& s, double rs, double rl)
{
    const double ratio = std::max(rs, rl) / std::min(rs, rl);
    if (!std::isfinite(s.q) || s.q * s.q + 1 <= ratio) {
        char text[96];
        std::snprintf(text, sizeof text, "This network needs a loaded Q above %.3f.",
                      std::sqrt(ratio - 1));
        throw std::invalid_argument(text);
    }
}

/// @brief Appends a Pi match: shunt, series, shunt.
/// @details Two L sections meet at a virtual resistance Rv = Rmax/(Q^2 + 1), below both
/// terminations, and the series elements of the two halves share one branch. Each half
/// is low-pass or high-pass, as set by MatchSpecification::halves with the goal-side
/// half first. The load half is solved for a complex start impedance, so the start
/// reactance is absorbed and needs no compensating element. The series reactance of
/// the load half is written as Rv (B2 |Z|^2 - X)/R, which equals the form in the
/// Qucs-S tool without its 1/B2 singularity.
/// @par Reference
/// M. Steer, Microwave and RF Design: Networks, NC State University Libraries, 2019,
/// p. 174; ported from the PiMatching tool of Qucs-S
/// (https://github.com/andresmmera/qucs-s-spar-viewer).
/// @param document Document that receives the elements, in start-to-goal order.
/// @param s Match specification. Uses the loaded Q and the band of each half.
/// @throws std::invalid_argument If the loaded Q is too low (see requireLoadedQ()).
void addPiNetwork(Document& document, const MatchSpecification& s)
{
    const double rs = s.goalOhms.real();
    const double rl = s.startOhms.real();
    const double xl = s.startOhms.imag();
    requireLoadedQ(s, rs, rl);
    const double rv = std::max(rs, rl) / (s.q * s.q + 1);
    const bool sourceLowPass = s.halves == HalfBands::LpLp || s.halves == HalfBands::LpHp;
    const bool loadLowPass = s.halves == HalfBands::LpLp || s.halves == HalfBands::HpLp;
    // Source half, Rv -> Rs: series X1a, then shunt B1 across Rs.
    const double x1a = std::sqrt(rv * (rs - rv));
    const double b1 = std::sqrt((rs - rv) / rv) / rs;
    // Load half, Zl -> Rv: shunt B2 across Zl makes the real part of the parallel
    // combination Rv, and the series X1b cancels its remaining reactance.
    const double magnitude = std::norm(s.startOhms);
    const double root = std::sqrt(rl * (magnitude - rv * rl) / rv);
    const double b2 = (xl + (loadLowPass ? root : -root)) / magnitude;
    const double x1b = rv * (b2 * magnitude - xl) / rl;
    addShuntSusceptance(document, b2);
    addSeriesReactance(document, (sourceLowPass ? x1a : -x1a) + x1b);
    addShuntSusceptance(document, sourceLowPass ? b1 : -b1);
}

/// @brief Appends a Tee match: series, shunt, series.
/// @details Two L sections meet at a virtual resistance Rv = (Q^2 + 1) Rmin, above both
/// terminations, and the shunt elements of the two halves share one branch. Each half
/// is low-pass or high-pass, as set by MatchSpecification::halves with the goal-side
/// half first. The start reactance is absorbed by the load-side series element. The
/// Qucs-S tool adds a stray RS/Rv term to the source-side series reactance, which
/// leaves a small residual reactance at the goal; it is omitted here.
/// @par Reference
/// P. Vizmuller, RF Design Guide: Systems, Circuits and Equations, Artech House,
/// 1995; ported from the TeeMatching tool of Qucs-S
/// (https://github.com/andresmmera/qucs-s-spar-viewer).
/// @param document Document that receives the elements, in start-to-goal order.
/// @param s Match specification. Uses the loaded Q and the band of each half.
/// @throws std::invalid_argument If the loaded Q is too low (see requireLoadedQ()).
void addTeeNetwork(Document& document, const MatchSpecification& s)
{
    const double rs = s.goalOhms.real();
    const double rl = s.startOhms.real();
    const double xl = s.startOhms.imag();
    requireLoadedQ(s, rs, rl);
    const double rv = (s.q * s.q + 1) * std::min(rs, rl);
    const bool sourceLowPass = s.halves == HalfBands::LpLp || s.halves == HalfBands::LpHp;
    const bool loadLowPass = s.halves == HalfBands::LpLp || s.halves == HalfBands::HpLp;
    // Load half, Zl -> Rv: series X2 leaves Rl + j*sqrt(Rl*(Rv - Rl)), then shunt B1b
    // across it gives Rv.
    const double x2 = std::sqrt(rl * (rv - rl));
    const double b1b = std::sqrt((rv - rl) / rl) / rv;
    // Source half, Rv -> Rs: shunt B1a across Rv, then series X1 up to Rs.
    const double x1 = std::sqrt(rs * (rv - rs));
    const double b1a = std::sqrt((rv - rs) / rs) / rv;
    addSeriesReactance(document, (loadLowPass ? x2 : -x2) - xl);
    addShuntSusceptance(document, (sourceLowPass ? b1a : -b1a) + (loadLowPass ? b1b : -b1b));
    addSeriesReactance(document, sourceLowPass ? x1 : -x1);
}

/// @brief Appends a tapped-C or tapped-L match: shunt, series, shunt.
/// @details The loaded Q is that of the shunt element on the higher-resistance side.
/// With Rhigh and Rlow the larger and smaller terminations and
/// Q2 = sqrt(Rlow/Rhigh (Q^2 + 1) - 1):
/// - tapped-C: shunt L = Rhigh/(w Q) on the high side, shunt C2 = Q2/(w Rlow) on the
///   low side and series C1 = C2 (Q2^2 + 1)/(Q Q2 - Q2^2);
/// - tapped-L: shunt C = Q/(w Rhigh) on the high side, shunt L2 = Rlow/(w Q2) on the
///   low side and series L1 = L2 (Q Q2 - Q2^2)/(Q2^2 + 1).
///
/// The two networks differ only by the sign of every susceptance and reactance, which
/// the implementation uses. The start reactance is cancelled first by a lumped
/// series element, because the equations assume a real load.
/// @par Reference
/// Ported from the TappedCMatching and TappedLMatching tools of Qucs-S
/// (https://github.com/andresmmera/qucs-s-spar-viewer).
/// @param document Document that receives the elements, in start-to-goal order.
/// @param s Match specification. Uses the loaded Q and the tapped element.
/// @throws std::invalid_argument If the loaded Q is too low (see requireLoadedQ()).
void addTappedNetwork(Document& document, const MatchSpecification& s)
{
    const double rs = s.goalOhms.real();
    const double rl = s.startOhms.real();
    requireLoadedQ(s, rs, rl);
    const double high = std::max(rs, rl);
    const double low = std::min(rs, rl);
    const double q2 = std::sqrt(low / high * (s.q * s.q + 1) - 1);
    const double sign = s.tap == TapElement::Capacitor ? -1 : 1;
    const double highSusceptance = sign * s.q / high;
    const double lowSusceptance = -sign * q2 / low;
    const double reactance = sign * low * (s.q - q2) / (q2 * q2 + 1);
    addSeriesReactance(document, -s.startOhms.imag());
    addShuntSusceptance(document, rl > rs ? highSusceptance : lowSusceptance);
    addSeriesReactance(document, reactance);
    addShuntSusceptance(document, rl > rs ? lowSusceptance : highSusceptance);
}

/// @brief Appends a double-tapped resonator match: shunt, series, series, shunt.
/// @details From the goal side the network is shunt L1, series L2, series C1 and shunt
/// C2. L2 is the user-chosen tap inductor, and the loaded Q is that of L1 across the
/// goal resistance Rs. With Rl the start resistance and w the design angular frequency:
/// - L1 = Rs/(w Q) and Q2 = sqrt(Rl/Rs (Q^2 + 1) - 1);
/// - C2 = Q2/(w Rl), whose series equivalent is C2' = C2 (1 + Q2^2)/Q2^2;
/// - L1 and Rs have the series equivalent Leq = L1 Q^2/(1 + Q^2) + L2, which resonates
///   with Ceq = 1/(w^2 Leq);
/// - C1 = Ceq C2'/(C2' - Ceq), which is positive only if C2' exceeds Ceq, that is, if
///   L2 is large enough.
///
/// Unlike the tapped-C and tapped-L networks, Q refers to the goal side and no
/// termination ratio is swapped. The start reactance is cancelled first by a lumped
/// series element, because the equations assume a real load.
/// @par Reference
/// Ported from the DoubleTappedResonatorMatching tool of Qucs-S
/// (https://github.com/andresmmera/qucs-s-spar-viewer).
/// @param document Document that receives the elements, in start-to-goal order.
/// @param s Match specification. Uses the loaded Q and the tap inductance.
/// @throws std::invalid_argument If Q is not positive and finite, if it is too low for
/// the termination ratio, if the tap inductance is negative, or if it is too small to
/// give a positive C1 (the message reports the minimum).
void addDoubleTappedNetwork(Document& document, const MatchSpecification& s)
{
    const double rs = s.goalOhms.real();
    const double rl = s.startOhms.real();
    if (!std::isfinite(s.q) || s.q <= 0)
        throw std::invalid_argument("The loaded Q must be positive.");
    if (!std::isfinite(s.tapInductanceNh) || s.tapInductanceNh < 0)
        throw std::invalid_argument("The tap inductance must not be negative.");
    const double radicand = rl / rs * (s.q * s.q + 1) - 1;
    if (radicand <= 0) {
        char text[96];
        std::snprintf(text, sizeof text, "The double-tapped resonator needs a loaded Q above %.3f.",
                      std::sqrt(rs / rl - 1));
        throw std::invalid_argument(text);
    }
    const double omega = 2 * pi * s.frequencyMHz * 1e6;
    const double q2 = std::sqrt(radicand);
    const double l1 = rs / (omega * s.q);
    const double l2 = s.tapInductanceNh * 1e-9;
    const double l1Series = l1 * s.q * s.q / (1 + s.q * s.q);
    const double c2 = q2 / (omega * rl);
    const double c2Series = c2 * (1 + q2 * q2) / (q2 * q2);
    const double ceq = 1 / (omega * omega * (l1Series + l2));
    // C1 grows without bound as C2' approaches Ceq, so a near tie also needs more L2.
    if (c2Series <= ceq * (1 + 1e-9)) {
        char text[96];
        std::snprintf(text, sizeof text, "The tap inductor must exceed %.3f nH.",
                      std::max(0.0, 1 / (omega * omega * c2Series) - l1Series) * 1e9);
        throw std::invalid_argument(text);
    }
    const double c1 = ceq * c2Series / (c2Series - ceq);
    addSeriesReactance(document, -s.startOhms.imag());
    addShuntSusceptance(document, omega * c2);
    addSeriesReactance(document, -1 / (omega * c1));
    addSeriesReactance(document, omega * l2);
    addShuntSusceptance(document, -1 / (omega * l1));
}

/// @brief Characteristic impedances of a binomial multisection quarter-wave transformer.
/// @details The N lines separate N + 1 impedance steps. The logarithm of step k is
/// C(N, k)/2^N of ln(high/low), for k = 0..N, and the last step goes into the high
/// termination. This is the small-reflection design, which is maximally flat around the
/// design frequency. Its weights are symmetric, so it also matches exactly there.
/// @par Reference
/// D. M. Pozar, Microwave Engineering, 4th ed., Wiley, 2012, section 5.7; ported from
/// the MultisectionQuarterWave tool of Qucs-S
/// (https://github.com/andresmmera/qucs-s-spar-viewer).
/// @param sections Number of quarter-wave sections N.
/// @param low Smaller termination in ohms.
/// @param high Larger termination in ohms.
/// @return The N line impedances in ohms, ordered from the low to the high termination.
std::vector<double> binomialLines(int sections, double low, double high)
{
    const double logRatio = std::log(high / low);
    std::vector<double> lines;
    double logImpedance = std::log(low);
    double coefficient = 1;  // C(N, k), updated by C(N, k+1) = C(N, k) (N - k)/(k + 1)
    for (int k = 0; k < sections; ++k) {
        logImpedance += logRatio * coefficient / std::pow(2.0, sections);
        lines.push_back(std::exp(logImpedance));
        coefficient *= double(sections - k) / (k + 1);
    }
    return lines;
}

/// @brief Characteristic impedances of a Chebyshev multisection quarter-wave transformer.
/// @details The reflection response is Gm T_N(sec(theta_m) cos(theta)), with
/// cosh(N acosh(sec(theta_m))) = ln(high/low)/(2 Gm). Expanding T_N in cos(k theta)
/// gives the logarithmic step before each line as Gm times the coefficient of order
/// |N - 2i|, where the centre term (k = 0) counts twice. The coefficients come from the
/// recurrence T(m+1) = 2x T(m) - T(m-1) with x = sec(theta_m) cos(theta), which is
/// exact for any N; the Qucs-S tool only tabulates N up to 7. The response is
/// small-reflection theory with equiripple |Gamma| <= Gm in the passband. Odd N is
/// exactly matched at the design frequency, while even N reaches Gm there.
/// @par Reference
/// D. M. Pozar, Microwave Engineering, 4th ed., Wiley, 2012, section 5.8; ported from
/// the MultisectionQuarterWave tool of Qucs-S
/// (https://github.com/andresmmera/qucs-s-spar-viewer).
/// @param sections Number of quarter-wave sections N.
/// @param low Smaller termination in ohms.
/// @param high Larger termination in ohms.
/// @param maxReflection Maximum passband reflection coefficient Gm.
/// @return The N line impedances in ohms, ordered from the low to the high termination.
/// @throws std::invalid_argument If Gm is not in (0, 1), or if it is at least
/// ln(high/low)/2, because then no transformer is needed.
std::vector<double> chebyshevLines(int sections, double low, double high, double maxReflection)
{
    if (!std::isfinite(maxReflection) || maxReflection <= 0 || maxReflection >= 1)
        throw std::invalid_argument("The Chebyshev maximum reflection coefficient must be between 0 and 1.");
    const double logRatio = std::log(high / low);
    const double argument = logRatio / (2 * maxReflection);
    if (argument <= 1)
        throw std::invalid_argument("The Chebyshev maximum reflection coefficient must be below "
                                    "ln(Rhigh/Rlow)/2; a larger ripple needs no transformer.");
    const double secant = std::cosh(std::acosh(argument) / sections);
    // Coefficients of cos(k theta) in T_N(secant cos(theta)). Since
    // 2 secant cos(theta) cos(k theta) = secant (cos((k+1) theta) + cos((k-1) theta)),
    // multiplying by 2x shifts every coefficient to its two neighbours.
    std::vector<double> previous = {1};
    std::vector<double> current = {0, secant};
    for (int m = 1; m < sections; ++m) {
        std::vector<double> next(m + 2, 0.0);
        for (int k = 0; k <= m; ++k) {
            next[k + 1] += secant * current[k];
            next[std::abs(k - 1)] += secant * current[k];
        }
        for (size_t k = 0; k < previous.size(); ++k)
            next[k] -= previous[k];
        previous = current;
        current = next;
    }
    std::vector<double> lines;
    double logImpedance = std::log(low);
    for (int i = 0; i < sections; ++i) {
        const int order = std::abs(sections - 2 * i);
        logImpedance += maxReflection * current[order] * (order == 0 ? 2 : 1);
        lines.push_back(std::exp(logImpedance));
    }
    return lines;
}

/// @brief Verifies that the synthesized network ends on the goal impedance.
/// @details This checks the whole network, not just the range of each component:
/// extreme ratios can lose precision even when every element is representable.
/// Equiripple designs are not exactly matched at the design frequency, so they are
/// checked against their reflection bound instead.
/// @param document Synthesized document, evaluated from its start impedance.
/// @param allowedReflection Zero to require an exact match; otherwise the largest
/// allowed reflection coefficient at the goal, with a 1% tolerance.
/// @throws std::invalid_argument If the endpoint is open, or differs from the goal by
/// more than 1e-7 of max(1, |goal|) (exact match) or by more than the allowed
/// reflection.
void verifyEndpoint(const Document& document, double allowedReflection = 0)
{
    auto endpoint = Impedance::fromOhms(document.startOhms);
    for (const auto& c : document.components)
        endpoint = apply(endpoint, c, document.frequencyMHz);
    const double error = std::abs(endpoint.ohms() - document.goalOhms);
    const bool exceeds = allowedReflection > 0
                             ? error > (allowedReflection * 1.01 + 1e-7) * std::abs(endpoint.ohms() + document.goalOhms)
                             : error > 1e-7 * std::max(1.0, std::abs(document.goalOhms));
    if (endpoint.isOpen() || exceeds)
        throw std::invalid_argument("The matching network exceeds the supported numerical precision.");
}
}

Document synthesizeMatch(const MatchSpecification& s)
{
    // Behavioral reference: legacy smith1View.cpp::OnMatchme (revision a6340fb).
    // Document order is the order of impedance transformations from start to goal,
    // not the left-to-right source-to-load order used in exported schematics.
    if (s.type != MatchType::LowPass && s.type != MatchType::HighPass
        && s.type != MatchType::QuarterWave && !isStub(s.type) && s.type != MatchType::Pi && s.type != MatchType::Tee
        && s.type != MatchType::Tapped && s.type != MatchType::DoubleTapped)
        throw std::invalid_argument("Unknown matching topology.");
    if (usesSections(s.type) && (s.sections < 1 || s.sections > 10))
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
                        : s.type == MatchType::HighPass ? "lumped high-pass"
                        : s.type == MatchType::QuarterWave ? "quarter-wave transformer"
                        : s.type == MatchType::Pi ? "pi network"
                        : s.type == MatchType::Tee ? "tee network"
                        : s.type == MatchType::Tapped ? (s.tap == TapElement::Capacitor ? "tapped-C network" : "tapped-L network")
                        : s.type == MatchType::DoubleTapped ? "double-tapped resonator"
                                                            : std::string(s.type == MatchType::SingleStub ? "single " : "double ")
                                                                  + (s.stub == StubTermination::Open ? "open" : "shorted") + " stub")
                     + (usesSections(s.type) ? ", " + std::to_string(s.sections) + " section(s)" : "");
    document.qCurves = true;
    if (s.startOhms == s.goalOhms) {
        // The legacy wizard substituted a different load to demonstrate a match.
        // Preserve the user's impedances instead of manufacturing an example.
        finish(document);
        return document;
    }
    if (isStub(s.type)) {
        // The stubs absorb the start reactance, so no compensating element is added.
        if (s.type == MatchType::SingleStub) addSingleStub(document, s);
        else addDoubleStub(document, s);
        finish(document);
        verifyEndpoint(document);
        return document;
    }
    if (s.type == MatchType::Pi || s.type == MatchType::Tee || s.type == MatchType::Tapped
        || s.type == MatchType::DoubleTapped) {
        // Each of these handles the start reactance itself; only the goal reactance is added.
        if (s.type == MatchType::Pi) addPiNetwork(document, s);
        else if (s.type == MatchType::Tee) addTeeNetwork(document, s);
        else if (s.type == MatchType::Tapped) addTappedNetwork(document, s);
        else addDoubleTappedNetwork(document, s);
        addSeriesReactance(document, s.goalOhms.imag());
        finish(document);
        verifyEndpoint(document);
        return document;
    }
    // Reduce the problem to real terminations, then restore the goal reactance.
    // Unlike the legacy 2%-of-R threshold, compensate every nonzero reactance.
    double startR = s.startOhms.real();
    if (s.type == MatchType::QuarterWave && s.startReactance == ReactanceMatch::LambdaEighth
        && s.startOhms.imag() != 0) {
        // A lambda/8 line with Z = |Zstart| turns Zstart into the real resistance
        // R*Z/(Z - X). I. J. Bahl, Fundamentals of RF and Microwave Transistor
        // Amplifiers, Wiley, 2009, pp. 159-160 (as in the Qucs-S Lambda8Lambda4 tool).
        const double magnitude = std::abs(s.startOhms);
        auto line = defaultComponent(Kind::Line, s.frequencyMHz, s.referenceOhms);
        line.lengthDegrees = 45;
        line.lineOhms = magnitude;
        document.components.push_back(line);
        startR = s.startOhms.real() * magnitude / (magnitude - s.startOhms.imag());
    } else {
        addSeriesReactance(document, -s.startOhms.imag());
    }
    const double low = std::min(startR, s.goalOhms.real());
    const double high = std::max(startR, s.goalOhms.real());
    std::vector<Component> ladder;
    const double logRatio = std::log(high / low);
    // Equal resistance ratios give every L section the same loaded Q:
    // Q = sqrt(Rhigh/Rlow - 1). expm1 avoids cancellation for nearly equal R.
    const double stepQ = std::sqrt(std::expm1(logRatio / s.sections));
    const double omega = 2 * pi * s.frequencyMHz * 1e6;
    // One section has a single impedance whatever the weighting, so it is not weighted.
    const bool weighted = s.type == MatchType::QuarterWave && s.weighting != SectionWeighting::EqualRatio
                          && s.sections > 1;
    std::vector<double> weightedZ;
    if (weighted)
        weightedZ = s.weighting == SectionWeighting::Binomial
                        ? binomialLines(s.sections, low, high)
                        : chebyshevLines(s.sections, low, high, s.maxReflection);
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
                c.lineOhms = weighted ? weightedZ[i] : std::sqrt(r1) * std::sqrt(r2);
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
        if (startR > s.goalOhms.real())
            std::reverse(ladder.begin(), ladder.end());
    }
    document.components.insert(document.components.end(), ladder.begin(), ladder.end());
    addSeriesReactance(document, s.goalOhms.imag());
    finish(document);
    verifyEndpoint(document, weighted && s.weighting == SectionWeighting::Chebyshev ? s.maxReflection : 0);
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