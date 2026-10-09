// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's PivotTable commands (M74, D-083): Insert ▸ PivotTable and the
// contextual PivotTable Analyze and Design tabs — one ActionEntry each,
// shared by the ribbon, the Data ▸ PivotTable menu, the grid's context menu
// and the panel export — and what follows the cursor: the "pivot" ribbon
// context, the checked layout, totals and style, and the note a read-only
// pivot shows. The field list and the dialogs are calc_host_pivot_pane.cpp.

#include "calc_host.h"

#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "pivot_cache.h"
#include "pivot_style.h"
#include "sheet.h"

#include <QKeySequence>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

QString styleId(const QString &name)
{
    return QStringLiteral("calcPivotStyle_") + (name.isEmpty() ? QStringLiteral("None") : name);
}

QStringList styleIds()
{
    QStringList ids;
    for (const QString &name : pivotStyleNames()) {
        ids.append(styleId(name));
    }
    ids.append(styleId(QString()));
    return ids;
}

// Every row and column field of a pivot (Σ Values left out).
QList<qint32> axisFields(const PivotTable &pivot)
{
    QList<qint32> fields;
    for (const qint32 f : pivot.rowFields + pivot.colFields) {
        if (f >= 0) {
            fields.append(f);
        }
    }
    return fields;
}

} // namespace

PivotRef CalcHost::currentPivot() const
{
    return pivotAt(*m_document->workbook(), m_document->activeSheet(), cursor());
}

bool CalcHost::pivotActive() const
{
    const PivotTable *pivot = pivotOf(*m_document->workbook(), currentPivot());
    return pivot != nullptr && pivot->modelled;
}

// The field the cursor's cell belongs to: a label's field, a value's row
// field, else the pivot's first row (or column, or filter) field.
qint32 CalcHost::pivotFieldAtCursor() const
{
    const PivotRef ref = currentPivot();
    const PivotTable *pivot = pivotOf(*m_document->workbook(), ref);
    if (pivot == nullptr) {
        return -1;
    }
    const PivotCellInfo info = pivotCellInfo(*m_document->workbook(), ref, cursor());
    if (info.field >= 0) {
        return info.field;
    }
    for (const qint32 f : pivot->rowFields + pivot->colFields + pivot->pageFields) {
        if (f >= 0) {
            return f;
        }
    }
    return -1;
}

void CalcHost::changeCurrentPivot(const SpreadsheetDocument::PivotChange &change, const QString &undoText)
{
    const PivotRef ref = currentPivot();
    if (!ref.isValid()) {
        showStatusMessage(tr("Put the cursor in a PivotTable first."), 6000);
        return;
    }
    m_document->changePivot(ref, change, undoText);
    refreshGrid();
    syncFromCursor();
    emit pivotChanged();
}

void CalcHost::sortPivot(bool ascending)
{
    const PivotRef ref = currentPivot();
    const PivotCellInfo info = pivotCellInfo(*m_document->workbook(), ref, cursor());
    const qint32 field = pivotFieldAtCursor();
    if (field < 0) {
        showStatusMessage(tr("Put the cursor on a label or a value of the field to sort."), 6000);
        return;
    }
    // From a value: the items sort by that data field (Excel's Sort Largest to Smallest).
    const qint32 byData = info.value ? info.dataField : -1;
    changeCurrentPivot(
        [field, ascending, byData](PivotTable &p, const PivotCache &) {
            p.fields[field].sort = ascending ? PivotSort::Ascending : PivotSort::Descending;
            p.fields[field].sortData = byData;
        },
        ascending ? tr("Sort Ascending") : tr("Sort Descending"));
}

void CalcHost::wirePivotGrid()
{
    // A double click on a PivotTable value shows its records (Excel's drill-down).
    connect(m_grid, &SpreadsheetItem::pivotCellActivated, this, [this](int row, int col) {
        const PivotRef ref = pivotAt(*m_document->workbook(), m_document->activeSheet(), {row, col});
        if (pivotCellInfo(*m_document->workbook(), ref, {row, col}).value) {
            m_document->showPivotDetails(ref, {row, col});
            refreshGrid();
        }
    });
}

