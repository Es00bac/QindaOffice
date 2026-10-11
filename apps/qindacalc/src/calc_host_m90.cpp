// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's M90 commands (D-098): Data ▸ Analyze ▸ Solver (calc_solver.h)
// and the sparklines (calc_sparklines.h) — their actions, menus, enabled
// state and the contextual Sparkline tab — and Table Design's Table Name,
// Rename Table and Resize Table with the contextual Table Design tab.

#include "calc_host.h"

#include "standard_actions.h"

#include "parser.h"
#include "sheet.h"
#include "table_design.h"

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

void CalcHost::createM90Actions()
{
    m_solver = new CalcSolver(this);
    m_solver->createActions(actions());
    m_sparklines = new CalcSparklines(this);
    m_sparklines->createActions(actions());
    Quick::ActionModel *model = actions();
    auto *calculated = model->add(QString::fromLatin1(ActionId::kCalcPivotCalculatedField), tr("Calculated &Field…"), {}, QStringLiteral("sigma"));
    connect(calculated, &ActionEntry::triggered, this, [this] { if (!m_syncing) emit pivotCalculatedRequested(); });
    ActionEntry *rename = model->add(QString::fromLatin1(ActionId::kCalcTableRename), tr("Rena&me Table…"), {},
                                     QStringLiteral("text-cursor-input"));
    connect(rename, &ActionEntry::triggered, this, [this] {
        if (!m_syncing) {
            emit tableRenameRequested();
        }
    });
    ActionEntry *resize = model->add(QString::fromLatin1(ActionId::kCalcTableResize), tr("Resi&ze Table…"), {},
                                     QStringLiteral("expand"));
    connect(resize, &ActionEntry::triggered, this, [this] {
        if (!m_syncing) {
            emit tableResizeRequested();
        }
    });
}

void CalcHost::insertM90Menus()
{
    menuTree()->appendTo(QStringLiteral("calcPivotMenu"), MenuNode::action(QString::fromLatin1(ActionId::kCalcPivotCalculatedField)));
    m_solver->insertMenus(menuTree());
    m_sparklines->insertMenus(menuTree());
    menuTree()->appendTo(QStringLiteral("calcDataMenu"), MenuNode::action(QString::fromLatin1(ActionId::kCalcTableRename)));
    menuTree()->appendTo(QStringLiteral("calcDataMenu"), MenuNode::action(QString::fromLatin1(ActionId::kCalcTableResize)));
}

void CalcHost::syncM90FromCursor()
{
    m_syncing = true;
    m_solver->sync(actions(), !sheet().isChartSheet());
    const bool sparkline = m_sparklines->sync(actions());
    const bool table = sheet().tableCovering(cursor()) != nullptr;
    const auto *pivot = CalcEng::pivotOf(*m_document->workbook(), currentPivot());
    actions()->setEnabled(QString::fromLatin1(ActionId::kCalcPivotCalculatedField), pivot != nullptr && pivot->modelled);
    actions()->setEnabled(QString::fromLatin1(ActionId::kCalcTableRename), table);
    actions()->setEnabled(QString::fromLatin1(ActionId::kCalcTableResize), table);
    m_syncing = false;
    // The contextual tabs (the pivot's are calc_host_pivot.cpp's).
    QStringList contexts = ribbonContexts();
    contexts.removeAll(QStringLiteral("sparkline"));
    contexts.removeAll(QStringLiteral("table"));
    if (table) {
        contexts.append(QStringLiteral("table"));
    }
    if (sparkline) {
        contexts.append(QStringLiteral("sparkline"));
    }
    setRibbonContexts(contexts);
}

QString CalcHost::tableName() const
{
    const TableSpec *table = sheet().tableCovering(cursor());
    return table != nullptr ? table->name : QString();
}

QString CalcHost::tableRangeText() const
{
    const TableSpec *table = sheet().tableCovering(cursor());
    if (table == nullptr) {
        return {};
    }
    return addrText(table->range.topLeft, true, true) + QLatin1Char(':') + addrText(table->range.bottomRight, true, true);
}

QString CalcHost::renameTableTo(const QString &name)
{
    const TableSpec *table = sheet().tableCovering(cursor());
    if (table == nullptr) {
        return tr("Select a cell in a table first.");
    }
    const QString trimmed = name.trimmed();
    if (const QString problem = tableNameProblem(*m_document->workbook(), trimmed, table->name); !problem.isEmpty()) {
        showStatusMessage(problem, 8000);
        return problem;
    }
    QString refused;
    const auto connection = connect(m_document, &SpreadsheetDocument::editRefused, this, [&refused](const QString &why) { refused = why; });
    const bool done = m_document->renameTable(cursor(), trimmed);
    disconnect(connection);
    refreshGrid();
    emit cursorChanged();
    return done ? QString() : refused;
}

QString CalcHost::resizeTableTo(const QString &range)
{
    if (sheet().tableCovering(cursor()) == nullptr) {
        return tr("Select a cell in a table first.");
    }
    QString text = range.trimmed();
    if (text.startsWith(QLatin1Char('='))) {
        text = text.mid(1);
    }
    const ParseResult parsed = parseFormula(text);
    if (!parsed.error.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref
        || (!parsed.root->ref.sheet.isEmpty()
            && unquotedSheetName(parsed.root->ref.sheet).compare(sheet().name(), Qt::CaseInsensitive) != 0)) {
        return tr("The reference isn't valid.");
    }
    QString refused;
    const auto connection = connect(m_document, &SpreadsheetDocument::editRefused, this, [&refused](const QString &why) { refused = why; });
    const bool done = m_document->resizeTable(cursor(), parsed.root->ref.range);
    disconnect(connection);
    refreshGrid();
    syncFromCursor();
    emit cursorChanged();
    return done ? QString() : refused;
}

} // namespace QindaOffice::Apps::QindaCalc
