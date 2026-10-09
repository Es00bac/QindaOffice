// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's structural and data commands: insert/delete rows and columns,
// row height and column width, merge, borders, wrap, sort, fill, AutoSum,
// freeze panes, the sheet-list operations — and the Insert / Format / Data /
// Sheet menus, the grid context menu and the sheet-tab menu they are
// reached through. One ActionEntry per command, shared by every route.

#include "calc_host.h"

#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "sheet.h"

#include <QKeySequence>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {
enum BorderBit { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8, kAll = 15 };
}

void CalcHost::createStructureActions()
{
    Quick::ActionModel *model = actions();
    const auto make = [model](const char *id, const QString &text, const QKeySequence &shortcut = {},
                              const char *icon = nullptr) {
        return model->add(QString::fromLatin1(id), text, shortcut,
                          icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    // AGENT-NOTE: the shortcuts are the ones every spreadsheet uses. Insert
    // is Ctrl+Shift+Plus and delete Ctrl+Shift+Minus (LibreOffice's), so
    // View ▸ Zoom In (Ctrl+=) is beyond doubt untouched.
    connect(make(ActionId::kCalcInsertRows, tr("Insert &Rows Above"),
                 QKeySequence(QStringLiteral("Ctrl+Shift++")), "between-horizontal-start"),
            &ActionEntry::triggered, this, [this] {
                const CellRange range = selection();
                m_document->insertRows(range.topLeft.row, range.bottomRight.row - range.topLeft.row + 1);
                refreshGrid();
            });
    connect(make(ActionId::kCalcInsertColumns, tr("Insert &Columns Left"), {}, "between-vertical-start"),
            &ActionEntry::triggered, this, [this] {
                const CellRange range = selection();
                m_document->insertColumns(range.topLeft.col, range.bottomRight.col - range.topLeft.col + 1);
                refreshGrid();
            });
    connect(make(ActionId::kCalcDeleteRows, tr("&Delete Rows"),
                 QKeySequence(QStringLiteral("Ctrl+Shift+-")), "between-horizontal-end"),
            &ActionEntry::triggered, this, [this] {
                const CellRange range = selection();
                m_document->removeRows(range.topLeft.row, range.bottomRight.row - range.topLeft.row + 1);
                refreshGrid();
            });
    connect(make(ActionId::kCalcDeleteColumns, tr("Delete Colum&ns"), {}, "between-vertical-end"),
            &ActionEntry::triggered, this, [this] {
                const CellRange range = selection();
                m_document->removeColumns(range.topLeft.col, range.bottomRight.col - range.topLeft.col + 1);
                refreshGrid();
            });
    connect(make(ActionId::kCalcClearContents, tr("Clear &Contents"), QKeySequence(Qt::Key_Delete),
                 "eraser"),
            &ActionEntry::triggered, this, [this] {
                m_document->clearCells(selection());
                refreshGrid();
            });
    connect(make(ActionId::kCalcRowHeight, tr("Row &Height…")), &ActionEntry::triggered, this,
            [this] { emit rowHeightRequested(sheet().rowHeight(selection().topLeft.row)); });
    connect(make(ActionId::kCalcColumnWidth, tr("Column &Width…")), &ActionEntry::triggered, this,
            [this] { emit columnWidthRequested(sheet().columnWidth(selection().topLeft.col)); });
    connect(make(ActionId::kCalcOptimalWidth, tr("&Optimal Column Width")), &ActionEntry::triggered,
            this, [this] {
                if (m_grid == nullptr) {
                    return;
                }
                const CellRange range = selection();
                for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
                    m_document->setColumnWidths({c}, m_grid->autofitColumnWidth(int(c)));
                }
                refreshGrid();
            });
    connect(make(ActionId::kCalcMergeCells, tr("&Merge Cells"), {}, "combine"), &ActionEntry::triggered,
            this, [this] {
                m_document->mergeCells(selection());
                refreshGrid();
            });
    connect(make(ActionId::kCalcUnmergeCells, tr("&Unmerge Cells"), {}, "split"), &ActionEntry::triggered,
            this, [this] {
                m_document->unmergeCells(cursor());
                refreshGrid();
            });
    ActionEntry *wrap = make(ActionId::kCalcWrapText, tr("Wrap &Text"), {}, "wrap-text");
    wrap->setCheckable(true);
    connect(wrap, &ActionEntry::toggled, this, [this](bool on) {
        if (m_syncing) {
            return;
        }
        StylePatch patch;
        patch.wrap = on;
        m_document->restyleRange(selection(), patch, tr("Wrap text"));
        refreshGrid();
    });
    connect(make(ActionId::kCalcBordersAll, tr("&All Borders"), {}, "grid-2x2"), &ActionEntry::triggered,
            this, [this] { applyBorders(kAll, false); });
    connect(make(ActionId::kCalcBordersOutline, tr("&Outline")), &ActionEntry::triggered, this,
            [this] { applyBorders(kAll, true); });
    connect(make(ActionId::kCalcBordersNone, tr("&No Borders")), &ActionEntry::triggered, this,
            [this] { applyBorders(0, false); });
    connect(make(ActionId::kCalcSortAscending, tr("Sort &Ascending"), {}, "arrow-down-a-z"),
            &ActionEntry::triggered, this, [this] { sortSelection(true); });
    connect(make(ActionId::kCalcSortDescending, tr("Sort &Descending"), {}, "arrow-up-z-a"),
            &ActionEntry::triggered, this, [this] { sortSelection(false); });
    connect(make(ActionId::kCalcFillDown, tr("Fill &Down"), QKeySequence(QStringLiteral("Ctrl+D"))),
            &ActionEntry::triggered, this, [this] {
                m_document->fillRange(selection(), true);
                refreshGrid();
            });
    connect(make(ActionId::kCalcFillRight, tr("Fill &Right"), QKeySequence(QStringLiteral("Ctrl+R"))),
            &ActionEntry::triggered, this, [this] {
                m_document->fillRange(selection(), false);
                refreshGrid();
            });
    connect(make(ActionId::kCalcAutoSum, tr("&Sum"), QKeySequence(QStringLiteral("Alt+=")), "sigma"),
            &ActionEntry::triggered, this, [this] { autoSum(); });
    ActionEntry *freeze = make(ActionId::kCalcFreezePanes, tr("&Freeze Rows and Columns"), {},
                               "panel-top");
    freeze->setCheckable(true);
    connect(freeze, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            toggleFreeze(on);
        }
    });
    connect(make(ActionId::kInsertSheet, tr("&Insert Sheet"), {}, "plus"), &ActionEntry::triggered,
            this, [this] {
                m_document->insertSheet(tr("Sheet%1").arg(m_document->workbook()->sheetCount() + 1));
            });
    connect(make(ActionId::kCalcSheetRename, tr("&Rename Sheet…")), &ActionEntry::triggered, this,
            [this] {
                const int index = activeSheet();
                emit renameSheetRequested(index, m_document->workbook()->sheetName(qint32(index)));
            });
    connect(make(ActionId::kCalcSheetDelete, tr("&Delete Sheet…")), &ActionEntry::triggered, this,
            [this] {
                // Deleting a sheet destroys data and breaks every formula
                // that named it, so it asks first.
                const int index = activeSheet();
                emit deleteSheetRequested(index, m_document->workbook()->sheetName(qint32(index)));
            });
    connect(make(ActionId::kCalcSheetHide, tr("&Hide Sheet")), &ActionEntry::triggered, this,
            [this] { setSheetHidden(true); });
    connect(make(ActionId::kCalcSheetUnhide, tr("&Unhide Sheet…")), &ActionEntry::triggered, this,
            [this] {
                QStringList hidden;
                const Workbook *wb = m_document->workbook();
                for (qint32 i = 0; i < wb->sheetCount(); ++i) {
                    if (wb->sheetAt(i).isHidden()) {
                        hidden.append(wb->sheetName(i));
                    }
                }
                if (!hidden.isEmpty()) {
                    emit unhideSheetRequested(hidden);
                }
            });
    connect(make(ActionId::kCalcSheetDuplicate, tr("D&uplicate Sheet")), &ActionEntry::triggered, this,
            [this] {
                const qint32 index = m_document->activeSheet();
                const Workbook *wb = m_document->workbook();
                QString name = tr("%1 (copy)").arg(wb->sheetName(index));
                int suffix = 2;
                while (wb->sheetIndex(name) >= 0) {
                    name = tr("%1 (copy %2)").arg(wb->sheetName(index)).arg(suffix++);
                }
                m_document->duplicateSheet(index, name);
            });
    connect(make(ActionId::kCalcSheetMoveLeft, tr("Move Sheet &Left")), &ActionEntry::triggered, this,
            [this] {
                const qint32 index = m_document->activeSheet();
                m_document->moveSheet(index, index - 1);
            });
    connect(make(ActionId::kCalcSheetMoveRight, tr("Move Sheet R&ight")), &ActionEntry::triggered, this,
            [this] {
                const qint32 index = m_document->activeSheet();
                m_document->moveSheet(index, index + 1);
            });
}

