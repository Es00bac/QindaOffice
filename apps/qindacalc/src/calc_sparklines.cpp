// SPDX-License-Identifier: GPL-3.0-or-later
// Sparklines in QindaCalc (calc_sparklines.h, M90, D-098).

#include "calc_sparklines.h"

#include "calc_host.h"
#include "spreadsheet_document.h"
#include "standard_actions.h"

#include "parser.h"
#include "sheet.h"
#include "workbook.h"

#include <QLocale>

#include <algorithm>
#include <iterator>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

using FlagMember = bool SparklineGroup::*;
const std::pair<const char *, FlagMember> kFlags[] = {
    {ActionId::kCalcSparklineHighPoint, &SparklineGroup::high},
    {ActionId::kCalcSparklineLowPoint, &SparklineGroup::low},
    {ActionId::kCalcSparklineNegativePoints, &SparklineGroup::negative},
    {ActionId::kCalcSparklineFirstPoint, &SparklineGroup::first},
    {ActionId::kCalcSparklineLastPoint, &SparklineGroup::last},
    {ActionId::kCalcSparklineMarkers, &SparklineGroup::markers},
    {ActionId::kCalcSparklineAxisShow, &SparklineGroup::displayXAxis},
    {ActionId::kCalcSparklineAxisRightToLeft, &SparklineGroup::rightToLeft}};

QString rangeText(const CellRange &range)
{
    return addrText(range.topLeft, true, true) + QLatin1Char(':') + addrText(range.bottomRight, true, true);
}

} // namespace

CalcSparklines::CalcSparklines(CalcHost *host)
    : QObject(host)
    , m_host(host)
{
}

