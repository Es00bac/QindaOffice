// SPDX-License-Identifier: GPL-3.0-or-later
// Recovery JSON for the M66 state (calc_recovery_features.h, D-075).

#include "calc_recovery_features.h"

#include <array>
#include <utility>

namespace QindaOffice::Apps::QindaCalc {

namespace {

QJsonArray rangeToJson(const CalcEng::CellRange &range)
{
    return {range.topLeft.row, range.topLeft.col, range.bottomRight.row, range.bottomRight.col};
}

CalcEng::CellRange rangeFromJson(const QJsonValue &value)
{
    const QJsonArray quad = value.toArray();
    if (quad.size() != 4) {
        return {};
    }
    return {{quad.at(0).toInt(), quad.at(1).toInt()}, {quad.at(2).toInt(), quad.at(3).toInt()}};
}

QJsonObject linesToJson(const QMap<qint32, CalcEng::LineOutline> &lines)
{
    QJsonObject out;
    for (auto it = lines.constBegin(); it != lines.constEnd(); ++it) {
        out.insert(QString::number(it.key()),
                   QJsonArray{it.value().level, it.value().hidden, it.value().collapsed});
    }
    return out;
}

QMap<qint32, CalcEng::LineOutline> linesFromJson(const QJsonObject &object)
{
    QMap<qint32, CalcEng::LineOutline> out;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QJsonArray triple = it.value().toArray();
        const CalcEng::LineOutline line{quint8(qBound(0, triple.at(0).toInt(), int(CalcEng::kMaxOutlineLevel))),
                                        triple.at(1).toBool(), triple.at(2).toBool()};
        if (!line.isDefault()) {
            out.insert(it.key().toInt(), line);
        }
    }
    return out;
}

// The sheet protection's locked-action flags, with the schema defaults
// (only differences are written).
using Flag = bool CalcEng::SheetProtection::*;
const std::array<std::pair<const char *, Flag>, 15> &protectionFlags()
{
    static const std::array<std::pair<const char *, Flag>, 15> flags{{
        {"objects", &CalcEng::SheetProtection::objects},
        {"scenarios", &CalcEng::SheetProtection::scenarios},
        {"formatCells", &CalcEng::SheetProtection::formatCells},
        {"formatColumns", &CalcEng::SheetProtection::formatColumns},
        {"formatRows", &CalcEng::SheetProtection::formatRows},
        {"insertColumns", &CalcEng::SheetProtection::insertColumns},
        {"insertRows", &CalcEng::SheetProtection::insertRows},
        {"insertHyperlinks", &CalcEng::SheetProtection::insertHyperlinks},
        {"deleteColumns", &CalcEng::SheetProtection::deleteColumns},
        {"deleteRows", &CalcEng::SheetProtection::deleteRows},
        {"selectLockedCells", &CalcEng::SheetProtection::selectLockedCells},
        {"sort", &CalcEng::SheetProtection::sort},
        {"autoFilter", &CalcEng::SheetProtection::autoFilter},
        {"pivotTables", &CalcEng::SheetProtection::pivotTables},
        {"selectUnlockedCells", &CalcEng::SheetProtection::selectUnlockedCells},
    }};
    return flags;
}

} // namespace

QJsonArray hyperlinksToJson(const QList<CalcEng::Hyperlink> &links)
{
    QJsonArray out;
    for (const CalcEng::Hyperlink &link : links) {
        out.append(QJsonObject{{QStringLiteral("range"), rangeToJson(link.range)},
                               {QStringLiteral("address"), link.address},
                               {QStringLiteral("location"), link.location},
                               {QStringLiteral("display"), link.display},
                               {QStringLiteral("tooltip"), link.tooltip}});
    }
    return out;
}

QList<CalcEng::Hyperlink> hyperlinksFromJson(const QJsonArray &array)
{
    QList<CalcEng::Hyperlink> out;
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        CalcEng::Hyperlink link{rangeFromJson(o.value(QStringLiteral("range"))), o.value(QStringLiteral("address")).toString(),
                                o.value(QStringLiteral("location")).toString(), o.value(QStringLiteral("display")).toString(),
                                o.value(QStringLiteral("tooltip")).toString()};
        if (link.range.isValid()) {
            out.append(link);
        }
    }
    return out;
}

QJsonObject outlineToJson(const CalcEng::SheetOutline &outline)
{
    QJsonObject out;
    if (!outline.rows.isEmpty()) {
        out.insert(QStringLiteral("rows"), linesToJson(outline.rows));
    }
    if (!outline.cols.isEmpty()) {
        out.insert(QStringLiteral("cols"), linesToJson(outline.cols));
    }
    if (!outline.summaryBelow) {
        out.insert(QStringLiteral("summaryBelow"), false);
    }
    if (!outline.summaryRight) {
        out.insert(QStringLiteral("summaryRight"), false);
    }
    return out;
}

CalcEng::SheetOutline outlineFromJson(const QJsonObject &object)
{
    CalcEng::SheetOutline outline;
    outline.rows = linesFromJson(object.value(QStringLiteral("rows")).toObject());
    outline.cols = linesFromJson(object.value(QStringLiteral("cols")).toObject());
    outline.summaryBelow = object.value(QStringLiteral("summaryBelow")).toBool(true);
    outline.summaryRight = object.value(QStringLiteral("summaryRight")).toBool(true);
    return outline;
}