void CalcHost::insertMenus()
{
    const auto a = [](const char *id) { return MenuNode::action(QString::fromLatin1(id)); };
    QList<MenuNode> insertItems{a(ActionId::kCalcInsertRows), a(ActionId::kCalcInsertColumns),
                                MenuNode::separator(), a(ActionId::kCalcDeleteRows),
                                a(ActionId::kCalcDeleteColumns), MenuNode::separator(),
                                a(ActionId::kCalcComment), MenuNode::separator(),
                                a(ActionId::kInsertImage)};
    if (chartsAvailable()) {
        insertItems.append(a(ActionId::kInsertChart));
    }
    menuTree()->insertMenuBefore(QStringLiteral("aiMenu"),
                                 MenuNode::submenu(QStringLiteral("calcInsertMenu"), tr("&Insert"), insertItems));
    menuTree()->insertMenuBefore(
        QStringLiteral("aiMenu"),
        MenuNode::submenu(
            QStringLiteral("calcFormatMenu"), tr("F&ormat"),
            {a(ActionId::kFormatBold), a(ActionId::kFormatItalic), a(ActionId::kFormatUnderline),
             a(ActionId::kFormatStrikeOut),
             a(ActionId::kFormatTextColor), a(ActionId::kCalcFillColor), MenuNode::separator(),
             a(ActionId::kFormatAlignLeft), a(ActionId::kFormatAlignCenter),
             a(ActionId::kFormatAlignRight), MenuNode::separator(),
             a("calcThousandsFormatAction"), MenuNode::separator(), a(ActionId::kCalcWrapText),
             MenuNode::submenu(QStringLiteral("calcBordersMenu"), tr("&Borders"),
                               {a(ActionId::kCalcBordersAll), a(ActionId::kCalcBordersOutline),
                                a(ActionId::kCalcBordersNone)}),
             MenuNode::separator(), a(ActionId::kCalcMergeCells), a(ActionId::kCalcUnmergeCells),
             MenuNode::separator(), a(ActionId::kCalcRowHeight), a(ActionId::kCalcColumnWidth),
             a(ActionId::kCalcOptimalWidth), MenuNode::separator(),
             // M78 (D-087): Excel's Conditional Formatting menu.
             MenuNode::submenu(QStringLiteral("calcCondFormatMenu"), tr("Conditional &Formatting"),
                               {a(ActionId::kCalcCondFormatTop10), a(ActionId::kCalcCondFormatAboveAverage),
                                a(ActionId::kCalcCondFormatDuplicates), MenuNode::separator(),
                                a(ActionId::kCalcCondFormatDataBars), a(ActionId::kCalcCondFormatColorScale),
                                a(ActionId::kCalcCondFormatIconSet), MenuNode::separator(),
                                a(ActionId::kCalcCondFormatNewRule), a(ActionId::kCalcCondFormatClearSelection),
                                a(ActionId::kCalcCondFormatClearSheet), a(ActionId::kCalcCondFormat)})}));
    menuTree()->insertMenuBefore(
        QStringLiteral("aiMenu"),
        MenuNode::submenu(QStringLiteral("calcDataMenu"), tr("&Data"),
                          {a(ActionId::kCalcSortAscending), a(ActionId::kCalcSortDescending),
                           MenuNode::separator(), a(ActionId::kCalcAutoSum), a(ActionId::kCalcFillDown),
                           a(ActionId::kCalcFillRight), MenuNode::separator(),
                           a(ActionId::kCalcFreezePanes), MenuNode::separator(),
                           a(ActionId::kCalcAutoFilter), a(ActionId::kCalcValidation)}));
    menuTree()->insertMenuBefore(
        QStringLiteral("aiMenu"),
        MenuNode::submenu(QStringLiteral("calcSheetMenu"), tr("&Sheet"),
                          {a(ActionId::kInsertSheet), a(ActionId::kCalcSheetRename),
                           a(ActionId::kCalcSheetDuplicate), a(ActionId::kCalcSheetDelete),
                           MenuNode::separator(), a(ActionId::kCalcSheetMoveLeft),
                           a(ActionId::kCalcSheetMoveRight), MenuNode::separator(),
                           a(ActionId::kCalcSheetHide), a(ActionId::kCalcSheetUnhide)}));
    // The grid's context menu and the sheet-tab menu are submenus the
    // window pops up (MenuFromNode over the same entries); they are not
    // top-level menus, so they are kept off the tree and built by id.
    menuTree()->insertInto(QStringLiteral("calcSheetMenu"), QString(), MenuNode::separator());
}

