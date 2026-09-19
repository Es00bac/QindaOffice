// SPDX-License-Identifier: GPL-3.0-or-later
// The xlsx write half of the QXlsx <-> calc_engine bridge (split out of the
// old xlsx_bridge.cpp in M33): values and formulas WITH their formats
// (formula cells carried the default format in v1; writeFormula takes the
// format and registers it), styles on empty cells via writeBlank, merges,
// geometry, charts and defined names.

#include "xlsx_bridge.h"

#include "xlsx_manifest.h"
#include "xlsx_post.h"
#include "xlsx_style.h"
#include "xlsx_write_xml.h"

#include "eval.h"
#include "number_format.h"

#include <xlsxdocument.h>
#include <xlsxworkbook.h>
#include <xlsxworksheet.h>
#include <xlsxcellformula.h>
#include <xlsxchart.h>
#include <xlsxconditionalformatting.h>
#include <xlsxdatavalidation.h>

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

// The dxf for a conditional format. AGENT-NOTE: a dxf's visible fill is the
// file's bgColor element (Excel's dxf convention — the reverse of a cell
// fill), and QXlsx's solid-fill swap writes P_Fill_FgColor AS bgColor, so
// the foreground setter is the right one here and ONLY here.
QXlsx::Format dxfFromCondFormat(const CondFormat &cf)
{
    QXlsx::Format format;
    if (cf.bold.has_value()) {
        format.setFontBold(*cf.bold);
    }
    if (cf.italic.has_value()) {
        format.setFontItalic(*cf.italic);
    }
    if (cf.foreground.has_value()) {
        format.setFontColor(QColor(cf.foreground->r, cf.foreground->g, cf.foreground->b));
    }
    if (cf.background.has_value()) {
        format.setFillPattern(QXlsx::Format::PatternSolid);
        format.setPatternForegroundColor(
            QColor(cf.background->r, cf.background->g, cf.background->b));
    }
    return format;
}

void writeValidations(QXlsx::Worksheet *worksheet, const Sheet &sheet)
{
    for (const ValidationRule &rule : sheet.validations()) {
        QXlsx::DataValidation validation;
        switch (rule.kind) {
        case ValidationRule::Kind::List: {
            QStringList escaped = rule.list;
            for (QString &item : escaped) {
                item.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            }
            validation.setValidationType(QXlsx::DataValidation::List);
            validation.setFormula1(QStringLiteral("\"") + escaped.join(QLatin1Char(','))
                                   + QStringLiteral("\""));
            break;
        }
        case ValidationRule::Kind::WholeNumber:
            validation.setValidationType(QXlsx::DataValidation::Whole);
            validation.setValidationOperator(QXlsx::DataValidation::Between);
            validation.setFormula1(QString::number(rule.min));
            validation.setFormula2(QString::number(rule.max));
            break;
        case ValidationRule::Kind::Decimal:
            validation.setValidationType(QXlsx::DataValidation::Decimal);
            validation.setValidationOperator(QXlsx::DataValidation::Between);
            validation.setFormula1(QString::number(rule.min));
            validation.setFormula2(QString::number(rule.max));
            break;
        case ValidationRule::Kind::TextLength:
            validation.setValidationType(QXlsx::DataValidation::TextLength);
            validation.setValidationOperator(QXlsx::DataValidation::Between);
            validation.setFormula1(QString::number(rule.min));
            validation.setFormula2(QString::number(rule.max));
            break;
        case ValidationRule::Kind::NotEmpty:
            // Excel has no "not empty" validation; the idiom is a custom
            // formula over the range's first cell.
            validation.setValidationType(QXlsx::DataValidation::Custom);
            validation.setFormula1(QStringLiteral("LEN(TRIM(%1))>0")
                                       .arg(addrText(rule.range.topLeft)));
            break;
        }
        if (!rule.message.isEmpty()) {
            validation.setErrorMessage(rule.message);
        }
        validation.setErrorStyle(QXlsx::DataValidation::Stop);
        validation.addRange(QXlsx::CellRange(rule.range.topLeft.row + 1,
                                             rule.range.topLeft.col + 1,
                                             rule.range.bottomRight.row + 1,
                                             rule.range.bottomRight.col + 1));
        worksheet->addDataValidation(validation);
    }
}