void CalcHost::createPivotActions()
{
    Quick::ActionModel *model = actions();
    const auto make = [this, model](const char *id, const QString &text, const char *icon,
                                    const std::function<void()> &run) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        connect(entry, &ActionEntry::triggered, this, [this, run] {
            if (!m_syncing) {
                run();
            }
        });
        return entry;
    };
    make(ActionId::kCalcInsertPivotTable, tr("&PivotTable…"), "table-2", [this] { emit pivotCreateRequested(QStringLiteral("create")); });
    make(ActionId::kCalcPivotRefresh, tr("&Refresh"), "refresh-cw", [this] {
        m_document->refreshPivot(currentPivot());
        refreshGrid();
        emit pivotChanged();
    });
    make(ActionId::kCalcPivotRefreshAll, tr("Refresh &All"), "refresh-ccw", [this] {
        m_document->refreshAllPivots();
        refreshGrid();
        emit pivotChanged();
    });
    make(ActionId::kCalcPivotChangeSource, tr("Change Data &Source…"), "database",
         [this] { emit pivotCreateRequested(QStringLiteral("source")); });
    make(ActionId::kCalcPivotClear, tr("&Clear All"), "eraser", [this] {
        m_document->clearPivot(currentPivot());
        refreshGrid();
        emit pivotChanged();
    });
    make(ActionId::kCalcPivotMove, tr("&Move PivotTable…"), "move", [this] { emit pivotCreateRequested(QStringLiteral("move")); });
    make(ActionId::kCalcPivotOptions, tr("PivotTable &Options…"), "settings", [this] { emit pivotOptionsRequested(); });
    ActionEntry *fieldList = make(ActionId::kCalcPivotFieldList, tr("Field &List"), "panel-right", [] {});
    fieldList->setCheckable(true);
    fieldList->setChecked(true);
    connect(fieldList, &ActionEntry::toggled, this, [this](bool on) { emit pivotFieldListToggleRequested(on); });
    make(ActionId::kCalcPivotFieldSettings, tr("&Field Settings…"), "list-tree", [this] {
        emit pivotFieldRequested(pivotFieldAtCursor());
    });
    make(ActionId::kCalcPivotValueFieldSettings, tr("&Value Field Settings…"), "sigma", [this] {
        const PivotCellInfo info = pivotCellInfo(*m_document->workbook(), currentPivot(), cursor());
        emit pivotValueFieldRequested(qMax(0, info.dataField));
    });
    make(ActionId::kCalcPivotGroup, tr("&Group…"), "group", [this] { groupAtCursor(); });
    make(ActionId::kCalcPivotUngroup, tr("&Ungroup"), "ungroup", [this] {
        m_document->ungroupPivot(currentPivot(), pivotFieldAtCursor());
        refreshGrid();
        emit pivotChanged();
    });
    make(ActionId::kCalcPivotSortAscending, tr("Sort &A to Z"), "arrow-up-a-z", [this] { sortPivot(true); });
    make(ActionId::kCalcPivotSortDescending, tr("Sort &Z to A"), "arrow-down-z-a", [this] { sortPivot(false); });
    make(ActionId::kCalcPivotFilter, tr("F&ilter…"), "funnel", [this] { emit pivotFilterRequested(pivotFieldAtCursor()); });
    make(ActionId::kCalcPivotClearFilter, tr("Clear Filte&r"), "list-restart", [this] {
        const qint32 field = pivotFieldAtCursor();
        changeCurrentPivot(
            [field](PivotTable &p, const PivotCache &) {
                if (field >= 0 && field < p.fields.size()) {
                    p.fields[field].filters.clear();
                    p.fields[field].hidden.clear();
                    p.fields[field].pageItem = -1;
                }
            },
            tr("Clear Filter"));
    });
    make(ActionId::kCalcPivotShowDetails, tr("Show &Details"), "zoom-in", [this] {
        m_document->showPivotDetails(currentPivot(), cursor());
        refreshGrid();
    });
    // Report Layout, Subtotals, Grand Totals, Blank Rows and the styles are
    // radio groups the cursor's pivot checks (syncPivotFromCursor).
    const auto radio = [&make](const char *id, const QString &text, const char *group, const std::function<void()> &run) {
        ActionEntry *entry = make(id, text, "", run);
        entry->setCheckable(true);
        entry->setRadioGroup(QString::fromLatin1(group));
        return entry;
    };
    const auto form = [this](PivotForm form) {
        changeCurrentPivot([form](PivotTable &p, const PivotCache &) { p.options.form = form; }, tr("Report Layout"));
    };
    radio(ActionId::kCalcPivotLayoutCompact, tr("Show in &Compact Form"), "pivotLayout", [form] { form(PivotForm::Compact); });
    radio(ActionId::kCalcPivotLayoutOutline, tr("Show in &Outline Form"), "pivotLayout", [form] { form(PivotForm::Outline); });
    radio(ActionId::kCalcPivotLayoutTabular, tr("Show in &Tabular Form"), "pivotLayout", [form] { form(PivotForm::Tabular); });
    const auto labels = [this](bool repeat) {
        changeCurrentPivot(
            [repeat](PivotTable &p, const PivotCache &) {
                for (const qint32 f : axisFields(p)) {
                    p.fields[f].repeatLabels = repeat;
                }
            },
            tr("Repeat Item Labels"));
    };
    make(ActionId::kCalcPivotRepeatLabels, tr("&Repeat All Item Labels"), "", [labels] { labels(true); });
    make(ActionId::kCalcPivotNoRepeatLabels, tr("Do &Not Repeat Item Labels"), "", [labels] { labels(false); });
    const auto subtotals = [this](bool on, bool top) {
        changeCurrentPivot(
            [on, top](PivotTable &p, const PivotCache &) {
                for (const qint32 f : axisFields(p)) {
                    p.fields[f].subtotals = on;
                    p.fields[f].customSubtotals.clear();
                    p.fields[f].subtotalTop = top;
                }
            },
            tr("Subtotals"));
    };
    radio(ActionId::kCalcPivotSubtotalsOff, tr("Do &Not Show Subtotals"), "pivotSubtotals", [subtotals] { subtotals(false, true); });
    radio(ActionId::kCalcPivotSubtotalsBottom, tr("Show all Subtotals at &Bottom of Group"), "pivotSubtotals",
          [subtotals] { subtotals(true, false); });
    radio(ActionId::kCalcPivotSubtotalsTop, tr("Show all Subtotals at &Top of Group"), "pivotSubtotals",
          [subtotals] { subtotals(true, true); });
    // AGENT-NOTE: Excel's labels: "On for Rows Only" is the Grand Total
    // COLUMN (each row's total) — OOXML's colGrandTotals (pivot.h).
    const auto grand = [this](bool row, bool col) {
        changeCurrentPivot(
            [row, col](PivotTable &p, const PivotCache &) {
                p.options.rowGrandTotals = row;
                p.options.colGrandTotals = col;
            },
            tr("Grand Totals"));
    };
    radio(ActionId::kCalcPivotGrandTotalsOff, tr("&Off for Rows and Columns"), "pivotGrand", [grand] { grand(false, false); });
    radio(ActionId::kCalcPivotGrandTotalsOn, tr("O&n for Rows and Columns"), "pivotGrand", [grand] { grand(true, true); });
    radio(ActionId::kCalcPivotGrandTotalsRows, tr("On for &Rows Only"), "pivotGrand", [grand] { grand(false, true); });
    radio(ActionId::kCalcPivotGrandTotalsColumns, tr("On for &Columns Only"), "pivotGrand", [grand] { grand(true, false); });
    const auto blank = [this](bool on) {
        changeCurrentPivot(
            [on](PivotTable &p, const PivotCache &) {
                for (qsizetype i = 0; i + 1 < p.rowFields.size(); ++i) { // every row field but the innermost
                    if (p.rowFields.at(i) >= 0) {
                        p.fields[p.rowFields.at(i)].insertBlankRow = on;
                    }
                }
            },
            tr("Blank Rows"));
    };
    make(ActionId::kCalcPivotBlankRowsOn, tr("&Insert Blank Line after Each Item"), "", [blank] { blank(true); });
    make(ActionId::kCalcPivotBlankRowsOff, tr("&Remove Blank Line after Each Item"), "", [blank] { blank(false); });
    const auto option = [this, model](const char *id, const QString &text, bool PivotStyleInfo::*flag) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text);
        entry->setCheckable(true);
        connect(entry, &ActionEntry::toggled, this, [this, flag](bool on) {
            if (!m_syncing) {
                changeCurrentPivot([flag, on](PivotTable &p, const PivotCache &) { p.options.style.*flag = on; },
                                   tr("PivotTable Style Options"));
            }
        });
    };
    option(ActionId::kCalcPivotRowHeaders, tr("Row &Headers"), &PivotStyleInfo::rowHeaders);
    option(ActionId::kCalcPivotColumnHeaders, tr("&Column Headers"), &PivotStyleInfo::colHeaders);
    option(ActionId::kCalcPivotBandedRows, tr("&Banded Rows"), &PivotStyleInfo::rowStripes);
    option(ActionId::kCalcPivotBandedColumns, tr("Banded Co&lumns"), &PivotStyleInfo::colStripes);
    QStringList names = pivotStyleNames();
    names.append(QString());
    for (const QString &name : std::as_const(names)) {
        ActionEntry *entry = model->add(styleId(name), name.isEmpty() ? tr("None") : name, {}, QStringLiteral("palette"));
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("pivotStyle"));
        connect(entry, &ActionEntry::triggered, this, [this, name] {
            if (!m_syncing) {
                changeCurrentPivot([name](PivotTable &p, const PivotCache &) { p.options.style.name = name; },
                                   tr("PivotTable Style"));
            }
        });
    }
}

