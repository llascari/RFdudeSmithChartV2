#include "DocumentIO.h"
#include "RfModel.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <numbers>

namespace {
bool near(rf::Complex a, rf::Complex b, double tolerance = 1e-9)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}

rf::Component line(rf::Connection connection, double angle)
{
    auto c = rf::defaultComponent(rf::Kind::Line, 1000, 50);
    c.connection = connection;
    c.lengthDegrees = angle;
    return c;
}

QByteArray fixture()
{
    QFile file(QString(TEST_DATA_DIR) + "/legacy-example.lls");
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read legacy fixture.");
    return file.readAll();
}
}

class RfTests final : public QObject
{
    Q_OBJECT

private slots:
    void idealTransformers()
    {
        auto c = rf::defaultComponent(rf::Kind::Transformer, 1000, 50);
        c.turnsRatio = 2;
        const auto input = rf::Impedance::fromOhms({25, -10});
        for (const double frequency : {1.0, 1000.0, 10000.0})
            QVERIFY(near(rf::apply(input, c, frequency).ohms(), {100, -40}));
        QVERIFY(near(rf::apply(input, c, 1000, 0).ohms(), {25, -10}));
        QVERIFY(near(rf::apply(input, c, 1000, 0.5).ohms(), {50, -20}));
        QVERIFY(rf::apply(rf::Impedance::open(), c, 1000).isOpen());
        QVERIFY(near(rf::apply(rf::Impedance::fromOhms(0), c, 1000).ohms(), 0));
        c.turnsRatio = 0.5;
        QVERIFY(near(rf::apply(input, c, 1000).ohms(), {6.25, -2.5}));
        rf::tune(c, 10, true);
        QCOMPARE(c.turnsRatio, 0.55);
        c.turnsRatio = 0;
        QVERIFY_EXCEPTION_THROWN(rf::validate(c), std::invalid_argument);
        c.turnsRatio = 1;
        c.connection = rf::Connection::Shunt;
        QVERIFY_EXCEPTION_THROWN(rf::validate(c), std::invalid_argument);
    }

    void coaxPropagation()
    {
        auto coax = rf::defaultComponent(rf::Kind::Coax, 1000, 50);
        coax.lengthMode = rf::LengthMode::Physical;
        coax.velocityFactor = 0.66;
        coax.physicalLengthMm = 299792458000.0 * 0.66 / (4 * 1e9);
        QVERIFY(std::abs(rf::lineParameters(coax).lengthDegrees - 90) < 1e-12);
        QCOMPARE(rf::lineParameters(coax).attenuationNepers, 0.0);
        const auto load = rf::Impedance::fromOhms({100, 25});
        auto ideal = line(rf::Connection::ThroughLine, 90);
        for (const auto connection : {rf::Connection::ThroughLine, rf::Connection::OpenStub,
                                      rf::Connection::ShortStub}) {
            coax.connection = ideal.connection = connection;
            for (const double frequency : {500.0, 1000.0, 2000.0})
                QVERIFY(near(rf::apply(load, coax, frequency).ohms(),
                             rf::apply(load, ideal, frequency).ohms()));
        }
        coax.velocityMode = rf::VelocityMode::Permittivity;
        coax.dielectricConstant = 1 / (0.66 * 0.66);
        QVERIFY(std::abs(rf::lineParameters(coax).lengthDegrees - 90) < 1e-12);
        const double before = coax.physicalLengthMm;
        rf::tune(coax, 10, true);
        QVERIFY(std::abs(coax.physicalLengthMm / before - 1.1) < 1e-12);
        coax.lengthMode = rf::LengthMode::Electrical;
        coax.lengthDegrees = 90;
        rf::tune(coax, 10, true);
        QVERIFY(std::abs(coax.lengthDegrees - 99) < 1e-12);
        coax.physicalLengthMm = 0;
        coax.lengthMode = rf::LengthMode::Physical;
        QCOMPARE(rf::lineParameters(coax).lengthDegrees, 0.0);
        coax.velocityFactor = 1.01;
        QVERIFY_EXCEPTION_THROWN(rf::validate(coax), std::invalid_argument);
    }

