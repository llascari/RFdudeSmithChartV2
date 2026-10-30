#include "FrequencyResponse.h"
#include "Synthesis.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
bool near(rf::Complex a, rf::Complex b, double tolerance = 1e-10)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}

rf::SParameters response(const rf::Component& c, double frequency = 1000, double reference = 50)
{
    return rf::sParameters({c}, frequency, reference);
}
}

class FrequencyResponseTests final : public QObject
{
    Q_OBJECT

private slots:
    void throughAndReferences()
    {
        const auto empty = rf::sParameters({}, 1000, 50);
        QCOMPARE(empty.s11, rf::Complex{});
        QCOMPARE(empty.s22, rf::Complex{});
        QCOMPARE(empty.s21, rf::Complex(1));
        QCOMPARE(empty.s12, rf::Complex(1));
        auto resistor = rf::defaultComponent(rf::Kind::Resistor, 1000, 50);
        const auto s = response(resistor);
        QVERIFY(near(s.s11, 1.0 / 3));
        QVERIFY(near(s.s21, 2.0 / 3));
        QVERIFY(near(s.s22, s.s11));
        QVERIFY(near(s.s12, s.s21));
        QVERIFY(near(response(resistor, 1000, 75).s11, 0.25));
        resistor.connection = rf::Connection::Shunt;
        QVERIFY(near(response(resistor).s11, -1.0 / 3));
        QVERIFY(near(response(resistor).s21, 2.0 / 3));
    }

    void transformerRatioAndPhase()
    {
        auto transformer = rf::defaultComponent(rf::Kind::Transformer, 1000, 50);
        transformer.turnsRatio = 2;
        const auto s = response(transformer);
        QVERIFY(near(s.s11, 0.6));
        QVERIFY(near(s.s22, -0.6));
        QVERIFY(near(s.s21, 0.8));
        transformer.turnsRatio = 0.5;
        QVERIFY(near(response(transformer).s11, -0.6));
        auto line = rf::defaultComponent(rf::Kind::Line, 1000, 50);
        line.lengthDegrees = 90;
        const auto quarter = response(line);
        QVERIFY(near(quarter.s11, 0));
        QVERIFY(near(quarter.s21, {0, -1}));
        QVERIFY(near(response(line, 2000).s21, -1));
        line.lineOhms = 100;
        QVERIFY(near(response(line).s11, 0.6));
        QVERIFY(near(response(line).s21, {0, -0.8}));
        QVERIFY(near(std::norm(response(line).s11) + std::norm(response(line).s21), 1));
    }

    void blockingStubsAndResonance()
    {
        auto line = rf::defaultComponent(rf::Kind::Line, 1000, 50);
        for (const auto connection : {rf::Connection::OpenStub, rf::Connection::ShortStub}) {
            line.connection = connection;
            line.lengthDegrees = connection == rf::Connection::OpenStub ? 90 : 0;
            const auto s = response(line);
            QVERIFY(near(s.s11, -1));
            QCOMPARE(s.s21, rf::Complex{});
            line.lengthDegrees = connection == rf::Connection::OpenStub ? 0 : 90;
            QVERIFY(near(response(line).s11, 0));
            QVERIFY(near(response(line).s21, 1));
        }
        line.connection = rf::Connection::OpenStub;
        line.lengthDegrees = 90;
        auto half = rf::defaultComponent(rf::Kind::Line, 1000, 50);
        half.lengthDegrees = 180;
        const auto blocked = rf::sParameters({line, half, line}, 1000, 50);
        QVERIFY(near(blocked.s11, -1));
        QVERIFY(near(blocked.s22, -1));
        QCOMPARE(blocked.s21, rf::Complex{});
    }

    void dielectricLossAndLongCascades()
    {
        auto microstrip = rf::defaultComponent(rf::Kind::Microstrip, 1000, 50);
        microstrip.stripWidthMm = rf::microstripWidth(50, 1.5, 4.2);
        microstrip.physicalLengthMm = 100;
        const auto p = rf::lineParameters(microstrip);
        for (const double f : {100.0, 1000.0, 10000.0}) {
            const auto s = response(microstrip, f);
            const auto expected = std::exp(rf::Complex(-p.attenuationNepers * f / 1000,
                -p.lengthDegrees * std::numbers::pi / 180 * f / 1000));
            QVERIFY(near(s.s21, expected));
            QVERIFY(near(s.s11, 0));
            QVERIFY(near(s.s22, 0));
        }
        std::vector<rf::Component> components(1000, microstrip);
        const auto attenuated = rf::sParameters(components, 1000, 50);
        QVERIFY(std::abs(attenuated.s21) < 1e-12);
        QVERIFY(near(attenuated.s11, 0));
        microstrip.lossTangent = 0;
        QVERIFY(near(std::abs(response(microstrip).s21), 1));
        microstrip.physicalLengthMm = 10000;
        microstrip.lossTangent = 0.1;
        const auto veryLossy = response(microstrip, 1e9);
        QCOMPARE(veryLossy.s21, rf::Complex{});
        QVERIFY(near(veryLossy.s11, 0));
    }

