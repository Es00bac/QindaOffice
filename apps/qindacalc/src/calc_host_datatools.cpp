// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's Data ▸ Data Tools (M78, D-087): Text to Columns, Flash Fill
// (Ctrl+E), Remove Duplicates, Consolidate, Circle Invalid Data / Clear
// Validation Circles — and the Data menu's What-If Analysis and Data Tools
// entries. The engine does the work (text_to_columns.h, flash_fill.h,
// data_tools.h); each change is one undo step.

#include "calc_host.h"

#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "data_tools.h"
#include "flash_fill.h"
#include "parser.h"
#include "sheet.h"
#include "text_to_columns.h"

#include <QKeySequence>

#include <algorithm>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

constexpr int kPreviewLines = 200;
constexpr int kMaxCircles = 255; // Excel rings at most 255 cells

TextToColumnsOptions optionsFrom(const QVariantMap &map)
{
    TextToColumnsOptions options;
    options.mode = map.value(QStringLiteral("mode")).toString() == QLatin1String("fixed") ? TextToColumnsOptions::Mode::FixedWidth
                                                                                          : TextToColumnsOptions::Mode::Delimited;
    options.tab = map.value(QStringLiteral("tab"), true).toBool();
    options.semicolon = map.value(QStringLiteral("semicolon")).toBool();
    options.comma = map.value(QStringLiteral("comma")).toBool();
    options.space = map.value(QStringLiteral("space")).toBool();
    options.other = map.value(QStringLiteral("other")).toString().left(1);
    options.consecutiveAsOne = map.value(QStringLiteral("consecutive")).toBool();
    const QString qualifier = map.value(QStringLiteral("qualifier"), QStringLiteral("\"")).toString();
    options.qualifier = qualifier.isEmpty() ? QChar() : qualifier.at(0);
    for (const QVariant &value : map.value(QStringLiteral("breaks")).toList()) {
        options.breaks.append(value.toInt());
    }
    std::sort(options.breaks.begin(), options.breaks.end());
    for (const QVariant &value : map.value(QStringLiteral("formats")).toList()) {
        options.formats.append(TextToColumnsOptions::Format(qBound(0, value.toInt(), int(TextToColumnsOptions::Format::Skip))));
    }
    return options;
}

} // namespace

void CalcHost::createDataToolActions()
{
    Quick::ActionModel *model = actions();
    const auto make = [this, model](const char *id, const QString &text, const QKeySequence &key, const char *icon,
                                    const std::function<void()> &run) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, key, QString::fromLatin1(icon));
        connect(entry, &ActionEntry::triggered, this, [this, run] {
            if (!m_syncing) {
                run();
            }
        });
    };
    make(ActionId::kCalcTextToColumns, tr("Text to C&olumns…"), {}, "columns-3", [this] { emit textToColumnsRequested(); });
    make(ActionId::kCalcFlashFill, tr("&Flash Fill"), QKeySequence(QStringLiteral("Ctrl+E")), "zap", [this] { flashFillAtCursor(); });
    make(ActionId::kCalcRemoveDuplicates, tr("Remove &Duplicates…"), {}, "list-minus", [this] { emit removeDuplicatesRequested(); });
    make(ActionId::kCalcConsolidate, tr("Co&nsolidate…"), {}, "combine", [this] { emit consolidateRequested(); });
    make(ActionId::kCalcCircleInvalid, tr("&Circle Invalid Data"), {}, "circle-alert", [this] {
        m_circlesOn = true;
        refreshInvalidCircles();
        syncFromCursor();
    });
    make(ActionId::kCalcClearCircles, tr("C&lear Validation Circles"), {}, "circle-off", [this] {
        m_circlesOn = false;
        if (m_grid != nullptr) {
            m_grid->setInvalidCells({});
        }
        syncFromCursor();
    });
    // The rings follow the values and the sheet shown.
    connect(m_document, &SpreadsheetDocument::contentChanged, this, [this] { refreshInvalidCircles(); });
    connect(this, &CalcHost::sheetsChanged, this, [this] { refreshInvalidCircles(); });
}

