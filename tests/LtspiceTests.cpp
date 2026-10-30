#include "LtspiceExport.h"
#include "RfModel.h"
#include "Synthesis.h"

#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

Q_DECLARE_METATYPE(rf::Document)

namespace {
QString decode(const QByteArray& data)
{
    QStringDecoder decoder(QStringConverter::Utf16);
    return decoder(data);
}

QString read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read LTspice output: %s", qPrintable(path));
    const auto data = file.readAll();
    return data.startsWith("\xff\xfe") ? decode(data) : QString::fromUtf8(data);
}

rf::Document network(rf::Kind kind, rf::Connection connection, double length = 37)
{
    rf::Document document;
    document.startOhms = {73, -21};
    auto component = rf::defaultComponent(kind, 1200, 61);
    component.connection = connection;
    component.lengthDegrees = length;
    document.components.push_back(component);
    document.selected = 0;
    return document;
}
}

class LtspiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void newElementContents()
    {
        auto document = network(rf::Kind::Transformer, rf::Connection::ThroughLine);
        document.components.front().turnsRatio = 2;
        const auto transformer = QString::fromUtf8(ltspice::schematic(document));
        QVERIFY(transformer.contains("SYMATTR InstName Eideal1\r\nSYMATTR Value 2"));
        QVERIFY(transformer.contains("SYMATTR InstName Fideal1\r\nSYMATTR Value Eideal1 -2"));
        QVERIFY(transformer.contains("FLAG 368 272 XFMR1_OUT"));
        QVERIFY(transformer.contains("FLAG 656 256 XFMR1_OUT"));
        document = network(rf::Kind::Microstrip, rf::Connection::ThroughLine);
        const auto microstrip = QString::fromUtf8(ltspice::schematic(document));
        QVERIFY(microstrip.contains("SYMATTR InstName Rloss1a"));
        QVERIFY(microstrip.contains("SYMATTR InstName Rloss1b"));
        QVERIFY(microstrip.contains("SYMATTR InstName Rloss1g"));
        QVERIFY(microstrip.contains("exact only at exported analysis frequency"));
        const auto p = rf::lineParameters(document.components.front());
        QVERIFY(microstrip.contains("Z0=" + QString::number(p.impedanceOhms, 'g', 17)));
        document.components.front().lossTangent = 0;
        QVERIFY(!ltspice::schematic(document).contains("Rloss"));
    }

    void schematicContents()
    {
        auto document = network(rf::Kind::Line, rf::Connection::OpenStub);
        document.title = "Unicode \u03a9 title\n.ac malicious";
        const auto text = QString::fromUtf8(ltspice::schematic(document));
        QVERIFY(text.startsWith("Version 4\r\nSHEET"));
        QVERIFY(text.contains(";Unicode \u03a9 title .ac malicious\r\n"));
        QVERIFY(text.contains("SYMATTR InstName T1\r\n"));
        QVERIFY(text.contains("SYMATTR InstName Rload\r\n"));
        QVERIFY(text.contains("SYMATTR InstName Cload\r\n"));
        QVERIFY(!text.contains("SYMATTR InstName Lload\r\n"));
        QVERIFY(text.contains("!.ac lin 1 1000000000 1000000000"));
        QVERIFY(text.contains("!.meas AC ZinRe"));
        QVERIFY(text.contains("!.meas AC ZinIm"));
        QVERIFY(text.contains("!.meas AC GammaRe"));
        QVERIFY(text.contains("!.meas AC GammaIm"));
        const auto delay = 37.0 / 360 / (1200 * 1e6);
        QVERIFY(text.contains("TD=" + QString::number(delay, 'g', 17)));
        document.components.push_back(rf::defaultComponent(rf::Kind::Capacitor, 1000, 50));
        const auto ordered = QString::fromUtf8(ltspice::schematic(document));
        QVERIFY(ordered.indexOf(";#2") < ordered.indexOf(";#1"));
        QVERIFY(ordered.contains("SYMATTR InstName C2\r\n"));
    }

    void zeroLengthAndShortLoad()
    {
        for (const auto connection : {rf::Connection::ThroughLine, rf::Connection::OpenStub,
                                      rf::Connection::ShortStub}) {
            auto document = network(rf::Kind::Line, connection, 0);
            document.startOhms = 0;
            const auto text = QString::fromUtf8(ltspice::schematic(document));
            QVERIFY(!text.contains("SYMBOL tline"));
            QVERIFY(!text.contains("TD=0"));
            QVERIFY(!text.contains("SYMATTR InstName Rload"));
            QVERIFY(text.contains("FLAG 800 256 0"));
            QCOMPARE(text.contains("FLAG 320 256 0"), connection == rf::Connection::ShortStub);
        }
    }

    void atomicSaveAndValidation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        rf::Document document;
        const auto path = directory.filePath("network.asc");
        ltspice::save(path, document);
        const auto before = read(path);
        document.referenceOhms = -1;
        QVERIFY_EXCEPTION_THROWN(ltspice::save(path, document), std::invalid_argument);
        QCOMPARE(read(path), before);
        QVERIFY_EXCEPTION_THROWN(ltspice::save(directory.filePath("missing/network.asc"), {}),
                                 std::runtime_error);
    }

    void simulation_data()
    {
        QTest::addColumn<rf::Document>("document");
        for (const double ratio : {0.5, 1.0, 2.0, 4.0}) {
            auto document = network(rf::Kind::Transformer, rf::Connection::ThroughLine);
            document.components.front().turnsRatio = ratio;
            QTest::newRow(qPrintable(QString("transformer-%1").arg(ratio))) << document;
        }
        for (const auto kind : {rf::Kind::Coax, rf::Kind::Microstrip}) {
            for (const auto connection : {rf::Connection::ThroughLine, rf::Connection::OpenStub,
                                          rf::Connection::ShortStub}) {
                for (const bool physical : {true, false}) {
                    for (const double frequency : {600.0, 1200.0, 2400.0}) {
                        auto document = network(kind, connection, 90);
                        document.frequencyMHz = frequency;
                        auto& c = document.components.front();
                        c.lengthMode = physical ? rf::LengthMode::Physical : rf::LengthMode::Electrical;
                        c.physicalLengthMm = 40;
                        QTest::newRow(qPrintable(QString("new-line-%1-%2-%3-%4")
                            .arg(int(kind)).arg(int(connection)).arg(physical).arg(frequency))) << document;
                    }
                }
            }
        }
        for (int kind = 10; kind <= 15; ++kind) {
            const bool line = kind == int(rf::Kind::Line);
            for (int connection = line ? 3 : 1; connection <= (line ? 5 : 2); ++connection) {
                auto document = network(static_cast<rf::Kind>(kind),
                                        static_cast<rf::Connection>(connection));
                const auto name = QString("%1-%2").arg(kind).arg(connection);
                QTest::newRow(qPrintable(name)) << document;
            }
        }
        for (const auto connection : {rf::Connection::ThroughLine, rf::Connection::OpenStub,
                                      rf::Connection::ShortStub}) {
            for (const double degrees : {0.0, 90.0, 180.0, 36000.0}) {
                auto document = network(rf::Kind::Line, connection, degrees);
                document.frequencyMHz = 1200;
                document.startOhms = {50, 0};
                const auto name = QString("line-%1-%2").arg(int(connection)).arg(degrees);
                QTest::newRow(qPrintable(name)) << document;
            }
        }
        for (const auto start : {rf::Complex(0, 0), rf::Complex(0, 30),
                                 rf::Complex(0, -30), rf::Complex(50, 30), rf::Complex(50, 0)}) {
            rf::Document document;
            document.startOhms = start;
            const auto name = QString("load-%1-%2").arg(start.real()).arg(start.imag());
            QTest::newRow(qPrintable(name)) << document;
        }
        auto open = network(rf::Kind::Line, rf::Connection::ThroughLine, 90);
        open.frequencyMHz = 1200;
        open.startOhms = 0;
        QTest::newRow("ideal-open") << open;
        rf::Document mixed;
        mixed.title = "Mixed network \u03a9";
        mixed.frequencyMHz = 915;
        mixed.referenceOhms = 75;
        mixed.startOhms = {32, 15};
        mixed.selected = 0;
        for (int kind = 10; kind <= 15; ++kind) {
            auto component = rf::defaultComponent(static_cast<rf::Kind>(kind), 1000, 50);
            component.connection = kind == int(rf::Kind::Line) ? rf::Connection::ThroughLine
                : kind % 2 ? rf::Connection::Series : rf::Connection::Shunt;
            mixed.components.push_back(component);
        }
        QTest::newRow("mixed") << mixed;
        for (const auto type : {rf::MatchType::LowPass, rf::MatchType::HighPass,
                                rf::MatchType::QuarterWave}) {
            rf::MatchSpecification s;
            s.type = type;
            s.sections = 3;
            s.startOhms = {100, -25};
            s.goalOhms = {10, 13};
            const auto name = QString("synthesized-match-%1").arg(int(type));
            QTest::newRow(qPrintable(name)) << rf::synthesizeMatch(s);
        }
        for (const auto type : {rf::FilterType::LowPass, rf::FilterType::HighPass}) {
            for (const bool shunt : {true, false}) {
                rf::FilterSpecification s;
                s.type = type;
                s.shuntFirst = shunt;
                s.order = 4;
                s.rippleDb = 0.5;
                s.cutoffMHz = 123;
                const auto name = QString("synthesized-filter-%1-%2").arg(int(type)).arg(shunt);
                QTest::newRow(qPrintable(name)) << rf::synthesizeFilter(s);
            }
        }
    }

    void simulation()
    {
        const auto executable = qEnvironmentVariable("RFDUDE_LTSPICE_EXE");
        if (executable.isEmpty())
            QSKIP("Set RFDUDE_LTSPICE_EXE to enable optional LTspice/Wine integration.");
        QFETCH(rf::Document, document);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("network.asc");
        ltspice::save(path, document);
        QProcess process;
        process.setWorkingDirectory(directory.path());
        process.start("wine", {executable, "-netlist", "Z:" + path});
        QVERIFY2(process.waitForFinished(120000), qPrintable(process.errorString()));
        QCOMPARE(process.exitCode(), 0);
        QVERIFY2(QFile::exists(directory.filePath("network.net")),
                 process.readAll().constData());
        const auto netlist = read(directory.filePath("network.net"));
        QVERIFY(netlist.contains("Vdrive"));
        process.start("wine", {executable, "-b", "Z:" + directory.filePath("network.net")});
        QVERIFY2(process.waitForFinished(120000), qPrintable(process.errorString()));
        QVERIFY2(QFile::exists(directory.filePath("network.log")),
                 process.readAll().constData());
        const auto log = read(directory.filePath("network.log"));
        QVERIFY2(process.exitCode() == 0, qPrintable(log));
        const auto measurement = [&](const QString& name) {
            const QRegularExpression expression(name + R"(:.*?=\s*\(?([-+0-9.eE]+))",
                                               QRegularExpression::CaseInsensitiveOption);
            return expression.match(log);
        };
        const auto expected = rf::evaluate(document).nodes.back();
        const auto gammaReal = measurement("GammaRe");
        const auto gammaImaginary = measurement("GammaIm");
        QVERIFY2(gammaReal.hasMatch() && gammaImaginary.hasMatch(), qPrintable(log));
        bool gammaRealOk = false;
        bool gammaImaginaryOk = false;
        const rf::Complex measuredGamma(gammaReal.captured(1).toDouble(&gammaRealOk),
                                       gammaImaginary.captured(1).toDouble(&gammaImaginaryOk));
        QVERIFY(gammaRealOk && gammaImaginaryOk);
        QVERIFY2(std::abs(measuredGamma - expected.reflection(document.referenceOhms)) < 1e-5,
                 qPrintable(log + "\n" + netlist));
        if (expected.isOpen()) {
            QVERIFY(!netlist.contains(".meas AC ZinRe"));
            return;
        }
        const auto real = measurement("ZinRe");
        const auto imaginary = measurement("ZinIm");
        QVERIFY2(real.hasMatch() && imaginary.hasMatch(), qPrintable(log));
        QVERIFY2(!log.contains("dB"), qPrintable(log));
        bool realOk = false;
        bool imaginaryOk = false;
        const rf::Complex impedance(real.captured(1).toDouble(&realOk),
                                    imaginary.captured(1).toDouble(&imaginaryOk));
        QVERIFY(realOk && imaginaryOk);
        const auto gamma = (impedance - document.referenceOhms) / (impedance + document.referenceOhms);
        QVERIFY2(std::abs(gamma - expected.reflection(document.referenceOhms)) < 1e-5,
                 qPrintable(log + "\n" + netlist));
        QVERIFY2(std::abs(impedance - expected.ohms()) < 1e-5 * std::max(1.0, std::abs(expected.ohms())),
                 qPrintable(log + "\n" + netlist));
    }
};

QTEST_GUILESS_MAIN(LtspiceTests)
#include "LtspiceTests.moc"
