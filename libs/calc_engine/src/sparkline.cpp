// SPDX-License-Identifier: GPL-3.0-or-later
// Sparkline groups: Excel's defaults, Insert ▸ Sparklines' ranges, and the
// moves that structural edits and sheet renames make (sparkline.h, M90,
// D-098).

#include "sparkline.h"

#include "parser.h"
#include "sheet.h"
#include "structure_internal.h"
#include "workbook.h"

#include <QCoreApplication>

namespace QindaOffice::Calc {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Sparklines", text);
}

std::optional<RangeRef> refOf(const QString &text)
{
    QString body = text.trimmed();
    if (body.startsWith(QLatin1Char('='))) {
        body = body.mid(1);
    }
    const ParseResult parsed = parseFormula(body);
    if (!parsed.error.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref || parsed.root->ref.wholeRows
        || parsed.root->ref.wholeCols || !parsed.root->ref.range.isValid()) {
        return std::nullopt;
    }
    return parsed.root->ref;
}

// A reference through the edit; empty when it is removed whole.
QString moved(const QString &text, const StructureEdit &edit, const QString &editedSheetName)
{
    std::optional<RangeRef> ref = refOf(text);
    if (!ref.has_value()) {
        return text;
    }
    if (!StructureMath::adjustRef(*ref, edit, false, editedSheetName)) {
        return {};
    }
    return rangeRefText(*ref);
}

QString requalified(const QString &text, const QString &from, const QString &to)
{
    std::optional<RangeRef> ref = refOf(text);
    if (!ref.has_value() || unquotedSheetName(ref->sheet).compare(from, Qt::CaseInsensitive) != 0) {
        return text;
    }
    if (to.isEmpty()) {
        return {}; // the sheet is gone
    }
    ref->sheet = quotedSheetName(to);
    return rangeRefText(*ref);
}

} // namespace

SparklineGroup defaultSparklineGroup(SparklineGroup::Type type)
{
    // Excel 2016's Insert ▸ Sparklines: theme colours (Accent 1, Accent 2).
    SparklineGroup group;
    group.type = type;
    group.empty = SparklineGroup::Empty::Gap;
    group.series = {std::nullopt, 4, type == SparklineGroup::Type::Line ? -0.499984740745262 : -0.249977111117893};
    group.negativeColor = {std::nullopt, 5, 0};
    group.axisColor = {Rgb{0, 0, 0}, -1, 0};
    group.markersColor = {std::nullopt, 4, -0.499984740745262};
    group.firstColor = {std::nullopt, 4, 0.39997558519241921};
    group.lastColor = {std::nullopt, 4, 0.39997558519241921};
    group.highColor = {std::nullopt, 4, 0};
    group.lowColor = {std::nullopt, 4, 0};
    return group;
}

SparklineRef sparklineAt(const Sheet &sheet, const CellAddr &addr)
{
    const QList<SparklineGroup> &groups = sheet.sparklineGroups();
    for (qsizetype g = 0; g < groups.size(); ++g) {
        for (qsizetype i = 0; i < groups.at(g).sparklines.size(); ++i) {
            if (groups.at(g).sparklines.at(i).location == addr) {
                return {qint32(g), qint32(i)};
            }
        }
    }
    return {};
}