void CalcHost::syncStructureFromCursor()
{
    m_syncing = true;
    const Workbook *wb = m_document->workbook();
    const qint32 active = m_document->activeSheet();
    const Sheet &current = sheet();
    const CellAddr at = cursor();
    const CellStyle *style = wb->style(current.styleIdAt(at));
    Quick::ActionModel *model = actions();
    model->setChecked(QString::fromLatin1(ActionId::kCalcWrapText), style != nullptr && style->wrap);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcUnmergeCells), current.mergeCovering(at) != nullptr);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcMergeCells), selection().cellCount() > 1);
    const CellAddr frozen = m_grid != nullptr ? m_grid->frozen() : CellAddr{0, 0};
    model->setChecked(QString::fromLatin1(ActionId::kCalcFreezePanes), frozen.row > 0 || frozen.col > 0);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcSheetDelete), wb->sheetCount() > 1);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcSheetMoveLeft), active > 0);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcSheetMoveRight), active + 1 < wb->sheetCount());
    m_syncing = false;
}

void CalcHost::applyRowHeight(qreal px)
{
    const CellRange range = selection();
    QList<qint32> rows;
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
        rows.append(r);
    }
    m_document->setRowHeights(rows, qBound<qreal>(8, px, 2000));
    refreshGrid();
}

