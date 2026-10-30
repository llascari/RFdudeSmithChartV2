#include "DocumentIO.h"

#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace documents {
namespace {
[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toStdString());
}

double number(const QJsonObject& o, const QString& key)
{
    const auto v = o.value(key);
    if (!v.isDouble() || !std::isfinite(v.toDouble()))
        fail("Missing or invalid number: " + key);
    return v.toDouble();
}

int integer(const QJsonObject& o, const QString& key)
{
    const double value = number(o, key);
    if (std::trunc(value) != value || value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max())
        fail("Invalid integer: " + key);
    return static_cast<int>(value);
}

bool boolean(const QJsonObject& o, const QString& key)
{
    if (!o.value(key).isBool())
        fail("Missing or invalid boolean: " + key);
    return o.value(key).toBool();
}

QJsonObject object(const QJsonObject& o, const QString& key)
{
    if (!o.value(key).isObject())
        fail("Missing or invalid object: " + key);
    return o.value(key).toObject();
}

QString text(const QJsonObject& o, const QString& key)
{
    if (!o.value(key).isString())
        fail("Missing or invalid text: " + key);
    return o.value(key).toString();
}

QJsonObject complexJson(rf::Complex z)
{
    return {{"real", z.real()}, {"imaginary", z.imag()}};
}

rf::Complex complexValue(const QJsonObject& o)
{
    return {number(o, "real"), number(o, "imaginary")};
}

QJsonObject measuredJson(const rf::TouchstoneData& data)
{
    QJsonArray samples;
    for (const auto& sample : data.samples)
        samples.append(QJsonObject{{"frequencyMHz", sample.frequencyMHz},
            {"s11", complexJson(sample.s.s11)}, {"s21", complexJson(sample.s.s21)},
            {"s12", complexJson(sample.s.s12)}, {"s22", complexJson(sample.s.s22)}});
    return {{"name", QString::fromStdString(data.name)}, {"ports", data.ports},
            {"referenceOhms", data.referenceOhms}, {"samples", samples}};
}

rf::TouchstoneData measuredValue(const QJsonObject& o)
{
    rf::TouchstoneData data{text(o, "name").toStdString(), integer(o, "ports"),
                            number(o, "referenceOhms"), {}};
    if (!o.value("samples").isArray() || o.value("samples").toArray().size() > 100000)
        fail("Missing or oversized Touchstone sample array.");
    for (const auto value : o.value("samples").toArray()) {
        if (!value.isObject()) fail("Invalid Touchstone sample.");
        const auto sample = value.toObject();
        data.samples.push_back({number(sample, "frequencyMHz"),
            {complexValue(object(sample, "s11")), complexValue(object(sample, "s21")),
             complexValue(object(sample, "s12")), complexValue(object(sample, "s22"))}});
    }
    return data;
}

class Archive
{
public:
    explicit Archive(const QByteArray& data) : stream(data)
    {
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    }

    template<class T> T read()
    {
        T value{};
        stream >> value;
        check();
        return value;
    }

    void check()
    {
        if (stream.status() != QDataStream::Ok)
            fail("Truncated or malformed legacy project.");
    }

    rf::Complex complex()
    {
        const double real = read<double>();
        const double imaginary = read<double>();
        return {real, imaginary};
    }

    QString string()
    {
        bool unicode = false;
        quint32 count = read<quint8>();
        if (count == 0xff) {
            count = read<quint16>();
            if (count == 0xfffe) {
                unicode = true;
                count = read<quint8>();
                if (count == 0xff) count = read<quint16>();
            }
            if (count == 0xffff) count = read<quint32>();
        }
        if (count > 65536)
            fail("Legacy string is too long.");
        if (unicode) {
            QString result;
            result.reserve(static_cast<qsizetype>(count));
            for (quint32 i = 0; i < count; ++i)
                result.append(QChar(read<quint16>()));
            return result;
        }
        QByteArray bytes(static_cast<qsizetype>(count), Qt::Uninitialized);
        if (stream.readRawData(bytes.data(), static_cast<qint64>(count)) != count)
            fail("Truncated legacy string.");
        // The legacy project resources and Windows builds used code page 1252.
        constexpr std::array<ushort, 32> windows1252{
            0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
            0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
            0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
            0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};
        QString result;
        for (const auto byte : bytes) {
            const auto code = static_cast<unsigned char>(byte);
            result.append(QChar(code >= 0x80 && code <= 0x9f ? windows1252[code - 0x80] : code));
        }
        return result;
    }

    qint64 remaining() const { return stream.device()->bytesAvailable(); }

private:
    QDataStream stream;
};
}

