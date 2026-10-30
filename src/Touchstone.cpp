#include "Touchstone.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace rf {
namespace {
[[noreturn]] void fail(const std::string& message)
{
    throw std::invalid_argument("Touchstone: " + message);
}

bool finite(Complex z)
{
    return std::isfinite(z.real()) && std::isfinite(z.imag());
}

double number(std::string token)
{
    std::replace(token.begin(), token.end(), 'D', 'e');
    std::replace(token.begin(), token.end(), 'd', 'e');
    std::istringstream input(token);
    input.imbue(std::locale::classic());
    double value;
    if (!(input >> value) || !input.eof() || !std::isfinite(value))
        fail("invalid numeric token '" + token + "'.");
    return value;
}

SParameters renormalize(SParameters s, int ports, double oldReference, double reference)
{
    // Equal real power-wave references: S' = (S-rI)(I-rS)^-1,
    // r=(Znew-Zold)/(Znew+Zold). Pozar, Microwave Engineering, 4th ed.,
    // Sec. 4.3; avoids converting through singular open/short Z matrices.
    const double r = (reference - oldReference) / (reference + oldReference);
    if (r == 0) return s;
    if (ports == 1) {
        const auto d = 1.0 - r * s.s11;
        if (d == Complex{}) fail("one-port reference conversion is singular.");
        return {(s.s11 - r) / d, 0, 0, 0};
    }
    const auto a = 1.0 - r * s.s11;
    const auto d = 1.0 - r * s.s22;
    const auto determinant = a * d - r * r * s.s12 * s.s21;
    if (determinant == Complex{}) fail("two-port reference conversion is singular.");
    return {((s.s11 - r) * d + r * s.s12 * s.s21) / determinant,
            (1 - r * r) * s.s21 / determinant,
            (1 - r * r) * s.s12 / determinant,
            ((s.s22 - r) * a + r * s.s21 * s.s12) / determinant};
}
}

void validate(const TouchstoneData& data)
{
    if (data.ports != 1 && data.ports != 2) fail("only one or two ports are supported.");
    if (data.name.empty() || data.name.size() > 1024) fail("invalid dataset name.");
    if (!std::isfinite(data.referenceOhms) || data.referenceOhms <= 0 || data.referenceOhms > 1e12)
        fail("reference must be real, positive and at most 1e12 ohms.");
    if (data.samples.empty() || data.samples.size() > 100000)
        fail("dataset must have 1 to 100000 samples.");
    double previous = 0;
    for (const auto& sample : data.samples) {
        if (!std::isfinite(sample.frequencyMHz) || sample.frequencyMHz <= previous
            || sample.frequencyMHz > 1e12)
            fail("frequencies must be positive, strictly increasing and at most 1e12 MHz; noise data is unsupported.");
        previous = sample.frequencyMHz;
        for (const auto s : {sample.s.s11, sample.s.s21, sample.s.s12, sample.s.s22})
            if (!finite(s)) fail("S parameters must be finite.");
        if (data.ports == 1 && (sample.s.s21 != Complex{} || sample.s.s12 != Complex{}
                               || sample.s.s22 != Complex{}))
            fail("one-port data must contain S11 only.");
    }
}

