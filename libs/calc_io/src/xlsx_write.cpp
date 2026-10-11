// SPDX-License-Identifier: GPL-3.0-or-later
// The xlsx write half of the QXlsx <-> calc_engine bridge (split out of the
// old xlsx_bridge.cpp in M33): values and formulas WITH their formats
// (formula cells carried the default format in v1; writeFormula takes the
// format and registers it), styles on empty cells via writeBlank, merges,
// geometry and charts; rules in xlsx_write_rules, names in xlsx_write_book.
//
// AGENT-NOTE (M61, D-069): the writer is honest. Text goes in as text —
// QXlsx's own write(QVariant) turns any string starting with '=' into a
// formula and anything URL-shaped into a blue hyperlink. Formulas are
// written in Excel's spelling (_xlfn. prefixes, ',' separators). A cell
// whose real type QXlsx cannot express — a literal error, a formula whose
// cached result is text, a boolean or an error — is patched to its true t=
// by the post-pass. And the whole package is built in memory and saved
// atomically, so a failed save never leaves a half-written file.

#include "xlsx_bridge.h"

#include "xlsx_formula.h"
#include "xlsx_manifest.h"
#include "xlsx_post.h"
#include "xlsx_print.h"
#include "xlsx_rules.h"
#include "xlsx_sparklines.h"
#include "xlsx_style.h"
#include "xlsx_text.h"
#include "xlsx_views.h"
#include "xlsx_write_book.h"
#include "xlsx_write_features.h"
#include "xlsx_write_rules.h"
#include "xlsx_whatif.h"
#include "xlsx_write_pivot.h"
#include "xlsx_write_xml.h"

#include "eval.h"
#include "number_format.h"

#include <QBuffer>
#include <QSaveFile>

#include <algorithm>
#include <functional>

#include <xlsxcellformula.h>
#include <xlsxchart.h>
#include <xlsxdocument.h>
#include <xlsxworkbook.h>
#include <xlsxworksheet.h>

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

// The type and value Excel caches for a cell QXlsx can only write as a
// number: text, a boolean, an error. nullopt when the number QXlsx wrote is
// right (numbers and blanks; the circular marker, which Excel shows as 0).
std::optional<PatchManifest::CellType> realType(const Value &value)
{
    if (const auto *text = std::get_if<QString>(&value)) {
        return PatchManifest::CellType{QByteArrayLiteral("str"), encodeXString(*text)};
    }
    if (const auto *flag = std::get_if<bool>(&value)) {
        return PatchManifest::CellType{QByteArrayLiteral("b"), *flag ? QStringLiteral("1") : QStringLiteral("0")};
    }
    if (const auto *err = std::get_if<Err>(&value)) {
        if (const QString spelled = errorForFile(*err); !spelled.isEmpty()) {
            return PatchManifest::CellType{QByteArrayLiteral("e"), spelled};
        }
    }
    return std::nullopt;
}

using FormatFor = std::function<QXlsx::Format(qint32)>;

// The table holding a cell (the qualifier its structured references take
// in the file), or empty.
QString tableName(const Workbook &wb, qint32 sheet, const CellAddr &addr)
{
    const TableSpec *table = wb.tableAt(sheet, addr);
    return table != nullptr ? table->name : QString();
}

