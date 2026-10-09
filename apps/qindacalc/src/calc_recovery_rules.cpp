// SPDX-License-Identifier: GPL-3.0-or-later
// Crash-recovery serialization for the per-sheet RULES: conditional
// formats, data validation, the autofilter and cell comments. Split from
// calc_recovery.cpp to keep both files inside the line budget.
//
// AGENT-CONTRACT (D-012): the journal payload is the document's own state.
// A rule the payload does not carry is a rule the user loses on recovery,
// silently — which is worse than not offering the feature.

#include "calc_recovery_rules.h"

#include <QJsonArray>
#include <QJsonObject>

namespace QindaOffice::Apps::QindaCalc {

namespace {

QJsonArray rangeToJson(const CalcEng::CellRange &range)
{
    return QJsonArray{range.topLeft.row, range.topLeft.col, range.bottomRight.row,
                      range.bottomRight.col};
}

CalcEng::CellRange rangeFromJson(const QJsonArray &quad)
{
    if (quad.size() != 4) {
        return {};
    }
    return {{quad.at(0).toInt(), quad.at(1).toInt()},
            {quad.at(2).toInt(), quad.at(3).toInt()}};
}

QJsonValue rgbToJson(const std::optional<CalcEng::Rgb> &rgb)
{
    if (!rgb.has_value()) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonArray{rgb->r, rgb->g, rgb->b};
}

std::optional<CalcEng::Rgb> rgbFromJson(const QJsonValue &value)
{
    const QJsonArray triple = value.toArray();
    if (triple.size() != 3) {
        return std::nullopt;
    }
    return CalcEng::Rgb{quint8(triple.at(0).toInt()), quint8(triple.at(1).toInt()),
                        quint8(triple.at(2).toInt())};
}

QJsonValue boolToJson(const std::optional<bool> &flag)
{
    return flag.has_value() ? QJsonValue(*flag) : QJsonValue(QJsonValue::Null);
}

std::optional<bool> boolFromJson(const QJsonValue &value)
{
    return value.isBool() ? std::optional<bool>(value.toBool()) : std::nullopt;
}

} // namespace

QJsonObject condFormatsToJson(const QList<CalcEng::CondFormatRule> &rules)
{
    QJsonArray array;
    for (const CalcEng::CondFormatRule &rule : rules) {
        array.append(QJsonObject{
            {QStringLiteral("range"), rangeToJson(rule.range)},
            {QStringLiteral("op"), int(rule.op)},
            {QStringLiteral("first"), rule.first},
            {QStringLiteral("second"), rule.second},
            {QStringLiteral("text"), rule.text},
            {QStringLiteral("formula"), rule.formula},
            {QStringLiteral("bar"),
             QJsonArray{rule.barColor.r, rule.barColor.g, rule.barColor.b}},
            {QStringLiteral("stop"), rule.stopIfTrue},
            {QStringLiteral("bg"), rgbToJson(rule.format.background)},
            {QStringLiteral("fg"), rgbToJson(rule.format.foreground)},
            {QStringLiteral("bold"), boolToJson(rule.format.bold)},
            {QStringLiteral("italic"), boolToJson(rule.format.italic)},
        });
    }
    return QJsonObject{{QStringLiteral("rules"), array}};
}

QList<CalcEng::CondFormatRule> condFormatsFromJson(const QJsonObject &object)
{
    QList<CalcEng::CondFormatRule> rules;
    for (const QJsonValue &entry : object.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject item = entry.toObject();
        CalcEng::CondFormatRule rule;
        rule.range = rangeFromJson(item.value(QStringLiteral("range")).toArray());
        // Bound the stored enum: a corrupt payload must not index past the
        // switch in condRuleMatches.
        const int op = item.value(QStringLiteral("op")).toInt();
        rule.op = CalcEng::CondFormatRule::Op(
            qBound(0, op, int(CalcEng::CondFormatRule::Op::DataBar)));
        rule.first = item.value(QStringLiteral("first")).toDouble();
        rule.second = item.value(QStringLiteral("second")).toDouble();
        rule.text = item.value(QStringLiteral("text")).toString();
        rule.formula = item.value(QStringLiteral("formula")).toString();
        const QJsonArray bar = item.value(QStringLiteral("bar")).toArray();
        if (bar.size() == 3) {
            rule.barColor = CalcEng::Rgb{quint8(bar.at(0).toInt()), quint8(bar.at(1).toInt()),
                                         quint8(bar.at(2).toInt())};
        }
        rule.stopIfTrue = item.value(QStringLiteral("stop")).toBool(false);
        rule.format.background = rgbFromJson(item.value(QStringLiteral("bg")));
        rule.format.foreground = rgbFromJson(item.value(QStringLiteral("fg")));
        rule.format.bold = boolFromJson(item.value(QStringLiteral("bold")));
        rule.format.italic = boolFromJson(item.value(QStringLiteral("italic")));
        if (!rule.range.isValid()) {
            continue;
        }
        // An Expression rule whose formula does not reparse is dropped, like
        // an unreadable one on import — a rule that can never match must not
        // ride the document forever.
        if (CalcEng::prepareCondRule(rule)) {
            rules.append(rule);
        }
    }
    return rules;
}

QJsonObject validationsToJson(const QList<CalcEng::ValidationRule> &rules)
{
    QJsonArray array;
    for (const CalcEng::ValidationRule &rule : rules) {
        array.append(QJsonObject{
            {QStringLiteral("range"), rangeToJson(rule.range)},
            {QStringLiteral("kind"), int(rule.kind)},
            {QStringLiteral("min"), rule.min},
            {QStringLiteral("max"), rule.max},
            {QStringLiteral("list"), QJsonArray::fromStringList(rule.list)},
            {QStringLiteral("source"), rule.listSource},
            {QStringLiteral("message"), rule.message},
        });
    }
    return QJsonObject{{QStringLiteral("rules"), array}};
}

QList<CalcEng::ValidationRule> validationsFromJson(const QJsonObject &object)
{
    QList<CalcEng::ValidationRule> rules;
    for (const QJsonValue &entry : object.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject item = entry.toObject();
        CalcEng::ValidationRule rule;
        rule.range = rangeFromJson(item.value(QStringLiteral("range")).toArray());
        const int kind = item.value(QStringLiteral("kind")).toInt();
        rule.kind = CalcEng::ValidationRule::Kind(
            qBound(0, kind, int(CalcEng::ValidationRule::Kind::NotEmpty)));
        rule.min = item.value(QStringLiteral("min")).toDouble();
        rule.max = item.value(QStringLiteral("max")).toDouble();
        for (const QJsonValue &listed : item.value(QStringLiteral("list")).toArray()) {
            rule.list.append(listed.toString());
        }
        rule.listSource = item.value(QStringLiteral("source")).toString();
        rule.message = item.value(QStringLiteral("message")).toString();
        if (rule.range.isValid()) {
            rules.append(rule);
        }
    }
    return rules;
}

QJsonObject autoFilterToJson(const CalcEng::AutoFilter &filter)
{
    QJsonArray columns;
    for (const auto &[column, allowed] : filter.allowed.asKeyValueRange()) {
        columns.append(QJsonObject{{QStringLiteral("col"), column},
                                   {QStringLiteral("allowed"),
                                    QJsonArray::fromStringList(allowed)}});
    }
    return QJsonObject{{QStringLiteral("range"), rangeToJson(filter.range)},
                       {QStringLiteral("active"), filter.active},
                       {QStringLiteral("columns"), columns}};
}

CalcEng::AutoFilter autoFilterFromJson(const QJsonObject &object)
{
    CalcEng::AutoFilter filter;
    filter.range = rangeFromJson(object.value(QStringLiteral("range")).toArray());
    filter.active = object.value(QStringLiteral("active")).toBool();
    for (const QJsonValue &entry : object.value(QStringLiteral("columns")).toArray()) {
        const QJsonObject item = entry.toObject();
        QStringList allowed;
        for (const QJsonValue &value : item.value(QStringLiteral("allowed")).toArray()) {
            allowed.append(value.toString());
        }
        filter.allowed.insert(item.value(QStringLiteral("col")).toInt(), allowed);
    }
    return filter;
}

QJsonArray commentsToJson(const QHash<quint64, CalcEng::CellComment> &comments)
{
    QJsonArray array;
    for (const auto &[packed, comment] : comments.asKeyValueRange()) {
        array.append(QJsonObject{{QStringLiteral("row"), int(packed & 0xffffffffu)},
                                 {QStringLiteral("col"), int(packed >> 32)},
                                 {QStringLiteral("author"), comment.author},
                                 {QStringLiteral("text"), comment.text}});
    }
    return array;
}

QHash<quint64, CalcEng::CellComment> commentsFromJson(const QJsonArray &array)
{
    QHash<quint64, CalcEng::CellComment> comments;
    for (const QJsonValue &entry : array) {
        const QJsonObject item = entry.toObject();
        const CalcEng::CellAddr addr{item.value(QStringLiteral("row")).toInt(),
                                     item.value(QStringLiteral("col")).toInt()};
        if (!addr.isValid()) {
            continue;
        }
        comments.insert(addr.key(),
                        CalcEng::CellComment{item.value(QStringLiteral("author")).toString(),
                                             item.value(QStringLiteral("text")).toString()});
    }
    return comments;
}

QJsonArray tablesToJson(const QList<CalcEng::TableSpec> &tables)
{
    QJsonArray array;
    for (const CalcEng::TableSpec &table : tables) {
        array.append(QJsonObject{
            {QStringLiteral("name"), table.name},
            {QStringLiteral("range"), rangeToJson(table.range)},
            {QStringLiteral("header"), table.headerRow},
            {QStringLiteral("totals"), table.totalsRow},
            {QStringLiteral("style"), table.styleName},
            {QStringLiteral("rowStripes"), table.rowStripes},
            {QStringLiteral("colStripes"), table.colStripes},
            {QStringLiteral("columns"), QJsonArray::fromStringList(table.columnNames)},
            {QStringLiteral("sourcePart"), table.sourcePart}, // M66: its query table travels
        });
    }
    return array;
}

QList<CalcEng::TableSpec> tablesFromJson(const QJsonArray &array)
{
    QList<CalcEng::TableSpec> tables;
    for (const QJsonValue &entry : array) {
        const QJsonObject item = entry.toObject();
        CalcEng::TableSpec table;
        table.name = item.value(QStringLiteral("name")).toString();
        table.range = rangeFromJson(item.value(QStringLiteral("range")).toArray());
        table.headerRow = item.value(QStringLiteral("header")).toBool(true);
        table.totalsRow = item.value(QStringLiteral("totals")).toBool(false);
        table.styleName = item.value(QStringLiteral("style")).toString();
        table.rowStripes = item.value(QStringLiteral("rowStripes")).toBool(true);
        table.colStripes = item.value(QStringLiteral("colStripes")).toBool(false);
        table.sourcePart = item.value(QStringLiteral("sourcePart")).toString();
        for (const QJsonValue &column : item.value(QStringLiteral("columns")).toArray()) {
            table.columnNames.append(column.toString());
        }
        if (table.range.isValid() && !table.name.isEmpty()) {
            tables.append(table);
        }
    }
    return tables;
}

QJsonArray definedNamesToJson(const QList<CalcEng::DefinedName> &names)
{
    QJsonArray array;
    for (const CalcEng::DefinedName &name : names) {
        array.append(QJsonObject{
            {QStringLiteral("name"), name.name},
            {QStringLiteral("sheet"), name.sheet},
            {QStringLiteral("formula"), name.formula},
            {QStringLiteral("hidden"), name.hidden},
            {QStringLiteral("comment"), name.comment},
        });
    }
    return array;
}

QList<CalcEng::DefinedName> definedNamesFromJson(const QJsonArray &array)
{
    QList<CalcEng::DefinedName> names;
    for (const QJsonValue &entry : array) {
        const QJsonObject item = entry.toObject();
        CalcEng::DefinedName name;
        name.name = item.value(QStringLiteral("name")).toString();
        name.sheet = item.value(QStringLiteral("sheet")).toInt(-1);
        name.formula = item.value(QStringLiteral("formula")).toString();
        name.hidden = item.value(QStringLiteral("hidden")).toBool();
        name.comment = item.value(QStringLiteral("comment")).toString();
        if (!name.name.isEmpty()) {
            names.append(name); // the body is parsed by Workbook::setDefinedNames
        }
    }
    return names;
}

QJsonObject sourceToJson(const CalcEng::WorkbookSource &source)
{
    return QJsonObject{
        {QStringLiteral("theme"), QString::fromLatin1(source.themeXml.toBase64())},
        {QStringLiteral("title"), source.title},
        {QStringLiteral("subject"), source.subject},
        {QStringLiteral("creator"), source.creator},
        {QStringLiteral("keywords"), source.keywords},
        {QStringLiteral("description"), source.description},
        {QStringLiteral("category"), source.category},
        {QStringLiteral("created"), source.created.toString(Qt::ISODate)},
        // M66 (D-075): the parts a save carries, so a recovered workbook
        // still saves its pivot tables, slicers, controls and macros.
        {QStringLiteral("carried"), QString::fromLatin1(source.carried.toBase64())},
        {QStringLiteral("codeName"), source.codeName},
        {QStringLiteral("macroEnabled"), source.macroEnabled},
    };
}

CalcEng::WorkbookSource sourceFromJson(const QJsonObject &object)
{
    CalcEng::WorkbookSource source;
    source.themeXml = QByteArray::fromBase64(object.value(QStringLiteral("theme")).toString().toLatin1());
    source.title = object.value(QStringLiteral("title")).toString();
    source.subject = object.value(QStringLiteral("subject")).toString();
    source.creator = object.value(QStringLiteral("creator")).toString();
    source.keywords = object.value(QStringLiteral("keywords")).toString();
    source.description = object.value(QStringLiteral("description")).toString();
    source.category = object.value(QStringLiteral("category")).toString();
    source.created = QDateTime::fromString(object.value(QStringLiteral("created")).toString(), Qt::ISODate);
    source.carried = QByteArray::fromBase64(object.value(QStringLiteral("carried")).toString().toLatin1());
    source.codeName = object.value(QStringLiteral("codeName")).toString();
    source.macroEnabled = object.value(QStringLiteral("macroEnabled")).toBool(false);
    return source;
}

} // namespace QindaOffice::Apps::QindaCalc
