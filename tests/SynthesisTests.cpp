#include "DocumentIO.h"
#include "LtspiceExport.h"
#include "Synthesis.h"

#include <QtTest>

#include <cmath>
#include <numbers>

namespace {
double insertionLoss(const rf::Document& document, double frequencyMHz)
{
    rf::Complex voltage = 1;
    rf::Complex current = 1.0 / document.startOhms.real();
    for (const auto& c : document.components) {
        const double omega = 2 * std::numbers::pi * frequencyMHz * 1e6;
        const rf::Complex z = c.kind == rf::Kind::Inductor
            ? rf::Complex(0, omega * c.inductanceNh * 1e-9)
            : rf::Complex(0, -1 / (omega * c.capacitancePf * 1e-12));
        if (c.connection == rf::Connection::Series) voltage += z * current;
        else current += voltage / z;
    }
    const double source = document.goalOhms.real();
    const double gain = 4 * source / (document.startOhms.real()
                                       * std::norm(voltage + source * current));
    return -10 * std::log10(gain);
}
}

class SynthesisTests final : public QObject
{
    Q_OBJECT

private slots:
    void matching()
    {
        for (const auto type : {rf::MatchType::LowPass, rf::MatchType::HighPass,
                                rf::MatchType::QuarterWave}) {
            for (const int sections : {1, 2, 10}) {
                for (const auto start : {rf::Complex(5, 0), rf::Complex(100, 25),
                                         rf::Complex(50, -13), rf::Complex(50, 0)}) {
                    for (const auto goal : {rf::Complex(50, 0), rf::Complex(10, -15),
                                            rf::Complex(50, 1e-3)}) {
                        rf::MatchSpecification s;
                        s.type = type;
                        s.sections = sections;
                        s.startOhms = start;
                        s.goalOhms = goal;
                        s.frequencyMHz = 915;
                        const auto document = rf::synthesizeMatch(s);
                        QCOMPARE(document.startOhms, start);
                        QCOMPARE(document.goalOhms, goal);
                        QCOMPARE(document.frequencyMHz, 915.0);
                        const auto endpoint = rf::evaluate(document).nodes.back();
                        QVERIFY(!endpoint.isOpen());
                        QVERIFY(std::abs(endpoint.ohms() - goal) < 1e-8 * std::max(1.0, std::abs(goal)));
                        QVERIFY(document.vswrCircle);
                        QVERIFY(document.qCurves);
                        for (const auto& c : document.components) {
                            if (c.kind == rf::Kind::Line) {
                                QCOMPARE(c.referenceMHz, 915.0);
                                QCOMPARE(c.lengthDegrees, 90.0);
                            }
                        }
                        QCOMPARE(documents::fromJson(documents::toJson(document)), document);
                        QVERIFY(!ltspice::schematic(document).isEmpty());
                    }
                }
            }
        }
    }

    void legacyMatchValues()
    {
        rf::MatchSpecification s;
        s.startOhms = 5;
        s.goalOhms = 50;
        auto document = rf::synthesizeMatch(s);
        QCOMPARE(document.components.size(), std::size_t(2));
        QVERIFY(document.components[0].kind == rf::Kind::Inductor);
        QVERIFY(document.components[0].connection == rf::Connection::Series);
        QVERIFY(document.components[1].kind == rf::Kind::Capacitor);
        QVERIFY(document.components[1].connection == rf::Connection::Shunt);
        const double omega = 2 * std::numbers::pi * 1e9;
        QVERIFY(std::abs(document.components[0].inductanceNh - 15 / omega * 1e9) < 1e-12);
        QVERIFY(std::abs(document.components[1].capacitancePf - 3 / (omega * 50) * 1e12) < 1e-12);
        s.type = rf::MatchType::QuarterWave;
        document = rf::synthesizeMatch(s);
        QCOMPARE(document.components.size(), std::size_t(1));
        QVERIFY(std::abs(document.components.front().lineOhms - std::sqrt(250.0)) < 1e-12);
        s.startOhms = s.goalOhms;
        document = rf::synthesizeMatch(s);
        QVERIFY(document.components.empty());
        QCOMPARE(document.startOhms, s.goalOhms);
    }

