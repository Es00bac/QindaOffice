// SPDX-License-Identifier: GPL-3.0-or-later
// Custom Views in the crash-recovery payload (calc_recovery_views.h).

#include "calc_recovery_views.h"

#include "calc_recovery_m78.h"
#include "calc_recovery_rules.h"

#include <QJsonObject>

namespace QindaOffice::Apps::QindaCalc {

namespace {

QJsonArray pairsToJson(const QList<std::pair<QString, QString>> &pairs)
{
    QJsonArray out;
    for (const auto &[name, value] : pairs) {
        out.append(QJsonArray{name, value});
    }
    return out;
}

QList<std::pair<QString, QString>> pairsFromJson(const QJsonValue &value)
{
    QList<std::pair<QString, QString>> out;
    for (const QJsonValue &entry : value.toArray()) {
        const QJsonArray pair = entry.toArray();
        if (pair.size() == 2) {
            out.append({pair.at(0).toString(), pair.at(1).toString()});
        }
    }
    return out;
}

QJsonArray cellToJson(const CalcEng::CellAddr &addr)
{
    return QJsonArray{addr.row, addr.col};
}

CalcEng::CellAddr cellFromJson(const QJsonValue &value)
{
    const QJsonArray pair = value.toArray();
    return pair.size() == 2 ? CalcEng::CellAddr{pair.at(0).toInt(), pair.at(1).toInt()} : CalcEng::CellAddr{0, 0};
}

QJsonValue rangeToJson(const std::optional<CalcEng::CellRange> &range)
{
    if (!range.has_value()) {
        return QJsonValue();
    }
    return QJsonArray{range->topLeft.row, range->topLeft.col, range->bottomRight.row, range->bottomRight.col};
}

std::optional<CalcEng::CellRange> rangeFromJson(const QJsonValue &value)
{
    const QJsonArray quad = value.toArray();
    if (quad.size() != 4) {
        return std::nullopt;
    }
    return CalcEng::CellRange{{quad.at(0).toInt(), quad.at(1).toInt()}, {quad.at(2).toInt(), quad.at(3).toInt()}};
}

QJsonArray linesToJson(const QList<qint32> &lines)
{
    QJsonArray out;
    for (const qint32 line : lines) {
        out.append(line);
    }
    return out;
}

QList<qint32> linesFromJson(const QJsonValue &value)
{
    QList<qint32> out;
    for (const QJsonValue &line : value.toArray()) {
        out.append(line.toInt());
    }
    return out;
}

} // namespace

QJsonArray customViewSpecsToJson(const QList<CalcEng::CustomViewSpec> &views)
{
    QJsonArray out;
    for (const CalcEng::CustomViewSpec &view : views) {
        out.append(QJsonObject{{QStringLiteral("name"), view.name},
                               {QStringLiteral("guid"), view.guid},
                               {QStringLiteral("print"), view.includePrintSettings},
                               {QStringLiteral("hidden"), view.includeHiddenRowCol},
                               {QStringLiteral("sheet"), view.activeSheet},
                               {QStringLiteral("attributes"), pairsToJson(view.attributes)}});
    }
    return out;
}

QList<CalcEng::CustomViewSpec> customViewSpecsFromJson(const QJsonArray &array)
{
    QList<CalcEng::CustomViewSpec> out;
    for (const QJsonValue &entry : array) {
        const QJsonObject o = entry.toObject();
        CalcEng::CustomViewSpec view;
        view.name = o.value(QStringLiteral("name")).toString();
        view.guid = o.value(QStringLiteral("guid")).toString();
        view.includePrintSettings = o.value(QStringLiteral("print")).toBool(true);
        view.includeHiddenRowCol = o.value(QStringLiteral("hidden")).toBool(true);
        view.activeSheet = o.value(QStringLiteral("sheet")).toString();
        view.attributes = pairsFromJson(o.value(QStringLiteral("attributes")));
        if (!view.guid.isEmpty()) {
            out.append(view);
        }
    }
    return out;
}

QJsonArray sheetCustomViewsToJson(const QList<CalcEng::Sheet::CustomView> &parts)
{
    QJsonArray out;
    for (const CalcEng::Sheet::CustomView &part : parts) {
        QJsonObject o{{QStringLiteral("guid"), part.guid},
                      {QStringLiteral("scale"), part.scale},
                      {QStringLiteral("split"), QJsonArray{part.splitX, part.splitY}},
                      {QStringLiteral("topLeft"), cellToJson(part.topLeft)},
                      {QStringLiteral("splitTopLeft"), cellToJson(part.splitTopLeft)},
                      {QStringLiteral("active"), cellToJson(part.activeCell)},
                      {QStringLiteral("freeze"), QJsonArray{part.freezeRows, part.freezeCols}},
                      {QStringLiteral("sheetHidden"), part.sheetHidden},
                      {QStringLiteral("hiddenRows"), linesToJson(part.hiddenRows)},
                      {QStringLiteral("hiddenColumns"), linesToJson(part.hiddenColumns)},
                      {QStringLiteral("filter"), part.filter},
                      {QStringLiteral("autoFilter"), autoFilterToJson(part.autoFilter)},
                      {QStringLiteral("printSettings"), part.printSettings},
                      {QStringLiteral("attributes"), pairsToJson(part.attributes)}};
        if (part.printSettings) {
            o.insert(QStringLiteral("pageSetup"), pageSetupToJson(part.pageSetup));
            o.insert(QStringLiteral("printArea"), rangeToJson(part.printArea));
            o.insert(QStringLiteral("printTitleRows"), rangeToJson(part.printTitleRows));
            o.insert(QStringLiteral("printTitleCols"), rangeToJson(part.printTitleCols));
        }
        out.append(o);
    }
    return out;
}

QList<CalcEng::Sheet::CustomView> sheetCustomViewsFromJson(const QJsonArray &array)
{
    QList<CalcEng::Sheet::CustomView> out;
    for (const QJsonValue &entry : array) {
        const QJsonObject o = entry.toObject();
        CalcEng::Sheet::CustomView part;
        part.guid = o.value(QStringLiteral("guid")).toString();
        part.scale = o.value(QStringLiteral("scale")).toInt(100);
        const QJsonArray split = o.value(QStringLiteral("split")).toArray();
        part.splitX = split.at(0).toDouble();
        part.splitY = split.at(1).toDouble();
        part.topLeft = cellFromJson(o.value(QStringLiteral("topLeft")));
        part.splitTopLeft = cellFromJson(o.value(QStringLiteral("splitTopLeft")));
        part.activeCell = cellFromJson(o.value(QStringLiteral("active")));
        const QJsonArray freeze = o.value(QStringLiteral("freeze")).toArray();
        part.freezeRows = freeze.at(0).toInt();
        part.freezeCols = freeze.at(1).toInt();
        part.sheetHidden = o.value(QStringLiteral("sheetHidden")).toBool();
        part.hiddenRows = linesFromJson(o.value(QStringLiteral("hiddenRows")));
        part.hiddenColumns = linesFromJson(o.value(QStringLiteral("hiddenColumns")));
        part.filter = o.value(QStringLiteral("filter")).toBool();
        part.autoFilter = autoFilterFromJson(o.value(QStringLiteral("autoFilter")).toObject());
        part.printSettings = o.value(QStringLiteral("printSettings")).toBool();
        if (part.printSettings) {
            part.pageSetup = pageSetupFromJson(o.value(QStringLiteral("pageSetup")));
            part.printArea = rangeFromJson(o.value(QStringLiteral("printArea")));
            part.printTitleRows = rangeFromJson(o.value(QStringLiteral("printTitleRows")));
            part.printTitleCols = rangeFromJson(o.value(QStringLiteral("printTitleCols")));
        }
        part.attributes = pairsFromJson(o.value(QStringLiteral("attributes")));
        if (!part.guid.isEmpty()) {
            out.append(part);
        }
    }
    return out;
}

} // namespace QindaOffice::Apps::QindaCalc