void CalcHost::insertDataToolMenus()
{
    const auto a = [](const char *id) { return MenuNode::action(QString::fromLatin1(id)); };
    Quick::MenuTree *tree = menuTree();
    tree->appendTo(QStringLiteral("calcDataMenu"), a(ActionId::kCalcCircleInvalid));
    tree->appendTo(QStringLiteral("calcDataMenu"), a(ActionId::kCalcClearCircles));
    tree->appendTo(QStringLiteral("calcDataMenu"), MenuNode::separator());
    tree->appendTo(QStringLiteral("calcDataMenu"), a(ActionId::kCalcTextToColumns));
    tree->appendTo(QStringLiteral("calcDataMenu"), a(ActionId::kCalcFlashFill));
    tree->appendTo(QStringLiteral("calcDataMenu"), a(ActionId::kCalcRemoveDuplicates));
    tree->appendTo(QStringLiteral("calcDataMenu"), a(ActionId::kCalcConsolidate));
    tree->appendTo(QStringLiteral("calcDataMenu"),
                   MenuNode::submenu(QStringLiteral("calcWhatIfMenu"), tr("&What-If Analysis"),
                                     {a(ActionId::kCalcScenarioManager), a(ActionId::kCalcGoalSeek), a(ActionId::kCalcDataTable)}));
}

void CalcHost::syncDataToolsFromCursor()
{
    m_syncing = true;
    Quick::ActionModel *model = actions();
    const bool cells = !sheet().isChartSheet();
    const CellRange range = selection();
    model->setEnabled(QString::fromLatin1(ActionId::kCalcTextToColumns), cells && range.topLeft.col == range.bottomRight.col);
    for (const char *id : {ActionId::kCalcFlashFill, ActionId::kCalcRemoveDuplicates, ActionId::kCalcConsolidate,
                           ActionId::kCalcGoalSeek, ActionId::kCalcScenarioManager, ActionId::kCalcDataTable}) {
        model->setEnabled(QString::fromLatin1(id), cells);
    }
    model->setEnabled(QString::fromLatin1(ActionId::kCalcCircleInvalid), cells && !sheet().validations().isEmpty());
    model->setEnabled(QString::fromLatin1(ActionId::kCalcClearCircles), m_circlesOn);
    m_syncing = false;
}

// ---- Text to Columns --------------------------------------------------------------

QVariantMap CalcHost::textToColumnsState() const
{
    const CellRange range = selection();
    QStringList lines;
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row && lines.size() < kPreviewLines; ++r) {
        lines.append(toText(m_document->workbook()->cellValue(m_document->activeSheet(), r, range.topLeft.col)));
    }
    return {{QStringLiteral("lines"), lines},
            {QStringLiteral("oneColumn"), range.topLeft.col == range.bottomRight.col},
            {QStringLiteral("destination"), addrText(range.topLeft, true, true)},
            {QStringLiteral("breaks"), QVariant::fromValue(guessFixedWidthBreaks(lines))}};
}

QVariantList CalcHost::textToColumnsPreview(const QVariantMap &options) const
{
    QVariantList rows;
    const QStringList lines = textToColumnsState().value(QStringLiteral("lines")).toStringList();
    for (const QStringList &fields : splitTextToColumns(lines.mid(0, 12), optionsFrom(options))) {
        rows.append(fields);
    }
    return rows;
}

QString CalcHost::applyTextToColumns(const QVariantMap &options, bool overwrite)
{
    const CellRange range = selection();
    if (range.topLeft.col != range.bottomRight.col) {
        return tr("Text to Columns can convert only one column at a time.");
    }
    QStringList lines;
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
        lines.append(toText(m_document->workbook()->cellValue(m_document->activeSheet(), r, range.topLeft.col)));
    }
    const QString destinationText = options.value(QStringLiteral("destination")).toString();
    const CellAddr destination = destinationText.trimmed().isEmpty() ? range.topLeft : cellFromText(destinationText);
    if (!destination.isValid()) {
        return tr("Destination reference is not valid.");
    }
    const TextToColumnsOptions parsed = optionsFrom(options);
    const int width = qMax(1, textToColumnsWidth(lines, parsed));
    const CellRange block{{range.topLeft.row, destination.col},
                          {range.bottomRight.row, destination.col + width - 1}};
    if (!overwrite) {
        // Excel asks before it writes over cells other than the source's.
        for (qint32 r = block.topLeft.row; r <= block.bottomRight.row; ++r) {
            for (qint32 c = block.topLeft.col; c <= block.bottomRight.col; ++c) {
                const bool source = c == range.topLeft.col;
                if (!source && !isBlank(m_document->workbook()->cellValue(m_document->activeSheet(), r, c))) {
                    return QStringLiteral("confirm");
                }
            }
        }
    }
    const CellAddr start{range.topLeft.row, destination.col};
    if (!m_document->applyTextColumns(textToColumnsCells(lines, range.topLeft.row, start, parsed), block)) {
        return tr("The text could not be converted here.");
    }
    refreshGrid();
    emit cursorChanged();
    return {};
}