std::optional<QList<Sparkline>> sparklinesFor(const Workbook &book, qint32 sheet, const QString &data,
                                             const QString &location, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<QList<Sparkline>> {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    };
    const std::optional<RangeRef> where = refOf(location);
    const bool onSheet = where.has_value()
        && (where->sheet.isEmpty() || book.sheetIndex(unquotedSheetName(where->sheet)) == sheet);
    if (!onSheet) {
        return fail(tr("Location reference is not valid."));
    }
    const CellRange cells = where->range;
    const qint32 locRows = cells.bottomRight.row - cells.topLeft.row + 1;
    const qint32 locCols = cells.bottomRight.col - cells.topLeft.col + 1;
    if (locRows > 1 && locCols > 1) {
        return fail(tr("Location reference is not valid. Choose one row or one column of cells."));
    }
    std::optional<RangeRef> source = refOf(data);
    if (!source.has_value()) {
        return fail(tr("Data reference is not valid."));
    }
    const qint32 dataSheet = source->sheet.isEmpty() ? sheet : book.sheetIndex(unquotedSheetName(source->sheet));
    if (dataSheet < 0) {
        return fail(tr("Data reference is not valid."));
    }
    source->sheet = quotedSheetName(book.sheetName(dataSheet));
    source->absRow0 = source->absRow1 = source->absCol0 = source->absCol1 = false;
    const CellRange range = source->range;
    const qint32 dataRows = range.bottomRight.row - range.topLeft.row + 1;
    const qint32 dataCols = range.bottomRight.col - range.topLeft.col + 1;
    const qint32 count = qMax(locRows, locCols);
    QList<Sparkline> out;
    // One location cell: the data is one row or one column. Several: one
    // row (or column) of the data per location cell — a square range goes
    // the location's own way.
    bool byRows = false;
    if (count == 1) {
        if (dataRows != 1 && dataCols != 1) {
            return fail(tr("Data reference is not valid. The data must be one row or one column."));
        }
    } else if (dataRows == count && (dataCols != count || locCols == 1)) {
        byRows = true;
    } else if (dataCols != count) {
        return fail(tr("Data reference is not valid. The data must have one row or column for each location cell."));
    }
    for (qint32 i = 0; i < count; ++i) {
        RangeRef part = *source;
        if (count > 1 && byRows) {
            part.range.topLeft.row = part.range.bottomRight.row = range.topLeft.row + i;
        } else if (count > 1) {
            part.range.topLeft.col = part.range.bottomRight.col = range.topLeft.col + i;
        }
        const CellAddr at = locCols == 1 ? CellAddr{cells.topLeft.row + i, cells.topLeft.col}
                                         : CellAddr{cells.topLeft.row, cells.topLeft.col + i};
        out.append({at, rangeRefText(part)});
    }
    return out;
}

void moveSparklinesForEdit(Workbook &book, const StructureEdit &edit)
{
    const QString editedSheetName = book.sheetName(edit.sheet);
    const StructureMath::Band band = StructureMath::bandOf(edit);
    const bool rows = StructureMath::editsRows(edit);
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        QList<SparklineGroup> groups = book.sheetAt(s).sparklineGroups();
        if (groups.isEmpty()) {
            continue;
        }
        QList<SparklineGroup> kept;
        for (SparklineGroup group : std::as_const(groups)) {
            QList<Sparkline> lines;
            for (Sparkline line : std::as_const(group.sparklines)) {
                if (s == edit.sheet) {
                    qint32 &axis = rows ? line.location.row : line.location.col;
                    axis = StructureMath::movedIndex(axis, band);
                    if (axis < 0) {
                        continue; // its cell went
                    }
                }
                line.data = moved(line.data, edit, editedSheetName);
                lines.append(line);
            }
            if (lines.isEmpty()) {
                continue;
            }
            group.sparklines = lines;
            group.dateRange = moved(group.dateRange, edit, editedSheetName);
            kept.append(group);
        }
        book.sheetAt(s).setSparklineGroups(kept);
    }
}

void requalifySparklines(Workbook &book, const QString &from, const QString &to)
{
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        QList<SparklineGroup> groups = book.sheetAt(s).sparklineGroups();
        if (groups.isEmpty()) {
            continue;
        }
        for (SparklineGroup &group : groups) {
            for (Sparkline &line : group.sparklines) {
                line.data = requalified(line.data, from, to);
            }
            group.dateRange = requalified(group.dateRange, from, to);
        }
        book.sheetAt(s).setSparklineGroups(groups);
    }
}

} // namespace QindaOffice::Calc
