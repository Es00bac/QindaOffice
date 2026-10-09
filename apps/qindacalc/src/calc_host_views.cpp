// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's View tools (M82, D-091): View ▸ Split — the grid's four panes
// (spreadsheet_item_panes.cpp), the window's own view state, never an undo
// step; Split and Freeze Panes take each other's place, as in Excel —
// View ▸ New Window (a view window on the workbook, CalcViewWindows.qml)
// and View ▸ Custom Views (calc_engine's custom_view.h, CustomViewsDialog.qml):
// Add takes the workbook as it is and this window's zoom, panes and cells;
// Show puts both back; Add, Show and Delete are one undo step each (a view
// lives in the file).

#include "calc_host.h"

#include "spreadsheet_document.h"
#include "spreadsheet_item.h"

#include "custom_view.h"

#include "menu_tree.h"
#include "standard_actions.h"

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

// AGENT-NOTE: Excel keeps a split's bars in twips (1/20 pt) on the screen,
// measured from the cell area's edge as LibreOffice reads them; the grid's
// are px of the cell area at 96 dpi — 15 twips a px. A view's zoom is
// Excel's percent (10–400); the grid's zoom runs 50–300 %.
constexpr double kTwipsPerPx = 15.0;

SpreadsheetItem::SheetWindow windowOf(const Sheet::CustomView &part)
{
    SpreadsheetItem::SheetWindow window;
    window.zoom = part.scale / 100.0;
    window.splitX = part.splitX / kTwipsPerPx;
    window.splitY = part.splitY / kTwipsPerPx;
    window.topLeft = part.topLeft;
    window.farTopLeft = part.splitTopLeft;
    return window;
}

} // namespace

void CalcHost::createViewActions()
{
    ActionEntry *split = actions()->add(QString::fromLatin1(ActionId::kCalcViewSplit), tr("&Split"), {},
                                        QStringLiteral("split"));
    split->setCheckable(true);
    split->setEnabled(true);
    connect(split, &ActionEntry::toggled, this, [this](bool on) {
        if (m_syncing || m_grid == nullptr || on == m_grid->isSplit()) {
            return;
        }
        if (on && (sheet().freezeRows() > 0 || sheet().freezeCols() > 0)) {
            m_document->setFreeze(0, 0); // Split takes Freeze Panes' place
        }
        m_grid->toggleSplit();
        refreshGrid();
        syncFromCursor();
    });
    ActionEntry *newWindow = actions()->add(QString::fromLatin1(ActionId::kCalcViewNewWindow), tr("&New Window"), {},
                                            QStringLiteral("square-stack"));
    connect(newWindow, &ActionEntry::triggered, this, [this] { emit viewWindowRequested(); });
    ActionEntry *views = actions()->add(QString::fromLatin1(ActionId::kCalcCustomViews), tr("C&ustom Views…"), {},
                                        QStringLiteral("layout-template"));
    connect(views, &ActionEntry::triggered, this, [this] { emit customViewsRequested(); });
    // A table in the workbook turns Custom Views off, as in Excel.
    connect(m_document, &SpreadsheetDocument::contentChanged, this, [this] { syncViewsFromCursor(); });
}

void CalcHost::insertViewMenus()
{
    Quick::MenuTree *tree = menuTree();
    const QString zoomIn = QString::fromLatin1(ActionId::kViewZoomIn);
    tree->insertInto(QStringLiteral("viewMenu"), zoomIn, MenuNode::action(QString::fromLatin1(ActionId::kCalcCustomViews)));
    tree->insertInto(QStringLiteral("viewMenu"), zoomIn, MenuNode::action(QString::fromLatin1(ActionId::kCalcViewNewWindow)));
    tree->insertInto(QStringLiteral("viewMenu"), zoomIn, MenuNode::action(QString::fromLatin1(ActionId::kCalcViewSplit)));
    tree->insertInto(QStringLiteral("viewMenu"), zoomIn, MenuNode::separator());
}

void CalcHost::syncViewsFromCursor()
{
    if (m_grid == nullptr) {
        return;
    }
    const bool was = m_syncing;
    m_syncing = true;
    actions()->setChecked(QString::fromLatin1(ActionId::kCalcViewSplit), m_grid->isSplit());
    actions()->setEnabled(QString::fromLatin1(ActionId::kCalcCustomViews), customViewsAllowed(*m_document->workbook()));
    m_syncing = was;
}

void CalcHost::attachViewGrid(SpreadsheetItem *grid)
{
    if (grid == nullptr) {
        return;
    }
    grid->setReadOnly(true);
    grid->setDocument(m_document);
    if (m_grid != nullptr) {
        grid->setCellFont(m_grid->cellFont());
        grid->setZoom(m_grid->zoom());
    }
    // AGENT-GUARD: the view grid repaints on the document's own signals —
    // the host's refreshGrid() only knows the document window's grid.
    connect(m_document, &SpreadsheetDocument::contentChanged, grid, &SpreadsheetItem::refreshAll);
    connect(m_document, &SpreadsheetDocument::structureChanged, grid, &SpreadsheetItem::refreshAll);
}

