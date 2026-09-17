// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaCalc's structural and data commands: insert/delete rows and columns,
// row height and column width, merge, borders, wrap, sort, fill, AutoSum,
// freeze panes and the sheet-list operations — plus the grid context menu
// and the sheet-tab context menu they are reached through.
//
// Rationale (D-015). Before this pass the whole Insert menu was "Chart…",
// the whole Sheet menu was "Insert Sheet", there was no Data menu, and the
// grid had no context menu: a spreadsheet that cannot insert a row, sort a
// column, widen a column or delete a sheet is not one yet.
//
// Same action discipline as everything else in the suite: persistent
// window-scoped QActions with stable objectNames (AGENTS.md rule 5), shared
// by menu bar, toolbar and context menus so their state can never diverge.

#include "value.h"

#include <QAction>
#include <QObject>

class QMenu;
class QPoint;

namespace CalcEng = QindaOffice::Calc;

namespace QindaOffice::Shell {
class DocumentWindow;
}

namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;
class SpreadsheetView;

class CalcStructureActions final : public QObject {
    Q_OBJECT
public:
    CalcStructureActions(Shell::DocumentWindow *window, SpreadsheetDocument *document,
                         SpreadsheetView *view);

    // Menu builders. Each reuses the persistent actions, so the menu bar,
    // the toolbar and the context menu always agree.
    void populateInsertMenu(QMenu *menu);
    void populateFormatMenu(QMenu *menu);
    void populateDataMenu(QMenu *menu);
    void populateSheetMenu(QMenu *menu);
    void populateContextMenu(QMenu *menu);
    // The subset that earns a place on the formatting strip.
    [[nodiscard]] QList<QAction *> stripActions() const;
    // The sheet tab bar's own menu (rename/duplicate/delete/move).
    void showSheetTabMenu(int tabIndex, const QPoint &globalPos);

    // Commands the window drives directly (tab double-click, header drags).
    void renameSheet(int index);
    void applyColumnWidth(qint32 col, qreal px);
    void applyRowHeight(qint32 row, qreal px);
    void clearSelection();

private:
    void createActions();
    void applyBorders(int bits, bool outlineOnly);
    void sortSelection(bool ascending);
    void autoSum();
    void toggleFreeze(bool on);
    // A single-cell selection grows to the contiguous block around it, the
    // way Sort and AutoSum are expected to behave.
    [[nodiscard]] CalcEng::CellRange blockAroundSelection() const;
    void syncFromCursor();

    Shell::DocumentWindow *m_window;
    SpreadsheetDocument *m_document;
    SpreadsheetView *m_view;
    QAction *m_insertRows = nullptr;
    QAction *m_insertColumns = nullptr;
    QAction *m_deleteRows = nullptr;
    QAction *m_deleteColumns = nullptr;
    QAction *m_clearContents = nullptr;
    QAction *m_rowHeight = nullptr;
    QAction *m_columnWidth = nullptr;
    QAction *m_optimalWidth = nullptr;
    QAction *m_mergeCells = nullptr;
    QAction *m_unmergeCells = nullptr;
    QAction *m_wrapText = nullptr;
    QAction *m_bordersAll = nullptr;
    QAction *m_bordersOutline = nullptr;
    QAction *m_bordersNone = nullptr;
    QAction *m_sortAscending = nullptr;
    QAction *m_sortDescending = nullptr;
    QAction *m_fillDown = nullptr;
    QAction *m_fillRight = nullptr;
    QAction *m_autoSum = nullptr;
    QAction *m_freezePanes = nullptr;
    QAction *m_sheetRename = nullptr;
    QAction *m_sheetDelete = nullptr;
    QAction *m_sheetDuplicate = nullptr;
    QAction *m_sheetMoveLeft = nullptr;
    QAction *m_sheetMoveRight = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::QindaCalc
