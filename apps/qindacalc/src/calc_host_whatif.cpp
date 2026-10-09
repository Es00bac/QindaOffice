// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's Data ▸ What-If Analysis (M78, D-087): Goal Seek with its
// status dialog (OK keeps the value as one undo step, Cancel puts the
// workbook back), the Scenario Manager (add, edit, delete, show, summary)
// and Data Table. The engine does the work (goal_seek.h, scenario.h,
// data_table.h); the dialogs (GoalSeekDialog.qml, ScenarioManagerDialog.qml,
// DataTableDialog.qml) read and answer through the invokables here.

#include "calc_host.h"

#include "calc_undo.h"
#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "goal_seek.h"
#include "parser.h"
#include "sheet.h"

#include <QDate>
#include <QLocale>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {

QString cellsText(const QList<CellAddr> &cells)
{
    QStringList parts;
    for (const CellAddr &cell : cells) {
        parts.append(addrText(cell, true, true));
    }
    return parts.join(QLatin1Char(','));
}

QString userName()
{
    const QString name = qEnvironmentVariable("USER");
    return name.isEmpty() ? QStringLiteral("QindaCalc") : name;
}

} // namespace

void CalcHost::createWhatIfActions()
{
    Quick::ActionModel *model = actions();
    const auto make = [this, model](const char *id, const QString &text, const char *icon, const std::function<void()> &run) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        connect(entry, &ActionEntry::triggered, this, [this, run] {
            if (!m_syncing) {
                run();
            }
        });
    };
    make(ActionId::kCalcScenarioManager, tr("&Scenario Manager…"), "layers", [this] { emit scenarioManagerRequested(); });
    make(ActionId::kCalcGoalSeek, tr("&Goal Seek…"), "target", [this] { emit goalSeekRequested(); });
    make(ActionId::kCalcDataTable, tr("Data &Table…"), "grid-3x3", [this] {
        const CellRange range = selection();
        emit dataTableRequested(addrText(range.topLeft, true, true) + QLatin1Char(':') + addrText(range.bottomRight, true, true));
    });
}

QVariantMap CalcHost::goalSeekState() const
{
    return {{QStringLiteral("setCell"), addrText(cursor(), true, true)}, {QStringLiteral("changingCell"), QString()}};
}

QVariantMap CalcHost::runGoalSeek(const QString &setCell, double target, const QString &changingCell)
{
    const CellAddr set = cellFromText(setCell);
    const CellAddr changing = cellFromText(changingCell);
    if (!set.isValid() || !changing.isValid()) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("Reference isn't valid.")}};
    }
    if (m_document->refuseCellEdit({changing, changing})) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("The changing cell cannot be changed.")}};
    }
    m_goalSeekBefore = m_document->snapshotNow();
    const GoalSeekResult result = goalSeek(*m_document->workbook(), m_document->activeSheet(), set, target, changing);
    if (!result.started) {
        m_goalSeekBefore.reset();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), result.error}};
    }
    refreshGrid();
    emit cursorChanged();
    const QString cell = addrText(set, true, true);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("found"), result.found},
            {QStringLiteral("message"), result.found ? tr("Goal Seeking with Cell %1 found a solution.").arg(cell)
                                                     : tr("Goal Seeking with Cell %1 may not have found a solution.").arg(cell)},
            {QStringLiteral("target"), QLocale::c().toString(target, 'g', 12)},
            {QStringLiteral("current"), m_document->displayTextAt(set)},
            {QStringLiteral("iterations"), result.iterations}};
}

void CalcHost::finishGoalSeek(bool keep)
{
    if (!m_goalSeekBefore) {
        return;
    }
    const std::shared_ptr<WorkbookSnapshot> before = std::move(m_goalSeekBefore);
    m_goalSeekBefore.reset();
    if (keep) {
        m_document->commitSnapshotStep(*before, tr("Goal Seek"));
    } else {
        m_document->restoreWithoutUndo(*before);
    }
    refreshGrid();
    syncFromCursor();
    emit cursorChanged();
}

QVariantList CalcHost::scenarioList() const
{
    QVariantList list;
    const ScenarioSet &set = sheet().scenarios();
    const bool protectedSheet = sheet().protection().enabled;
    for (qsizetype i = 0; i < set.scenarios.size(); ++i) {
        const Scenario &scenario = set.scenarios.at(i);
        if (protectedSheet && scenario.hidden) {
            continue; // Hide is in force while the sheet is protected
        }
        list.append(QVariantMap{{QStringLiteral("index"), int(i)},
                                {QStringLiteral("name"), scenario.name},
                                {QStringLiteral("cells"), cellsText(scenario.cells)},
                                {QStringLiteral("comment"), scenario.comment},
                                {QStringLiteral("locked"), scenario.locked},
                                {QStringLiteral("hidden"), scenario.hidden}});
    }
    return list;
}

