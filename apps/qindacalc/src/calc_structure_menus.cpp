// SPDX-License-Identifier: GPL-3.0-or-later
// The menus QindaCalc's structural commands are reached through: Insert,
// Format, Data, Sheet, the grid context menu and the sheet-tab menu. Split
// from calc_structure_actions.cpp to keep both inside the line budget.
//
// Every entry here reuses the persistent action built in
// CalcStructureActions::createActions(), so a command's enabled state and
// check mark are identical wherever the user meets it (AGENTS.md rule 5).

#include "calc_structure_actions.h"

#include "spreadsheet_document.h"
#include "spreadsheet_view.h"

#include "document_window.h"
#include "standard_actions.h"

#include <QMenu>

namespace QindaOffice::Apps::QindaCalc {

QList<QAction *> CalcStructureActions::stripActions() const
{
    return {m_autoSum,    m_sortAscending, m_sortDescending, m_mergeCells,
            m_bordersAll, m_wrapText,      m_insertRows,     m_deleteRows};
}

void CalcStructureActions::populateInsertMenu(QMenu *menu)
{
    menu->addAction(m_insertRows);
    menu->addAction(m_insertColumns);
    menu->addSeparator();
    menu->addAction(m_deleteRows);
    menu->addAction(m_deleteColumns);
}

void CalcStructureActions::populateFormatMenu(QMenu *menu)
{
    menu->addAction(m_wrapText);
    auto *borders = menu->addMenu(tr("&Borders"));
    borders->setObjectName(QStringLiteral("calcBordersMenu"));
    borders->addAction(m_bordersAll);
    borders->addAction(m_bordersOutline);
    borders->addAction(m_bordersNone);
    menu->addSeparator();
    menu->addAction(m_mergeCells);
    menu->addAction(m_unmergeCells);
    menu->addSeparator();
    menu->addAction(m_rowHeight);
    menu->addAction(m_columnWidth);
    menu->addAction(m_optimalWidth);
}

void CalcStructureActions::populateDataMenu(QMenu *menu)
{
    menu->addAction(m_sortAscending);
    menu->addAction(m_sortDescending);
    menu->addSeparator();
    menu->addAction(m_autoSum);
    menu->addAction(m_fillDown);
    menu->addAction(m_fillRight);
    menu->addSeparator();
    menu->addAction(m_freezePanes);
}

void CalcStructureActions::populateSheetMenu(QMenu *menu)
{
    menu->addAction(m_sheetRename);
    menu->addAction(m_sheetDuplicate);
    menu->addAction(m_sheetDelete);
    menu->addSeparator();
    menu->addAction(m_sheetMoveLeft);
    menu->addAction(m_sheetMoveRight);
}

void CalcStructureActions::populateContextMenu(QMenu *menu)
{
    using namespace Shell::ActionId;
    // The shell's own edit commands come first, exactly as they appear in
    // the Edit menu — the context menu is a shortcut to the same actions,
    // never a second set of them.
    for (const char *id : {kEditCut, kEditCopy, kEditPaste}) {
        if (QAction *action = m_window->action(QString::fromLatin1(id))) {
            menu->addAction(action);
        }
    }
    menu->addAction(m_clearContents);
    menu->addSeparator();
    menu->addAction(m_insertRows);
    menu->addAction(m_insertColumns);
    menu->addAction(m_deleteRows);
    menu->addAction(m_deleteColumns);
    menu->addSeparator();
    menu->addAction(m_mergeCells);
    menu->addAction(m_unmergeCells);
    menu->addSeparator();
    menu->addAction(m_sortAscending);
    menu->addAction(m_sortDescending);
    menu->addSeparator();
    menu->addAction(m_rowHeight);
    menu->addAction(m_columnWidth);
}

void CalcStructureActions::showSheetTabMenu(int tabIndex, const QPoint &globalPos)
{
    if (tabIndex < 0 || tabIndex >= m_document->workbook()->sheetCount()) {
        return;
    }
    // Act on the tab that was clicked, not on whichever sheet happened to be
    // active: right-clicking a tab and getting a menu for a different sheet
    // is how data gets deleted by accident.
    m_document->setActiveSheet(tabIndex);
    m_view->refreshAll();
    syncFromCursor();
    QMenu menu(m_window);
    menu.setObjectName(QStringLiteral("calcSheetTabMenu"));
    if (QAction *insert = m_window->action(QString::fromLatin1(Shell::ActionId::kInsertSheet))) {
        menu.addAction(insert);
    }
    populateSheetMenu(&menu);
    menu.exec(globalPos);
}

} // namespace QindaOffice::Apps::QindaCalc
