// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_recovery.h"

#include "calc_recovery_rules.h"

#include "eval.h"
#include "parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

namespace {

// --- Value ------------------------------------------------------------
// Typed, never display text: toText() of the string "007" and of the number
// 7 both read "007"/"7", so a text cell that looks numeric would come back
// as a number. Each variant alternative gets its own key.
QJsonObject valueToJson(const CalcEng::Value &value)
{
    QJsonObject out;
    if (const auto *d = std::get_if<double>(&value)) {
        out.insert(QStringLiteral("n"), *d);
    } else if (const auto *t = std::get_if<QString>(&value)) {
        out.insert(QStringLiteral("t"), *t);
    } else if (const auto *b = std::get_if<bool>(&value)) {
        out.insert(QStringLiteral("b"), *b);
    } else if (const auto *e = std::get_if<CalcEng::Err>(&value)) {
        out.insert(QStringLiteral("e"), CalcEng::errText(*e));
    }
    return out; // monostate: {} (blank)
}

CalcEng::Value valueFromJson(const QJsonObject &object)
{
    if (object.contains(QStringLiteral("n"))) {
        return object.value(QStringLiteral("n")).toDouble();
    }
    if (object.contains(QStringLiteral("t"))) {
        return object.value(QStringLiteral("t")).toString();
    }
    if (object.contains(QStringLiteral("b"))) {
        return object.value(QStringLiteral("b")).toBool();
    }
    if (object.contains(QStringLiteral("e"))) {
        return CalcEng::errFromExport(object.value(QStringLiteral("e")).toString());
    }
    return CalcEng::Value{};
}

// --- CellStyle --------------------------------------------------------
QJsonObject styleToJson(const CalcEng::CellStyle &style)
{
    QJsonObject out;
    out.insert(QStringLiteral("fmt"), style.numberFormat);
    if (!style.fontFamily.isEmpty()) out.insert(QStringLiteral("font"), style.fontFamily);
    if (style.fontSize > 0) out.insert(QStringLiteral("size"), style.fontSize);
    if (style.bold) out.insert(QStringLiteral("b"), true);
    if (style.italic) out.insert(QStringLiteral("i"), true);
    if (style.underline) out.insert(QStringLiteral("u"), true);
    if (style.strikeout) out.insert(QStringLiteral("strike"), true);
    if (style.wrap) out.insert(QStringLiteral("wrap"), true);
    if (style.alignment != 0) out.insert(QStringLiteral("align"), style.alignment);
    if (style.vAlign != 1) out.insert(QStringLiteral("valign"), style.vAlign);
    if (style.borders != 0) out.insert(QStringLiteral("borders"), style.borders);
    const auto rgb = [](const CalcEng::Rgb &c) {
        return QJsonArray{c.r, c.g, c.b};
    };
    if (style.fg.has_value()) out.insert(QStringLiteral("fg"), rgb(*style.fg));
    if (style.bg.has_value()) out.insert(QStringLiteral("bg"), rgb(*style.bg));
    return out;
}

CalcEng::CellStyle styleFromJson(const QJsonObject &object)
{
    CalcEng::CellStyle style;
    style.numberFormat = object.value(QStringLiteral("fmt")).toString(
        QStringLiteral("General"));
    style.fontFamily = object.value(QStringLiteral("font")).toString();
    style.fontSize = object.value(QStringLiteral("size")).toDouble(0);
    style.bold = object.value(QStringLiteral("b")).toBool(false);
    style.italic = object.value(QStringLiteral("i")).toBool(false);
    style.underline = object.value(QStringLiteral("u")).toBool(false);
    style.strikeout = object.value(QStringLiteral("strike")).toBool(false);
    style.wrap = object.value(QStringLiteral("wrap")).toBool(false);
    style.alignment = object.value(QStringLiteral("align")).toInt(0);
    // Absent means centered: payloads from before vAlign existed must not
    // flip every cell to the top.
    style.vAlign = object.value(QStringLiteral("valign")).toInt(1);
    style.borders = object.value(QStringLiteral("borders")).toInt(0);
    const auto rgb = [&object](const QString &key) -> std::optional<CalcEng::Rgb> {
        const QJsonArray array = object.value(key).toArray();
        if (array.size() != 3) {
            return std::nullopt;
        }
        return CalcEng::Rgb{quint8(array.at(0).toInt()), quint8(array.at(1).toInt()),
                            quint8(array.at(2).toInt())};
    };
    style.fg = rgb(QStringLiteral("fg"));
    style.bg = rgb(QStringLiteral("bg"));
    return style;
}

// --- ChartSpec --------------------------------------------------------
QJsonObject chartToJson(const CalcEng::ChartSpec &chart)
{
    QJsonArray series;
    for (const CalcEng::ChartSeriesSpec &ser : chart.series) {
        series.append(QJsonObject{{QStringLiteral("name"), ser.name},
                                  {QStringLiteral("cat"), ser.catRange},
                                  {QStringLiteral("val"), ser.valRange}});
    }
    return QJsonObject{
        {QStringLiteral("kind"), int(chart.kind)},
        {QStringLiteral("title"), chart.title},
        {QStringLiteral("range"), chart.rangeText},
        {QStringLiteral("headerH"), chart.headerH},
        {QStringLiteral("headerV"), chart.headerV},
        {QStringLiteral("series"), series},
        {QStringLiteral("sheet"), chart.sheet},
        {QStringLiteral("row"), chart.anchorRow},
        {QStringLiteral("col"), chart.anchorCol},
        {QStringLiteral("w"), chart.widthPx},
        {QStringLiteral("h"), chart.heightPx},
    };
}

// Pictures carry their encoded bytes base64'd. A crash payload is written
// on a timer, so this is the one place the bytes are copied per save — the
// alternative (a side-car directory) would leave the payload incomplete,
// and a recovery file that cannot restore what was on screen is worse than
// a larger one.
QJsonObject imageToJson(const CalcEng::ImageSpec &image)
{
    return QJsonObject{
        {QStringLiteral("bytes"), QString::fromLatin1(image.bytes->toBase64())},
        {QStringLiteral("format"), image.format},
        {QStringLiteral("sheet"), image.sheet},
        {QStringLiteral("row"), image.anchorRow},
        {QStringLiteral("col"), image.anchorCol},
        {QStringLiteral("w"), image.widthPx},
        {QStringLiteral("h"), image.heightPx},
    };
}

CalcEng::ImageSpec imageFromJson(const QJsonObject &object)
{
    CalcEng::ImageSpec image;
    image.bytes = QSharedPointer<const QByteArray>::create(QByteArray::fromBase64(
        object.value(QStringLiteral("bytes")).toString().toLatin1()));
    image.format = object.value(QStringLiteral("format")).toString(QStringLiteral("png"));
    image.sheet = object.value(QStringLiteral("sheet")).toInt(0);
    image.anchorRow = object.value(QStringLiteral("row")).toInt(0);
    image.anchorCol = object.value(QStringLiteral("col")).toInt(0);
    image.widthPx = object.value(QStringLiteral("w")).toInt(0);
    image.heightPx = object.value(QStringLiteral("h")).toInt(0);
    return image;
}

CalcEng::ChartSpec chartFromJson(const QJsonObject &object)
{
    CalcEng::ChartSpec chart;
    chart.kind = CalcEng::ChartSpec::Kind(
        qBound(0, object.value(QStringLiteral("kind")).toInt(0), int(CalcEng::ChartSpec::Kind::Scatter)));
    chart.title = object.value(QStringLiteral("title")).toString();
    chart.rangeText = object.value(QStringLiteral("range")).toString();
    chart.headerH = object.value(QStringLiteral("headerH")).toBool(false);
    chart.headerV = object.value(QStringLiteral("headerV")).toBool(false);
    for (const QJsonValue &entry : object.value(QStringLiteral("series")).toArray()) {
        const QJsonObject ser = entry.toObject();
        chart.series.append(CalcEng::ChartSeriesSpec{
            ser.value(QStringLiteral("name")).toString(),
            ser.value(QStringLiteral("cat")).toString(),
            ser.value(QStringLiteral("val")).toString()});
    }
    chart.sheet = object.value(QStringLiteral("sheet")).toInt(0);
    chart.anchorRow = object.value(QStringLiteral("row")).toInt(0);
    chart.anchorCol = object.value(QStringLiteral("col")).toInt(0);
    chart.widthPx = object.value(QStringLiteral("w")).toInt(480);
    chart.heightPx = object.value(QStringLiteral("h")).toInt(320);
    return chart;
}

// --- Cells ------------------------------------------------------------
QJsonObject cellToJson(quint64 key, const CalcEng::Cell &cell)
{
    QJsonObject out;
    out.insert(QStringLiteral("r"), int(key & 0xffffffffu));
    out.insert(QStringLiteral("c"), int(key >> 32));
    if (cell.hasFormula()) {
        // The formula TEXT is the source of truth; the AST and the volatile
        // flag are rebuilt by re-parsing, and every value is recomputed by
        // restoreSnapshot()'s recalc.
        out.insert(QStringLiteral("f"), cell.formula);
        return out;
    }
    const QJsonObject value = valueToJson(cell.value);
    for (auto it = value.constBegin(); it != value.constEnd(); ++it) {
        out.insert(it.key(), it.value());
    }
    return out;
}

CalcEng::Cell cellFromJson(const QJsonObject &object)
{
    CalcEng::Cell cell;
    if (object.contains(QStringLiteral("f"))) {
        cell.formula = object.value(QStringLiteral("f")).toString();
        CalcEng::ParseResult parsed = CalcEng::parseFormula(cell.formula);
        cell.ast = parsed.root;
        cell.isVolatile = CalcEng::containsVolatileCall(cell.ast);
        return cell;
    }
    cell.value = valueFromJson(object);
    return cell;
}

quint64 cellKey(const QJsonObject &object)
{
    const quint64 row = quint32(object.value(QStringLiteral("r")).toInt());
    const quint64 col = quint32(object.value(QStringLiteral("c")).toInt());
    return (col << 32) | row;
}

} // namespace