// Every cell of one sheet. `headerText` overrides table header cells (see
// tableForFile); `patch` collects the cells the post-pass must retype.
void writeSheetCells(QXlsx::Worksheet *worksheet, const Workbook &wb, qint32 sheetIndex, const FormatFor &formatFor,
                     const QHash<quint64, QString> &headerText, PatchManifest::SheetPatch *patch)
{
    const Sheet &sheet = wb.sheetAt(sheetIndex);
    for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
        const quint64 key = it.key();
        const CellAddr addr{qint32(key & 0xffffffffu), qint32(key >> 32)};
        const Cell &cell = it.value();
        const QXlsx::Format format = formatFor(sheet.styleIdAt(addr));
        const int row = addr.row + 1;
        const int col = addr.col + 1;
        if (const auto header = headerText.constFind(key); header != headerText.constEnd()) {
            worksheet->writeString(row, col, encodeXString(header.value()), format);
            continue;
        }
        // AGENT-NOTE: the engine's evaluated value is written as the
        // cached result: consumers that do not recalculate on load
        // (LibreOffice's default for xlsx) still show correct data.
        if (cell.hasFormula()) {
            const double cached = isNumber(cell.value) ? std::get<double>(cell.value) : 0.0;
            // Excel's spelling: structured references qualified with the
            // table the cell is in, '@' as _xlfn.SINGLE (M69, D-078).
            const QString text = formulaForFile(cell.formula, tableName(wb, sheetIndex, addr));
            // A dynamic-array anchor (M66, D-075): <f t="array" ref="spill">
            // here, cm="1" from the post-pass; its spilled cells are plain
            // values below. A blocked spill names the anchor alone. A legacy
            // array formula (M69) is <f t="array"> over its block, no cm.
            const auto spill = cell.dynamicArray ? wb.spillAt(sheetIndex, addr) : std::nullopt;
            const std::optional<CellRange> block = spill.has_value()
                ? std::optional<CellRange>(spill->blocked ? CellRange{addr, addr} : spill->extent)
                : (cell.arrayRange.isValid() ? std::optional<CellRange>(cell.arrayRange) : std::nullopt);
            if (block.has_value()) {
                const CellRange &ref = *block;
                worksheet->writeFormula(row, col,
                                        QXlsx::CellFormula(text,
                                                           QXlsx::CellRange(ref.topLeft.row + 1, ref.topLeft.col + 1,
                                                                            ref.bottomRight.row + 1, ref.bottomRight.col + 1),
                                                           QXlsx::CellFormula::ArrayType),
                                        format, cached);
                if (patch != nullptr && spill.has_value()) {
                    patch->arrayRefs.insert(addrText(addr), rangeText(ref));
                }
            } else {
                worksheet->writeFormula(row, col, QXlsx::CellFormula(text), format, cached);
            }
            if (const auto type = realType(cell.value); type.has_value() && patch != nullptr) {
                patch->cellTypes.insert(addrText(addr), *type);
            }
        } else if (const auto *number = std::get_if<double>(&cell.value)) {
            worksheet->writeNumeric(row, col, *number, format);
        } else if (const auto *text = std::get_if<QString>(&cell.value)) {
            worksheet->writeString(row, col, encodeXString(*text), format);
        } else if (const auto *flag = std::get_if<bool>(&cell.value)) {
            worksheet->writeBool(row, col, *flag, format);
        } else if (isErr(cell.value)) {
            // QXlsx has no API for an error cell: a number now, t="e" later.
            worksheet->writeNumeric(row, col, 0.0, format);
            if (const auto type = realType(cell.value); type.has_value() && patch != nullptr) {
                patch->cellTypes.insert(addrText(addr), *type);
            }
        } else if (!format.isEmpty()) {
            // AGENT-GUARD: a BLANK cell that still carries a style. The
            // reader makes a Cell for every <c> element in the file,
            // painted-but-empty ones included, so these have a Cell and no
            // value — the styled-empty loop below skips them precisely
            // BECAUSE a Cell exists. Without this branch the author's empty
            // painted cells (2,114 of them in the sample workbook) disappear
            // on save, which is exactly the silent loss D-012 forbids.
            worksheet->writeBlank(row, col, format);
        }
    }
    // Table header cells the model leaves blank still need their names.
    for (auto it = headerText.constBegin(); it != headerText.constEnd(); ++it) {
        const CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
        if (sheet.cellAt(addr) == nullptr) {
            worksheet->writeString(addr.row + 1, addr.col + 1, encodeXString(it.value()),
                                   formatFor(sheet.styleIdAt(addr)));
        }
    }
    // Styles live on EMPTY cells too (a painted fill, a border on a
    // blank): writeBlank carries the format with no content.
    for (auto it = sheet.styleIds().constBegin(); it != sheet.styleIds().constEnd(); ++it) {
        const CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
        if (sheet.cellAt(addr) != nullptr || it.value() == 0 || headerText.contains(it.key())) {
            continue;
        }
        worksheet->writeBlank(addr.row + 1, addr.col + 1, formatFor(it.value()));
    }
}

