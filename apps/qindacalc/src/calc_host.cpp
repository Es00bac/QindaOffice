// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_host.h"

#include "calc_clipboard.h"
#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "parser.h"
#include "preferences.h"
#include "sheet.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLocale>
#include <QMimeData>
#include <QRegularExpression>
#include <QUndoStack>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {

// The selection readout is bounded: a whole-column selection can cover the
// used range, and walking millions of addresses on every cursor move would
// make the grid feel broken.
constexpr qint64 kMaxAggregateCells = 200000;

// A name the workbook can hold: identifier-shaped and not something the
// engine would read as a reference (or the name would shadow a real cell).
bool isDefinableName(const QString &text)
{
    static const QRegularExpression shape(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.]*$"));
    if (!shape.match(text).hasMatch() || text.size() > 255) {
        return false;
    }
    const ParseResult parsed = parseFormula(text);
    return !(parsed.root && parsed.root->kind == Node::Kind::Ref);
}

} // namespace

CalcHost::CalcHost()
    : m_document(new SpreadsheetDocument)
{
    setDocument(m_document);
    createFormatActions();
    createStructureActions();
    createRuleActions();
    createTextActions();
    createAssistant();
    insertMenus();
    wireEditCommands();
    // Undo of a sheet insert/delete (or a load) changes the sheet list.
    connect(m_document, &SpreadsheetDocument::structureChanged, this, [this] {
        emit sheetsChanged();
        refreshGrid();
        emit chartsChanged();
        syncFromCursor();
    });
    // Data edits change what charts show: their CONTENTS re-render, the
    // windows stay where the user put them (chartsChanged is a refresh).
    connect(m_document, &SpreadsheetDocument::contentChanged, this, [this] { emit chartsChanged(); });
    // A lossy-but-successful save (CSV keeps one sheet) has to be visible.
    connect(m_document, &SpreadsheetDocument::saveWarning, this,
            [this](const QString &text) { showStatusMessage(text, 12000); });
    // A value a validation rule refused must SAY so.
    connect(m_document, &SpreadsheetDocument::inputRejected, this,
            [this](const CellAddr &addr, const QString &reason) {
                showStatusMessage(tr("%1 only accepts %2").arg(addrText(addr), reason), 8000);
                refreshGrid();
            });
    finalizeHost();
    updateSelectionStatus();
}

CalcHost::~CalcHost() = default;

const Sheet &CalcHost::sheet() const
{
    return m_document->workbook()->sheetAt(m_document->activeSheet());
}

CellRange CalcHost::selection() const
{
    if (m_grid != nullptr) {
        return m_grid->selectionRange();
    }
    return {{0, 0}, {0, 0}};
}

CellAddr CalcHost::cursor() const
{
    return m_grid != nullptr ? m_grid->cursorCell() : CellAddr{0, 0};
}

void CalcHost::refreshGrid()
{
    if (m_grid != nullptr) {
        m_grid->refreshAll();
    }
}

void CalcHost::attachGrid(SpreadsheetItem *grid)
{
    m_grid = grid;
    if (grid == nullptr) {
        return;
    }
    grid->setDocument(m_document);
    wireGrid();
    applyPreferences();
    syncFromCursor();
    updateSelectionStatus();
}