void writeCondFormats(QXlsx::Worksheet *worksheet, const Sheet &sheet)
{
    using Op = CondFormatRule::Op;
    for (const CondFormatRule &rule : sheet.condFormats()) {
        QXlsx::ConditionalFormatting cf;
        const QXlsx::Format dxf = dxfFromCondFormat(rule.format);
        const QString first = QString::number(rule.first);
        const QString second = QString::number(rule.second);
        bool added = true;
        switch (rule.op) {
        case Op::GreaterThan:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_GreaterThan, first, dxf, rule.stopIfTrue);
            break;
        case Op::GreaterThanOrEqual:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_GreaterThanOrEqual, first, dxf, rule.stopIfTrue);
            break;
        case Op::LessThan:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_LessThan, first, dxf, rule.stopIfTrue);
            break;
        case Op::LessThanOrEqual:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_LessThanOrEqual, first, dxf, rule.stopIfTrue);
            break;
        case Op::Between:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_Between, first, second, dxf, rule.stopIfTrue);
            break;
        case Op::EqualTo:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_Equal, first, dxf, rule.stopIfTrue);
            break;
        case Op::NotEqualTo:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_NotEqual, first, dxf, rule.stopIfTrue);
            break;
        case Op::ContainsText:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_ContainsText, rule.text, dxf, rule.stopIfTrue);
            break;
        case Op::IsEmpty:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_Blanks, dxf, rule.stopIfTrue);
            break;
        case Op::NotEmpty:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_NoBlanks, dxf, rule.stopIfTrue);
            break;
        case Op::Expression:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_Expression, rule.formula, dxf, rule.stopIfTrue);
            break;
        case Op::DataBar:
            cf.addDataBarRule(QColor(rule.barColor.r, rule.barColor.g, rule.barColor.b),
                              true, rule.stopIfTrue);
            break;
        }
        if (!added) {
            continue;
        }
        cf.addRange(QXlsx::CellRange(rule.range.topLeft.row + 1, rule.range.topLeft.col + 1,
                                     rule.range.bottomRight.row + 1,
                                     rule.range.bottomRight.col + 1));
        worksheet->addConditionalFormatting(cf);
    }
}

} // namespace

// Builds the QXlsx model and, when `manifest` is non-null, the post-pass
// patch list for the features QXlsx has no model for (autofilter, comments).
std::unique_ptr<QXlsx::Document> buildXlsx(const Workbook &wb, PatchManifest *manifest,
                                           QString *error)
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
        writeValidations(worksheet, sheet);
        writeCondFormats(worksheet, sheet);
        if (manifest != nullptr) {
            // QXlsx's sheet part naming is deterministic (document order);
            // the post-pass asserts it before touching anything.
            PatchManifest::SheetPatch patch;
            patch.sheetPart = QStringLiteral("xl/worksheets/sheet%1.xml").arg(s + 1);
            if (sheet.autoFilter().active) {
                patch.autoFilterXml = autoFilterXml(sheet.autoFilter());
            }
            if (!sheet.comments().isEmpty()) {
                patch.commentsPart = QStringLiteral("xl/comments%1.xml").arg(s + 1);
                patch.commentsXml = commentsPartXml(sheet.comments());
            }
            if (!patch.autoFilterXml.isEmpty() || !patch.commentsPart.isEmpty()) {
                manifest->sheets.append(patch);
            }
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

std::unique_ptr<QXlsx::Document> writeXlsx(const Workbook &wb, QString *error)
{
    // No post-pass without a file: the returned document carries everything
    // QXlsx has a model for (which now includes validations and conditional
    // formats) but not the manifest-only features (autofilter, comments).
    return buildXlsx(wb, nullptr, error);
}

bool writeXlsxFile(const Workbook &wb, const QString &path, QString *error)
{
    PatchManifest manifest;
    std::unique_ptr<QXlsx::Document> doc = buildXlsx(wb, &manifest, error);
    if (doc == nullptr) {
        return false;
    }
    if (!doc->saveAs(path)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1").arg(path);
        }
        return false;
    }
    // The manifest-only parts land after QXlsx finished the package.
    if (!manifest.sheets.isEmpty() && !applyPatchManifest(path, manifest, error)) {
        return false;
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
