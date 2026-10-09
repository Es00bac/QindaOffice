// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's Home ▸ Conditional Formatting (M78, D-087): New Rule and Edit
// Rule (CondFormatRuleDialog.qml) for every kind of rule Excel has, the
// gallery commands — Data Bars, Color Scales, Icon Sets (applied at once
// with Excel's first choice), Top 10 Items, Above Average, Duplicate
// Values (the rule dialog, prefilled) —, Clear Rules from the selection or
// the sheet, and the rules the Rules Manager lists that QindaCalc keeps as
// Excel wrote them. Also the answer to a validation Warning or
// Information question. One undo step per change.

#include "calc_host.h"

#include "calc_condformat_maps.h"
#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "sheet.h"

#include <QCoreApplication>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;
using Op = CondFormatRule::Op;

namespace {

// Excel's "Light Red Fill with Dark Red Text", the dialogs' default format.
CondFormat lightRed()
{
    CondFormat format;
    format.background = Rgb{0xff, 0xc7, 0xce};
    format.foreground = Rgb{0x9c, 0x00, 0x06};
    return format;
}

QString keptKindText(KeptRule::Kind kind)
{
    switch (kind) {
    case KeptRule::Kind::CondFormat:
    case KeptRule::Kind::CondFormatX14:
        return QCoreApplication::translate("CalcHost", "Conditional format");
    case KeptRule::Kind::Validation:
    case KeptRule::Kind::ValidationX14:
        return QCoreApplication::translate("CalcHost", "Validation");
    }
    return {};
}

} // namespace

void CalcHost::createCondFormatActions()
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
    make(ActionId::kCalcCondFormatNewRule, tr("&New Rule…"), "list-plus", [this] {
        m_condPreset.clear();
        emit condFormatRuleRequested(-1);
    });
    make(ActionId::kCalcCondFormatDataBars, tr("&Data Bars"), "chart-bar",
         [this] { quickCondFormat(defaultDataBar(selection()), tr("Data Bars")); });
    make(ActionId::kCalcCondFormatColorScale, tr("Color &Scales"), "blend",
         [this] { quickCondFormat(defaultColorScale(selection()), tr("Color Scales")); });
    make(ActionId::kCalcCondFormatIconSet, tr("&Icon Sets"), "flag",
         [this] { quickCondFormat(defaultIconSet(selection()), tr("Icon Sets")); });
    const auto preset = [this](const char *name) {
        m_condPreset = QString::fromLatin1(name);
        emit condFormatRuleRequested(-1);
    };
    make(ActionId::kCalcCondFormatTop10, tr("&Top 10 Items…"), "trending-up", [preset] { preset("top10"); });
    make(ActionId::kCalcCondFormatAboveAverage, tr("&Above Average…"), "arrow-up-to-line", [preset] { preset("above"); });
    make(ActionId::kCalcCondFormatDuplicates, tr("D&uplicate Values…"), "copy", [preset] { preset("duplicate"); });
    make(ActionId::kCalcCondFormatClearSelection, tr("Clear Rules from &Selected Cells"), "eraser", [this] {
        m_document->setCondFormats(rulesWithout(sheet().condFormats(), selection()));
        refreshGrid();
    });
    make(ActionId::kCalcCondFormatClearSheet, tr("Clear Rules from &Entire Sheet"), "trash-2", [this] {
        // Excel clears every rule of the sheet — those QindaCalc only keeps too.
        QList<KeptRule> kept = sheet().keptRules();
        kept.removeIf([](const KeptRule &rule) {
            return rule.kind == KeptRule::Kind::CondFormat || rule.kind == KeptRule::Kind::CondFormatX14;
        });
        m_document->setCondFormats({}, kept);
        refreshGrid();
    });
}

void CalcHost::quickCondFormat(CondFormatRule rule, const QString &undoText)
{
    Q_UNUSED(undoText);
    if (!rule.range.isValid() || sheet().isChartSheet()) {
        return;
    }
    // Excel puts a new rule first: it takes precedence.
    QList<CondFormatRule> rules = sheet().condFormats();
    rules.prepend(rule);
    m_document->setCondFormats(rules);
    refreshGrid();
}

QVariantMap CalcHost::condFormatRuleState(int index) const
{
    const QList<CondFormatRule> &rules = sheet().condFormats();
    if (index >= 0 && index < rules.size()) {
        QVariantMap map = condRuleToMap(rules.at(index));
        map.insert(QStringLiteral("index"), index);
        return map;
    }
    CondFormatRule rule;
    rule.range = selection();
    rule.format = lightRed();
    if (m_condPreset == QLatin1String("top10")) {
        rule.op = Op::Top10;
    } else if (m_condPreset == QLatin1String("above")) {
        rule.op = Op::AboveAverage;
    } else if (m_condPreset == QLatin1String("duplicate")) {
        rule.op = Op::Duplicate;
    } else {
        rule.op = Op::GreaterThan;
    }
    QVariantMap map = condRuleToMap(rule);
    map.insert(QStringLiteral("index"), -1);
    return map;
}

QString CalcHost::saveCondFormatRule(int index, const QVariantMap &map)
{
    QList<CondFormatRule> rules = sheet().condFormats();
    const bool editing = index >= 0 && index < rules.size();
    CondFormatRule rule = condRuleFromMap(map, editing ? rules.at(index) : CondFormatRule{});
    if (!rule.range.isValid()) {
        return tr("Reference isn't valid.");
    }
    if (!prepareCondRule(rule)) {
        return tr("There's a problem with this formula.");
    }
    if (rule.op == Op::ColorScale && (rule.thresholds.size() < 2 || rule.thresholds.size() != rule.colors.size())) {
        return tr("A color scale needs two or three points, each with a color.");
    }
    if (rule.op == Op::IconSet && rule.thresholds.isEmpty()) {
        return tr("An icon set needs its thresholds.");
    }
    if (editing) {
        rules[index] = rule;
    } else {
        rule.priority = 0;
        rule.dxfId = -1;
        rules.prepend(rule); // a new rule takes precedence, as in Excel
    }
    m_document->setCondFormats(rules);
    refreshGrid();
    return {};
}

QVariantList CalcHost::keptRuleList() const
{
    QVariantList list;
    for (const KeptRule &rule : sheet().keptRules()) {
        QStringList ranges;
        for (const CellRange &range : rule.ranges) {
            ranges.append(range.topLeft == range.bottomRight ? addrText(range.topLeft)
                                                             : addrText(range.topLeft) + QLatin1Char(':') + addrText(range.bottomRight));
        }
        list.append(QVariantMap{{QStringLiteral("kind"), keptKindText(rule.kind)},
                                {QStringLiteral("range"), ranges.join(QLatin1Char(' '))},
                                {QStringLiteral("reason"), rule.reason}});
    }
    return list;
}

void CalcHost::answerQuestionedInput(bool keep)
{
    if (!keep && m_document->undoIfTop(tr("Set cell"))) {
        refreshGrid();
        emit cursorChanged();
    }
}

} // namespace QindaOffice::Apps::QindaCalc
