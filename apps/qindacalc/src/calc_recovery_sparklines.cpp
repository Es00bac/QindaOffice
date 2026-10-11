// SPDX-License-Identifier: GPL-3.0-or-later
// Sparkline groups in the crash-recovery payload (calc_recovery_sparklines.h).

#include "calc_recovery_sparklines.h"

#include <QJsonObject>

namespace QindaOffice::Apps::QindaCalc {

using CalcEng::SparklineColor;
using CalcEng::SparklineGroup;

namespace {

QJsonValue colorToJson(const SparklineColor &color)
{
    if (!color.isSet()) {
        return QJsonValue();
    }
    QJsonObject out;
    if (color.rgb.has_value()) {
        out.insert(QStringLiteral("rgb"), QJsonArray{color.rgb->r, color.rgb->g, color.rgb->b});
    }
    out.insert(QStringLiteral("theme"), color.theme);
    out.insert(QStringLiteral("tint"), color.tint);
    return out;
}

SparklineColor colorFromJson(const QJsonValue &value)
{
    SparklineColor color;
    const QJsonObject object = value.toObject();
    if (object.isEmpty()) {
        return color;
    }
    const QJsonArray rgb = object.value(QStringLiteral("rgb")).toArray();
    if (rgb.size() == 3) {
        color.rgb = CalcEng::Rgb{quint8(rgb.at(0).toInt()), quint8(rgb.at(1).toInt()), quint8(rgb.at(2).toInt())};
    }
    color.theme = object.value(QStringLiteral("theme")).toInt(-1);
    color.tint = object.value(QStringLiteral("tint")).toDouble();
    return color;
}

// The eight colour slots, in the file's order.
QList<std::pair<const char *, SparklineColor SparklineGroup::*>> colorSlots()
{
    return {{"series", &SparklineGroup::series},       {"negative", &SparklineGroup::negativeColor},
            {"axis", &SparklineGroup::axisColor},       {"markersColor", &SparklineGroup::markersColor},
            {"firstColor", &SparklineGroup::firstColor}, {"lastColor", &SparklineGroup::lastColor},
            {"highColor", &SparklineGroup::highColor},   {"lowColor", &SparklineGroup::lowColor}};
}

QList<std::pair<const char *, bool SparklineGroup::*>> flagSlots()
{
    return {{"markers", &SparklineGroup::markers},     {"high", &SparklineGroup::high},
            {"low", &SparklineGroup::low},             {"first", &SparklineGroup::first},
            {"last", &SparklineGroup::last},           {"negativePoints", &SparklineGroup::negative},
            {"displayXAxis", &SparklineGroup::displayXAxis}, {"displayHidden", &SparklineGroup::displayHidden},
            {"rightToLeft", &SparklineGroup::rightToLeft},   {"dateAxis", &SparklineGroup::dateAxis}};
}

} // namespace

QJsonArray sparklineGroupsToJson(const QList<SparklineGroup> &groups)
{
    QJsonArray out;
    for (const SparklineGroup &group : groups) {
        QJsonObject object{{QStringLiteral("type"), int(group.type)},
                           {QStringLiteral("lineWeight"), group.lineWeight},
                           {QStringLiteral("empty"), int(group.empty)},
                           {QStringLiteral("minAxis"), int(group.minAxis)},
                           {QStringLiteral("maxAxis"), int(group.maxAxis)},
                           {QStringLiteral("manualMin"), group.manualMin},
                           {QStringLiteral("manualMax"), group.manualMax},
                           {QStringLiteral("dateRange"), group.dateRange}};
        for (const auto &[key, member] : flagSlots()) {
            object.insert(QLatin1String(key), group.*member);
        }
        for (const auto &[key, member] : colorSlots()) {
            object.insert(QLatin1String(key), colorToJson(group.*member));
        }
        QJsonArray lines;
        for (const CalcEng::Sparkline &line : group.sparklines) {
            lines.append(QJsonArray{line.location.row, line.location.col, line.data});
        }
        object.insert(QStringLiteral("sparklines"), lines);
        QJsonArray extra;
        for (const auto &[name, value] : group.extraAttributes) {
            extra.append(QJsonArray{name, value});
        }
        object.insert(QStringLiteral("extra"), extra);
        out.append(object);
    }
    return out;
}

QList<SparklineGroup> sparklineGroupsFromJson(const QJsonArray &array)
{
    QList<SparklineGroup> out;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        SparklineGroup group;
        group.type = SparklineGroup::Type(qBound(0, object.value(QStringLiteral("type")).toInt(), 2));
        group.lineWeight = object.value(QStringLiteral("lineWeight")).toDouble(0.75);
        group.empty = SparklineGroup::Empty(qBound(0, object.value(QStringLiteral("empty")).toInt(), 2));
        group.minAxis = SparklineGroup::AxisScale(qBound(0, object.value(QStringLiteral("minAxis")).toInt(), 2));
        group.maxAxis = SparklineGroup::AxisScale(qBound(0, object.value(QStringLiteral("maxAxis")).toInt(), 2));
        group.manualMin = object.value(QStringLiteral("manualMin")).toDouble();
        group.manualMax = object.value(QStringLiteral("manualMax")).toDouble();
        group.dateRange = object.value(QStringLiteral("dateRange")).toString();
        for (const auto &[key, member] : flagSlots()) {
            group.*member = object.value(QLatin1String(key)).toBool();
        }
        for (const auto &[key, member] : colorSlots()) {
            group.*member = colorFromJson(object.value(QLatin1String(key)));
        }
        for (const QJsonValue &entry : object.value(QStringLiteral("sparklines")).toArray()) {
            const QJsonArray line = entry.toArray();
            if (line.size() == 3) {
                group.sparklines.append({{line.at(0).toInt(), line.at(1).toInt()}, line.at(2).toString()});
            }
        }
        for (const QJsonValue &entry : object.value(QStringLiteral("extra")).toArray()) {
            const QJsonArray pair = entry.toArray();
            if (pair.size() == 2) {
                group.extraAttributes.append({pair.at(0).toString(), pair.at(1).toString()});
            }
        }
        if (!group.sparklines.isEmpty()) {
            out.append(group);
        }
    }
    return out;
}

} // namespace QindaOffice::Apps::QindaCalc