QStringList CalcHost::viewableSheets() const
{
    QStringList out;
    const Workbook *wb = m_document->workbook();
    for (qint32 s = 0; s < wb->sheetCount(); ++s) {
        if (!wb->sheetAt(s).isHidden() && !wb->sheetAt(s).isChartSheet()) {
            out.append(wb->sheetName(s));
        }
    }
    return out;
}

QVariantList CalcHost::customViewList() const
{
    QVariantList out;
    for (const CustomViewSpec &view : m_document->workbook()->customViews) {
        out.append(QVariantMap{{QStringLiteral("name"), view.name}, {QStringLiteral("guid"), view.guid}});
    }
    return out;
}

QString CalcHost::addCustomView(const QString &rawName, bool printSettings, bool hiddenRowsCols)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty()) {
        return tr("Type a name for the view.");
    }
    Workbook *wb = m_document->workbook();
    if (!customViewsAllowed(*wb)) {
        return tr("Custom Views cannot be used in a workbook that contains a table.");
    }
    QHash<qint32, Sheet::CustomView> window;
    for (qint32 s = 0; s < wb->sheetCount(); ++s) {
        const SpreadsheetItem::SheetWindow w =
            m_grid != nullptr ? m_grid->sheetWindow(wb->sheetName(s)) : SpreadsheetItem::SheetWindow();
        Sheet::CustomView part;
        part.scale = qBound(10, qRound(w.zoom * 100), 400);
        part.splitX = w.splitX * kTwipsPerPx;
        part.splitY = w.splitY * kTwipsPerPx;
        part.topLeft = w.topLeft;
        part.splitTopLeft = w.farTopLeft;
        part.activeCell = s == m_document->activeSheet() ? cursor() : wb->sheetAt(s).cursor();
        window.insert(s, part);
    }
    const std::shared_ptr<WorkbookSnapshot> before = m_document->snapshotNow();
    CalcEng::addCustomView(*wb, name, printSettings, hiddenRowsCols, m_document->activeSheet(), window);
    m_document->commitSnapshotStep(*before, tr("Add Custom View"));
    return {};
}

QString CalcHost::showCustomView(const QString &guid)
{
    Workbook *wb = m_document->workbook();
    if (m_grid != nullptr) {
        m_grid->cancelEditor();
    }
    const std::shared_ptr<WorkbookSnapshot> before = m_document->snapshotNow();
    qint32 active = -1;
    if (!CalcEng::showCustomView(*wb, guid, &active)) {
        return tr("The view is no longer in the workbook.");
    }
    for (qint32 s = 0; s < wb->sheetCount(); ++s) {
        if (const Sheet::CustomView *part = customViewOf(wb->sheetAt(s), guid)) {
            wb->sheetAt(s).setCursor(part->activeCell);
        }
    }
    // The sheet the view shows stays visible (a view Excel wrote never
    // hides it; a damaged one must not leave the window without a sheet).
    if (active < 0 || active >= wb->sheetCount()) {
        active = m_document->activeSheet();
    }
    wb->sheetAt(active).setHidden(false);
    m_document->commitSnapshotStep(*before, tr("Show Custom View"));
    emit sheetsChanged();
    setActiveSheet(int(viewSheetIndex(active)));
    if (m_grid != nullptr) {
        // The window part: every other sheet's split for when it is shown,
        // then the shown sheet's zoom, panes and cell.
        for (qint32 s = 0; s < wb->sheetCount(); ++s) {
            const Sheet::CustomView *part = customViewOf(wb->sheetAt(s), guid);
            if (part != nullptr && s != active) {
                m_grid->setSheetWindow(wb->sheetName(s), windowOf(*part));
            }
        }
        refreshGrid();
        if (const Sheet::CustomView *part = customViewOf(wb->sheetAt(active), guid)) {
            m_grid->setCursorCell(part->activeCell);
            m_grid->setSheetWindow(wb->sheetName(active), windowOf(*part));
        }
    }
    syncFromCursor();
    updateSelectionStatus();
    emit cursorChanged();
    return {};
}

void CalcHost::deleteCustomView(const QString &guid)
{
    const std::shared_ptr<WorkbookSnapshot> before = m_document->snapshotNow();
    if (CalcEng::deleteCustomView(*m_document->workbook(), guid)) {
        m_document->commitSnapshotStep(*before, tr("Delete Custom View"));
    }
}

} // namespace QindaOffice::Apps::QindaCalc