void CalcSparklines::createActions(Quick::ActionModel *model)
{
    const auto make = [this, model](const char *id, const QString &text, const char *icon, const std::function<void()> &run) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        if (run) {
            connect(entry, &ActionEntry::triggered, this, [this, run] {
                if (!m_host->m_syncing) {
                    run();
                }
            });
        }
        return entry;
    };
    make(ActionId::kCalcInsertSparklineLine, tr("&Line Sparklines…"), "chart-line", [this] { emit createRequested(0); });
    make(ActionId::kCalcInsertSparklineColumn, tr("&Column Sparklines…"), "chart-column", [this] { emit createRequested(1); });
    make(ActionId::kCalcInsertSparklineWinLoss, tr("&Win/Loss Sparklines…"), "chart-column-big", [this] { emit createRequested(2); });
    make(ActionId::kCalcSparklineEditData, tr("Edit Group Location && &Data…"), "square-pen", [this] { emit editRequested(false); });
    make(ActionId::kCalcSparklineEditSingle, tr("Edit &Single Sparkline's Data…"), "pencil", [this] { emit editRequested(true); });
    make(ActionId::kCalcSparklineHiddenEmpty, tr("&Hidden && Empty Cells…"), "eye-off", [this] { emit hiddenEmptyRequested(); });
    const std::pair<const char *, SparklineGroup::Type> types[] = {
        {ActionId::kCalcSparklineTypeLine, SparklineGroup::Type::Line},
        {ActionId::kCalcSparklineTypeColumn, SparklineGroup::Type::Column},
        {ActionId::kCalcSparklineTypeWinLoss, SparklineGroup::Type::WinLoss}};
    const QStringList typeTexts{tr("Line"), tr("Column"), tr("Win/Loss")};
    const char *typeIcons[] = {"chart-line", "chart-column", "chart-column-big"};
    for (int i = 0; i < 3; ++i) {
        const SparklineGroup::Type type = types[i].second;
        ActionEntry *entry = make(types[i].first, typeTexts.at(i), typeIcons[i], [this, type] {
            change([type](SparklineGroup &g) { g.type = type; }, tr("Change Sparkline Type"));
        });
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("calcSparklineType"));
    }
    const QStringList flagTexts{tr("&High Point"), tr("&Low Point"), tr("&Negative Points"), tr("&First Point"),
                                tr("L&ast Point"), tr("&Markers"), tr("&Show Axis"), tr("&Plot Data Right-to-Left")};
    const char *flagIcons[] = {"arrow-up-to-line", "arrow-down-to-line", "minus", "arrow-left", "arrow-right",
                               "circle-dot", "baseline", "arrow-left-right"};
    for (int i = 0; i < 8; ++i) {
        const FlagMember member = kFlags[i].second;
        ActionEntry *entry = make(kFlags[i].first, flagTexts.at(i), flagIcons[i], {});
        entry->setCheckable(true);
        connect(entry, &ActionEntry::toggled, this, [this, member](bool on) {
            if (!m_host->m_syncing) {
                change([member, on](SparklineGroup &g) { g.*member = on; }, tr("Sparkline Show"));
            }
        });
    }
    make(ActionId::kCalcSparklineColors, tr("Sparkline &Colors…"), "palette", [this] { emit colorsRequested(); });
    ActionEntry *general = make(ActionId::kCalcSparklineAxisGeneral, tr("&General Axis Type"), "ruler", [this] {
        change([](SparklineGroup &g) { g.dateAxis = false; }, tr("Sparkline Axis"));
    });
    ActionEntry *date = make(ActionId::kCalcSparklineAxisDate, tr("&Date Axis Type…"), "calendar", [this] { emit dateAxisRequested(); });
    for (ActionEntry *entry : {general, date}) {
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("calcSparklineAxisType"));
    }
    const std::pair<const char *, int> scales[] = {
        {ActionId::kCalcSparklineMinAuto, 0}, {ActionId::kCalcSparklineMinSame, 1}, {ActionId::kCalcSparklineMinCustom, 2},
        {ActionId::kCalcSparklineMaxAuto, 3}, {ActionId::kCalcSparklineMaxSame, 4}, {ActionId::kCalcSparklineMaxCustom, 5}};
    const QStringList scaleTexts{tr("Minimum: Automatic for Each Sparkline"), tr("Minimum: Same for All Sparklines"),
                                 tr("Minimum: Custom Value…"), tr("Maximum: Automatic for Each Sparkline"),
                                 tr("Maximum: Same for All Sparklines"), tr("Maximum: Custom Value…")};
    const char *scaleIcons[] = {"scaling", "between-horizontal-start", "text-cursor-input"};
    for (const auto &[id, which] : scales) {
        const bool max = which >= 3;
        const int kind = which % 3;
        ActionEntry *entry = make(id, scaleTexts.at(which), scaleIcons[kind], [this, max, kind] {
            if (kind == 2) {
                emit axisValueRequested(max);
                return;
            }
            const SparklineGroup::AxisScale scale = kind == 0 ? SparklineGroup::AxisScale::Individual : SparklineGroup::AxisScale::Group;
            change([max, scale](SparklineGroup &g) { (max ? g.maxAxis : g.minAxis) = scale; }, tr("Sparkline Axis"));
        });
        entry->setCheckable(true);
        entry->setRadioGroup(max ? QStringLiteral("calcSparklineMax") : QStringLiteral("calcSparklineMin"));
    }
    make(ActionId::kCalcSparklineGroup, tr("&Group"), "group", [this] { group(); });
    make(ActionId::kCalcSparklineUngroup, tr("&Ungroup"), "ungroup", [this] { ungroup(); });
    make(ActionId::kCalcSparklineClear, tr("Clear Selected &Sparklines"), "eraser", [this] { clear(false); });
    make(ActionId::kCalcSparklineClearGroup, tr("Clear Selected Sparkline &Groups"), "trash-2", [this] { clear(true); });
}

