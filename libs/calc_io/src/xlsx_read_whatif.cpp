// SPDX-License-Identifier: GPL-3.0-or-later
// Reading scenarios and data tables (xlsx_whatif.h, M78, D-087).
//
// A data table lives in its top-left result cell's formula:
// <c r="B4"><f t="dataTable" ref="B4:C6" dt2D="0" dtr="0" r1="A1"/><v>…</v></c>.
// QXlsx reads that cell as a plain value (its formula has no text) and the
// other result cells are plain values too, so the cells need nothing here:
// the table's record goes on the sheet and the load's recalculation works
// the results out (Workbook::recomputeDataTables). Element names are
// compared as local names (producers may prefix them).

#include "xlsx_whatif.h"

#include "xlsx_parts.h"
#include "xlsx_text.h"

#include "sheet.h"

#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO {

namespace {

bool flag(const QXmlStreamAttributes &attributes, QLatin1StringView name)
{
    const QStringView value = attributes.value(name);
    return value == QLatin1String("1") || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

std::optional<DataTableSpec> dataTableFrom(const QXmlStreamAttributes &f, const CellAddr &cell)
{
    DataTableSpec spec;
    const std::optional<CellRange> ref = rangeFromText(f.value(QLatin1String("ref")).toString());
    spec.ref = ref.value_or(CellRange{cell, cell});
    spec.twoD = flag(f, QLatin1StringView("dt2D"));
    spec.rowInput = flag(f, QLatin1StringView("dtr"));
    spec.r1Deleted = flag(f, QLatin1StringView("del1"));
    spec.r2Deleted = flag(f, QLatin1StringView("del2"));
    const std::optional<CellAddr> r1 = addrFromText(f.value(QLatin1String("r1")).toString());
    const std::optional<CellAddr> r2 = addrFromText(f.value(QLatin1String("r2")).toString());
    spec.r1 = r1.value_or(CellAddr{});
    spec.r2 = r2.value_or(CellAddr{});
    // An input cell the file names nowhere is one Excel lost.
    spec.r1Deleted = spec.r1Deleted || !r1.has_value();
    spec.r2Deleted = spec.twoD && (spec.r2Deleted || !r2.has_value());
    if (!spec.ref.isValid() || spec.ref.topLeft.row < 1 || spec.ref.topLeft.col < 1) {
        return std::nullopt; // no room for the formulas and inputs: not a table
    }
    return spec;
}

// <scenarios current show sqref> and its <scenario>/<inputCells> children;
// the reader is left on </scenarios>.
ScenarioSet readScenarios(QXmlStreamReader &reader)
{
    ScenarioSet set;
    const auto attributes = reader.attributes();
    set.current = attributes.value(QLatin1String("current")).toInt();
    set.shown = attributes.hasAttribute(QLatin1String("show")) ? attributes.value(QLatin1String("show")).toInt() : -1;
    for (const QString &part : attributes.value(QLatin1String("sqref")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (const std::optional<CellRange> range = rangeFromText(part)) {
            set.resultCells.append(*range);
        }
    }
    Scenario *scenario = nullptr;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("scenarios")) {
            break;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const auto a = reader.attributes();
        if (reader.name() == QLatin1String("scenario")) {
            Scenario next;
            next.name = decodeXString(a.value(QLatin1String("name")).toString());
            next.locked = flag(a, QLatin1StringView("locked"));
            next.hidden = flag(a, QLatin1StringView("hidden"));
            next.user = decodeXString(a.value(QLatin1String("user")).toString());
            next.comment = decodeXString(a.value(QLatin1String("comment")).toString());
            set.scenarios.append(next);
            scenario = &set.scenarios.last();
        } else if (reader.name() == QLatin1String("inputCells") && scenario != nullptr) {
            const std::optional<CellAddr> cell = addrFromText(a.value(QLatin1String("r")).toString());
            if (!cell.has_value() || flag(a, QLatin1StringView("deleted"))) {
                continue; // a changing cell Excel lost with its row or column
            }
            scenario->cells.append(*cell);
            scenario->values.append(decodeXString(a.value(QLatin1String("val")).toString()));
        }
    }
    // A scenario left with no cell changes nothing: Excel's schema has none.
    QHash<qint32, qint32> newIndex;
    QList<Scenario> kept;
    for (qsizetype i = 0; i < set.scenarios.size(); ++i) {
        if (!set.scenarios.at(i).cells.isEmpty()) {
            newIndex.insert(qint32(i), qint32(kept.size()));
            kept.append(set.scenarios.at(i));
        }
    }
    set.scenarios = kept;
    set.current = newIndex.value(set.current, 0);
    set.shown = newIndex.value(set.shown, -1);
    return set;
}

} // namespace

void readSheetWhatIf(const QByteArray &sheetXml, Sheet *sheet)
{
    if (sheet == nullptr) {
        return;
    }
    QList<DataTableSpec> tables;
    std::optional<ScenarioSet> scenarios;
    QXmlStreamReader reader(sheetXml);
    CellAddr cell;
    bool inCell = false;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("c")) {
            inCell = false;
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        if (name == QLatin1String("c")) {
            inCell = true;
            cell = addrFromText(reader.attributes().value(QLatin1String("r")).toString()).value_or(CellAddr{});
        } else if (name == QLatin1String("f") && inCell) {
            const auto attributes = reader.attributes();
            if (attributes.value(QLatin1String("t")) == QLatin1String("dataTable") && cell.isValid()) {
                if (const std::optional<DataTableSpec> spec = dataTableFrom(attributes, cell)) {
                    tables.append(*spec);
                }
            }
            reader.skipCurrentElement();
        } else if (name == QLatin1String("scenarios") && !inCell) {
            scenarios = readScenarios(reader);
        } else if (name == QLatin1String("extLst")) {
            reader.skipCurrentElement(); // Excel 2010's rules carry <xm:f> elements: not cells
        }
    }
    if (!tables.isEmpty()) {
        sheet->setDataTables(tables);
    }
    if (scenarios.has_value() && !scenarios->scenarios.isEmpty()) {
        sheet->setScenarios(*scenarios);
    }
}

} // namespace QindaOffice::Calc::IO
