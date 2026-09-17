// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_structure_actions.h"

#include "spreadsheet_document.h"
#include "spreadsheet_view.h"

#include "document_window.h"
#include "standard_actions.h"

#include "sheet.h"
#include "workbook.h"

#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

namespace QindaOffice::Apps::QindaCalc {

using CalcEng::CellAddr;
using CalcEng::CellRange;

namespace {

// Bit positions of CellStyle::borders, mirrored in the painter.
enum BorderBit { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8, kAll = 15 };

QAction *make(QObject *parent, const char *id, const QString &text,
              const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

} // namespace

CalcStructureActions::CalcStructureActions(Shell::DocumentWindow *window,
                                           SpreadsheetDocument *document, SpreadsheetView *view)
    : QObject(window)
    , m_window(window)
    , m_document(document)
    , m_view(view)
{
    createActions();
    connect(m_view, &SpreadsheetView::selectionChanged, this,
            [this](const CellRange &) { syncFromCursor(); });
    connect(m_view, &SpreadsheetView::clearRequested, this,
            [this](const CellRange &range) { m_document->clearCells(range); m_view->refreshAll(); });
    connect(m_view, &SpreadsheetView::columnWidthChanged, this,
            &CalcStructureActions::applyColumnWidth);
    connect(m_view, &SpreadsheetView::rowHeightChanged, this,
            &CalcStructureActions::applyRowHeight);
    syncFromCursor();
}

void CalcStructureActions::createActions()
{
    using namespace Shell::ActionId;
    // AGENT-NOTE: the shortcuts are the ones every spreadsheet uses, so
    // muscle memory carries over: Ctrl++/Ctrl+- insert and delete, Ctrl+D
    // and Ctrl+R fill, Alt+= sums. Ctrl++ is NOT a conflict with View ▸ Zoom
    // In: the shell binds zoom to QKeySequence::ZoomIn, which on this
    // platform theme is Ctrl+= — but to keep that beyond doubt, insert uses
    // Ctrl+Shift+Plus and delete Ctrl+Shift+Minus, as LibreOffice does.
    m_insertRows = make(this, kCalcInsertRows, tr("Insert &Rows Above"),
                        QKeySequence(QStringLiteral("Ctrl+Shift++")), "edit-table-insert-row-above");
    m_insertColumns = make(this, kCalcInsertColumns, tr("Insert &Columns Left"), {},
                           "edit-table-insert-column-left");
    m_deleteRows = make(this, kCalcDeleteRows, tr("&Delete Rows"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+-")), "edit-table-delete-row");
    m_deleteColumns =
        make(this, kCalcDeleteColumns, tr("Delete Colum&ns"), {}, "edit-table-delete-column");
    m_clearContents = make(this, kCalcClearContents, tr("Clear &Contents"),
                           QKeySequence(Qt::Key_Delete), "edit-clear");
    m_rowHeight = make(this, kCalcRowHeight, tr("Row &Height…"));
    m_columnWidth = make(this, kCalcColumnWidth, tr("Column &Width…"));
    m_optimalWidth = make(this, kCalcOptimalWidth, tr("&Optimal Column Width"));
    m_mergeCells = make(this, kCalcMergeCells, tr("&Merge Cells"), {}, "format-text-direction-ltr");
    m_unmergeCells = make(this, kCalcUnmergeCells, tr("&Unmerge Cells"));
    m_wrapText = make(this, kCalcWrapText, tr("Wrap &Text"));
    m_wrapText->setCheckable(true);
    m_bordersAll = make(this, kCalcBordersAll, tr("&All Borders"));
    m_bordersOutline = make(this, kCalcBordersOutline, tr("&Outline"));
    m_bordersNone = make(this, kCalcBordersNone, tr("&No Borders"));
    m_sortAscending =
        make(this, kCalcSortAscending, tr("Sort &Ascending"), {}, "view-sort-ascending");
    m_sortDescending =
        make(this, kCalcSortDescending, tr("Sort &Descending"), {}, "view-sort-descending");
    m_fillDown = make(this, kCalcFillDown, tr("Fill &Down"),
                      QKeySequence(QStringLiteral("Ctrl+D")));
    m_fillRight = make(this, kCalcFillRight, tr("Fill &Right"),
                       QKeySequence(QStringLiteral("Ctrl+R")));
    m_autoSum = make(this, kCalcAutoSum, tr("&Sum"), QKeySequence(QStringLiteral("Alt+=")));
    m_autoSum->setIconText(QStringLiteral("Σ"));
    m_freezePanes = make(this, kCalcFreezePanes, tr("&Freeze Rows and Columns"));
    m_freezePanes->setCheckable(true);
    m_sheetRename = make(this, kCalcSheetRename, tr("&Rename Sheet…"));
    m_sheetDelete = make(this, kCalcSheetDelete, tr("&Delete Sheet"));
    m_sheetDuplicate = make(this, kCalcSheetDuplicate, tr("D&uplicate Sheet"));
    m_sheetMoveLeft = make(this, kCalcSheetMoveLeft, tr("Move Sheet &Left"));
    m_sheetMoveRight = make(this, kCalcSheetMoveRight, tr("Move Sheet R&ight"));

    const auto selection = [this] { return m_view->selectionRange(); };
    connect(m_insertRows, &QAction::triggered, this, [this, selection] {
        const CellRange range = selection();
        m_document->insertRows(range.topLeft.row, range.bottomRight.row - range.topLeft.row + 1);
        m_view->refreshAll();
    });
    connect(m_insertColumns, &QAction::triggered, this, [this, selection] {
        const CellRange range = selection();
        m_document->insertColumns(range.topLeft.col,
                                  range.bottomRight.col - range.topLeft.col + 1);
        m_view->refreshAll();
    });
    connect(m_deleteRows, &QAction::triggered, this, [this, selection] {
        const CellRange range = selection();
        m_document->removeRows(range.topLeft.row, range.bottomRight.row - range.topLeft.row + 1);
        m_view->refreshAll();
    });
    connect(m_deleteColumns, &QAction::triggered, this, [this, selection] {
        const CellRange range = selection();
        m_document->removeColumns(range.topLeft.col,
                                  range.bottomRight.col - range.topLeft.col + 1);
        m_view->refreshAll();
    });
    connect(m_clearContents, &QAction::triggered, this, &CalcStructureActions::clearSelection);
    connect(m_rowHeight, &QAction::triggered, this, [this, selection] {
        const CellRange range = selection();
        const qreal current = m_document->workbook()
                                  ->sheetAt(m_document->activeSheet())
                                  .rowHeight(range.topLeft.row);
        bool ok = false;
        const double value = QInputDialog::getDouble(m_window, tr("Row Height"),
                                                     tr("Height (px):"), current, 8, 2000, 0, &ok);
        if (!ok) {
            return;
        }
        QList<qint32> rows;
        for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
            rows.append(r);
        }
        m_document->setRowHeights(rows, value);
        m_view->refreshAll();
    });
    connect(m_columnWidth, &QAction::triggered, this, [this, selection] {
        const CellRange range = selection();
        const qreal current = m_document->workbook()
                                  ->sheetAt(m_document->activeSheet())
                                  .columnWidth(range.topLeft.col);
        bool ok = false;
        const double value = QInputDialog::getDouble(m_window, tr("Column Width"),
                                                     tr("Width (px):"), current, 8, 2000, 0, &ok);
        if (!ok) {
            return;
        }
        QList<qint32> cols;
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            cols.append(c);
        }
        m_document->setColumnWidths(cols, value);
        m_view->refreshAll();
    });
    connect(m_optimalWidth, &QAction::triggered, this, [this, selection] {
        const CellRange range = selection();
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            m_document->setColumnWidths({c}, m_view->autofitColumnWidth(c));
        }
        m_view->refreshAll();
    });
    connect(m_mergeCells, &QAction::triggered, this, [this, selection] {
        m_document->mergeCells(selection());
        m_view->refreshAll();
    });
    connect(m_unmergeCells, &QAction::triggered, this, [this] {
        m_document->unmergeCells(m_view->cursorCell());
        m_view->refreshAll();
    });
    connect(m_wrapText, &QAction::triggered, this, [this, selection](bool on) {
        if (m_syncing) {
            return;
        }
        StylePatch patch;
        patch.wrap = on;
        m_document->restyleRange(selection(), patch, tr("Wrap text"));
        m_view->refreshAll();
    });
    connect(m_bordersAll, &QAction::triggered, this, [this] { applyBorders(kAll, false); });
    connect(m_bordersOutline, &QAction::triggered, this, [this] { applyBorders(kAll, true); });
    connect(m_bordersNone, &QAction::triggered, this, [this] { applyBorders(0, false); });
    connect(m_sortAscending, &QAction::triggered, this, [this] { sortSelection(true); });
    connect(m_sortDescending, &QAction::triggered, this, [this] { sortSelection(false); });
    connect(m_fillDown, &QAction::triggered, this, [this, selection] {
        m_document->fillRange(selection(), true);
        m_view->refreshAll();
    });
    connect(m_fillRight, &QAction::triggered, this, [this, selection] {
        m_document->fillRange(selection(), false);
        m_view->refreshAll();
    });
    connect(m_autoSum, &QAction::triggered, this, &CalcStructureActions::autoSum);
    connect(m_freezePanes, &QAction::triggered, this, [this](bool on) {
        if (!m_syncing) {
            toggleFreeze(on);
        }
    });
    connect(m_sheetRename, &QAction::triggered, this,
            [this] { renameSheet(int(m_document->activeSheet())); });
    connect(m_sheetDelete, &QAction::triggered, this, [this] {
        const qint32 index = m_document->activeSheet();
        const QString name = m_document->workbook()->sheetName(index);
        // Deleting a sheet destroys data and breaks every formula that named
        // it, so it asks — unlike every other structural edit here, which
        // Ctrl+Z covers on its own.
        if (QMessageBox::warning(m_window, tr("Delete sheet"),
                                 tr("Delete “%1” and everything on it?").arg(name),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        m_document->removeSheet(index);
    });
    connect(m_sheetDuplicate, &QAction::triggered, this, [this] {
        const qint32 index = m_document->activeSheet();
        const CalcEng::Workbook *wb = m_document->workbook();
        QString name = tr("%1 (copy)").arg(wb->sheetName(index));
        int suffix = 2;
        while (wb->sheetIndex(name) >= 0) {
            name = tr("%1 (copy %2)").arg(wb->sheetName(index)).arg(suffix++);
        }
        m_document->duplicateSheet(index, name);
    });
    connect(m_sheetMoveLeft, &QAction::triggered, this, [this] {
        const qint32 index = m_document->activeSheet();
        m_document->moveSheet(index, index - 1);
    });
    connect(m_sheetMoveRight, &QAction::triggered, this, [this] {
        const qint32 index = m_document->activeSheet();
        m_document->moveSheet(index, index + 1);
    });
}

void CalcStructureActions::clearSelection()
{
    m_document->clearCells(m_view->selectionRange());
    m_view->refreshAll();
}

void CalcStructureActions::applyColumnWidth(qint32 col, qreal px)
{
    m_document->setColumnWidths({col}, px);
    m_view->refreshAll();
}

void CalcStructureActions::applyRowHeight(qint32 row, qreal px)
{
    m_document->setRowHeights({row}, px);
    m_view->refreshAll();
}

void CalcStructureActions::renameSheet(int index)
{
    const CalcEng::Workbook *wb = m_document->workbook();
    if (index < 0 || index >= wb->sheetCount()) {
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(m_window, tr("Rename Sheet"), tr("Name:"), QLineEdit::Normal,
                              wb->sheetName(index), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (wb->sheetIndex(name) >= 0 && wb->sheetIndex(name) != index) {
        QMessageBox::warning(m_window, tr("Rename Sheet"),
                             tr("A sheet called “%1” already exists.").arg(name));
        return;
    }
    m_document->renameSheet(index, name);
}

void CalcStructureActions::applyBorders(int bits, bool outlineOnly)
{
    const CellRange range = m_view->selectionRange();
    if (!outlineOnly) {
        StylePatch patch;
        patch.borders = bits;
        m_document->restyleRange(range, patch, bits == 0 ? tr("Clear borders") : tr("Borders"));
        m_view->refreshAll();
        return;
    }
    // Outline: each edge cell gets only the edges that face outwards, so the
    // block reads as one framed box rather than a grid.
    for (qint32 row = range.topLeft.row; row <= range.bottomRight.row; ++row) {
        for (qint32 col = range.topLeft.col; col <= range.bottomRight.col; ++col) {
            int edges = 0;
            if (col == range.topLeft.col) {
                edges |= kLeft;
            }
            if (col == range.bottomRight.col) {
                edges |= kRight;
            }
            if (row == range.topLeft.row) {
                edges |= kTop;
            }
            if (row == range.bottomRight.row) {
                edges |= kBottom;
            }
            StylePatch patch;
            patch.borders = edges;
            m_document->restyleRange({{row, col}, {row, col}}, patch, tr("Outline"));
        }
    }
    m_view->refreshAll();
}

CellRange CalcStructureActions::blockAroundSelection() const
{
    const CellRange selection = m_view->selectionRange();
    if (selection.cellCount() > 1) {
        return selection;
    }
    const CalcEng::Sheet &sheet =
        m_document->workbook()->sheetAt(m_document->activeSheet());
    const auto filled = [&sheet](qint32 row, qint32 col) {
        const CalcEng::Cell *cell = sheet.cellAt({row, col});
        return cell != nullptr && !isBlank(cell->value);
    };
    const CellAddr cursor = m_view->cursorCell();
    if (!filled(cursor.row, cursor.col)) {
        return selection;
    }
    CellRange block{cursor, cursor};
    while (block.topLeft.row > 0 && filled(block.topLeft.row - 1, cursor.col)) {
        --block.topLeft.row;
    }
    while (filled(block.bottomRight.row + 1, cursor.col)) {
        ++block.bottomRight.row;
    }
    while (block.topLeft.col > 0 && filled(cursor.row, block.topLeft.col - 1)) {
        --block.topLeft.col;
    }
    while (filled(cursor.row, block.bottomRight.col + 1)) {
        ++block.bottomRight.col;
    }
    return block;
}

void CalcStructureActions::sortSelection(bool ascending)
{
    const CellRange block = blockAroundSelection();
    if (block.cellCount() < 2) {
        return;
    }
    const CalcEng::Sheet &sheet =
        m_document->workbook()->sheetAt(m_document->activeSheet());
    // Header heuristic: a text cell on top of a numeric column is a label,
    // not data. Getting this wrong buries the header in the middle of the
    // sorted block, which is the one outcome nobody wants.
    const qint32 key = m_view->cursorCell().col;
    const CalcEng::Cell *first = sheet.cellAt({block.topLeft.row, key});
    const CalcEng::Cell *second = sheet.cellAt({block.topLeft.row + 1, key});
    const bool headerRow = first != nullptr && isText(first->value) && second != nullptr
        && !isText(second->value);
    m_document->sortRange(block, key, ascending, headerRow);
    m_view->refreshAll();
}

void CalcStructureActions::autoSum()
{
    const CellRange selection = m_view->selectionRange();
    const CalcEng::Sheet &sheet =
        m_document->workbook()->sheetAt(m_document->activeSheet());
    const auto numeric = [&sheet](qint32 row, qint32 col) {
        const CalcEng::Cell *cell = sheet.cellAt({row, col});
        return cell != nullptr && isNumber(cell->value);
    };
    QList<QPair<CellAddr, QString>> edits;
    if (selection.cellCount() > 1) {
        // One total per selected column, written under the selection.
        for (qint32 col = selection.topLeft.col; col <= selection.bottomRight.col; ++col) {
            const QString range = QStringLiteral("%1:%2")
                                      .arg(CalcEng::addrText({selection.topLeft.row, col}),
                                           CalcEng::addrText({selection.bottomRight.row, col}));
            edits.append({CellAddr{selection.bottomRight.row + 1, col},
                          QStringLiteral("=SUM(%1)").arg(range)});
        }
    } else {
        const CellAddr cursor = m_view->cursorCell();
        qint32 top = cursor.row;
        while (top > 0 && numeric(top - 1, cursor.col)) {
            --top;
        }
        if (top < cursor.row) {
            const QString range =
                QStringLiteral("%1:%2").arg(CalcEng::addrText({top, cursor.col}),
                                            CalcEng::addrText({cursor.row - 1, cursor.col}));
            edits.append({cursor, QStringLiteral("=SUM(%1)").arg(range)});
        } else {
            qint32 left = cursor.col;
            while (left > 0 && numeric(cursor.row, left - 1)) {
                --left;
            }
            const QString range =
                left < cursor.col
                ? QStringLiteral("%1:%2").arg(CalcEng::addrText({cursor.row, left}),
                                              CalcEng::addrText({cursor.row, cursor.col - 1}))
                : QString();
            // With nothing to add up, leave the user an empty SUM to fill in
            // rather than silently doing nothing.
            edits.append({cursor, QStringLiteral("=SUM(%1)").arg(range)});
        }
    }
    m_document->applyInputs(edits, tr("AutoSum"));
    m_view->refreshAll();
}

void CalcStructureActions::toggleFreeze(bool on)
{
    if (!on) {
        m_view->setFrozen(0, 0);
        return;
    }
    // Freeze above and left of the cursor, the convention every spreadsheet
    // shares: put the cursor in B2 and the first row and column stay put.
    const CellAddr cursor = m_view->cursorCell();
    m_view->setFrozen(cursor.row, cursor.col);
}

void CalcStructureActions::syncFromCursor()
{
    m_syncing = true;
    const CalcEng::Workbook *wb = m_document->workbook();
    const qint32 active = m_document->activeSheet();
    const CalcEng::Sheet &sheet = wb->sheetAt(active);
    const CellAddr cursor = m_view->cursorCell();
    const CalcEng::CellStyle *style = wb->style(sheet.styleIdAt(cursor));
    m_wrapText->setChecked(style != nullptr && style->wrap);
    m_unmergeCells->setEnabled(sheet.mergeCovering(cursor) != nullptr);
    m_mergeCells->setEnabled(m_view->selectionRange().cellCount() > 1);
    const CellAddr frozen = m_view->frozen();
    m_freezePanes->setChecked(frozen.row > 0 || frozen.col > 0);
    m_sheetDelete->setEnabled(wb->sheetCount() > 1);
    m_sheetMoveLeft->setEnabled(active > 0);
    m_sheetMoveRight->setEnabled(active + 1 < wb->sheetCount());
    m_syncing = false;
}

} // namespace QindaOffice::Apps::QindaCalc