void CalcHost::insertPivotMenus()
{
    const auto a = [](const char *id) { return MenuNode::action(QString::fromLatin1(id)); };
    Quick::MenuTree *tree = menuTree();
    tree->insertInto(QStringLiteral("calcInsertMenu"), QString::fromLatin1(ActionId::kCalcInsertTable),
                     a(ActionId::kCalcInsertPivotTable));
    QList<MenuNode> styles;
    for (const QString &id : styleIds()) {
        styles.append(MenuNode::action(id));
    }
    tree->appendTo(QStringLiteral("calcDataMenu"), MenuNode::separator());
    tree->appendTo(
        QStringLiteral("calcDataMenu"),
        MenuNode::submenu(
            QStringLiteral("calcPivotMenu"), tr("&PivotTable"),
            {a(ActionId::kCalcPivotRefresh), a(ActionId::kCalcPivotRefreshAll), a(ActionId::kCalcPivotChangeSource),
             MenuNode::separator(), a(ActionId::kCalcPivotFieldList), a(ActionId::kCalcPivotFieldSettings),
             a(ActionId::kCalcPivotValueFieldSettings), a(ActionId::kCalcPivotOptions), MenuNode::separator(),
             a(ActionId::kCalcPivotSortAscending), a(ActionId::kCalcPivotSortDescending), a(ActionId::kCalcPivotFilter),
             a(ActionId::kCalcPivotClearFilter), MenuNode::separator(), a(ActionId::kCalcPivotGroup),
             a(ActionId::kCalcPivotUngroup), a(ActionId::kCalcPivotShowDetails), MenuNode::separator(),
             MenuNode::submenu(QStringLiteral("calcPivotLayoutMenu"), tr("Report &Layout"),
                               {a(ActionId::kCalcPivotLayoutCompact), a(ActionId::kCalcPivotLayoutOutline),
                                a(ActionId::kCalcPivotLayoutTabular), MenuNode::separator(),
                                a(ActionId::kCalcPivotRepeatLabels), a(ActionId::kCalcPivotNoRepeatLabels)}),
             MenuNode::submenu(QStringLiteral("calcPivotSubtotalsMenu"), tr("&Subtotals"),
                               {a(ActionId::kCalcPivotSubtotalsOff), a(ActionId::kCalcPivotSubtotalsBottom),
                                a(ActionId::kCalcPivotSubtotalsTop)}),
             MenuNode::submenu(QStringLiteral("calcPivotGrandMenu"), tr("&Grand Totals"),
                               {a(ActionId::kCalcPivotGrandTotalsOff), a(ActionId::kCalcPivotGrandTotalsOn),
                                a(ActionId::kCalcPivotGrandTotalsRows), a(ActionId::kCalcPivotGrandTotalsColumns)}),
             MenuNode::submenu(QStringLiteral("calcPivotBlankMenu"), tr("&Blank Rows"),
                               {a(ActionId::kCalcPivotBlankRowsOn), a(ActionId::kCalcPivotBlankRowsOff)}),
             MenuNode::submenu(QStringLiteral("calcPivotStyleOptionsMenu"), tr("Style &Options"),
                               {a(ActionId::kCalcPivotRowHeaders), a(ActionId::kCalcPivotColumnHeaders),
                                a(ActionId::kCalcPivotBandedRows), a(ActionId::kCalcPivotBandedColumns)}),
             MenuNode::submenu(QStringLiteral("calcPivotStylesMenu"), tr("PivotTable S&tyles"), styles),
             MenuNode::separator(), a(ActionId::kCalcPivotMove), a(ActionId::kCalcPivotClear)}));
}