void CalcSparklines::insertMenus(Quick::MenuTree *tree)
{
    const auto a = [](const char *id) { return MenuNode::action(QString::fromLatin1(id)); };
    tree->appendTo(QStringLiteral("calcInsertMenu"), MenuNode::separator());
    tree->appendTo(QStringLiteral("calcInsertMenu"),
                   MenuNode::submenu(QStringLiteral("calcSparklinesMenu"), tr("S&parklines"),
                                     {a(ActionId::kCalcInsertSparklineLine), a(ActionId::kCalcInsertSparklineColumn),
                                      a(ActionId::kCalcInsertSparklineWinLoss), MenuNode::separator(),
                                      a(ActionId::kCalcSparklineEditData), a(ActionId::kCalcSparklineEditSingle),
                                      a(ActionId::kCalcSparklineHiddenEmpty), MenuNode::separator(),
                                      a(ActionId::kCalcSparklineTypeLine), a(ActionId::kCalcSparklineTypeColumn),
                                      a(ActionId::kCalcSparklineTypeWinLoss), MenuNode::separator(),
                                      a(ActionId::kCalcSparklineHighPoint), a(ActionId::kCalcSparklineLowPoint),
                                      a(ActionId::kCalcSparklineNegativePoints), a(ActionId::kCalcSparklineFirstPoint),
                                      a(ActionId::kCalcSparklineLastPoint), a(ActionId::kCalcSparklineMarkers),
                                      a(ActionId::kCalcSparklineColors), MenuNode::separator(),
                                      MenuNode::submenu(QStringLiteral("calcSparklineAxisMenu"), tr("&Axis"),
                                                        {a(ActionId::kCalcSparklineAxisGeneral), a(ActionId::kCalcSparklineAxisDate),
                                                         a(ActionId::kCalcSparklineAxisShow), a(ActionId::kCalcSparklineAxisRightToLeft),
                                                         MenuNode::separator(), a(ActionId::kCalcSparklineMinAuto),
                                                         a(ActionId::kCalcSparklineMinSame), a(ActionId::kCalcSparklineMinCustom),
                                                         MenuNode::separator(), a(ActionId::kCalcSparklineMaxAuto),
                                                         a(ActionId::kCalcSparklineMaxSame), a(ActionId::kCalcSparklineMaxCustom)}),
                                      a(ActionId::kCalcSparklineGroup), a(ActionId::kCalcSparklineUngroup),
                                      a(ActionId::kCalcSparklineClear), a(ActionId::kCalcSparklineClearGroup)}));
}

SparklineRef CalcSparklines::current() const
{
    return sparklineAt(m_host->sheet(), m_host->cursor());
}

QList<qint32> CalcSparklines::touchedGroups() const
{
    QList<qint32> out;
    const CellRange selection = m_host->selection();
    const QList<SparklineGroup> &groups = m_host->sheet().sparklineGroups();
    for (qsizetype g = 0; g < groups.size(); ++g) {
        for (const Sparkline &line : groups.at(g).sparklines) {
            if (selection.contains(line.location) || line.location == m_host->cursor()) {
                out.append(qint32(g));
                break;
            }
        }
    }
    return out;
}