QVariantMap CalcHost::scenarioState(int index) const
{
    const ScenarioSet &set = sheet().scenarios();
    if (index >= 0 && index < set.scenarios.size()) {
        const Scenario &scenario = set.scenarios.at(index);
        return {{QStringLiteral("name"), scenario.name},
                {QStringLiteral("cells"), cellsText(scenario.cells)},
                {QStringLiteral("values"), scenario.values},
                {QStringLiteral("comment"), scenario.comment},
                {QStringLiteral("locked"), scenario.locked},
                {QStringLiteral("hidden"), scenario.hidden}};
    }
    // Add Scenario starts from the selection and its values, with Excel's
    // "Created by … on …" comment.
    const CellRange range = selection();
    QList<CellAddr> cells;
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row && cells.size() < kMaxScenarioCells; ++r) {
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col && cells.size() < kMaxScenarioCells; ++c) {
            cells.append({r, c});
        }
    }
    return {{QStringLiteral("name"), QString()},
            {QStringLiteral("cells"), cellsText(cells)},
            {QStringLiteral("values"), scenarioInputs(*m_document->workbook(), m_document->activeSheet(), cells)},
            {QStringLiteral("comment"), tr("Created by %1 on %2").arg(userName(), QLocale().toString(QDate::currentDate(), QLocale::ShortFormat))},
            {QStringLiteral("locked"), true},
            {QStringLiteral("hidden"), false}};
}

QStringList CalcHost::scenarioCellValues(const QString &cells) const
{
    return scenarioInputs(*m_document->workbook(), m_document->activeSheet(), cellsFromText(cells));
}

QString CalcHost::saveScenario(int index, const QVariantMap &map)
{
    Scenario scenario;
    scenario.name = map.value(QStringLiteral("name")).toString().trimmed();
    scenario.cells = cellsFromText(map.value(QStringLiteral("cells")).toString());
    if (scenario.cells.isEmpty()) {
        return tr("Reference isn't valid.");
    }
    scenario.values = map.value(QStringLiteral("values")).toStringList();
    if (scenario.values.size() != scenario.cells.size()) {
        scenario.values = scenarioInputs(*m_document->workbook(), m_document->activeSheet(), scenario.cells);
    }
    scenario.comment = map.value(QStringLiteral("comment")).toString();
    scenario.locked = map.value(QStringLiteral("locked"), true).toBool();
    scenario.hidden = map.value(QStringLiteral("hidden")).toBool();
    scenario.user = userName();
    ScenarioSet set = sheet().scenarios();
    const qint32 at = index >= 0 && index < set.scenarios.size() ? qint32(index) : -1;
    if (const QString problem = scenarioProblem(set, scenario, at); !problem.isEmpty()) {
        return problem;
    }
    if (at >= 0) {
        set.scenarios[at] = scenario;
        set.current = at;
    } else {
        set.scenarios.append(scenario);
        set.current = qint32(set.scenarios.size()) - 1;
    }
    m_document->setScenarios(set, at >= 0 ? tr("Edit Scenario") : tr("Add Scenario"));
    return {};
}

void CalcHost::deleteScenario(int index)
{
    ScenarioSet set = sheet().scenarios();
    if (index < 0 || index >= set.scenarios.size()) {
        return;
    }
    set.scenarios.removeAt(index);
    set.current = qBound(qint32(0), set.current >= index ? set.current - 1 : set.current, qMax(qint32(0), qint32(set.scenarios.size()) - 1));
    if (set.shown == index) {
        set.shown = -1;
    } else if (set.shown > index) {
        --set.shown;
    }
    m_document->setScenarios(set, tr("Delete Scenario"));
}

QString CalcHost::showScenarioAt(int index)
{
    if (!m_document->showScenario(qint32(index))) {
        return tr("The scenario could not be shown.");
    }
    refreshGrid();
    emit cursorChanged();
    return {};
}

QString CalcHost::scenarioSummaryFor(const QString &resultCells)
{
    QList<CellRange> results;
    for (const QString &part : resultCells.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const ParseResult parsed = parseFormula(part.trimmed());
        if (!parsed.error.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref || parsed.root->ref.wholeRows
            || parsed.root->ref.wholeCols) {
            return tr("Reference isn't valid.");
        }
        results.append(parsed.root->ref.range);
    }
    if (sheet().scenarios().isEmpty()) {
        return tr("There are no scenarios on this sheet.");
    }
    if (m_document->addScenarioSummary(results) < 0) {
        return tr("The summary could not be made.");
    }
    emit sheetsChanged();
    refreshGrid();
    return {};
}

QString CalcHost::createDataTable(const QString &rowInput, const QString &colInput)
{
    const CellAddr row = rowInput.trimmed().isEmpty() ? CellAddr{} : cellFromText(rowInput);
    const CellAddr col = colInput.trimmed().isEmpty() ? CellAddr{} : cellFromText(colInput);
    if ((!rowInput.trimmed().isEmpty() && !row.isValid()) || (!colInput.trimmed().isEmpty() && !col.isValid())) {
        return tr("Input cell reference is not valid.");
    }
    QString error;
    const std::optional<DataTableSpec> spec = makeDataTable(selection(), row, col, &error);
    if (!spec) {
        return error.isEmpty() ? tr("Input cell reference is not valid.") : error;
    }
    if (!m_document->addDataTable(*spec)) {
        return tr("The data table could not be made here.");
    }
    refreshGrid();
    emit cursorChanged();
    return {};
}

} // namespace QindaOffice::Apps::QindaCalc
