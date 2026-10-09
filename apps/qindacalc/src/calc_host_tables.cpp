// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's M69 commands (D-078): Insert ▸ Table (Ctrl+T) and Data ▸ Total
// Row — one ActionEntry each, shared by the menus and the ribbon's Tables
// group — array-formula entry ("{=…}", Ctrl+Shift+Enter), and the chart
// sheet view: while the active sheet is a chart sheet the window shows its
// chart where the grid was (CalcGrid.qml).

#include "calc_host.h"

#include "chart_series.h"
#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "sheet.h"

#include <QKeySequence>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

void CalcHost::createTableActions()
{
    Quick::ActionModel *model = actions();
    connect(model->add(QString::fromLatin1(ActionId::kCalcInsertTable), tr("&Table"),
                       QKeySequence(QStringLiteral("Ctrl+T")), QStringLiteral("table")),
            &ActionEntry::triggered, this, [this] {
                // One cell grows to the block of data around it (Excel's
                // current region); its first row becomes the header row.
                if (m_document->insertTable(blockAroundSelection())) {
                    const TableSpec *table = sheet().tableCovering(cursor());
                    showStatusMessage(tr("Made table “%1”").arg(table != nullptr ? table->name : QString()), 6000);
                }
                refreshGrid();
                syncFromCursor();
            });
    ActionEntry *totals = model->add(QString::fromLatin1(ActionId::kCalcTableTotalRow), tr("Total &Row"), {},
                                     QStringLiteral("sigma"));
    totals->setCheckable(true);
    connect(totals, &ActionEntry::toggled, this, [this](bool on) {
        if (m_syncing) {
            return;
        }
        m_document->setTotalsRow(cursor(), on);
        refreshGrid();
        syncFromCursor();
    });
    // The chart sheet view follows the sheet and the data.
    connect(this, &CalcHost::sheetsChanged, this, &CalcHost::chartSheetChanged);
    connect(this, &CalcHost::chartsChanged, this, &CalcHost::chartSheetChanged);
}

void CalcHost::insertTableMenus()
{
    Quick::MenuTree *tree = menuTree();
    tree->appendTo(QStringLiteral("calcInsertMenu"), MenuNode::separator());
    tree->appendTo(QStringLiteral("calcInsertMenu"), MenuNode::action(QString::fromLatin1(ActionId::kCalcInsertTable)));
    tree->appendTo(QStringLiteral("calcDataMenu"), MenuNode::separator());
    tree->appendTo(QStringLiteral("calcDataMenu"), MenuNode::action(QString::fromLatin1(ActionId::kCalcTableTotalRow)));
}

void CalcHost::syncTablesFromCursor()
{
    m_syncing = true;
    Quick::ActionModel *model = actions();
    const TableSpec *table = sheet().tableCovering(cursor());
    model->setEnabled(QString::fromLatin1(ActionId::kCalcInsertTable), table == nullptr && !sheet().isChartSheet());
    model->setEnabled(QString::fromLatin1(ActionId::kCalcTableTotalRow), table != nullptr);
    model->setChecked(QString::fromLatin1(ActionId::kCalcTableTotalRow), table != nullptr && table->totalsRow);
    m_syncing = false;
}

bool CalcHost::commitArrayInput(const CellAddr &addr, const QString &input)
{
    // "{=…}" — Ctrl+Shift+Enter, or the braces an array formula shows in —
    // enters a legacy array formula (D-078): over the array already there,
    // else the selection holding the cursor, else the one cell.
    const QString trimmed = input.trimmed();
    if (!trimmed.startsWith(QLatin1String("{=")) || !trimmed.endsWith(QLatin1Char('}'))) {
        return false;
    }
    CellRange block{addr, addr};
    const CellRange selected = selection();
    if (const auto existing = m_document->workbook()->arrayBlockAt(m_document->activeSheet(), addr)) {
        block = *existing;
    } else if (selected.cellCount() > 1 && selected.contains(addr)) {
        block = selected;
    }
    m_document->applyArrayFormula(block, trimmed);
    return true;
}

bool CalcHost::chartSheetActive() const
{
    return sheet().isChartSheet();
}

QVariantMap CalcHost::chartSheetData() const
{
    const Workbook *wb = m_document->workbook();
    if (!sheet().isChartSheet()) {
        return {};
    }
    for (const ChartSpec &chart : wb->charts) {
        if (chart.sheet == m_document->activeSheet()) {
            return chartSeriesData(chart, *wb);
        }
    }
    return {};
}

} // namespace QindaOffice::Apps::QindaCalc