void CalcHost::applyColumnWidth(qreal px)
{
    const CellRange range = selection();
    QList<qint32> cols;
    for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
        cols.append(c);
    }
    m_document->setColumnWidths(cols, qBound<qreal>(8, px, 2000));
    refreshGrid();
}

void CalcHost::applyBorders(int bits, bool outlineOnly)
{
    const CellRange range = selection();
    if (!outlineOnly) {
        StylePatch patch;
        patch.borders = bits;
        m_document->restyleRange(range, patch, bits == 0 ? tr("Clear borders") : tr("Borders"));
        refreshGrid();
        return;
    }
    // Outline: each edge cell gets only the edges that face outwards.
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
    refreshGrid();
}

CellRange CalcHost::blockAroundSelection() const
{
    const CellRange current = selection();
    if (current.cellCount() > 1) {
        return current;
    }
    const Sheet &s = sheet();
    const auto filled = [&s](qint32 row, qint32 col) {
        const Cell *cell = s.cellAt({row, col});
        return cell != nullptr && !isBlank(cell->value);
    };
    const CellAddr at = cursor();
    if (!filled(at.row, at.col)) {
        return current;
    }
    CellRange block{at, at};
    while (block.topLeft.row > 0 && filled(block.topLeft.row - 1, at.col)) {
        --block.topLeft.row;
    }
    while (filled(block.bottomRight.row + 1, at.col)) {
        ++block.bottomRight.row;
    }
    while (block.topLeft.col > 0 && filled(at.row, block.topLeft.col - 1)) {
        --block.topLeft.col;
    }
    while (filled(at.row, block.bottomRight.col + 1)) {
        ++block.bottomRight.col;
    }
    return block;
}

