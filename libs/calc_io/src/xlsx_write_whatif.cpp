// SPDX-License-Identifier: GPL-3.0-or-later
// Writing scenarios and data tables (xlsx_whatif.h, M78, D-087), in
// Excel's markup:
//   <scenarios current="0" show="0" sqref="B5"><scenario name="Best"
//     locked="1" count="1" user="…" comment="…"><inputCells r="A1"
//     val="7"/></scenario></scenarios>
//   <c r="B4"><f t="dataTable" ref="B4:C6" dt2D="0" dtr="0" r1="A1"/><v>10</v></c>
// A two-variable table is dt2D="1" dtr="1" with r1 the ROW input cell and
// r2 the COLUMN input cell — what Excel writes and LibreOffice 25.2 reads
// (checked headless: it imports such a table as MULTIPLE.OPERATIONS with
// Excel's results). xlsx_post_m78.cpp places both.

#include "xlsx_whatif.h"

#include "xlsx_text.h"
#include "xlsx_write_features.h"
#include "xlsx_xml.h"

#include "workbook.h"

namespace QindaOffice::Calc::IO {

namespace {

QByteArray attribute(const char *name, const QString &value)
{
    return ' ' + QByteArray(name) + QByteArrayLiteral("=\"") + xmlAttribute(encodeXString(value)) + '"';
}

QByteArray scenariosXml(const ScenarioSet &set)
{
    QByteArray xml = QByteArrayLiteral("<scenarios");
    const qint32 count = qint32(set.scenarios.size());
    xml += attribute("current", QString::number(qBound(0, set.current, count - 1)));
    if (set.shown >= 0 && set.shown < count) {
        xml += attribute("show", QString::number(set.shown));
    }
    QStringList sqref;
    for (const CellRange &range : set.resultCells) {
        if (range.isValid()) {
            sqref.append(rangeText(range));
        }
    }
    if (!sqref.isEmpty()) {
        xml += attribute("sqref", sqref.join(QLatin1Char(' ')));
    }
    xml += '>';
    for (const Scenario &scenario : set.scenarios) {
        QList<qsizetype> cells;
        for (qsizetype i = 0; i < scenario.cells.size(); ++i) {
            if (scenario.cells.at(i).isValid()) {
                cells.append(i);
            }
        }
        if (cells.isEmpty()) {
            continue; // the schema wants an inputCells per scenario
        }
        xml += QByteArrayLiteral("<scenario") + attribute("name", scenario.name);
        // The schema's defaults are false: only a set flag is written.
        if (scenario.locked) {
            xml += attribute("locked", QStringLiteral("1"));
        }
        if (scenario.hidden) {
            xml += attribute("hidden", QStringLiteral("1"));
        }
        xml += attribute("count", QString::number(cells.size()));
        if (!scenario.user.isEmpty()) {
            xml += attribute("user", scenario.user);
        }
        if (!scenario.comment.isEmpty()) {
            xml += attribute("comment", scenario.comment);
        }
        xml += '>';
        for (const qsizetype i : std::as_const(cells)) {
            xml += QByteArrayLiteral("<inputCells") + attribute("r", addrText(scenario.cells.at(i)))
                + attribute("val", scenario.values.value(i)) + QByteArrayLiteral("/>");
        }
        xml += QByteArrayLiteral("</scenario>");
    }
    return xml + QByteArrayLiteral("</scenarios>");
}

QByteArray dataTableXml(const DataTableSpec &spec)
{
    QByteArray xml = QByteArrayLiteral("<f t=\"dataTable\"") + attribute("ref", rangeText(spec.ref))
        + attribute("dt2D", spec.twoD ? QStringLiteral("1") : QStringLiteral("0"))
        + attribute("dtr", spec.rowInput ? QStringLiteral("1") : QStringLiteral("0"));
    if (spec.r1Deleted) {
        xml += attribute("del1", QStringLiteral("1"));
    }
    if (spec.twoD && spec.r2Deleted) {
        xml += attribute("del2", QStringLiteral("1"));
    }
    if (spec.r1.isValid()) {
        xml += attribute("r1", addrText(spec.r1));
    }
    if (spec.twoD && spec.r2.isValid()) {
        xml += attribute("r2", addrText(spec.r2));
    }
    return xml + QByteArrayLiteral("/>");
}

} // namespace

void fillWhatIfPatch(const Workbook &wb, qint32 sheetIndex, PatchManifest::SheetPatch *patch)
{
    if (patch == nullptr || sheetIndex < 0 || sheetIndex >= wb.sheetCount()) {
        return;
    }
    const Sheet &sheet = wb.sheetAt(sheetIndex);
    if (sheet.isChartSheet()) {
        return;
    }
    bool anyScenario = false;
    for (const Scenario &scenario : sheet.scenarios().scenarios) {
        for (const CellAddr &cell : scenario.cells) {
            anyScenario = anyScenario || cell.isValid();
        }
    }
    if (anyScenario) {
        patch->scenariosXml = scenariosXml(sheet.scenarios());
    }
    for (const DataTableSpec &spec : sheet.dataTables()) {
        // AGENT-CONTRACT (data_table.h): a table has been worked out before
        // a save (every recalculation does it), so its top-left result
        // cell holds a value and the writer has a <c> to put the formula
        // in. One that holds nothing has no cell in the part: it is left
        // out rather than failing the save.
        const CellAddr master = spec.ref.topLeft;
        const Cell *cell = sheet.cellAt(master);
        if (!spec.ref.isValid() || cell == nullptr || isBlank(cell->value) || cell->hasFormula()) {
            continue;
        }
        const QString ref = addrText(master);
        patch->cellFormulaXml.insert(ref, dataTableXml(spec));
        // A formula's text result is t="str" (QXlsx wrote a shared string).
        if (const auto *text = std::get_if<QString>(&cell->value); text != nullptr && !patch->cellTypes.contains(ref)) {
            patch->cellTypes.insert(ref, PatchManifest::CellType{QByteArrayLiteral("str"), encodeXString(*text)});
        }
    }
}

} // namespace QindaOffice::Calc::IO