bool CalcSparklines::sync(Quick::ActionModel *model)
{
    const bool cells = !m_host->sheet().isChartSheet();
    for (const char *id : {ActionId::kCalcInsertSparklineLine, ActionId::kCalcInsertSparklineColumn,
                           ActionId::kCalcInsertSparklineWinLoss}) {
        model->setEnabled(QString::fromLatin1(id), cells);
    }
    const SparklineRef ref = current();
    const bool on = cells && ref.isValid();
    const QStringList ids{QString::fromLatin1(ActionId::kCalcSparklineEditData), QString::fromLatin1(ActionId::kCalcSparklineEditSingle),
                          QString::fromLatin1(ActionId::kCalcSparklineHiddenEmpty), QString::fromLatin1(ActionId::kCalcSparklineTypeLine),
                          QString::fromLatin1(ActionId::kCalcSparklineTypeColumn), QString::fromLatin1(ActionId::kCalcSparklineTypeWinLoss),
                          QString::fromLatin1(ActionId::kCalcSparklineColors), QString::fromLatin1(ActionId::kCalcSparklineAxisGeneral),
                          QString::fromLatin1(ActionId::kCalcSparklineAxisDate), QString::fromLatin1(ActionId::kCalcSparklineMinAuto),
                          QString::fromLatin1(ActionId::kCalcSparklineMinSame), QString::fromLatin1(ActionId::kCalcSparklineMinCustom),
                          QString::fromLatin1(ActionId::kCalcSparklineMaxAuto), QString::fromLatin1(ActionId::kCalcSparklineMaxSame),
                          QString::fromLatin1(ActionId::kCalcSparklineMaxCustom), QString::fromLatin1(ActionId::kCalcSparklineUngroup),
                          QString::fromLatin1(ActionId::kCalcSparklineClear), QString::fromLatin1(ActionId::kCalcSparklineClearGroup)};
    model->setEnabled(ids, on);
    for (const auto &[id, member] : kFlags) {
        model->setEnabled(QString::fromLatin1(id), on);
    }
    model->setEnabled(QString::fromLatin1(ActionId::kCalcSparklineGroup), on && touchedGroups().size() > 1);
    if (!on) {
        return false;
    }
    const SparklineGroup &group = m_host->sheet().sparklineGroups().at(ref.group);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcSparklineMarkers), group.type == SparklineGroup::Type::Line);
    model->setChecked(QString::fromLatin1(group.type == SparklineGroup::Type::Line     ? ActionId::kCalcSparklineTypeLine
                                          : group.type == SparklineGroup::Type::Column ? ActionId::kCalcSparklineTypeColumn
                                                                                       : ActionId::kCalcSparklineTypeWinLoss),
                      true);
    for (const auto &[id, member] : kFlags) {
        model->setChecked(QString::fromLatin1(id), group.*member);
    }
    model->setChecked(QString::fromLatin1(group.dateAxis ? ActionId::kCalcSparklineAxisDate : ActionId::kCalcSparklineAxisGeneral), true);
    const auto scaleId = [](SparklineGroup::AxisScale scale, const char *automatic, const char *same, const char *custom) {
        return QString::fromLatin1(scale == SparklineGroup::AxisScale::Group ? same
                                   : scale == SparklineGroup::AxisScale::Custom ? custom
                                                                                : automatic);
    };
    model->setChecked(scaleId(group.minAxis, ActionId::kCalcSparklineMinAuto, ActionId::kCalcSparklineMinSame,
                              ActionId::kCalcSparklineMinCustom),
                      true);
    model->setChecked(scaleId(group.maxAxis, ActionId::kCalcSparklineMaxAuto, ActionId::kCalcSparklineMaxSame,
                              ActionId::kCalcSparklineMaxCustom),
                      true);
    return true;
}

bool CalcSparklines::change(const std::function<void(SparklineGroup &)> &edit, const QString &undoText)
{
    const QList<qint32> touched = touchedGroups();
    if (touched.isEmpty()) {
        return false;
    }
    QList<SparklineGroup> groups = m_host->sheet().sparklineGroups();
    for (const qint32 g : touched) {
        edit(groups[g]);
    }
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    document->setSparklineGroups(document->activeSheet(), groups, undoText);
    m_host->refreshGrid();
    m_host->syncFromCursor();
    return true;
}

QVariantMap CalcSparklines::createState() const
{
    return {{QStringLiteral("data"), rangeText(m_host->selection())}, {QStringLiteral("location"), QString()}};
}

QString CalcSparklines::create(int type, const QString &data, const QString &location)
{
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    QString error;
    const std::optional<QList<Sparkline>> lines = sparklinesFor(*document->workbook(), document->activeSheet(), data, location, &error);
    if (!lines.has_value()) {
        return error;
    }
    SparklineGroup group = defaultSparklineGroup(SparklineGroup::Type(qBound(0, type, 2)));
    group.sparklines = *lines;
    // A new sparkline takes its cell's place in any other group.
    QList<SparklineGroup> groups;
    for (SparklineGroup existing : m_host->sheet().sparklineGroups()) {
        existing.sparklines.removeIf([&lines](const Sparkline &line) {
            return std::any_of(lines->begin(), lines->end(), [&line](const Sparkline &added) { return added.location == line.location; });
        });
        if (!existing.sparklines.isEmpty()) {
            groups.append(existing);
        }
    }
    groups.append(group);
    document->setSparklineGroups(document->activeSheet(), groups, tr("Insert Sparklines"));
    m_host->refreshGrid();
    m_host->syncFromCursor();
    return {};
}

