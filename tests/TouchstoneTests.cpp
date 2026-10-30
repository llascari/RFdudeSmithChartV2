#include "Touchstone.h"
#include "FrequencyResponse.h"
#include "DocumentIO.h"
#include "LtspiceExport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <cmath>
#include <numbers>

namespace {
bool near(rf::Complex a, rf::Complex b, double tolerance = 1e-10)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}

rf::Component block(const rf::TouchstoneData& data)
{
    auto c = rf::defaultComponent(rf::Kind::Measured, 1000, 50);
    c.measured = data;
    return c;
}

rf::TouchstoneData asymmetric()
{
    return rf::parseTouchstone("# MHz S RI R 50\n"
        "500 .2 .1 2 -.3 .1 .02 -.1 .05\n"
        "1500 .4 -.1 1 .3 .2 -.02 -.2 -.05\n", 2, "amplifier.s2p");
}
}

class TouchstoneTests final : public QObject
{
    Q_OBJECT
private slots:
    void formatsAndUnits()
    {
        const auto ri = rf::parseTouchstone("! comment\n# mHz s rI r 75 ! options\n"
            "500 0.1 0.2\n .3 .4 ! continued S21\n .5 .6 .7 .8\n"
            "1500 .2 .3 .4 .5 .6 .7 .8 .9\n", 2, "fixture.s2p");
        QCOMPARE(ri.ports, 2);
        QCOMPARE(ri.referenceOhms, 75.0);
        QCOMPARE(ri.samples.size(), std::size_t(2));
        QCOMPARE(ri.samples.front().s.s11, rf::Complex(.1, .2));
        QCOMPARE(ri.samples.front().s.s21, rf::Complex(.3, .4));
        QCOMPARE(ri.samples.front().s.s12, rf::Complex(.5, .6));
        QCOMPARE(ri.samples.front().s.s22, rf::Complex(.7, .8));
        const auto ma = rf::parseTouchstone("# kHz S MA R 50\n1D6 .5 90\n", 1, "ma.s1p");
        QCOMPARE(ma.samples.front().frequencyMHz, 1000.0);
        QVERIFY(near(ma.samples.front().s.s11, {0, .5}));
        const auto db = rf::parseTouchstone("# Hz S DB R 50\n1e9 -20 -90\n", 1, "db.s1p");
        QVERIFY(near(db.samples.front().s.s11, {0, -.1}));
        const auto defaults = rf::parseTouchstone("1 .5 180\n", 1, "default.s1p");
        QCOMPARE(defaults.samples.front().frequencyMHz, 1000.0);
        QCOMPARE(defaults.referenceOhms, 50.0);
        QVERIFY(near(defaults.samples.front().s.s11, -.5));
        const auto partial = rf::parseTouchstone("# S RI\n1 1 0\n", 1, "open.s1p");
        QCOMPARE(partial.samples.front().s.s11, rf::Complex(1));
    }