// ---- Flash Fill ---------------------------------------------------------------------

void CalcHost::flashFillAtCursor()
{
    const Workbook &book = *m_document->workbook();
    const qint32 sheetIndex = m_document->activeSheet();
    const CellAddr at = cursor();
    // The data beside the target column: Excel's current region around the
    // (blank) cell being filled.
    const CellRange region = currentRegion(book.sheetAt(sheetIndex), at);
    if (!region.isValid() || region.topLeft.col == region.bottomRight.col) {
        showStatusMessage(tr("Flash Fill needs data in the columns next to this one."), 8000);
        return;
    }
    // One reading of the region: with or without its first row as headers.
    struct Reading {
        qint32 firstRow = 0;
        QHash<int, QString> examples;
        FlashFillResult result;
    };
    const auto read = [&](bool header) {
        Reading reading;
        reading.firstRow = region.topLeft.row + (header ? 1 : 0);
        QList<QStringList> sources;
        QStringList names;
        for (qint32 c = region.topLeft.col; c <= region.bottomRight.col; ++c) {
            if (c != at.col) {
                names.append(header ? toText(book.cellValue(sheetIndex, region.topLeft.row, c)) : tr("Column %1").arg(colName(c)));
            }
        }
        for (qint32 r = reading.firstRow; r <= region.bottomRight.row; ++r) {
            QStringList row;
            for (qint32 c = region.topLeft.col; c <= region.bottomRight.col; ++c) {
                if (c != at.col) {
                    row.append(m_document->displayTextAt({r, c}));
                }
            }
            sources.append(row);
            const QString typed = m_document->displayTextAt({r, at.col});
            if (!typed.isEmpty()) {
                reading.examples.insert(int(r - reading.firstRow), typed);
            }
        }
        reading.result = flashFill(sources, reading.examples, names);
        return reading;
    };
    // AGENT-NOTE: an all-text title row ("Full name" | "Last") does not look
    // like headers to looksLikeHeaderRow (no number below it), yet Excel
    // reads it as one: when the first row's own example fits no pattern the
    // examples below agree on, the first row is the header.
    const bool header = looksLikeHeaderRow(book, sheetIndex, region);
    Reading reading = read(header);
    if (!reading.result.ok && !header && region.bottomRight.row > region.topLeft.row) {
        Reading titled = read(true);
        if (titled.result.ok) {
            reading = titled;
        }
    }
    const FlashFillResult &result = reading.result;
    const QHash<int, QString> &examples = reading.examples;
    const qint32 firstRow = reading.firstRow;
    if (!result.ok) {
        showStatusMessage(tr("Flash Fill didn't recognize a pattern: %1").arg(result.failure), 12000);
        return;
    }
    QList<QPair<CellAddr, QString>> edits;
    for (qsizetype i = 0; i < result.outputs.size(); ++i) {
        if (!examples.contains(int(i)) && !result.outputs.at(i).isEmpty()) {
            edits.append({{firstRow + qint32(i), at.col}, result.outputs.at(i)});
        }
    }
    if (edits.isEmpty()) {
        showStatusMessage(tr("Flash Fill found nothing left to fill."), 8000);
        return;
    }
    m_document->applyInputs(edits, tr("Flash Fill"));
    refreshGrid();
    showStatusMessage(tr("Flash Fill changed %n cell(s): %1.", nullptr, int(edits.size())).arg(result.pattern), 10000);
}

// ---- Remove Duplicates --------------------------------------------------------------

QVariantMap CalcHost::removeDuplicatesState(bool header) const
{
    CellRange range = selection();
    if (range.cellCount() <= 1) {
        range = blockAroundSelection();
    }
    QVariantList columns;
    for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
        const QString title = header ? toText(m_document->workbook()->cellValue(m_document->activeSheet(), range.topLeft.row, c))
                                     : QString();
        columns.append(QVariantMap{{QStringLiteral("index"), c},
                                   {QStringLiteral("name"), title.isEmpty() ? tr("Column %1").arg(colName(c)) : title}});
    }
    return {{QStringLiteral("range"), addrText(range.topLeft, true, true) + QLatin1Char(':') + addrText(range.bottomRight, true, true)},
            {QStringLiteral("suggestedHeader"), looksLikeHeaderRow(*m_document->workbook(), m_document->activeSheet(), range)},
            {QStringLiteral("columns"), columns}};
}