QByteArray toJson(const rf::Document& d)
{
    rf::validate(d);
    QJsonArray components;
    bool measured = d.measuredLoad.has_value();
    for (const auto& c : d.components) {
        QJsonObject component{
            {"kind", static_cast<int>(c.kind)}, {"connection", static_cast<int>(c.connection)},
            {"tune", static_cast<int>(c.tune)}, {"resistanceOhms", c.resistance},
            {"capacitancePf", c.capacitancePf}, {"inductanceNh", c.inductanceNh},
            {"lineOhms", c.lineOhms}, {"lengthDegrees", c.lengthDegrees},
            {"referenceMHz", c.referenceMHz}, {"turnsRatio", c.turnsRatio},
            {"lengthMode", static_cast<int>(c.lengthMode)},
            {"velocityMode", static_cast<int>(c.velocityMode)},
            {"physicalLengthMm", c.physicalLengthMm}, {"velocityFactor", c.velocityFactor},
            {"dielectricConstant", c.dielectricConstant}, {"substrateHeightMm", c.substrateHeightMm},
            {"stripWidthMm", c.stripWidthMm}, {"substrateDk", c.substrateDk},
            {"lossTangent", c.lossTangent}};
        if (c.kind == rf::Kind::Measured) {
            measured = true;
            component.insert("measured", measuredJson(c.measured));
        }
        components.append(component);
    }
    QJsonArray presets;
    for (double frequency : d.frequencyPresets) presets.append(frequency);
    QJsonObject root{
        {"format", "RFdudeSmithChart"}, {"version", measured ? 3 : 2},
        {"title", QString::fromStdString(d.title)},
        {"frequencyMHz", d.frequencyMHz}, {"referenceOhms", d.referenceOhms},
        {"startOhms", complexJson(d.startOhms)}, {"goalOhms", complexJson(d.goalOhms)},
        {"tunePercent", d.tunePercent}, {"frequencyPresetsMHz", presets},
        {"selected", d.selected}, {"components", components},
        {"display", QJsonObject{{"admittance", d.admittance}, {"polar", d.polar},
                               {"qCurves", d.qCurves}, {"markers", d.markers},
                               {"vswrCircle", d.vswrCircle}}}};
    if (measured)
        root.insert("measuredLoad", d.measuredLoad ? QJsonValue(measuredJson(*d.measuredLoad))
                                                  : QJsonValue(QJsonValue::Null));
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

rf::Document fromJson(const QByteArray& data)
{
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        fail("Invalid project JSON: " + error.errorString());
    const auto o = parsed.object();
    const int version = integer(o, "version");
    if (text(o, "format") != "RFdudeSmithChart" || version < 1 || version > 3)
        fail("Unsupported project format or version.");
    if (version < 3 && o.contains("measuredLoad"))
        fail("Measured loads require project version 3.");
    rf::Document d;
    if (version == 3) {
        if (!o.contains("measuredLoad")) fail("Missing measuredLoad field.");
        if (!o.value("measuredLoad").isNull())
            d.measuredLoad = measuredValue(object(o, "measuredLoad"));
    }
    d.title = text(o, "title").toStdString();
    d.frequencyMHz = number(o, "frequencyMHz");
    d.referenceOhms = number(o, "referenceOhms");
    d.startOhms = complexValue(object(o, "startOhms"));
    d.goalOhms = complexValue(object(o, "goalOhms"));
    d.tunePercent = number(o, "tunePercent");
    d.selected = integer(o, "selected");
    const auto presets = o.value("frequencyPresetsMHz").toArray();
    if (presets.size() != 3) fail("Exactly three frequency presets are required.");
    for (int i = 0; i < 3; ++i) {
        if (!presets[i].isDouble()) fail("Invalid frequency preset.");
        d.frequencyPresets[static_cast<std::size_t>(i)] = presets[i].toDouble();
    }
    const auto display = object(o, "display");
    d.admittance = boolean(display, "admittance");
    d.polar = boolean(display, "polar");
    d.qCurves = boolean(display, "qCurves");
    d.markers = boolean(display, "markers");
    d.vswrCircle = boolean(display, "vswrCircle");
    if (!o.value("components").isArray()) fail("Missing component array.");
    for (const auto entry : o.value("components").toArray()) {
        if (!entry.isObject()) fail("Invalid component entry.");
        const auto c = entry.toObject();
        rf::Component component;
        component.kind = static_cast<rf::Kind>(integer(c, "kind"));
        component.connection = static_cast<rf::Connection>(integer(c, "connection"));
        component.tune = static_cast<rf::Tune>(integer(c, "tune"));
        component.resistance = number(c, "resistanceOhms");
        component.capacitancePf = number(c, "capacitancePf");
        component.inductanceNh = number(c, "inductanceNh");
        component.lineOhms = number(c, "lineOhms");
        component.lengthDegrees = number(c, "lengthDegrees");
        component.referenceMHz = number(c, "referenceMHz");
        if (version >= 2) {
            component.turnsRatio = number(c, "turnsRatio");
            component.lengthMode = static_cast<rf::LengthMode>(integer(c, "lengthMode"));
            component.velocityMode = static_cast<rf::VelocityMode>(integer(c, "velocityMode"));
            component.physicalLengthMm = number(c, "physicalLengthMm");
            component.velocityFactor = number(c, "velocityFactor");
            component.dielectricConstant = number(c, "dielectricConstant");
            component.substrateHeightMm = number(c, "substrateHeightMm");
            component.stripWidthMm = number(c, "stripWidthMm");
            component.substrateDk = number(c, "substrateDk");
            component.lossTangent = number(c, "lossTangent");
        } else if (component.kind > rf::Kind::SeriesRlc) {
            fail("Component type is not supported in version 1 projects.");
        }
        if (component.kind == rf::Kind::Measured) {
            if (version < 3) fail("Measured blocks require project version 3.");
            component.measured = measuredValue(object(c, "measured"));
        }
        d.components.push_back(component);
    }
    rf::validate(d);
    return d;
}

rf::Document fromLegacy(const QByteArray& data)
{
    Archive a(data);
    rf::Document d;
    d.frequencyMHz = a.read<double>();
    const auto reference = a.complex();
    if (reference.imag() != 0)
        fail("Legacy complex chart reference impedances are not yet supported.");
    d.referenceOhms = reference.real();
    d.goalOhms = a.complex();
    d.startOhms = a.complex();
    d.tunePercent = a.read<double>();
    d.admittance = a.read<qint32>() != 0;
    d.polar = a.read<qint32>() != 0;
    d.qCurves = a.read<qint32>() != 0;
    d.markers = a.read<qint32>() != 0;
    d.vswrCircle = a.read<quint8>() != 0;
    d.selected = a.read<qint32>();
    const auto count = a.read<qint32>();
    if (count < 0 || count > 25) fail("Invalid legacy component count.");
    d.title = a.string().toStdString();
    a.read<quint8>(); a.read<quint8>();
    a.string(); a.string();
    a.read<qint32>(); a.read<qint32>();
    // The 2006 revision added three doubles; earlier archives have identical component records.
    if (a.remaining() == count * 78 + 24) {
        for (auto& frequency : d.frequencyPresets) frequency = a.read<double>();
    } else if (a.remaining() == count * 78) {
        d.frequencyPresets.fill(d.frequencyMHz);
    } else {
        fail("Unrecognized legacy archive layout; expected a 2003-2007 .lls project.");
    }
    for (int i = 0; i < count; ++i) {
        const auto kind = static_cast<rf::Kind>(a.read<qint32>());
        if (kind < rf::Kind::Resistor || kind > rf::Kind::SeriesRlc)
            fail("Unknown legacy component type.");
        auto c = rf::defaultComponent(kind, d.frequencyMHz, d.referenceOhms);
        c.connection = static_cast<rf::Connection>(a.read<qint32>());
        const auto tuning = a.read<qint32>();
        const double resistance = a.read<double>();
        const double capacitance = a.read<double>();
        const double inductance = a.read<double>();
        const double lineOhms = a.read<double>();
        const double degrees = a.read<double>();
        const double frequency = a.read<double>();
        // Unused fields in the original records were often uninitialized.
        if (kind == rf::Kind::Resistor || kind == rf::Kind::ParallelRlc || kind == rf::Kind::SeriesRlc)
            c.resistance = resistance;
        if (kind == rf::Kind::Capacitor || kind == rf::Kind::ParallelRlc || kind == rf::Kind::SeriesRlc)
            c.capacitancePf = capacitance;
        if (kind == rf::Kind::Inductor || kind == rf::Kind::ParallelRlc || kind == rf::Kind::SeriesRlc)
            c.inductanceNh = inductance;
        if (kind == rf::Kind::ParallelRlc || kind == rf::Kind::SeriesRlc)
            c.tune = static_cast<rf::Tune>(tuning);
        if (kind == rf::Kind::Line) {
            c.lineOhms = lineOhms;
            c.lengthDegrees = degrees;
            c.referenceMHz = frequency;
        }
        // Two floats, two one-byte bools, and two ints reserved by the legacy writer.
        a.read<quint32>(); a.read<quint32>();
        a.read<quint8>(); a.read<quint8>();
        a.read<qint32>(); a.read<qint32>();
        d.components.push_back(c);
    }
    if (d.components.empty()) d.selected = -1;
    rf::validate(d);
    return d;
}

rf::Document load(const QString& path, bool* imported)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
    if (file.size() > 4 * 1024 * 1024) fail("Project exceeds the 4 MiB size limit.");
    const auto data = file.readAll();
    if (file.error() != QFileDevice::NoError) fail(file.errorString());
    const bool legacy = !data.trimmed().startsWith('{');
    auto document = legacy ? fromLegacy(data) : fromJson(data);
    if (imported) *imported = legacy;
    return document;
}

void save(const QString& path, const rf::Document& document)
{
    const auto data = toJson(document);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) fail(file.errorString());
    if (file.write(data) != data.size()) fail(file.errorString());
    if (!file.commit()) fail(file.errorString());
}
}