// The post-pass work one sheet needs; `tables` are already file-ready.
void fillSheetPatch(const Sheet &sheet, qint32 index, const QList<PatchManifest::TablePart> &tables, int *nextVmlBlock,
                    PatchManifest::SheetPatch *patch)
{
    if (sheet.autoFilter().active) {
        patch->autoFilterXml = autoFilterXml(sheet.autoFilter());
    }
    if (!sheet.comments().isEmpty()) {
        patch->commentsPart = QStringLiteral("xl/comments%1.xml").arg(index + 1);
        patch->commentsXml = commentsPartXml(sheet.comments());
        QList<quint64> keys = sheet.comments().keys();
        std::sort(keys.begin(), keys.end());
        for (const quint64 key : std::as_const(keys)) {
            patch->commentCells.append({qint32(key & 0xffffffffu), qint32(key >> 32)});
        }
        // Each VML drawing owns blocks of 1024 shape ids, numbered across
        // the workbook; a sheet with more notes takes more blocks.
        patch->vmlNumber = *nextVmlBlock;
        *nextVmlBlock += int(keys.size() / 1024) + 1;
    }
    patch->tables = tables;
    patch->freezeRows = sheet.freezeRows();
    patch->freezeCols = sheet.freezeCols();
    if (sheet.cursor().isValid() && sheet.cursor() != CellAddr{0, 0}) {
        patch->activeCell = addrText(sheet.cursor());
    }
    fillPrintPatch(sheet, patch); // M78 (D-087): every print setting
}

// Floating charts (plan section 7.2). Charts with an explicit series list
// (imported, usually multi-range) cannot go through QXlsx's single-range
// chart API — they travel in the manifest and the post-pass generates their
// parts and drawing anchors. rangeText charts keep the QXlsx path;
// StackedBar gets its grouping in the post-pass (QXlsx's grouping
// serialization is commented out upstream).
void writeCharts(QXlsx::Document *doc, const Workbook &wb, PatchManifest *manifest)
{
    int qxlsxChartCount = 0;
    for (const ChartSpec &chart : wb.charts) {
        // A chart sheet's chart travels with its carried part (M69), a
        // PivotChart with its carried anchor (M74, D-083).
        if (chart.sheet < 0 || chart.sheet >= wb.sheetCount() || wb.sheetAt(chart.sheet).isChartSheet()
            || !chart.pivotSource.isEmpty()) {
            continue;
        }
        if (manifest != nullptr && !chart.series.isEmpty()) {
            const Sheet &sheet = wb.sheetAt(chart.sheet);
            PatchManifest::ChartPatch patch;
            patch.generate = true;
            patch.spec = chart;
            patch.sheetPart = QStringLiteral("xl/worksheets/sheet%1.xml").arg(chart.sheet + 1);
            // The anchor's far corner, from the pixel size against the
            // sheet's actual geometry.
            int width = 0;
            int col = chart.anchorCol;
            while (col < Sheet::kMaxCols && width < chart.widthPx) {
                width += int(sheet.columnWidth(col));
                ++col;
            }
            int height = 0;
            int row = chart.anchorRow;
            while (row < Sheet::kMaxRows && height < chart.heightPx) {
                height += int(sheet.rowHeight(row));
                ++row;
            }
            patch.toCol = col;
            patch.toRow = row;
            manifest->charts.append(patch);
            continue;
        }
        const CellRange range = parseChartRange(chart.rangeText);
        if (!range.isValid()) {
            continue;
        }
        doc->selectSheet(wb.sheetName(chart.sheet));
        auto *worksheet = static_cast<QXlsx::Worksheet *>(doc->sheet(wb.sheetName(chart.sheet)));
        if (worksheet == nullptr) {
            continue;
        }
        QXlsx::Chart *qxChart =
            worksheet->insertChart(chart.anchorRow + 1, chart.anchorCol + 1, QSize(chart.widthPx, chart.heightPx));
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
        ++qxlsxChartCount;
        if (manifest != nullptr && chart.kind == ChartSpec::Kind::StackedBar) {
            PatchManifest::ChartPatch patch;
            patch.stacked = true;
            patch.sheetPart = QStringLiteral("xl/worksheets/sheet%1.xml").arg(chart.sheet + 1);
            // QXlsx names chart parts xl/charts/chart<N>.xml in insertion
            // order (xlsxdocument.cpp) — the post-pass asserts existence.
            patch.qxlsxChartPart = QStringLiteral("xl/charts/chart%1.xml").arg(qxlsxChartCount);
            manifest->charts.append(patch);
        }
    }
}

} // namespace