void CalcHost::wireGrid()
{
    connect(m_grid, &SpreadsheetItem::cursorChanged, this, [this] {
        syncFromCursor();
        emit cursorChanged();
    });
    connect(m_grid, &SpreadsheetItem::selectionChanged, this,
            [this](const CellRange &) { updateSelectionStatus(); });
    connect(m_grid, &SpreadsheetItem::editCommitted, this, &CalcHost::commitInput);
    connect(m_grid, &SpreadsheetItem::clearRequested, this, [this](const CellRange &range) {
        m_document->clearCells(range);
        refreshGrid();
    });
    connect(m_grid, &SpreadsheetItem::columnWidthChanged, this, [this](int col, qreal px) {
        m_document->setColumnWidths({qint32(col)}, px);
        refreshGrid();
    });
    connect(m_grid, &SpreadsheetItem::rowHeightChanged, this, [this](int row, qreal px) {
        m_document->setRowHeights({qint32(row)}, px);
        refreshGrid();
    });
    connect(m_grid, &SpreadsheetItem::contextMenuRequested, this, &CalcHost::contextMenuRequested);
    connect(m_grid, &SpreadsheetItem::filterButtonClicked, this, [this](int column, qreal x, qreal y) {
        const AutoFilter filter = sheet().autoFilter();
        if (!filter.active || !filter.range.isValid()) {
            return;
        }
        const QStringList values = sheet().filterValuesFor(
            qint32(column), [this](const CellAddr &addr) { return m_document->displayTextAt(addr); });
        const QStringList allowed =
            filter.allowed.contains(qint32(column)) ? filter.allowed.value(qint32(column)) : values;
        emit filterMenuRequested(column, values, allowed, x, y);
    });
    connect(m_grid, &SpreadsheetItem::zoomChanged, this, [this] { updateSelectionStatus(); });
}

void CalcHost::syncFromCursor()
{
    if (m_grid == nullptr) {
        return;
    }
    syncFormatFromCursor();
    syncStructureFromCursor();
    syncRulesFromCursor();
}

void CalcHost::commitInput(const CellAddr &addr, const QString &input)
{
    m_document->applyInput(addr, input);
    refreshGrid();
}

void CalcHost::documentReplaced()
{
    if (m_document->activeSheet() >= m_document->workbook()->sheetCount()) {
        m_document->setActiveSheet(0);
    }
    emit sheetsChanged();
    refreshGrid();
    emit chartsChanged();
    if (m_grid != nullptr) {
        m_grid->setCursorCell(CellAddr{0, 0});
    }
    syncFromCursor();
    updateSelectionStatus();
}

// ---- sheets --------------------------------------------------------------------

QStringList CalcHost::sheetNames() const
{
    QStringList names;
    const Workbook *wb = m_document->workbook();
    for (qint32 i = 0; i < wb->sheetCount(); ++i) {
        names.append(wb->sheetName(i));
    }
    return names;
}

int CalcHost::activeSheet() const
{
    return int(m_document->activeSheet());
}

void CalcHost::setActiveSheet(int index)
{
    if (index < 0 || index >= m_document->workbook()->sheetCount()
        || index == m_document->activeSheet()) {
        return;
    }
    if (m_grid != nullptr) {
        m_grid->cancelEditor();
    }
    m_document->setActiveSheet(qint32(index));
    refreshGrid();
    emit sheetsChanged();
    syncFromCursor();
    updateSelectionStatus();
    emit cursorChanged();
}

void CalcHost::activateSheetForMenu(int index)
{
    // Act on the tab that was clicked, not on whichever sheet happened to
    // be active: right-clicking a tab and getting a menu for a different
    // sheet is how data gets deleted by accident.
    setActiveSheet(index);
}

void CalcHost::moveSheet(int from, int to)
{
    m_document->moveSheet(qint32(from), qint32(to));
}

void CalcHost::renameSheet(int index, const QString &rawName)
{
    const QString name = rawName.trimmed();
    const Workbook *wb = m_document->workbook();
    if (index < 0 || index >= wb->sheetCount() || name.isEmpty()) {
        return;
    }
    if (wb->sheetIndex(name) >= 0 && wb->sheetIndex(name) != index) {
        showStatusMessage(tr("A sheet called “%1” already exists.").arg(name), 8000);
        return;
    }
    m_document->renameSheet(qint32(index), name);
}

void CalcHost::deleteSheetConfirmed(int index)
{
    if (index < 0 || index >= m_document->workbook()->sheetCount()
        || m_document->workbook()->sheetCount() <= 1) {
        return;
    }
    m_document->removeSheet(qint32(index));
}