    void malformed_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<int>("ports");
        const std::pair<const char*, const char*> failures[]{
            {"empty", "! no measurements"},
            {"truncated", "# MHz S RI R 50\n1000 0 0 1 0\n"},
            {"duplicate", "# MHz S RI R 50\n1 0 0 1 0 1 0 0 0\n1 0 0 1 0 1 0 0 0"},
            {"descending", "# MHz S RI R 50\n2 0 0 1 0 1 0 0 0\n1 0 0 1 0 1 0 0 0"},
            {"noise", "# MHz S RI R 50\n2 0 0 1 0 1 0 0 0\n1 2 3 4 5"},
            {"zero-reference", "# MHz S RI R 0\n1 0 0 1 0 1 0 0 0"},
            {"complex-reference", "# MHz S RI R 50 1\n1 0 0 1 0 1 0 0 0"},
            {"zero-frequency", "# MHz S RI R 50\n0 0 0 1 0 1 0 0 0"},
            {"nan", "# MHz S RI R 50\n1 nan 0 1 0 1 0 0 0"},
            {"infinity", "# MHz S RI R 50\n1 0 0 1e999 0 1 0 0 0"},
            {"bad-token", "# MHz S RI R 50\n1 0oops 0 1 0 1 0 0 0"},
            {"negative-ma", "# MHz S MA R 50\n1 -1 0 1 0 1 0 0 0"},
            {"db-overflow", "# MHz S DB R 50\n1 100000 0 1 0 1 0 0 0"},
            {"non-s", "# MHz Y RI R 50\n1 0 0 1 0 1 0 0 0"},
            {"mixed-mode", "[Mixed-Mode Order] D1,2"},
            {"v2", "[Version] 2.0\n# GHz S RI R 50"},
            {"repeated-option", "# MHz S RI R 50\n# MHz S RI R 50"},
            {"late-option", "1 0 0 1 0 1 0 0 0\n# MHz S RI R 50"},
            {"duplicate-format", "# S RI MA"},
            {"duplicate-unit", "# MHz GHz"},
            {"bad-reference", "# R"}
        };
        for (const auto& [name, text] : failures) QTest::newRow(name) << QByteArray(text) << 2;
        QTest::newRow("unsupported-ports") << QByteArray("1 0 0") << 3;
    }

    void malformed()
    {
        QFETCH(QByteArray, text);
        QFETCH(int, ports);
        QVERIFY_EXCEPTION_THROWN(rf::parseTouchstone(text.toStdString(), ports, "bad.s2p"), std::invalid_argument);
    }

    void interpolationAndRenormalization()
    {
        const auto data = asymmetric();
        const auto midpoint = rf::measuredParameters(data, 1000, 50);
        QVERIFY(near(midpoint.s11, .3));
        QVERIFY(near(midpoint.s21, 1.5));
        QVERIFY(near(midpoint.s12, .15));
        QVERIFY(near(midpoint.s22, -.15));
        QCOMPARE(rf::measuredParameters(data, 500, 50), data.samples.front().s);
        QCOMPARE(rf::measuredParameters(data, 1500, 50), data.samples.back().s);
        QVERIFY_EXCEPTION_THROWN(rf::measuredParameters(data, 499, 50), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(rf::measuredParameters(data, 1501, 50), std::invalid_argument);
        auto converted = data;
        converted.referenceOhms = 75;
        for (auto& sample : converted.samples)
            sample.s = rf::measuredParameters(data, sample.frequencyMHz, 75);
        const auto roundtrip = rf::measuredParameters(converted, 500, 50);
        QVERIFY(near(roundtrip.s11, data.samples.front().s.s11));
        QVERIFY(near(roundtrip.s21, data.samples.front().s.s21));
        QVERIFY(near(roundtrip.s12, data.samples.front().s.s12));
        QVERIFY(near(roundtrip.s22, data.samples.front().s.s22));
        auto c = block(data);
        // Independent termination formula, preserving nonreciprocal S12 and S21.
        const rf::Complex loadGamma(.25, -.1);
        const auto& s = data.samples.front().s;
        const auto expected = s.s11 + s.s12 * s.s21 * loadGamma / (1.0 - s.s22 * loadGamma);
        const auto load = rf::Impedance::ratio(50.0 * (1.0 + loadGamma), 1.0 - loadGamma);
        QVERIFY(near(rf::apply(load, c, 500).reflection(50), expected));
        // Port-2 matched at the new reference must give the renormalized S11.
        QVERIFY(near(rf::apply(rf::Impedance::fromOhms(75), c, 500).reflection(75),
                     rf::measuredParameters(data, 500, 75).s11));
        QVERIFY_EXCEPTION_THROWN(rf::tune(c, 5, true), std::invalid_argument);
        const auto resistor = rf::parseTouchstone("# MHz S RI R 75\n1000 .25 0 .75 0 .75 0 .25 0\n",
                                                  2, "50-ohm-series.s2p");
        const auto normalized = rf::measuredParameters(resistor, 1000, 50);
        QVERIFY(near(normalized.s11, 1.0 / 3));
        QVERIFY(near(normalized.s22, 1.0 / 3));
        QVERIFY(near(normalized.s21, 2.0 / 3));
        QVERIFY(near(normalized.s12, 2.0 / 3));
        const auto matchedLoad = rf::parseTouchstone("# MHz S RI R 75\n1000 0 0\n", 1, "75-ohm-load.s1p");
        QVERIFY(near(rf::measuredParameters(matchedLoad, 1000, 50).s11, .2));
    }

    void idealLimitsAndCascade()
    {
        const auto through = rf::parseTouchstone("# MHz S RI R 75\n500 0 0 1 0 1 0 0 0\n"
                                                "1500 0 0 1 0 1 0 0 0\n", 2, "through.s2p");
        auto c = block(through);
        QVERIFY(rf::apply(rf::Impedance::open(), c, 1000).isOpen());
        QVERIFY(near(rf::apply(rf::Impedance::fromOhms(0), c, 1000).ohms(), 0));
        const auto s = rf::sParameters({c}, 1000, 50);
        QVERIFY(near(s.s11, 0));
        QVERIFY(near(s.s21, 1));
        auto resistor = rf::defaultComponent(rf::Kind::Resistor, 1000, 50);
        resistor.resistance = 100;
        const auto cascade = rf::sParameters({c, resistor}, 1000, 50);
        QVERIFY(near(cascade.s11, .5));
        QVERIFY(near(cascade.s21, .5));
        auto data = asymmetric();
        auto amplifier = block(data);
        auto line = rf::defaultComponent(rf::Kind::Line, 1000, 50);
        for (const auto& components : {std::vector{amplifier, line}, std::vector{line, amplifier}}) {
            auto input = rf::Impedance::fromOhms(50);
            for (const auto& element : components) input = rf::apply(input, element, 1000);
            QVERIFY(near(input.reflection(50), rf::sParameters(components, 1000, 50).s11));
        }
        rf::Document d;
        d.components = {amplifier};
        d.selected = 0;
        const auto evaluation = rf::evaluate(d);
        QCOMPARE(evaluation.trajectories.front().size(), std::size_t(2));
        QCOMPARE(evaluation.nodes.size(), std::size_t(2));
        QVERIFY(near(evaluation.nodes.back().reflection(50), .3));
        QVERIFY_EXCEPTION_THROWN(rf::apply(rf::Impedance::fromOhms(50), amplifier, 1000, .5), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(ltspice::schematic(d), std::invalid_argument);
    }

    void measuredLoadAndPersistence()
    {
        rf::Document d;
        d.measuredLoad = rf::parseTouchstone("# MHz S RI R 75\n500 .5 0\n1500 1 0\n", 1, "load.s1p");
        d.components = {block(asymmetric())};
        d.selected = 0;
        const auto decoded = documents::fromJson(documents::toJson(d));
        QCOMPARE(decoded, d);
        QVERIFY(near(rf::startingImpedance(d).ohms(), 525));
        QVERIFY(near(rf::inputReflection(d, 1000, 50),
                     rf::evaluate(d).nodes.back().reflection(50)));
        d.frequencyMHz = 1500;
        QVERIFY(rf::startingImpedance(d).isOpen());
        d.frequencyMHz = 499;
        QVERIFY_EXCEPTION_THROWN(rf::validate(d), std::invalid_argument);
        d.frequencyMHz = 1000;
        auto json = QJsonDocument::fromJson(documents::toJson(d)).object();
        QCOMPARE(json.value("version").toInt(), 3);
        json.insert("version", 2);
        QVERIFY_EXCEPTION_THROWN(documents::fromJson(QJsonDocument(json).toJson()), std::runtime_error);
        json.insert("version", 3);
        json.remove("measuredLoad");
        QVERIFY_EXCEPTION_THROWN(documents::fromJson(QJsonDocument(json).toJson()), std::runtime_error);
        json = QJsonDocument::fromJson(documents::toJson(d)).object();
        auto load = json.value("measuredLoad").toObject();
        load.remove("referenceOhms");
        json.insert("measuredLoad", load);
        QVERIFY_EXCEPTION_THROWN(documents::fromJson(QJsonDocument(json).toJson()), std::runtime_error);
        d.measuredLoad->ports = 2;
        QVERIFY_EXCEPTION_THROWN(rf::validate(d), std::invalid_argument);
        d.measuredLoad->ports = 1;
        d.components.front().measured.ports = 1;
        QVERIFY_EXCEPTION_THROWN(rf::validate(d), std::invalid_argument);
        d.components.front().measured.ports = 2;
        d.components.clear();
        d.selected = -1;
        QVERIFY_EXCEPTION_THROWN(ltspice::schematic(d), std::invalid_argument);
        d.measuredLoad = rf::parseTouchstone("# MHz S RI R 50\n500 -1 0\n1500 -1 0\n", 1, "short.s1p");
        QVERIFY(near(rf::startingImpedance(d).ohms(), 0));
        d.measuredLoad.reset();
        QCOMPARE(QJsonDocument::fromJson(documents::toJson(d)).object().value("version").toInt(), 2);
        d.measuredLoad = rf::parseTouchstone("# MHz S RI R 50\n500 2 0\n1500 2 0\n", 1, "active.s1p");
        const auto active = rf::evaluate(d);
        QVERIFY(near(active.nodes.back().ohms(), -150));
        QVERIFY(!active.qDefined);
        QVERIFY(near(active.nodes.back().reflection(50), 2));
    }

    void sizeLimits()
    {
        const std::string tooLarge(16 * 1024 * 1024 + 1, ' ');
        QVERIFY_EXCEPTION_THROWN(rf::parseTouchstone(tooLarge, 2, "large.s2p"), std::invalid_argument);
        auto data = asymmetric();
        data.samples.resize(100001);
        QVERIFY_EXCEPTION_THROWN(rf::validate(data), std::invalid_argument);
    }
};

QTEST_APPLESS_MAIN(TouchstoneTests)
#include "TouchstoneTests.moc"