    void filters()
    {
        for (const auto type : {rf::FilterType::LowPass, rf::FilterType::HighPass}) {
            for (const bool shunt : {true, false}) {
                for (const int requestedOrder : {3, 4, 7, 14, 15}) {
                    for (const double ripple : {0.0001, 0.1, 1.0, 10.0}) {
                        rf::FilterSpecification s;
                        s.type = type;
                        s.shuntFirst = shunt;
                        s.order = requestedOrder;
                        s.rippleDb = ripple;
                        s.cutoffMHz = 123;
                        s.impedanceOhms = 75;
                        const int order = requestedOrder % 2 == 0 ? requestedOrder + 1 : requestedOrder;
                        const auto document = rf::synthesizeFilter(s);
                        QCOMPARE(document.components.size(), std::size_t(order));
                        QCOMPARE(document.referenceOhms, 75.0);
                        QCOMPARE(document.startOhms, rf::Complex(75, 0));
                        QCOMPARE(document.goalOhms, rf::Complex(75, 0));
                        QCOMPARE(document.frequencyMHz, 123.0);
                        QCOMPARE(document.components.front().connection,
                                 shunt ? rf::Connection::Shunt : rf::Connection::Series);
                        for (const double normalized : {0.1, 0.5, 0.9, 1.0, 1.1, 2.0}) {
                            const double frequency = s.cutoffMHz
                                * (type == rf::FilterType::LowPass ? normalized : 1 / normalized);
                            const double polynomial = normalized <= 1
                                ? std::cos(order * std::acos(normalized))
                                : std::cosh(order * std::acosh(normalized));
                            const double expected = 10 * std::log10(1
                                + std::expm1(ripple * std::log(10.0) / 10) * polynomial * polynomial);
                            QVERIFY2(std::abs(insertionLoss(document, frequency) - expected) < 1e-7,
                                     qPrintable(QString("order=%1 ripple=%2 normalized=%3")
                                         .arg(order).arg(ripple).arg(normalized)));
                        }
                        QCOMPARE(documents::fromJson(documents::toJson(document)), document);
                    }
                }
            }
        }
    }

    void legacyFilterValues()
    {
        // Independent transcription of the legacy prototype recurrence, including 17.37.
        const double gamma = std::sinh(std::log(1 / std::tanh(0.1 / 17.37)) / 6);
        const double g1 = 1 / gamma;
        const double g2 = 2 / ((gamma * gamma + 0.75) * g1);
        const auto document = rf::synthesizeFilter({});
        const double omega = 2 * std::numbers::pi * 1e9;
        const double c1 = g1 / (50 * omega) * 1e12;
        const double l2 = g2 * 50 / omega * 1e9;
        QVERIFY(std::abs(document.components[0].capacitancePf / c1 - 1) < 5e-5);
        QVERIFY(std::abs(document.components[1].inductanceNh / l2 - 1) < 5e-5);
        QVERIFY(std::abs(document.components[2].capacitancePf / c1 - 1) < 5e-5);
    }

    void validation()
    {
        rf::MatchSpecification m;
        m.sections = 0;
        QVERIFY_EXCEPTION_THROWN(rf::synthesizeMatch(m), std::invalid_argument);
        m.sections = 11;
        QVERIFY_EXCEPTION_THROWN(rf::synthesizeMatch(m), std::invalid_argument);
        m.sections = 1;
        m.startOhms = {0, 10};
        QVERIFY_EXCEPTION_THROWN(rf::synthesizeMatch(m), std::invalid_argument);
        m.startOhms = 50;
        m.goalOhms = {-1, 0};
        QVERIFY_EXCEPTION_THROWN(rf::synthesizeMatch(m), std::invalid_argument);
        rf::FilterSpecification f;
        f.order = 16;
        QVERIFY_EXCEPTION_THROWN(rf::synthesizeFilter(f), std::invalid_argument);
        f.order = 3;
        f.rippleDb = 0;
        QVERIFY_EXCEPTION_THROWN(rf::synthesizeFilter(f), std::invalid_argument);
        f.rippleDb = 0.1;
        f.cutoffMHz = 0;
        QVERIFY_EXCEPTION_THROWN(rf::synthesizeFilter(f), std::invalid_argument);
        f.cutoffMHz = 1000;
        f.impedanceOhms = -50;
        QVERIFY_EXCEPTION_THROWN(rf::synthesizeFilter(f), std::invalid_argument);
    }
};

QTEST_GUILESS_MAIN(SynthesisTests)
#include "SynthesisTests.moc"