TouchstoneData parseTouchstone(std::string_view text, int ports, const std::string& name)
{
    // Touchstone 1.x option line and two-port order: S11,S21,S12,S22.
    // IBIS Open Forum, Touchstone File Format Specification 2.0,
    // "Compatibility with Touchstone 1.0", "Option Line", "Network Data".
    // https://ibis.org/touchstone_ver2.0/touchstone_ver2_0.pdf
    // Version 2 keyword blocks, noise data, and non-S parameter formats are rejected.
    if (text.size() > 16 * 1024 * 1024) fail("file exceeds the 16 MiB limit.");
    if (ports != 1 && ports != 2) fail("expected .s1p or .s2p.");
    TouchstoneData result{name, ports, 50, {}};
    double frequencyScale = 1000;
    std::string format = "MA";
    bool optionSeen = false;
    std::vector<double> record;
    std::istringstream lines{std::string(text)};
    lines.imbue(std::locale::classic());
    std::string line;
    while (std::getline(lines, line)) {
        if (const auto comment = line.find('!'); comment != std::string::npos) line.erase(comment);
        std::istringstream words(line);
        words.imbue(std::locale::classic());
        std::string token;
        if (!(words >> token)) continue;
        if (token.front() == '[') fail("Touchstone 2 keyword blocks are not supported; export as Touchstone 1.x.");
        if (token.front() == '#') {
            if (optionSeen || !record.empty() || !result.samples.empty())
                fail("option line must occur once, before network data.");
            optionSeen = true;
            if (token != "#") fail("expected whitespace after '#'.");
            bool unitSeen = false, parameterSeen = false, formatSeen = false, referenceSeen = false;
            while (words >> token) {
                for (auto& ch : token) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (token == "HZ" || token == "KHZ" || token == "MHZ" || token == "GHZ") {
                    if (unitSeen) fail("duplicate frequency unit.");
                    unitSeen = true;
                    frequencyScale = token == "HZ" ? 1e-6 : token == "KHZ" ? 1e-3 : token == "MHZ" ? 1 : 1000;
                } else if (token == "S") {
                    if (parameterSeen) fail("duplicate parameter type.");
                    parameterSeen = true;
                } else if (token == "RI" || token == "MA" || token == "DB") {
                    if (formatSeen) fail("duplicate numeric format.");
                    formatSeen = true;
                    format = token;
                } else if (token == "R") {
                    if (referenceSeen || !(words >> token)) fail("invalid reference option.");
                    referenceSeen = true;
                    result.referenceOhms = number(token);
                } else {
                    fail("unsupported option '" + token + "' (only S, RI/MA/DB, real R are supported).");
                }
            }
            continue;
        }
        do {
            record.push_back(number(token));
            const std::size_t recordSize = ports == 1 ? 3 : 9;
            if (record.size() == recordSize) {
                const auto pair = [&](int index) {
                    const double a = record[static_cast<std::size_t>(index)];
                    const double b = record[static_cast<std::size_t>(index + 1)];
                    if (format == "RI") return Complex(a, b);
                    if (format == "MA" && a < 0) fail("negative magnitude.");
                    return std::polar(format == "DB" ? std::pow(10.0, a / 20) : a,
                                      std::remainder(b, 360.0) * std::numbers::pi / 180);
                };
                result.samples.push_back({record[0] * frequencyScale,
                    {pair(1), ports == 2 ? pair(3) : Complex{},
                     ports == 2 ? pair(5) : Complex{}, ports == 2 ? pair(7) : Complex{}}});
                if (result.samples.size() > 100000) fail("more than 100000 samples.");
                record.clear();
            }
        } while (words >> token);
    }
    if (!record.empty()) fail("incomplete network data record.");
    validate(result);
    return result;
}

SParameters measuredParameters(const TouchstoneData& data, double frequency, double reference)
{
    validate(data);
    if (!std::isfinite(reference) || reference <= 0 || reference > 1e12)
        fail("invalid requested reference impedance.");
    if (!std::isfinite(frequency) || frequency < data.samples.front().frequencyMHz
        || frequency > data.samples.back().frequencyMHz)
        fail(data.name + ": frequency is outside the measured range ("
             + std::to_string(data.samples.front().frequencyMHz) + " to "
             + std::to_string(data.samples.back().frequencyMHz) + " MHz). No extrapolation.");
    const auto upper = std::lower_bound(data.samples.begin(), data.samples.end(), frequency,
        [](const ResponseSample& sample, double f) { return sample.frequencyMHz < f; });
    auto s = upper->s;
    if (upper->frequencyMHz != frequency) {
        const auto& lower = *(upper - 1);
        const double t = (frequency - lower.frequencyMHz) / (upper->frequencyMHz - lower.frequencyMHz);
        const auto mix = [t](Complex a, Complex b) { return a + t * (b - a); };
        s = {mix(lower.s.s11, s.s11), mix(lower.s.s21, s.s21),
             mix(lower.s.s12, s.s12), mix(lower.s.s22, s.s22)};
    }
    s = renormalize(s, data.ports, data.referenceOhms, reference);
    for (const auto value : {s.s11, s.s21, s.s12, s.s22})
        if (!finite(value)) fail("reference conversion exceeded the numeric range.");
    return s;
}

Impedance startingImpedance(const Document& document)
{
    return startingImpedance(document, document.frequencyMHz);
}

Impedance startingImpedance(const Document& document, double frequencyMHz)
{
    if (!document.measuredLoad) return Impedance::fromOhms(document.startOhms);
    const auto& data = *document.measuredLoad;
    const auto gamma = measuredParameters(data, frequencyMHz, data.referenceOhms).s11;
    return Impedance::ratio(data.referenceOhms * (1.0 + gamma), 1.0 - gamma);
}

std::pair<double, double> measuredFrequencyRange(const Document& document)
{
    std::pair<double, double> range{0, 1e12};
    const auto include = [&](const TouchstoneData& data) {
        if (data.samples.empty()) fail("empty dataset.");
        range.first = std::max(range.first, data.samples.front().frequencyMHz);
        range.second = std::min(range.second, data.samples.back().frequencyMHz);
    };
    if (document.measuredLoad) include(*document.measuredLoad);
    for (const auto& c : document.components)
        if (c.kind == Kind::Measured) include(c.measured);
    if (range.first > range.second) fail("datasets have no common frequency range.");
    return range;
}
}