QVariantMap CalcSparklines::editState(bool single) const
{
    const SparklineRef ref = current();
    if (!ref.isValid()) {
        return {};
    }
    const SparklineGroup &group = m_host->sheet().sparklineGroups().at(ref.group);
    if (single) {
        return {{QStringLiteral("data"), group.sparklines.at(ref.index).data},
                {QStringLiteral("location"), addrText(group.sparklines.at(ref.index).location, true, true)}};
    }
    // The group's data as one range, when its sparklines make one.
    CellRange cells{group.sparklines.first().location, group.sparklines.first().location};
    QString data;
    std::optional<RangeRef> joined;
    for (const Sparkline &line : group.sparklines) {
        cells.topLeft = {qMin(cells.topLeft.row, line.location.row), qMin(cells.topLeft.col, line.location.col)};
        cells.bottomRight = {qMax(cells.bottomRight.row, line.location.row), qMax(cells.bottomRight.col, line.location.col)};
        const ParseResult parsed = parseFormula(line.data);
        if (parsed.root && parsed.root->kind == Node::Kind::Ref) {
            const RangeRef &ref = parsed.root->ref;
            if (!joined.has_value()) {
                joined = ref;
            } else if (joined->sheet == ref.sheet) {
                joined->range.topLeft = {qMin(joined->range.topLeft.row, ref.range.topLeft.row),
                                         qMin(joined->range.topLeft.col, ref.range.topLeft.col)};
                joined->range.bottomRight = {qMax(joined->range.bottomRight.row, ref.range.bottomRight.row),
                                             qMax(joined->range.bottomRight.col, ref.range.bottomRight.col)};
            }
        }
    }
    if (joined.has_value()) {
        data = rangeRefText(*joined);
    }
    return {{QStringLiteral("data"), data}, {QStringLiteral("location"), rangeText(cells)}};
}

QString CalcSparklines::applyEdit(bool single, const QString &data, const QString &location)
{
    const SparklineRef ref = current();
    if (!ref.isValid()) {
        return tr("Select a sparkline first.");
    }
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    QList<SparklineGroup> groups = m_host->sheet().sparklineGroups();
    QString error;
    const QString where = single ? addrText(groups.at(ref.group).sparklines.at(ref.index).location) : location;
    const std::optional<QList<Sparkline>> lines = sparklinesFor(*document->workbook(), document->activeSheet(), data, where, &error);
    if (!lines.has_value()) {
        return error;
    }
    if (single) {
        groups[ref.group].sparklines[ref.index].data = lines->first().data;
    } else {
        groups[ref.group].sparklines = *lines;
    }
    document->setSparklineGroups(document->activeSheet(), groups, tr("Edit Sparklines"));
    m_host->refreshGrid();
    m_host->syncFromCursor();
    return {};
}

QVariantMap CalcSparklines::hiddenEmptyState() const
{
    const SparklineRef ref = current();
    if (!ref.isValid()) {
        return {};
    }
    const SparklineGroup &group = m_host->sheet().sparklineGroups().at(ref.group);
    return {{QStringLiteral("empty"), int(group.empty)}, {QStringLiteral("hidden"), group.displayHidden}};
}

void CalcSparklines::applyHiddenEmpty(int empty, bool hidden)
{
    change([empty, hidden](SparklineGroup &g) {
        g.empty = SparklineGroup::Empty(qBound(0, empty, 2));
        g.displayHidden = hidden;
    }, tr("Hidden and Empty Cell Settings"));
}

} // namespace QindaOffice::Apps::QindaCalc