QByteArray snapshotToJson(const WorkbookSnapshot &snapshot)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kRecoveryVersion);
    root.insert(QStringLiteral("activeSheet"), snapshot.activeSheet);

    QJsonArray styles;
    for (const CalcEng::CellStyle &style : snapshot.styles) {
        styles.append(styleToJson(style));
    }
    root.insert(QStringLiteral("styles"), styles);

    // Named ranges keep their canonical text form ("Sheet1!$A$1:$A$5"), which
    // the engine's own reference parser reads back.
    QJsonObject names;
    for (auto it = snapshot.names.constBegin(); it != snapshot.names.constEnd(); ++it) {
        names.insert(it.key(), CalcEng::rangeRefText(it.value()));
    }
    root.insert(QStringLiteral("names"), names);
    root.insert(QStringLiteral("definedNames"), definedNamesToJson(snapshot.definedNames));
    root.insert(QStringLiteral("source"), sourceToJson(snapshot.source));

    QJsonArray charts;
    for (const CalcEng::ChartSpec &chart : snapshot.charts) {
        charts.append(chartToJson(chart));
    }
    root.insert(QStringLiteral("charts"), charts);
    QJsonArray images;
    for (const CalcEng::ImageSpec &image : snapshot.images) {
        if (image.isValid()) {
            images.append(imageToJson(image));
        }
    }
    root.insert(QStringLiteral("images"), images);

    QJsonArray sheets;
    for (const SheetSnapshot &sheet : snapshot.sheets) {
        QJsonObject out;
        out.insert(QStringLiteral("name"), sheet.name);
        QJsonArray cells;
        for (auto it = sheet.cells.constBegin(); it != sheet.cells.constEnd(); ++it) {
            cells.append(cellToJson(it.key(), it.value()));
        }
        out.insert(QStringLiteral("cells"), cells);
        // Style ids live in their own sparse map: restyleRange() styles EMPTY
        // cells too, and those have no entry in cells().
        QJsonArray styleIds;
        for (auto it = sheet.styleIds.constBegin(); it != sheet.styleIds.constEnd(); ++it) {
            styleIds.append(QJsonArray{int(it.key() & 0xffffffffu), int(it.key() >> 32),
                                       it.value()});
        }
        out.insert(QStringLiteral("styleIds"), styleIds);
        QJsonArray cols;
        for (auto it = sheet.colWidths.constBegin(); it != sheet.colWidths.constEnd(); ++it) {
            cols.append(QJsonArray{it.key(), it.value()});
        }
        out.insert(QStringLiteral("cols"), cols);
        QJsonArray rows;
        for (auto it = sheet.rowHeights.constBegin(); it != sheet.rowHeights.constEnd(); ++it) {
            rows.append(QJsonArray{it.key(), it.value()});
        }
        out.insert(QStringLiteral("rows"), rows);
        QJsonArray merges;
        for (const CalcEng::CellRange &merge : sheet.merges) {
            merges.append(QJsonArray{merge.topLeft.row, merge.topLeft.col,
                                     merge.bottomRight.row, merge.bottomRight.col});
        }
        out.insert(QStringLiteral("merges"), merges);
        out.insert(QStringLiteral("condFormats"), condFormatsToJson(sheet.condFormats));
        out.insert(QStringLiteral("validations"), validationsToJson(sheet.validations));
        out.insert(QStringLiteral("filter"), autoFilterToJson(sheet.filter));
        out.insert(QStringLiteral("comments"), commentsToJson(sheet.comments));
        out.insert(QStringLiteral("tables"), tablesToJson(sheet.tables));
        // View & print state (M37): round-trips to xlsx, rides recovery.
        if (sheet.freezeRows != 0 || sheet.freezeCols != 0) {
            out.insert(QStringLiteral("freeze"), QJsonArray{sheet.freezeRows, sheet.freezeCols});
        }
        if (sheet.hidden) {
            out.insert(QStringLiteral("hidden"), true);
        }
        if (sheet.cursor != CalcEng::CellAddr{0, 0}) {
            out.insert(QStringLiteral("cursor"), QJsonArray{sheet.cursor.row, sheet.cursor.col});
        }
        const auto optionalRange = [](const std::optional<CalcEng::CellRange> &range) {
            return range.has_value()
                ? QJsonArray{range->topLeft.row, range->topLeft.col,
                             range->bottomRight.row, range->bottomRight.col}
                : QJsonArray{};
        };
        if (sheet.printArea.has_value()) {
            out.insert(QStringLiteral("printArea"), optionalRange(sheet.printArea));
        }
        if (sheet.printTitleRows.has_value()) {
            out.insert(QStringLiteral("printTitleRows"), optionalRange(sheet.printTitleRows));
        }
        if (sheet.printTitleCols.has_value()) {
            out.insert(QStringLiteral("printTitleCols"), optionalRange(sheet.printTitleCols));
        }
        if (sheet.pageSetup.orientation != 0 || sheet.pageSetup.fitToWidth != 0
            || sheet.pageSetup.fitToHeight != 0) {
            out.insert(QStringLiteral("pageSetup"),
                       QJsonArray{sheet.pageSetup.orientation, sheet.pageSetup.fitToWidth,
                                  sheet.pageSetup.fitToHeight});
        }
        sheets.append(out);
    }
    root.insert(QStringLiteral("sheets"), sheets);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool snapshotFromJson(const QByteArray &json, WorkbookSnapshot *out, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(parseError.error != QJsonParseError::NoError
                        ? parseError.errorString()
                        : QStringLiteral("recovery payload is not a JSON object"));
    }
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt(0);
    if (version != kRecoveryVersion) {
        return fail(QStringLiteral("unsupported recovery format (version %1)").arg(version));
    }
    const QJsonArray sheetArray = root.value(QStringLiteral("sheets")).toArray();
    if (sheetArray.isEmpty()) {
        return fail(QStringLiteral("recovery payload has no sheets"));
    }

    WorkbookSnapshot snapshot;
    snapshot.activeSheet = root.value(QStringLiteral("activeSheet")).toInt(0);
    for (const QJsonValue &value : root.value(QStringLiteral("styles")).toArray()) {
        snapshot.styles.append(styleFromJson(value.toObject()));
    }
    const QJsonObject names = root.value(QStringLiteral("names")).toObject();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        CalcEng::ParseResult parsed = CalcEng::parseFormula(it.value().toString());
        if (parsed.root && parsed.root->kind == CalcEng::Node::Kind::Ref) {
            snapshot.names.insert(it.key(), parsed.root->ref);
        }
    }
    // Absent in payloads written before M61; an old one simply has none.
    snapshot.definedNames = definedNamesFromJson(root.value(QStringLiteral("definedNames")).toArray());
    snapshot.source = sourceFromJson(root.value(QStringLiteral("source")).toObject());
    for (const QJsonValue &value : root.value(QStringLiteral("charts")).toArray()) {
        snapshot.charts.append(chartFromJson(value.toObject()));
    }
    // Absent in payloads written before M39; an old one simply has none.
    for (const QJsonValue &value : root.value(QStringLiteral("images")).toArray()) {
        const CalcEng::ImageSpec image = imageFromJson(value.toObject());
        if (image.isValid()) {
            snapshot.images.append(image);
        }
    }
    for (const QJsonValue &value : sheetArray) {
        const QJsonObject object = value.toObject();
        SheetSnapshot sheet;
        sheet.name = object.value(QStringLiteral("name")).toString();
        if (sheet.name.isEmpty()) {
            return fail(QStringLiteral("recovery payload has an unnamed sheet"));
        }
        for (const QJsonValue &cell : object.value(QStringLiteral("cells")).toArray()) {
            const QJsonObject cellObject = cell.toObject();
            sheet.cells.insert(cellKey(cellObject), cellFromJson(cellObject));
        }
        for (const QJsonValue &entry : object.value(QStringLiteral("styleIds")).toArray()) {
            const QJsonArray triple = entry.toArray();
            if (triple.size() != 3) {
                continue;
            }
            const quint64 key = (quint64(quint32(triple.at(1).toInt())) << 32)
                | quint64(quint32(triple.at(0).toInt()));
            sheet.styleIds.insert(key, triple.at(2).toInt());
        }
        for (const QJsonValue &entry : object.value(QStringLiteral("cols")).toArray()) {
            const QJsonArray pair = entry.toArray();
            if (pair.size() == 2) {
                sheet.colWidths.insert(pair.at(0).toInt(), pair.at(1).toDouble());
            }
        }
        for (const QJsonValue &entry : object.value(QStringLiteral("rows")).toArray()) {
            const QJsonArray pair = entry.toArray();
            if (pair.size() == 2) {
                sheet.rowHeights.insert(pair.at(0).toInt(), pair.at(1).toDouble());
            }
        }
        for (const QJsonValue &entry : object.value(QStringLiteral("merges")).toArray()) {
            const QJsonArray quad = entry.toArray();
            if (quad.size() == 4) {
                sheet.merges.append(CalcEng::CellRange{
                    {quad.at(0).toInt(), quad.at(1).toInt()},
                    {quad.at(2).toInt(), quad.at(3).toInt()}});
            }
        }
        sheet.condFormats =
            condFormatsFromJson(object.value(QStringLiteral("condFormats")).toObject());
        sheet.validations =
            validationsFromJson(object.value(QStringLiteral("validations")).toObject());
        sheet.filter = autoFilterFromJson(object.value(QStringLiteral("filter")).toObject());
        sheet.comments = commentsFromJson(object.value(QStringLiteral("comments")).toArray());
        sheet.tables = tablesFromJson(object.value(QStringLiteral("tables")).toArray());
        const QJsonArray freeze = object.value(QStringLiteral("freeze")).toArray();
        if (freeze.size() == 2) {
            sheet.freezeRows = freeze.at(0).toInt();
            sheet.freezeCols = freeze.at(1).toInt();
        }
        sheet.hidden = object.value(QStringLiteral("hidden")).toBool(false);
        const QJsonArray cursor = object.value(QStringLiteral("cursor")).toArray();
        if (cursor.size() == 2) {
            sheet.cursor = {cursor.at(0).toInt(), cursor.at(1).toInt()};
        }
        const auto optionalRange = [](const QJsonValue &value) -> std::optional<CalcEng::CellRange> {
            const QJsonArray quad = value.toArray();
            if (quad.size() != 4) {
                return std::nullopt;
            }
            return CalcEng::CellRange{{quad.at(0).toInt(), quad.at(1).toInt()},
                                      {quad.at(2).toInt(), quad.at(3).toInt()}};
        };
        sheet.printArea = optionalRange(object.value(QStringLiteral("printArea")));
        sheet.printTitleRows = optionalRange(object.value(QStringLiteral("printTitleRows")));
        sheet.printTitleCols = optionalRange(object.value(QStringLiteral("printTitleCols")));
        const QJsonArray pageSetup = object.value(QStringLiteral("pageSetup")).toArray();
        if (pageSetup.size() == 3) {
            sheet.pageSetup.orientation = pageSetup.at(0).toInt();
            sheet.pageSetup.fitToWidth = pageSetup.at(1).toInt();
            sheet.pageSetup.fitToHeight = pageSetup.at(2).toInt();
        }
        snapshot.sheets.append(sheet);
    }
    snapshot.activeSheet = qBound(0, snapshot.activeSheet, snapshot.sheets.size() - 1);
    *out = std::move(snapshot);
    return true;
}

} // namespace QindaOffice::Apps::QindaCalc