void CalcHost::sortSelection(bool ascending)
{
    if (currentPivot().isValid()) {
        sortPivot(ascending); // inside a PivotTable, Excel sorts its field (M74)
        return;
    }
    const CellRange block = blockAroundSelection();
    if (block.cellCount() < 2) {
        return;
    }
    const Sheet &s = sheet();
    // Header heuristic: a text cell on top of a numeric column is a label.
    const qint32 key = cursor().col;
    const Cell *first = s.cellAt({block.topLeft.row, key});
    const Cell *second = s.cellAt({block.topLeft.row + 1, key});
    const bool headerRow = first != nullptr && isText(first->value) && second != nullptr
        && !isText(second->value);
    m_document->sortRange(block, key, ascending, headerRow);
    refreshGrid();
}

void CalcHost::autoSum()
{
    const CellRange current = selection();
    const Sheet &s = sheet();
    const auto numeric = [&s](qint32 row, qint32 col) {
        const Cell *cell = s.cellAt({row, col});
        return cell != nullptr && isNumber(cell->value);
    };
    QList<QPair<CellAddr, QString>> edits;
    if (current.cellCount() > 1) {
        // One total per selected column, written under the selection.
        for (qint32 col = current.topLeft.col; col <= current.bottomRight.col; ++col) {
            const QString range = QStringLiteral("%1:%2").arg(addrText({current.topLeft.row, col}),
                                                             addrText({current.bottomRight.row, col}));
            edits.append({CellAddr{current.bottomRight.row + 1, col}, QStringLiteral("=SUM(%1)").arg(range)});
        }
    } else {
        const CellAddr at = cursor();
        qint32 top = at.row;
        while (top > 0 && numeric(top - 1, at.col)) {
            --top;
        }
        if (top < at.row) {
            const QString range = QStringLiteral("%1:%2").arg(addrText({top, at.col}), addrText({at.row - 1, at.col}));
            edits.append({at, QStringLiteral("=SUM(%1)").arg(range)});
        } else {
            qint32 left = at.col;
            while (left > 0 && numeric(at.row, left - 1)) {
                --left;
            }
            const QString range = left < at.col
                ? QStringLiteral("%1:%2").arg(addrText({at.row, left}), addrText({at.row, at.col - 1}))
                : QString();
            // With nothing to add up, leave an empty SUM to fill in rather
            // than silently doing nothing.
            edits.append({at, QStringLiteral("=SUM(%1)").arg(range)});
        }
    }
    m_document->applyInputs(edits, tr("AutoSum"));
    refreshGrid();
}

void CalcHost::toggleFreeze(bool on)
{
    if (!on) {
        m_document->setFreeze(0, 0);
        refreshGrid();
        return;
    }
    // Freeze Panes takes the place of a split (M82, as in Excel).
    if (m_grid != nullptr) {
        m_grid->clearSplit();
    }
    // Freeze above and left of the cursor: put the cursor in B2 and the
    // first row and column stay put. The state lives on the SHEET (it
    // round-trips to xlsx and rides undo/recovery).
    const CellAddr at = cursor();
    m_document->setFreeze(qint32(at.row), qint32(at.col));
    refreshGrid();
}

} // namespace QindaOffice::Apps::QindaCalc