    void allElementsMatchTerminatedModel()
    {
        const rf::Complex load(73, -21);
        const double reference = 61;
        for (int kind = int(rf::Kind::Resistor); kind <= int(rf::Kind::Microstrip); ++kind) {
            const auto k = static_cast<rf::Kind>(kind);
            const auto connections = rf::isTransmissionLine(k)
                ? std::vector{rf::Connection::ThroughLine, rf::Connection::OpenStub, rf::Connection::ShortStub}
                : k == rf::Kind::Transformer ? std::vector{rf::Connection::ThroughLine}
                : std::vector{rf::Connection::Series, rf::Connection::Shunt};
            for (const auto connection : connections) {
                auto c = rf::defaultComponent(k, 1200, 50);
                c.connection = connection;
                c.turnsRatio = 2.3;
                for (const double frequency : {1.0, 600.0, 1200.0, 5000.0}) {
                    const auto s = response(c, frequency, reference);
                    const auto gammaLoad = rf::Impedance::fromOhms(load).reflection(reference);
                    const auto gammaIn = s.s11 + s.s12 * s.s21 * gammaLoad / (1.0 - s.s22 * gammaLoad);
                    QVERIFY(near(gammaIn, rf::apply(rf::Impedance::fromOhms(load), c, frequency)
                                             .reflection(reference)));
                    QVERIFY(near(s.s12, s.s21));
                    QVERIFY(std::norm(s.s11) + std::norm(s.s21) <= 1 + 1e-10);
                    QVERIFY(std::norm(s.s22) + std::norm(s.s12) <= 1 + 1e-10);
                }
            }
        }
    }

    void cascadeOrientation()
    {
        std::vector<rf::Component> components;
        for (const auto kind : {rf::Kind::Capacitor, rf::Kind::Transformer, rf::Kind::Inductor,
                                rf::Kind::Line, rf::Kind::Microstrip}) {
            auto c = rf::defaultComponent(kind, 1000, 50);
            if (kind == rf::Kind::Inductor) c.connection = rf::Connection::Shunt;
            c.turnsRatio = 2;
            components.push_back(c);
        }
        for (const double frequency : {100.0, 1000.0, 5000.0}) {
            auto z = rf::Impedance::fromOhms(50);
            for (const auto& c : components) z = rf::apply(z, c, frequency);
            const auto s = rf::sParameters(components, frequency, 50);
            QVERIFY(near(s.s11, z.reflection(50)));
            auto reverse = components;
            std::reverse(reverse.begin(), reverse.end());
            for (auto& c : reverse)
                if (c.kind == rf::Kind::Transformer) c.turnsRatio = 1 / c.turnsRatio;
            const auto reversed = rf::sParameters(reverse, frequency, 50);
            QVERIFY(near(s.s22, reversed.s11));
            QVERIFY(near(s.s11, reversed.s22));
            QVERIFY(near(s.s21, reversed.s21));
        }
    }

    void filterResponse()
    {
        for (const auto type : {rf::FilterType::LowPass, rf::FilterType::HighPass}) {
            rf::FilterSpecification specification;
            specification.type = type;
            specification.order = 5;
            specification.rippleDb = 0.5;
            specification.cutoffMHz = 1000;
            const auto d = rf::synthesizeFilter(specification);
            for (const double normalized : {0.1, 0.5, 0.9, 1.0, 1.5, 3.0}) {
                const double x = type == rf::FilterType::LowPass ? normalized : 1 / normalized;
                const double t = x <= 1 ? std::cos(5 * std::acos(x)) : std::cosh(5 * std::acosh(x));
                const double expected = 1 / (1 + (std::pow(10, 0.05) - 1) * t * t);
                const auto s = rf::sParameters(d.components, 1000 * normalized, d.referenceOhms);
                QVERIFY(std::abs(std::norm(s.s21) - expected) < 1e-10);
                QVERIFY(near(std::norm(s.s11) + std::norm(s.s21), 1));
            }
        }
    }

    void sweepAndValidation()
    {
        rf::Sweep sweep{10, 1000, 3, true, 50};
        const auto log = rf::frequencyResponse({}, sweep);
        QCOMPARE(log.size(), std::size_t(3));
        QCOMPARE(log.front().frequencyMHz, 10.0);
        QVERIFY(std::abs(log[1].frequencyMHz - 100) < 1e-10);
        QCOMPARE(log.back().frequencyMHz, 1000.0);
        sweep.logarithmic = false;
        QCOMPARE(rf::frequencyResponse({}, sweep)[1].frequencyMHz, 505.0);
        sweep.stopMHz = sweep.startMHz;
        QVERIFY_EXCEPTION_THROWN(rf::frequencyResponse({}, sweep), std::invalid_argument);
        sweep.stopMHz = 1000;
        sweep.points = 2002;
        QVERIFY_EXCEPTION_THROWN(rf::frequencyResponse({}, sweep), std::invalid_argument);
        sweep.points = 1;
        QVERIFY_EXCEPTION_THROWN(rf::validate(sweep), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(rf::sParameters({}, 0, 50), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(rf::sParameters({}, 1000, -1), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(rf::sParameters({}, NAN, 50), std::invalid_argument);
    }
};

QTEST_GUILESS_MAIN(FrequencyResponseTests)
#include "FrequencyResponseTests.moc"