// ---- formula bar and name box ----------------------------------------------------

QString CalcHost::cursorText() const
{
    return addrText(cursor());
}

QString CalcHost::formulaText() const
{
    return m_document->inputTextAt(cursor());
}

void CalcHost::commitFormulaBar(const QString &text)
{
    if (m_grid != nullptr) {
        m_grid->commitFormulaBar(text);
    } else {
        commitInput(cursor(), text);
    }
}

void CalcHost::goToRange(const RangeRef &ref)
{
    if (!ref.sheet.isEmpty()) {
        const qint32 index = m_document->workbook()->sheetIndex(ref.sheet);
        if (index >= 0 && index != m_document->activeSheet()) {
            setActiveSheet(int(index));
        }
    }
    if (m_grid == nullptr) {
        return;
    }
    m_grid->setCursorCell(ref.range.topLeft);
    if (ref.range.topLeft != ref.range.bottomRight) {
        m_grid->setCursorCell(ref.range.bottomRight, true);
    }
}

void CalcHost::commitNameBox(const QString &rawText)
{
    const QString text = rawText.trimmed();
    if (text.isEmpty()) {
        return;
    }
    // Name-box semantics: a reference or an existing name navigates;
    // anything else that is a legal name defines one over the selection.
    const ParseResult parsed = parseFormula(text);
    if (parsed.root && parsed.root->kind == Node::Kind::Ref) {
        goToRange(parsed.root->ref);
        return;
    }
    const auto existing = m_document->workbook()->namedRange(text.toUpper());
    if (existing.has_value()) {
        goToRange(*existing);
        showStatusMessage(tr("%1 = %2").arg(text.toUpper(), rangeRefText(*existing)));
        return;
    }
    if (!isDefinableName(text)) {
        showStatusMessage(tr("“%1” is not a cell reference or a usable name.").arg(text), 8000);
        return;
    }
    RangeRef target;
    target.range = selection();
    m_document->defineName(text, target);
    showStatusMessage(tr("Named %1 as “%2”").arg(rangeRefText(target), text.toUpper()));
}

// ---- status ---------------------------------------------------------------------

void CalcHost::updateSelectionStatus()
{
    const CellRange range = selection();
    const Workbook *wb = m_document->workbook();
    QString summary = addrText(cursor());
    if (range.cellCount() > 1) {
        summary = QStringLiteral("%1:%2").arg(addrText(range.topLeft), addrText(range.bottomRight));
    }
    if (range.cellCount() <= kMaxAggregateCells) {
        double sum = 0;
        qint64 numbers = 0;
        qint64 filled = 0;
        for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
            for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
                const Value value = wb->cellValue(m_document->activeSheet(), r, c);
                if (isBlank(value)) {
                    continue;
                }
                ++filled;
                if (const auto number = toNumber(value); number && isNumber(value)) {
                    sum += *number;
                    ++numbers;
                }
            }
        }
        const QLocale locale;
        if (numbers > 0) {
            summary += tr("   Sum: %1   Average: %2")
                           .arg(locale.toString(sum, 'g', 10),
                                locale.toString(sum / double(numbers), 'g', 10));
        }
        if (filled > 0) {
            summary += tr("   Count: %1").arg(filled);
        }
    }
    const qreal zoom = m_grid != nullptr ? m_grid->zoom() : 1.0;
    summary += tr("   Zoom: %1%").arg(qRound(zoom * 100));
    setStatusInfo(summary);
}

// ---- the shell's edit commands ----------------------------------------------------