QString CalcHost::applyRemoveDuplicates(const QVariantList &columns, bool header)
{
    const QString rangeText = removeDuplicatesState(header).value(QStringLiteral("range")).toString();
    const ParseResult parsed = parseFormula(rangeText);
    if (!parsed.root || parsed.root->kind != Node::Kind::Ref) {
        return tr("Select a range of data first.");
    }
    QList<qint32> picked;
    for (const QVariant &column : columns) {
        picked.append(qint32(column.toInt()));
    }
    if (picked.isEmpty()) {
        return tr("Select one or more columns.");
    }
    const std::optional<RemoveDuplicatesResult> result = m_document->removeDuplicateRows(parsed.root->ref.range, picked, header);
    if (!result) {
        return tr("Duplicates could not be removed here.");
    }
    refreshGrid();
    if (result->removed == 0) {
        return tr("No duplicate values found.");
    }
    return tr("%1 duplicate values found and removed; %2 unique values remain.").arg(result->removed).arg(result->remaining);
}

// ---- Consolidate --------------------------------------------------------------------

QVariantMap CalcHost::consolidateState() const
{
    return {{QStringLiteral("functions"), QStringList{tr("Sum"), tr("Count"), tr("Average"), tr("Max"), tr("Min"), tr("Product"),
                                                      tr("Count Numbers"), tr("StdDev"), tr("StdDevp"), tr("Var"), tr("Varp")}},
            {QStringLiteral("destination"), addrText(cursor(), true, true)},
            {QStringLiteral("reference"), addrText(selection().topLeft, true, true) + QLatin1Char(':')
                                              + addrText(selection().bottomRight, true, true)}};
}

QString CalcHost::applyConsolidate(const QVariantMap &options)
{
    ConsolidateOptions request;
    request.function = ConsolidateFunction(qBound(0, options.value(QStringLiteral("function")).toInt(), int(ConsolidateFunction::VarP)));
    request.topRow = options.value(QStringLiteral("topRow")).toBool();
    request.leftColumn = options.value(QStringLiteral("leftColumn")).toBool();
    for (const QVariant &value : options.value(QStringLiteral("references")).toList()) {
        const ParseResult parsed = parseFormula(value.toString().trimmed());
        if (!parsed.root || parsed.root->kind != Node::Kind::Ref || parsed.root->ref.wholeRows || parsed.root->ref.wholeCols) {
            return tr("Reference isn't valid: %1").arg(value.toString());
        }
        const RangeRef &ref = parsed.root->ref;
        const qint32 sheetIndex = ref.sheet.isEmpty() ? m_document->activeSheet()
                                                      : m_document->workbook()->sheetIndex(unquotedSheetName(ref.sheet));
        if (sheetIndex < 0) {
            return tr("Reference isn't valid: %1").arg(value.toString());
        }
        request.sources.append({sheetIndex, ref.range});
    }
    if (request.sources.isEmpty()) {
        return tr("Add at least one reference.");
    }
    const ConsolidateResult result = consolidate(*m_document->workbook(), request);
    if (!result.error.isEmpty()) {
        return result.error;
    }
    const QString destinationText = options.value(QStringLiteral("destination")).toString();
    const CellAddr destination = destinationText.trimmed().isEmpty() ? cursor() : cellFromText(destinationText);
    if (!destination.isValid()) {
        return tr("Destination reference is not valid.");
    }
    if (!m_document->writeValues(destination, result.grid, tr("Consolidate"))) {
        return tr("The result could not be written there.");
    }
    refreshGrid();
    return {};
}

// ---- Circle Invalid Data ------------------------------------------------------------

void CalcHost::refreshInvalidCircles()
{
    if (m_grid == nullptr) {
        return;
    }
    if (!m_circlesOn) {
        return;
    }
    const Workbook &book = *m_document->workbook();
    const qint32 sheetIndex = m_document->activeSheet();
    QList<CellAddr> invalid;
    for (const ValidationRule &rule : sheet().validations()) {
        for (const CellRange &range : QList<CellRange>{rule.range} + rule.moreRanges) {
            for (const auto &[addr, value] : book.nonBlankCellsIn(sheetIndex, range)) {
                if (invalid.size() < kMaxCircles && !invalid.contains(addr)
                    && !validationAcceptsValue(rule, addr, value, book, sheetIndex)) {
                    invalid.append(addr);
                }
            }
        }
    }
    m_grid->setInvalidCells(invalid);
}

} // namespace QindaOffice::Apps::QindaCalc