// Builds the QXlsx model and, when `manifest` is non-null, the post-pass
// patch list for everything QXlsx cannot write (or writes wrongly).
std::unique_ptr<QXlsx::Document> buildXlsx(const Workbook &wb, PatchManifest *manifest, QString *error)
{
    Q_UNUSED(error);
    auto doc = std::make_unique<QXlsx::Document>();
    // QXlsx guesses on write: off, even though the honest cell writer below
    // never routes text through the guessing write(QVariant).
    doc->workbook()->setStringsToHyperlinksEnabled(false);
    doc->workbook()->setStringsToNumbersEnabled(false);
    doc->workbook()->setHtmlToRichStringEnabled(false);
    // The engine style table -> QXlsx formats, built once. AGENT-NOTE: the
    // v1 writer rebuilt the Format per cell; with full field coverage that
    // is the same mapping work a hundred thousand times on a big sheet.
    QHash<qint32, QXlsx::Format> formats;
    const FormatFor formatFor = [&wb, &formats](qint32 styleId) -> QXlsx::Format {
        if (styleId == 0 || wb.style(styleId) == nullptr) {
            return {};
        }
        if (!formats.contains(styleId)) {
            formats.insert(styleId, formatFromStyle(*wb.style(styleId)));
        }
        return formats.value(styleId);
    };
    HiddenLists lists{hiddenListsSheetName(wb), {}};
    // M78 (D-087): the dxfs of the whole save — the source's first.
    DxfTable dxfs(wb.source.dxfs, wb.source.themeXml.isEmpty() ? ThemePalette::officeDefault()
                                                               : themePaletteFromXml(wb.source.themeXml));
    int nextVmlBlock = 1;
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        const Sheet &sheet = wb.sheetAt(s);
        if (s == 0 && !doc->sheetNames().isEmpty()) {
            doc->renameSheet(doc->sheetNames().first(), sheet.name());
        } else {
            doc->addSheet(sheet.name());
        }
        doc->selectSheet(sheet.name());
        auto *worksheet = static_cast<QXlsx::Worksheet *>(doc->sheet(sheet.name()));
        if (sheet.isHidden()) {
            worksheet->setSheetState(QXlsx::AbstractSheet::SS_Hidden);
        }
        QHash<quint64, QString> headerText;
        QList<PatchManifest::TablePart> tables;
        for (const TableSpec &table : sheet.tables()) {
            tables.append(tablePartForFile(wb, s, table, &headerText));
        }
        // QXlsx's sheet part naming is deterministic (document order); the
        // post-pass asserts it before touching anything.
        PatchManifest::SheetPatch patch;
        patch.sheetPart = QStringLiteral("xl/worksheets/sheet%1.xml").arg(s + 1);
        patch.chartSheet = sheet.isChartSheet();
        // AGENT-GUARD: merges before cells. QXlsx's mergeCells() blanks every
        // cell under a merge but its top-left one; Excel files can hold
        // values there (hidden, still seen by formulas), and writing the
        // cells afterwards keeps them (found by tst_calc_io_corpus).
        for (const CellRange &merge : sheet.merges()) {
            doc->mergeCells(QXlsx::CellRange(merge.topLeft.row + 1, merge.topLeft.col + 1,
                                             merge.bottomRight.row + 1, merge.bottomRight.col + 1));
        }
        writeSheetCells(worksheet, wb, s, formatFor, headerText, &patch);
        // A run of equal widths is one call: QXlsx splits its column records
        // on every call, so a width per column out to XFD took it minutes
        // (M69, found by tst_calc_io_corpus).
        QList<qint32> sized = sheet.sizedColumns();
        std::sort(sized.begin(), sized.end());
        for (qsizetype i = 0; i < sized.size();) {
            const qreal width = sheet.columnWidth(sized.at(i));
            qsizetype last = i;
            while (last + 1 < sized.size() && sized.at(last + 1) == sized.at(last) + 1
                   && sheet.columnWidth(sized.at(last + 1)) == width) {
                ++last;
            }
            worksheet->setColumnWidth(sized.at(i) + 1, sized.at(last) + 1, pxToCharWidth(width));
            i = last + 1;
        }
        for (const qint32 row : sheet.sizedRows()) {
            worksheet->setRowHeight(row + 1, row + 1, pxToPt(sheet.rowHeight(row)));
        }
        if (manifest == nullptr) {
            // No post-pass: QXlsx's own model is all there is.
            writeValidations(worksheet, wb, sheet, &lists);
            writeCondFormats(worksheet, sheet);
        } else {
            // M78 (D-087): every rule kind in Excel's markup, kept rules
            // byte for byte (xlsx_rules.h).
            fillRulesPatch(wb, sheet, &dxfs, &lists, &patch, manifest->warnings);
        }
        fillSheetPatch(sheet, s, tables, &nextVmlBlock, &patch);
        patch.customViewsXml = customSheetViewsXml(wb, s); // M82 (D-091)
        fillSheetFeatures(wb, sheet, &patch); // links, outline, protection, source (M66)
        fillWhatIfPatch(wb, s, &patch);       // scenarios and data tables (M78)
        fillSparklinePatch(wb, s, &patch);    // sparklines, after the x14 rules (M90)
        if (manifest != nullptr) {
            // Every sheet: the carry stage and the features need each one.
            manifest->dynamicArrays = manifest->dynamicArrays || !patch.arrayRefs.isEmpty();
            manifest->sheets.append(patch);
        }
    }
    // Lists too long for Excel's inline form live on a hidden sheet AFTER
    // the user's sheets, so no sheet index — print names, scopes, the
    // active tab, the post-pass's part names — moves.
    if (!lists.columns.isEmpty()) {
        doc->addSheet(lists.sheetName);
        doc->selectSheet(lists.sheetName);
        auto *listSheet = static_cast<QXlsx::Worksheet *>(doc->sheet(lists.sheetName));
        listSheet->setSheetState(QXlsx::AbstractSheet::SS_Hidden);
        for (qsizetype col = 0; col < lists.columns.size(); ++col) {
            const QStringList &items = lists.columns.at(col);
            for (qsizetype row = 0; row < items.size(); ++row) {
                listSheet->writeString(int(row) + 1, int(col) + 1, encodeXString(items.at(row)));
            }
        }
    }
    writeCharts(doc.get(), wb, manifest);
    if (manifest != nullptr) {
        // Floating images (M39). QXlsx's insertImage re-encodes every image
        // as PNG and sizes it from the QImage's DPI, which loses the
        // author's format and extent, so pictures always travel in the
        // manifest.
        for (const ImageSpec &image : wb.images) {
            if (image.sheet < 0 || image.sheet >= wb.sheetCount() || !image.isValid()) {
                continue;
            }
            PatchManifest::ImagePatch patch;
            patch.spec = image;
            patch.sheetPart = QStringLiteral("xl/worksheets/sheet%1.xml").arg(image.sheet + 1);
            manifest->images.append(patch);
        }
        manifest->definedNames = definedNamesForFile(wb);
        manifest->definedNames += customViewNames(wb); // M82 (D-091): the views' names
        manifest->customWorkbookViewsXml = customWorkbookViewsXml(wb);
        manifest->dxfsXml = dxfs.xml(); // M78 (D-087)
        planPivotParts(wb, manifest); // M74 (D-083)
        manifest->source = wb.source;
        manifest->protection = wb.protection;
    } else {
        // No post-pass: the workbook range names go through QXlsx itself.
        const QHash<QString, RangeRef> &names = wb.namedRanges();
        for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
            static_cast<void>(doc->workbook()->defineName(it.key(), rangeRefText(it.value())));
        }
    }
    // The write loop selected every sheet in turn; restore the engine's
    // active tab unconditionally, or the file opens on the LAST sheet.
    doc->workbook()->setActiveSheet(wb.activeTab());
    return doc;
}