QJsonObject protectionToJson(const CalcEng::SheetProtection &p)
{
    if (p == CalcEng::SheetProtection{}) {
        return {};
    }
    QJsonObject out{{QStringLiteral("enabled"), p.enabled},
                    {QStringLiteral("algorithm"), p.algorithmName},
                    {QStringLiteral("hash"), p.hashValue},
                    {QStringLiteral("salt"), p.saltValue},
                    {QStringLiteral("spin"), p.spinCount},
                    {QStringLiteral("legacy"), p.legacyHash}};
    const CalcEng::SheetProtection defaults;
    for (const auto &[name, member] : protectionFlags()) {
        if (p.*member != defaults.*member) {
            out.insert(QString::fromLatin1(name), p.*member);
        }
    }
    return out;
}

CalcEng::SheetProtection protectionFromJson(const QJsonObject &o)
{
    CalcEng::SheetProtection p;
    p.enabled = o.value(QStringLiteral("enabled")).toBool(false);
    p.algorithmName = o.value(QStringLiteral("algorithm")).toString();
    p.hashValue = o.value(QStringLiteral("hash")).toString();
    p.saltValue = o.value(QStringLiteral("salt")).toString();
    p.spinCount = o.value(QStringLiteral("spin")).toInt();
    p.legacyHash = o.value(QStringLiteral("legacy")).toString();
    for (const auto &[name, member] : protectionFlags()) {
        p.*member = o.value(QString::fromLatin1(name)).toBool(p.*member);
    }
    return p;
}

QJsonObject workbookProtectionToJson(const CalcEng::WorkbookProtection &p)
{
    if (p == CalcEng::WorkbookProtection{}) {
        return {};
    }
    return {{QStringLiteral("structure"), p.lockStructure},   {QStringLiteral("windows"), p.lockWindows},
            {QStringLiteral("revision"), p.lockRevision},     {QStringLiteral("algorithm"), p.algorithmName},
            {QStringLiteral("hash"), p.hashValue},            {QStringLiteral("salt"), p.saltValue},
            {QStringLiteral("spin"), p.spinCount},            {QStringLiteral("legacy"), p.legacyHash},
            {QStringLiteral("rAlgorithm"), p.revisionsAlgorithmName}, {QStringLiteral("rHash"), p.revisionsHashValue},
            {QStringLiteral("rSalt"), p.revisionsSaltValue},  {QStringLiteral("rSpin"), p.revisionsSpinCount},
            {QStringLiteral("rLegacy"), p.revisionsLegacyHash}};
}

CalcEng::WorkbookProtection workbookProtectionFromJson(const QJsonObject &o)
{
    CalcEng::WorkbookProtection p;
    p.lockStructure = o.value(QStringLiteral("structure")).toBool(false);
    p.lockWindows = o.value(QStringLiteral("windows")).toBool(false);
    p.lockRevision = o.value(QStringLiteral("revision")).toBool(false);
    p.algorithmName = o.value(QStringLiteral("algorithm")).toString();
    p.hashValue = o.value(QStringLiteral("hash")).toString();
    p.saltValue = o.value(QStringLiteral("salt")).toString();
    p.spinCount = o.value(QStringLiteral("spin")).toInt();
    p.legacyHash = o.value(QStringLiteral("legacy")).toString();
    p.revisionsAlgorithmName = o.value(QStringLiteral("rAlgorithm")).toString();
    p.revisionsHashValue = o.value(QStringLiteral("rHash")).toString();
    p.revisionsSaltValue = o.value(QStringLiteral("rSalt")).toString();
    p.revisionsSpinCount = o.value(QStringLiteral("rSpin")).toInt();
    p.revisionsLegacyHash = o.value(QStringLiteral("rLegacy")).toString();
    return p;
}

QJsonObject sheetSourceToJson(const CalcEng::SheetSource &source)
{
    if (source == CalcEng::SheetSource{}) {
        return {};
    }
    QJsonArray steps;
    for (const CalcEng::SourceStep &step : source.steps) {
        steps.append(QJsonArray{step.rows, step.insert, step.at, step.count});
    }
    QJsonArray modelled;
    for (const qint32 index : source.modelledObjects) {
        modelled.append(index);
    }
    return {{QStringLiteral("part"), source.part},
            {QStringLiteral("codeName"), source.codeName},
            {QStringLiteral("steps"), steps},
            {QStringLiteral("modelled"), modelled}};
}

CalcEng::SheetSource sheetSourceFromJson(const QJsonObject &object)
{
    CalcEng::SheetSource source;
    source.part = object.value(QStringLiteral("part")).toString();
    source.codeName = object.value(QStringLiteral("codeName")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("steps")).toArray()) {
        const QJsonArray step = value.toArray();
        if (step.size() == 4) {
            source.steps.append({step.at(0).toBool(), step.at(1).toBool(), step.at(2).toInt(), step.at(3).toInt()});
        }
    }
    for (const QJsonValue &value : object.value(QStringLiteral("modelled")).toArray()) {
        source.modelledObjects.append(value.toInt());
    }
    return source;
}

} // namespace QindaOffice::Apps::QindaCalc