void CalcHost::wireEditCommands()
{
    ActionEntry *undo = action(QString::fromLatin1(ActionId::kEditUndo));
    ActionEntry *redo = action(QString::fromLatin1(ActionId::kEditRedo));
    undo->setEnabled(false);
    redo->setEnabled(false);
    connect(m_document->undoStack(), &QUndoStack::canUndoChanged, undo, &ActionEntry::setEnabled);
    connect(m_document->undoStack(), &QUndoStack::canRedoChanged, redo, &ActionEntry::setEnabled);
    connect(undo, &ActionEntry::triggered, this, [this] {
        m_document->undoStack()->undo();
        refreshGrid();
    });
    connect(redo, &ActionEntry::triggered, this, [this] {
        m_document->undoStack()->redo();
        refreshGrid();
    });

    action(QString::fromLatin1(ActionId::kEditCut))->setEnabled(true);
    action(QString::fromLatin1(ActionId::kEditCopy))->setEnabled(true);
    connect(action(QString::fromLatin1(ActionId::kEditCut)), &ActionEntry::triggered, this, [this] {
        cutRangeToClipboard(m_document, selection());
        refreshGrid();
    });
    connect(action(QString::fromLatin1(ActionId::kEditCopy)), &ActionEntry::triggered, this,
            [this] { copyRangeToClipboard(m_document, selection()); });
    ActionEntry *paste = action(QString::fromLatin1(ActionId::kEditPaste));
    const auto updatePaste = [paste] {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        paste->setEnabled(mime != nullptr
                          && (mime->hasText()
                              || mime->hasFormat(QString::fromLatin1(richCellsMimeType()))));
    };
    updatePaste();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, updatePaste);
    connect(paste, &ActionEntry::triggered, this, [this] {
        if (pasteClipboardAt(m_document, cursor())) {
            refreshGrid();
        }
    });
    action(QString::fromLatin1(ActionId::kEditSelectAll))->setEnabled(true);
    connect(action(QString::fromLatin1(ActionId::kEditSelectAll)), &ActionEntry::triggered, this,
            [this] {
                if (m_grid != nullptr) {
                    m_grid->selectUsedRange();
                }
            });
    for (const char *id : {ActionId::kViewZoomIn, ActionId::kViewZoomOut, ActionId::kViewZoomReset}) {
        action(QString::fromLatin1(id))->setEnabled(true);
    }
    connect(action(QString::fromLatin1(ActionId::kViewZoomIn)), &ActionEntry::triggered, this,
            [this] { if (m_grid != nullptr) m_grid->setZoom(m_grid->zoom() * 1.25); });
    connect(action(QString::fromLatin1(ActionId::kViewZoomOut)), &ActionEntry::triggered, this,
            [this] { if (m_grid != nullptr) m_grid->setZoom(m_grid->zoom() / 1.25); });
    connect(action(QString::fromLatin1(ActionId::kViewZoomReset)), &ActionEntry::triggered, this,
            [this] { if (m_grid != nullptr) m_grid->setZoom(1.0); });
}

void CalcHost::applyPreferences()
{
    const Quick::Preferences &prefs = Quick::Preferences::instance();
    if (m_grid == nullptr) {
        return;
    }
    QFont cellFont = m_grid->cellFont();
    if (!prefs.documentFontFamily().isEmpty()) {
        cellFont.setFamily(prefs.documentFontFamily());
    }
    if (prefs.documentFontSize() > 0) {
        cellFont.setPointSizeF(prefs.documentFontSize());
    }
    m_grid->setCellFont(cellFont);
    m_grid->setZoom(prefs.defaultZoomPercent() / 100.0);
    refreshGrid();
    updateSelectionStatus();
}

QString CalcHost::filterString() const
{
    // Mirrors SpreadsheetDocument::mimeTypes() and the .desktop MimeType
    // list: ods/xls open through the LibreOffice bridge.
    return tr("Spreadsheets (*.xlsx *.ods *.xls *.xlsm);;CSV (*.csv *.tsv);;All files (*)");
}

QString CalcHost::saveFilterString() const
{
    // xlsx first: the native format, so Save As defaults to a round-trip
    // that keeps formulas, styles, names and charts.
    return tr("Excel Workbook (*.xlsx);;OpenDocument Spreadsheet (*.ods);;CSV (*.csv)");
}

} // namespace QindaOffice::Apps::QindaCalc