std::unique_ptr<QXlsx::Document> writeXlsx(const Workbook &wb, QString *error)
{
    // No post-pass without a file: the returned document carries everything
    // QXlsx has a model for (which includes validations and conditional
    // formats) but none of the manifest-only parts (autofilter, comments,
    // tables, typed error cells, scoped names, properties, …).
    return buildXlsx(wb, nullptr, error);
}

bool writeXlsxFile(const Workbook &wb, const QString &path, QString *error, QStringList *warnings)
{
    PatchManifest manifest;
    manifest.macroEnabled = path.endsWith(QLatin1String(".xlsm"), Qt::CaseInsensitive);
    QStringList notes;
    manifest.warnings = &notes;
    std::unique_ptr<QXlsx::Document> doc = buildXlsx(wb, &manifest, error);
    if (doc == nullptr) {
        return false;
    }
    QBuffer raw;
    raw.open(QIODevice::WriteOnly);
    if (!doc->saveAs(&raw)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot build the package for %1").arg(path);
        }
        return false;
    }
    QByteArray package = raw.data();
    // The post-pass always runs: at the least it replaces QXlsx's document
    // properties ("Qt Xlsx Library", "Microsoft Excel").
    if (!applyPatchManifest(&package, manifest, error)) {
        return false;
    }
    if (warnings != nullptr) {
        *warnings = notes;
    }
    // AGENT-GUARD: one atomic write of the finished bytes. The old save
    // wrote QXlsx's package to `path` and rewrote it in place, so a failing
    // post-pass left the user's file half-done.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(package) != package.size() || !file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