void CalcHost::syncPivotFromCursor()
{
    m_syncing = true;
    Quick::ActionModel *model = actions();
    const PivotRef ref = currentPivot();
    const PivotTable *pivot = pivotOf(*m_document->workbook(), ref);
    const bool active = pivot != nullptr && pivot->modelled;
    model->setEnabled(QString::fromLatin1(ActionId::kCalcInsertPivotTable), pivot == nullptr && !sheet().isChartSheet());
    QStringList ids{QString::fromLatin1(ActionId::kCalcPivotRefresh), QString::fromLatin1(ActionId::kCalcPivotChangeSource),
                    QString::fromLatin1(ActionId::kCalcPivotClear), QString::fromLatin1(ActionId::kCalcPivotMove),
                    QString::fromLatin1(ActionId::kCalcPivotOptions), QString::fromLatin1(ActionId::kCalcPivotFieldSettings),
                    QString::fromLatin1(ActionId::kCalcPivotValueFieldSettings), QString::fromLatin1(ActionId::kCalcPivotGroup),
                    QString::fromLatin1(ActionId::kCalcPivotUngroup), QString::fromLatin1(ActionId::kCalcPivotSortAscending),
                    QString::fromLatin1(ActionId::kCalcPivotSortDescending), QString::fromLatin1(ActionId::kCalcPivotFilter),
                    QString::fromLatin1(ActionId::kCalcPivotClearFilter), QString::fromLatin1(ActionId::kCalcPivotShowDetails),
                    QString::fromLatin1(ActionId::kCalcPivotLayoutCompact), QString::fromLatin1(ActionId::kCalcPivotLayoutOutline),
                    QString::fromLatin1(ActionId::kCalcPivotLayoutTabular), QString::fromLatin1(ActionId::kCalcPivotRepeatLabels),
                    QString::fromLatin1(ActionId::kCalcPivotNoRepeatLabels), QString::fromLatin1(ActionId::kCalcPivotSubtotalsOff),
                    QString::fromLatin1(ActionId::kCalcPivotSubtotalsBottom), QString::fromLatin1(ActionId::kCalcPivotSubtotalsTop),
                    QString::fromLatin1(ActionId::kCalcPivotGrandTotalsOff), QString::fromLatin1(ActionId::kCalcPivotGrandTotalsOn),
                    QString::fromLatin1(ActionId::kCalcPivotGrandTotalsRows), QString::fromLatin1(ActionId::kCalcPivotGrandTotalsColumns),
                    QString::fromLatin1(ActionId::kCalcPivotBlankRowsOn), QString::fromLatin1(ActionId::kCalcPivotBlankRowsOff),
                    QString::fromLatin1(ActionId::kCalcPivotRowHeaders), QString::fromLatin1(ActionId::kCalcPivotColumnHeaders),
                    QString::fromLatin1(ActionId::kCalcPivotBandedRows), QString::fromLatin1(ActionId::kCalcPivotBandedColumns)};
    ids += styleIds();
    model->setEnabled(ids, active);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcPivotRefreshAll), !m_document->workbook()->pivotCaches.isEmpty());
    if (active) {
        const PivotOptions &o = pivot->options;
        model->setChecked(QString::fromLatin1(o.form == PivotForm::Compact   ? ActionId::kCalcPivotLayoutCompact
                                              : o.form == PivotForm::Outline ? ActionId::kCalcPivotLayoutOutline
                                                                             : ActionId::kCalcPivotLayoutTabular),
                          true);
        const QList<qint32> fields = axisFields(*pivot);
        const PivotFieldSettings *first = fields.isEmpty() ? nullptr : &pivot->fields.at(fields.first());
        const bool on = first != nullptr && (first->subtotals || !first->customSubtotals.isEmpty());
        model->setChecked(QString::fromLatin1(!on                 ? ActionId::kCalcPivotSubtotalsOff
                                              : first->subtotalTop ? ActionId::kCalcPivotSubtotalsTop
                                                                   : ActionId::kCalcPivotSubtotalsBottom),
                          true);
        model->setChecked(QString::fromLatin1(o.rowGrandTotals && o.colGrandTotals ? ActionId::kCalcPivotGrandTotalsOn
                                              : o.colGrandTotals                    ? ActionId::kCalcPivotGrandTotalsRows
                                              : o.rowGrandTotals                    ? ActionId::kCalcPivotGrandTotalsColumns
                                                                                    : ActionId::kCalcPivotGrandTotalsOff),
                          true);
        model->setChecked(QString::fromLatin1(ActionId::kCalcPivotRowHeaders), o.style.rowHeaders);
        model->setChecked(QString::fromLatin1(ActionId::kCalcPivotColumnHeaders), o.style.colHeaders);
        model->setChecked(QString::fromLatin1(ActionId::kCalcPivotBandedRows), o.style.rowStripes);
        model->setChecked(QString::fromLatin1(ActionId::kCalcPivotBandedColumns), o.style.colStripes);
        model->setChecked(styleId(o.style.name), true);
    }
    m_syncing = false;
    setRibbonContexts(active ? QStringList{QStringLiteral("pivot")} : QStringList{});
    // A pivot QindaCalc keeps as read says why it is read-only, once per visit.
    if (pivot != nullptr && !pivot->modelled && m_readOnlyPivotNoted != ref) {
        showStatusMessage(tr("PivotTable “%1” %2; QindaCalc keeps it as it is, read-only.").arg(pivot->name, pivot->note),
                          10000);
    }
    m_readOnlyPivotNoted = pivot != nullptr && !pivot->modelled ? ref : PivotRef{};
    if (m_lastPivot != ref) {
        m_lastPivot = ref;
        emit pivotChanged();
    }
}

} // namespace QindaOffice::Apps::QindaCalc