    void microstripDimensions()
    {
        // Independent, conventional piecewise microstrip equations agree with H-J
        // to within 2% over these common PCB geometries.
        for (const double dk : {2.2, 4.2, 10.0}) {
            for (const double u : {0.1, 0.5, 1.0, 2.0, 5.0, 10.0}) {
                const double ee = (dk + 1) / 2 + (dk - 1) / 2
                    * (1 / std::sqrt(1 + 12 / u) + (u < 1 ? 0.04 * std::pow(1 - u, 2) : 0));
                const double expected = u <= 1
                    ? 60 / std::sqrt(ee) * std::log(8 / u + u / 4)
                    : 120 * std::numbers::pi / (std::sqrt(ee) * (u + 1.393 + 0.667 * std::log(u + 1.444)));
                const auto p = rf::microstrip(u * 1.5, 1.5, dk);
                QVERIFY(std::abs(p.impedanceOhms / expected - 1) < 0.02);
                QVERIFY(p.effectivePermittivity > 1 && p.effectivePermittivity < dk);
                QVERIFY(std::abs(rf::microstripWidth(p.impedanceOhms, 1.5, dk) / (u * 1.5) - 1) < 1e-10);
            }
        }
        const double width = rf::microstripWidth(50, 1.5, 4.2);
        QVERIFY(width > 2.9 && width < 3.2);
        QVERIFY(std::abs(rf::microstrip(width, 1.5, 4.2).impedanceOhms - 50) < 1e-10);
        QCOMPARE(rf::microstrip(1.5, 1.5, 1).effectivePermittivity, 1.0);
        QVERIFY_EXCEPTION_THROWN(rf::microstrip(0, 1.5, 4.2), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(rf::microstrip(1, 0, 4.2), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(rf::microstrip(1, 1.5, 0.9), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(rf::microstrip(1000, 1.5, 4.2), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(rf::microstripWidth(1e6, 1.5, 4.2), std::invalid_argument);
    }

    void microstripLoss()
    {
        auto c = rf::defaultComponent(rf::Kind::Microstrip, 1000, 50);
        QCOMPARE(c.substrateDk, 4.2);
        QCOMPARE(c.substrateHeightMm, 1.5);
        QCOMPARE(c.lossTangent, 0.02);
        c.stripWidthMm = rf::microstripWidth(50, 1.5, 4.2);
        c.physicalLengthMm = 100;
        const auto p = rf::lineParameters(c);
        const auto m = rf::microstrip(c.stripWidthMm, c.substrateHeightMm, c.substrateDk);
        // Dielectric attenuation in Np/m, using the filling-factor expression.
        const double alphaPerMetre = std::numbers::pi * 1e9 / 299792458.0
            * c.substrateDk / std::sqrt(m.effectivePermittivity)
            * (m.effectivePermittivity - 1) / (c.substrateDk - 1) * c.lossTangent;
        QVERIFY(std::abs(p.attenuationNepers - alphaPerMetre * 0.1) < 1e-12);
        QVERIFY(p.attenuationNepers * 20 / std::log(10.0) > 0.25);
        QVERIFY(p.attenuationNepers * 20 / std::log(10.0) < 0.4);
        auto differentReference = c;
        differentReference.referenceMHz = 100;
        QVERIFY(near(rf::apply(rf::Impedance::open(), c, 1000).reflection(50),
                     rf::apply(rf::Impedance::open(), differentReference, 1000).reflection(50)));
        for (const double frequency : {500.0, 1000.0, 2000.0}) {
            QVERIFY(near(rf::apply(rf::Impedance::fromOhms(50), c, frequency).ohms(), 50));
            const auto openGamma = rf::apply(rf::Impedance::open(), c, frequency).reflection(50);
            const auto expected = std::exp(rf::Complex(-2 * p.attenuationNepers * frequency / 1000,
                -2 * p.lengthDegrees * frequency / 1000 * std::numbers::pi / 180));
            QVERIFY(near(openGamma, expected));
        }
        for (const auto connection : {rf::Connection::ThroughLine, rf::Connection::OpenStub,
                                      rf::Connection::ShortStub}) {
            c.connection = connection;
            const auto load = rf::Impedance::fromOhms({70, -15});
            const auto lossy = rf::apply(load, c, 1000);
            QVERIFY(lossy.ohms().real() >= 0);
            QVERIFY(near(rf::apply(load, c, 1000, 0).ohms(), load.ohms()));
            c.lossTangent = 0;
            auto ideal = line(connection, p.lengthDegrees);
            QVERIFY(near(rf::apply(load, c, 1000).ohms(), rf::apply(load, ideal, 1000).ohms()));
            c.lossTangent = 0.02;
        }
        c.lengthMode = rf::LengthMode::Electrical;
        c.lengthDegrees = p.lengthDegrees;
        QCOMPARE(rf::lineParameters(c).attenuationNepers, p.attenuationNepers);
        c.lossTangent = -0.01;
        QVERIFY_EXCEPTION_THROWN(rf::validate(c), std::invalid_argument);
        c.lossTangent = 0.11;
        QVERIFY_EXCEPTION_THROWN(rf::validate(c), std::invalid_argument);
    }

    void newElementsJsonAndTrajectories()
    {
        rf::Document d;
        d.selected = 0;
        for (const auto kind : {rf::Kind::Transformer, rf::Kind::Coax, rf::Kind::Microstrip}) {
            auto c = rf::defaultComponent(kind, 915, 50);
            c.turnsRatio = 1.5;
            c.physicalLengthMm = 25.4;
            c.velocityMode = rf::VelocityMode::Permittivity;
            c.dielectricConstant = 2.1;
            c.stripWidthMm = 2.9;
            c.lossTangent = 0.03;
            d.components.push_back(c);
        }
        QVERIFY(documents::fromJson(documents::toJson(d)) == d);
        const auto result = rf::evaluate(d);
        for (std::size_t i = 0; i < d.components.size(); ++i) {
            QVERIFY(near(result.trajectories[i].front(), result.nodes[i].reflection(d.referenceOhms)));
            QVERIFY(near(result.trajectories[i].back(), result.nodes[i + 1].reflection(d.referenceOhms)));
            for (const auto gamma : result.trajectories[i])
                QVERIFY(std::abs(gamma) <= 1 + 1e-10);
        }
        auto root = QJsonDocument::fromJson(documents::toJson(d)).object();
        auto array = root["components"].toArray();
        auto entry = array[2].toObject();
        entry.remove("lossTangent");
        array[2] = entry;
        root["components"] = array;
        QVERIFY_EXCEPTION_THROWN(documents::fromJson(QJsonDocument(root).toJson()), std::runtime_error);
        auto old = documents::fromLegacy(fixture());
        root = QJsonDocument::fromJson(documents::toJson(old)).object();
        root["version"] = 1;
        array = root["components"].toArray();
        const QStringList addedFields{"turnsRatio", "lengthMode", "velocityMode", "physicalLengthMm",
            "velocityFactor", "dielectricConstant", "substrateHeightMm", "stripWidthMm", "substrateDk", "lossTangent"};
        for (int i = 0; i < array.size(); ++i) {
            entry = array[i].toObject();
            for (const auto& key : addedFields) entry.remove(key);
            array[i] = entry;
        }
        root["components"] = array;
        QVERIFY(documents::fromJson(QJsonDocument(root).toJson()) == old);
        auto invalidLegacy = fixture();
        invalidLegacy[156] = static_cast<char>(rf::Kind::Transformer);
        QVERIFY_EXCEPTION_THROWN(documents::fromLegacy(invalidLegacy), std::runtime_error);
    }

    void impedanceAndReflection()
    {
        QVERIFY(near(rf::Impedance::fromOhms(50).reflection(50), 0));
        QVERIFY(near(rf::Impedance::fromOhms(0).reflection(50), -1));
        QVERIFY(near(rf::Impedance::open().reflection(50), 1));
        QVERIFY(rf::Impedance::open().isOpen());
        QVERIFY_EXCEPTION_THROWN(rf::Impedance::open().ohms(), std::domain_error);
        QVERIFY_EXCEPTION_THROWN(rf::Impedance::ratio(0, 0), std::invalid_argument);
        QVERIFY(near(rf::Impedance::fromOhms({50, 50}).reflection(50), {0.2, 0.4}));
    }

    void lumpedElements()
    {
        const auto input = rf::Impedance::fromOhms(50);
        auto resistor = rf::defaultComponent(rf::Kind::Resistor, 1000, 50);
        QVERIFY(near(rf::apply(input, resistor, 1000).ohms(), 100));
        resistor.connection = rf::Connection::Shunt;
        QVERIFY(near(rf::apply(input, resistor, 1000).ohms(), 25));
        const auto c = rf::defaultComponent(rf::Kind::Capacitor, 1000, 50);
        const auto l = rf::defaultComponent(rf::Kind::Inductor, 1000, 50);
        QVERIFY(near(rf::apply(input, c, 1000).ohms(), {50, -50}));
        QVERIFY(near(rf::apply(input, l, 1000).ohms(), {50, 50}));
        QVERIFY(near(rf::apply(input, c, 2000).ohms(), {50, -25}));
        QVERIFY(near(rf::apply(input, l, 2000).ohms(), {50, 100}));
        auto shuntCap = c;
        shuntCap.connection = rf::Connection::Shunt;
        QVERIFY(near(rf::apply(input, shuntCap, 1000).ohms(), {25, -25}));
        QVERIFY(near(rf::apply(input, l, 1000, 0.5).ohms(), {50, 25}));
        QVERIFY(near(rf::apply(input, shuntCap, 1000, 0.5).ohms(), {40, -20}));
    }

    void rlcResonance()
    {
        const auto input = rf::Impedance::fromOhms(50);
        auto c = rf::defaultComponent(rf::Kind::SeriesRlc, 1000, 50);
        QVERIFY(near(rf::apply(input, c, 1000).ohms(), 100));
        c.kind = rf::Kind::ParallelRlc;
        QVERIFY(near(rf::apply(input, c, 1000).ohms(), 100));
        c.connection = rf::Connection::Shunt;
        QVERIFY(near(rf::apply(input, c, 1000).ohms(), 25));
    }

    void throughLines()
    {
        const auto input = rf::Impedance::fromOhms(100);
        QVERIFY(near(rf::apply(input, line(rf::Connection::ThroughLine, 0), 1000).ohms(), 100));
        QVERIFY(near(rf::apply(input, line(rf::Connection::ThroughLine, 90), 1000).ohms(), 25));
        QVERIFY(near(rf::apply(input, line(rf::Connection::ThroughLine, 180), 1000).ohms(), 100));
        QVERIFY(near(rf::apply(input, line(rf::Connection::ThroughLine, 45), 2000).ohms(), 25));
        auto quarter = line(rf::Connection::ThroughLine, 90);
        QVERIFY(near(rf::apply(rf::Impedance::open(), quarter, 1000).ohms(), 0));
        QVERIFY(rf::apply(rf::Impedance::fromOhms(0), quarter, 1000).isOpen());
        for (int angle = 0; angle <= 720; angle += 3) {
            const auto value = rf::apply(input, line(rf::Connection::ThroughLine, angle), 1000);
            QVERIFY(std::abs(std::abs(value.reflection(50)) - 1.0 / 3) < 1e-12);
        }
    }

    void stubsAtBoundaries()
    {
        const auto input = rf::Impedance::fromOhms(50);
        QVERIFY(near(rf::apply(input, line(rf::Connection::OpenStub, 0), 1000).ohms(), 50));
        QVERIFY(near(rf::apply(input, line(rf::Connection::OpenStub, 45), 1000).ohms(), {25, -25}));
        QVERIFY(near(rf::apply(input, line(rf::Connection::OpenStub, 90), 1000).ohms(), 0));
        QVERIFY(near(rf::apply(input, line(rf::Connection::ShortStub, 0), 1000).ohms(), 0));
        QVERIFY(near(rf::apply(input, line(rf::Connection::ShortStub, 45), 1000).ohms(), {25, 25}));
        QVERIFY(near(rf::apply(input, line(rf::Connection::ShortStub, 90), 1000).ohms(), 50));
        QVERIFY(near(rf::apply(input, line(rf::Connection::ShortStub, 180), 1000).ohms(), 0));
        // Starts at the unmodified network even for the shorted stub.
        QVERIFY(near(rf::apply(input, line(rf::Connection::ShortStub, 45), 1000, 0).ohms(), 50));
    }

    void networksAndTrajectories()
    {
        rf::Document d;
        d.components = {rf::defaultComponent(rf::Kind::Inductor, 1000, 50),
                        rf::defaultComponent(rf::Kind::Capacitor, 1000, 50)};
        d.selected = 1;
        const auto result = rf::evaluate(d);
        QCOMPARE(result.nodes.size(), std::size_t(3));
        QCOMPARE(result.trajectories.size(), std::size_t(2));
        QVERIFY(near(result.nodes.back().ohms(), 50));
        QVERIFY(std::abs(result.maxQ - 1) < 1e-10);
        for (std::size_t i = 0; i < result.trajectories.size(); ++i) {
            QVERIFY(result.trajectories[i].size() >= 257);
            QVERIFY(near(result.trajectories[i].front(), result.nodes[i].reflection(50)));
            QVERIFY(near(result.trajectories[i].back(), result.nodes[i + 1].reflection(50)));
            for (auto gamma : result.trajectories[i]) {
                QVERIFY(std::isfinite(gamma.real()) && std::isfinite(gamma.imag()));
                QVERIFY(std::abs(gamma) <= 1 + 1e-10);
            }
        }
    }

    void tuning()
    {
        auto c = rf::defaultComponent(rf::Kind::Resistor, 1000, 50);
        rf::tune(c, 2.5, true);
        QCOMPARE(c.resistance, 51.25);
        rf::tune(c, 2.5, false);
        QVERIFY(std::abs(c.resistance - 49.96875) < 1e-12);
        c.kind = rf::Kind::ParallelRlc;
        c.tune = rf::Tune::Capacitance;
        const double before = c.capacitancePf;
        rf::tune(c, 10, true);
        QVERIFY(std::abs(c.capacitancePf - before * 1.1) < 1e-12);
        QVERIFY_EXCEPTION_THROWN(rf::tune(c, 100, false), std::invalid_argument);
    }

    void idealOpenWithinNetwork()
    {
        rf::Document d;
        d.startOhms = {0, 50};
        auto cap = rf::defaultComponent(rf::Kind::Capacitor, 1000, 50);
        cap.connection = rf::Connection::Shunt;
        d.components = {cap, line(rf::Connection::ThroughLine, 90)};
        d.selected = 0;
        const auto result = rf::evaluate(d);
        QVERIFY(result.nodes[1].isOpen());
        QVERIFY(std::isfinite(result.nodes[1].reflection(50).real()));
        QVERIFY(std::abs(result.nodes.back().reflection(50) + 1.0) < 1e-9);
    }

    void validation()
    {
        rf::Document d;
        d.frequencyMHz = 0;
        QVERIFY_EXCEPTION_THROWN(rf::evaluate(d), std::invalid_argument);
        d = {};
        d.startOhms = {-1, 0};
        QVERIFY_EXCEPTION_THROWN(rf::validate(d), std::invalid_argument);
        d = {};
        d.selected = 0;
        QVERIFY_EXCEPTION_THROWN(rf::validate(d), std::invalid_argument);
        auto c = rf::defaultComponent(rf::Kind::Capacitor, 1000, 50);
        c.connection = rf::Connection::ThroughLine;
        QVERIFY_EXCEPTION_THROWN(rf::validate(c), std::invalid_argument);
        c.connection = rf::Connection::Series;
        c.capacitancePf = std::nan("");
        QVERIFY_EXCEPTION_THROWN(rf::validate(c), std::invalid_argument);
    }

    void jsonRoundTrip()
    {
        rf::Document d;
        d.title = "Unicode \xc2\xb5 network";
        d.components = {line(rf::Connection::ShortStub, 45)};
        d.selected = 0;
        d.admittance = d.qCurves = d.vswrCircle = d.polar = true;
        d.markers = false;
        d.startOhms = {25, -50};
        d.goalOhms = {75, 20};
        d.frequencyPresets = {400, 500, 600};
        QVERIFY(documents::fromJson(documents::toJson(d)) == d);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("network.rfs.json");
        documents::save(path, d);
        bool imported = true;
        QVERIFY(documents::load(path, &imported) == d);
        QVERIFY(!imported);
    }

    void invalidJson()
    {
        QVERIFY_EXCEPTION_THROWN(documents::fromJson("{}"), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(documents::fromJson("{"), std::runtime_error);
        auto root = QJsonDocument::fromJson(documents::toJson({})).object();
        root["version"] = 3;
        QVERIFY_EXCEPTION_THROWN(documents::fromJson(QJsonDocument(root).toJson()), std::runtime_error);
        root["version"] = 1;
        root["selected"] = 0.25;
        QVERIFY_EXCEPTION_THROWN(documents::fromJson(QJsonDocument(root).toJson()), std::runtime_error);
        root["selected"] = -1;
        root["frequencyMHz"] = "1000";
        QVERIFY_EXCEPTION_THROWN(documents::fromJson(QJsonDocument(root).toJson()), std::runtime_error);
    }

    void legacyImport()
    {
        const auto data = fixture();
        QCOMPARE(data.size(), qsizetype(312));
        const auto d = documents::fromLegacy(data);
        QCOMPARE(d.frequencyMHz, 1200.0);
        QCOMPARE(d.referenceOhms, 50.0);
        QCOMPARE(d.selected, 1);
        QCOMPARE(d.components.size(), std::size_t(2));
        QVERIFY(d.components[0].kind == rf::Kind::Inductor);
        QVERIFY(d.components[0].connection == rf::Connection::Series);
        QVERIFY(d.components[1].kind == rf::Kind::Capacitor);
        QVERIFY(d.components[1].connection == rf::Connection::Shunt);
        QVERIFY(std::abs(d.components[0].inductanceNh - 7.957747154594767) < 1e-9);
        QCOMPARE(d.frequencyPresets[0], 800.0);
        QCOMPARE(d.frequencyPresets[1], 1000.0);
        QCOMPARE(d.frequencyPresets[2], 1200.0);
        const auto result = rf::evaluate(d);
        const rf::Complex z1(50, 2 * std::numbers::pi * 1200e6 * d.components[0].inductanceNh * 1e-9);
        const rf::Complex zc(0, -1 / (2 * std::numbers::pi * 1200e6 * d.components[1].capacitancePf * 1e-12));
        QVERIFY(near(result.nodes.back().ohms(), z1 * zc / (z1 + zc)));
        QVERIFY(documents::fromJson(documents::toJson(d)) == d);
    }

    void legacyEarlierRevision()
    {
        auto data = fixture();
        data.remove(132, 24);
        const auto d = documents::fromLegacy(data);
        QCOMPARE(d.frequencyPresets[0], d.frequencyMHz);
        QCOMPARE(d.components.size(), std::size_t(2));
    }

    void legacyStringEncodings()
    {
        const auto data = fixture();
        auto unicode = data.left(89);
        unicode += QByteArray::fromHex("fffeff034100b500ac20");
        unicode += data.mid(112);
        const auto d = documents::fromLegacy(unicode);
        QCOMPARE(QString::fromStdString(d.title), QString::fromUtf8("A\xc2\xb5\xe2\x82\xac"));
        auto ansi = data;
        ansi[90] = static_cast<char>(0x80);
        const auto decoded = QString::fromStdString(documents::fromLegacy(ansi).title);
        QCOMPARE(decoded.front().unicode(), ushort(0x20ac));
    }

    void truncatedLegacy()
    {
        const auto data = fixture();
        for (qsizetype size = 0; size < data.size(); ++size)
            QVERIFY_EXCEPTION_THROWN(documents::fromLegacy(data.left(size)), std::exception);
        auto extra = data + QByteArray(1, '\0');
        QVERIFY_EXCEPTION_THROWN(documents::fromLegacy(extra), std::runtime_error);
    }
};

QTEST_GUILESS_MAIN(RfTests)
#include "RfTests.moc"
