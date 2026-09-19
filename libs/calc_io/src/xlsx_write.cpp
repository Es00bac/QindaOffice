// SPDX-License-Identifier: GPL-3.0-or-later
// The xlsx write half of the QXlsx <-> calc_engine bridge (split out of the
// old xlsx_bridge.cpp in M33): values and formulas WITH their formats
// (formula cells carried the default format in v1; writeFormula takes the
// format and registers it), styles on empty cells via writeBlank, merges,
// geometry, charts and defined names.

#include "xlsx_bridge.h"

#include "xlsx_style.h"

#include "eval.h"
#include "number_format.h"

#include <xlsxdocument.h>
#include <xlsxworkbook.h>
#include <xlsxworksheet.h>
#include <xlsxcellformula.h>
#include <xlsxchart.h>

namespace QindaOffice::Calc::IO {

namespace {

// QXlsx measures column widths in character units and row heights in points.
qreal pxToCharWidth(qreal px)
{
    return qMax(1.0, (px - 5.0) / 7.0);
}

qreal pxToPt(qreal px)
{
    return px * 72.0 / 96.0;
}

} // namespace

std::unique_ptr<QXlsx::Document> writeXlsx(const Workbook &wb, QString *error)
{
    Q_UNUSED(error);
    auto doc = std::make_unique<QXlsx::Document>();
    // The engine style table -> QXlsx formats, built once. AGENT-NOTE: the
    // v1 writer rebuilt the Format per cell; with full field coverage that
    // is the same mapping work a hundred thousand times on a big sheet.
    QHash<qint32, QXlsx::Format> formats;
    const auto formatFor = [&wb, &formats](qint32 styleId) -> QXlsx::Format {
        if (styleId == 0 || wb.style(styleId) == nullptr) {
            return {};
        }
        if (!formats.contains(styleId)) {
            formats.insert(styleId, formatFromStyle(*wb.style(styleId)));
        }
        return formats.value(styleId);
    };
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        const Sheet &sheet = wb.sheetAt(s);
        if (s == 0 && !doc->sheetNames().isEmpty()) {
            doc->renameSheet(doc->sheetNames().first(), sheet.name());
        } else {
            doc->addSheet(sheet.name());
        }
        doc->selectSheet(sheet.name());
        QXlsx::Worksheet *worksheet =
            static_cast<QXlsx::Worksheet *>(doc->sheet(sheet.name()));
        for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
            const quint64 key = it.key();
            const CellAddr addr{qint32(key & 0xffffffffu), qint32(key >> 32)};
            const Cell &cell = it.value();
            const QXlsx::Format format = formatFor(sheet.styleIdAt(addr));
            const int row = addr.row + 1;
            const int col = addr.col + 1;
            // AGENT-NOTE: value writes go through Document (it registers the
            // cell format into the workbook style table). Formulas use the
            // Worksheet API (QXlsx has no Document::writeFormula); the format
            // registers there just the same (writeFormula → addXfFormat), so
            // formula cells keep their look. The engine's evaluated value is
            // written as the cached result: consumers that do not recalculate
            // on load (LibreOffice's default for xlsx) still show correct
            // data.
            if (cell.hasFormula()) {
                const double cached = isNumber(cell.value) ? std::get<double>(cell.value) : 0.0;
                worksheet->writeFormula(row, col,
                                        QXlsx::CellFormula(QStringLiteral("=") + cell.formula),
                                        format, cached);
            } else if (isNumber(cell.value)) {
                doc->write(row, col, std::get<double>(cell.value), format);
            } else if (isText(cell.value)) {
                doc->write(row, col, std::get<QString>(cell.value), format);
            } else if (isBool(cell.value)) {
                doc->write(row, col, std::get<bool>(cell.value), format);
            } else if (isErr(cell.value)) {
                doc->write(row, col, errText(std::get<Err>(cell.value)), format);
            }
        }
        // Styles live on EMPTY cells too (a painted fill, a border on a
        // blank): writeBlank carries the format with no content.
        for (auto it = sheet.styleIds().constBegin(); it != sheet.styleIds().constEnd(); ++it) {
            const quint64 key = it.key();
            const CellAddr addr{qint32(key & 0xffffffffu), qint32(key >> 32)};
            if (sheet.cellAt(addr) != nullptr || it.value() == 0) {
                continue;
            }
            worksheet->writeBlank(addr.row + 1, addr.col + 1, formatFor(it.value()));
        }
        for (const CellRange &merge : sheet.merges()) {
            doc->mergeCells(QXlsx::CellRange(merge.topLeft.row + 1, merge.topLeft.col + 1,
                                             merge.bottomRight.row + 1,
                                             merge.bottomRight.col + 1));
        }
        for (const qint32 col : sheet.sizedColumns()) {
            worksheet->setColumnWidth(col + 1, col + 1, pxToCharWidth(sheet.columnWidth(col)));
        }
        for (const qint32 row : sheet.sizedRows()) {
            worksheet->setRowHeight(row + 1, row + 1, pxToPt(sheet.rowHeight(row)));
        }
    }
    // Floating charts (plan section 7.2): one chart part per spec, anchored
    // at its cell. QXlsx renders clustered bars for StackedBar (its chart
    // API has no grouping control) - the M35 post-pass inserts the grouping.
    for (const ChartSpec &chart : wb.charts) {
        const CellRange range = parseChartRange(chart.rangeText);
        if (!range.isValid() || chart.sheet < 0 || chart.sheet >= wb.sheetCount()) {
            continue;
        }
        doc->selectSheet(wb.sheetName(chart.sheet));
        auto *worksheet = static_cast<QXlsx::Worksheet *>(doc->sheet(wb.sheetName(chart.sheet)));
        if (worksheet == nullptr) {
            continue;
        }
        QXlsx::Chart *qxChart =
            worksheet->insertChart(chart.anchorRow + 1, chart.anchorCol + 1,
                                   QSize(chart.widthPx, chart.heightPx));
        if (qxChart == nullptr) {
            continue;
        }
        qxChart->addSeries(QXlsx::CellRange(range.topLeft.row + 1, range.topLeft.col + 1,
                                            range.bottomRight.row + 1, range.bottomRight.col + 1),
                           worksheet, chart.headerH, chart.headerV);
        QXlsx::Chart::ChartType type = QXlsx::Chart::CT_BarChart;
        switch (chart.kind) {
        case ChartSpec::Kind::Line: type = QXlsx::Chart::CT_LineChart; break;
        case ChartSpec::Kind::Area: type = QXlsx::Chart::CT_AreaChart; break;
        case ChartSpec::Kind::Pie: type = QXlsx::Chart::CT_PieChart; break;
        case ChartSpec::Kind::Scatter: type = QXlsx::Chart::CT_ScatterChart; break;
        case ChartSpec::Kind::Bar:
        case ChartSpec::Kind::StackedBar: type = QXlsx::Chart::CT_BarChart; break;
        }
        qxChart->setChartType(type);
        qxChart->setChartTitle(chart.title);
    }
    // Named ranges: the canonical text form is already the xlsx spelling
    // ("Sheet1!$A$1:$A$5"), so the workbook's own defineName takes it as is.
    const QHash<QString, RangeRef> &names = wb.namedRanges();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        static_cast<void>(doc->workbook()->defineName(it.key(), rangeRefText(it.value())));
    }
    return doc;
}

bool writeXlsxFile(const Workbook &wb, const QString &path, QString *error)
{
    std::unique_ptr<QXlsx::Document> doc = writeXlsx(wb, error);
    if (doc == nullptr) {
        return false;
    }
    if (!doc->saveAs(path)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
