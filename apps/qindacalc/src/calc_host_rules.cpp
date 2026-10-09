// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's rule commands: conditional formatting, data validation, the
// autofilter and cell comments — the four things a spreadsheet says about
// a range rather than about one cell. The editors are QML dialogs
// (CondFormatDialog.qml, ValidationDialog.qml, CommentDialog.qml,
// FilterPopover.qml) that read and write through the invokables here; the
// document keeps ONE undo step per visit.

#include "calc_host.h"

#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "calc_condformat_maps.h"

#include "chart_spec.h"
#include "number_format.h"
#include "sheet.h"

#include <QColor>
#include <QKeySequence>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {

QString rangeText(const CellRange &range)
{
    if (!range.isValid()) {
        return {};
    }
    if (range.topLeft == range.bottomRight) {
        return addrText(range.topLeft);
    }
    return QStringLiteral("%1:%2").arg(addrText(range.topLeft), addrText(range.bottomRight));
}

} // namespace

void CalcHost::createRuleActions()
{
    Quick::ActionModel *model = actions();
    connect(model->add(QString::fromLatin1(ActionId::kCalcCondFormat), tr("&Manage Rules…"),
                       {}, QStringLiteral("paint-bucket")),
            &ActionEntry::triggered, this, [this] { emit condFormatRequested(); });
    connect(model->add(QString::fromLatin1(ActionId::kCalcValidation), tr("Data &Validation…"), {},
                       QStringLiteral("list-checks")),
            &ActionEntry::triggered, this, [this] { emit validationRequested(); });
    ActionEntry *filter = model->add(QString::fromLatin1(ActionId::kCalcAutoFilter), tr("&AutoFilter"),
                                     QKeySequence(QStringLiteral("Ctrl+Shift+L")), QStringLiteral("filter"));
    filter->setCheckable(true);
    connect(filter, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            toggleFilter(on);
        }
    });
    connect(model->add(QString::fromLatin1(ActionId::kCalcComment), tr("Co&mment…"),
                       QKeySequence(QStringLiteral("Shift+F2")), QStringLiteral("message-square")),
            &ActionEntry::triggered, this, [this] {
                emit commentRequested(addrText(cursor()), commentAtCursor());
            });
    connect(model->add(QString::fromLatin1(ActionId::kCalcDeleteComment), tr("Delete Co&mment")),
            &ActionEntry::triggered, this, [this] {
                m_document->setComment(cursor(), CellComment{});
                refreshGrid();
                syncRulesFromCursor();
            });
}

void CalcHost::syncRulesFromCursor()
{
    m_syncing = true;
    const Sheet &s = sheet();
    actions()->setChecked(QString::fromLatin1(ActionId::kCalcAutoFilter), s.autoFilter().active);
    actions()->setEnabled(QString::fromLatin1(ActionId::kCalcDeleteComment), s.commentAt(cursor()) != nullptr);
    m_syncing = false;
    // A validated cell's input message (M78, D-087), where Excel shows its
    // tooltip: in the status line while the cell is selected.
    if (const ValidationRule *rule = s.validationAt(cursor()); rule != nullptr && rule->showInput && !rule->prompt.isEmpty()) {
        showStatusMessage(rule->promptTitle.isEmpty() ? rule->prompt : rule->promptTitle + QStringLiteral(": ") + rule->prompt, 8000);
    }
}

// ---- conditional formatting ---------------------------------------------------------

QString CalcHost::selectionRangeText() const
{
    return rangeText(selection());
}

QVariantList CalcHost::condFormatRules() const
{
    // M78 (D-087): every kind of rule is editable (calc_condformat_maps.h);
    // each map names its place in the list, so the Rules Manager can move,
    // edit and delete rules and hand the whole list back as one step.
    QVariantList list;
    const QList<CondFormatRule> &rules = sheet().condFormats();
    for (qsizetype i = 0; i < rules.size(); ++i) {
        QVariantMap map = condRuleToMap(rules.at(i));
        map.insert(QStringLiteral("index"), int(i));
        map.insert(QStringLiteral("imported"), false);
        list.append(map);
    }
    return list;
}

void CalcHost::setCondFormatRules(const QVariantList &rules)
{
    // The list comes back in the dialog's order: a map with an "index" is
    // that existing rule (with any field the map sets changed), a map
    // without one a new rule. Adding three rules and removing one is ONE
    // undo step — which is what the user did.
    const QList<CondFormatRule> &existing = sheet().condFormats();
    QList<CondFormatRule> out;
    for (const QVariant &value : rules) {
        const QVariantMap map = value.toMap();
        const int index = map.value(QStringLiteral("index"), -1).toInt();
        const bool known = index >= 0 && index < existing.size();
        CondFormatRule rule = condRuleFromMap(map, known ? existing.at(index) : CondFormatRule{});
        if (!rule.range.isValid() || !prepareCondRule(rule)) {
            continue;
        }
        out.append(rule);
    }
    m_document->setCondFormats(out);
    refreshGrid();
}

// ---- validation -------------------------------------------------------------------

namespace {

// An operand as the dialog shows it: a number, or "=formula".
QString operandText(const QString &formula, double number, ValidationRule::Kind kind)
{
    if (!formula.isEmpty()) {
        return QLatin1Char('=') + formula;
    }
    if (kind == ValidationRule::Kind::Date) {
        return NumberFormat(QStringLiteral("m/d/yyyy")).format(number).text;
    }
    if (kind == ValidationRule::Kind::Time) {
        return NumberFormat(QStringLiteral("h:mm:ss")).format(number).text;
    }
    return QString::number(number, 'g', 15);
}

// The dialog's operand: a number (dates and times as typed), else a formula.
void setOperand(const QString &typed, double *number, QString *formula)
{
    const QString text = typed.trimmed();
    formula->clear();
    if (text.startsWith(QLatin1Char('='))) {
        *formula = text.mid(1);
        return;
    }
    const Value value = Workbook::inputValue(text);
    if (const std::optional<double> parsed = isNumber(value) ? toNumber(value) : std::nullopt) {
        *number = *parsed;
    } else if (!text.isEmpty()) {
        *formula = text; // a name or a reference typed without '='
    }
}

} // namespace

QVariantMap CalcHost::validationAtCursor() const
{
    const ValidationRule *existing = sheet().validationAt(cursor());
    QVariantMap map{{QStringLiteral("kind"), -1},
                    {QStringLiteral("op"), 0},
                    {QStringLiteral("min"), 0.0},
                    {QStringLiteral("max"), 100.0},
                    {QStringLiteral("operand1"), QString()},
                    {QStringLiteral("operand2"), QString()},
                    {QStringLiteral("list"), QString()},
                    {QStringLiteral("message"), QString()},
                    {QStringLiteral("errorTitle"), QString()},
                    {QStringLiteral("style"), 0},
                    {QStringLiteral("prompt"), QString()},
                    {QStringLiteral("promptTitle"), QString()},
                    {QStringLiteral("showInput"), true},
                    {QStringLiteral("showError"), true},
                    {QStringLiteral("allowBlank"), true},
                    {QStringLiteral("inCellDropdown"), true},
                    {QStringLiteral("range"), rangeText(selection())}};
    if (existing != nullptr) {
        map.insert(QStringLiteral("kind"), int(existing->kind));
        map.insert(QStringLiteral("op"), int(existing->op));
        map.insert(QStringLiteral("min"), existing->min);
        map.insert(QStringLiteral("max"), existing->max);
        map.insert(QStringLiteral("operand1"), existing->kind == ValidationRule::Kind::Custom
                                                   ? QLatin1Char('=') + existing->formula1
                                                   : operandText(existing->formula1, existing->min, existing->kind));
        map.insert(QStringLiteral("operand2"), operandText(existing->formula2, existing->max, existing->kind));
        map.insert(QStringLiteral("list"), existing->listSource.isEmpty() ? existing->list.join(QStringLiteral(", "))
                                                                          : QLatin1Char('=') + existing->listSource);
        map.insert(QStringLiteral("message"), existing->message);
        map.insert(QStringLiteral("errorTitle"), existing->errorTitle);
        map.insert(QStringLiteral("style"), int(existing->style));
        map.insert(QStringLiteral("prompt"), existing->prompt);
        map.insert(QStringLiteral("promptTitle"), existing->promptTitle);
        map.insert(QStringLiteral("showInput"), existing->showInput);
        map.insert(QStringLiteral("showError"), existing->showError);
        map.insert(QStringLiteral("allowBlank"), existing->allowBlank);
        map.insert(QStringLiteral("inCellDropdown"), existing->inCellDropdown);
    }
    return map;
}

void CalcHost::setValidationRule(const QVariantMap &map)
{
    const CellRange current = selection();
    QList<ValidationRule> rules;
    for (const ValidationRule &rule : sheet().validations()) {
        // A new rule replaces any rule whose range is exactly the one being
        // edited; overlapping ranges elsewhere are left alone.
        if (!(rule.range == current && rule.moreRanges.isEmpty())) {
            rules.append(rule);
        }
    }
    const int kind = map.value(QStringLiteral("kind"), -1).toInt();
    if (kind >= 0) {
        ValidationRule rule;
        rule.range = current;
        rule.kind = ValidationRule::Kind(qBound(0, kind, int(ValidationRule::Kind::Any)));
        rule.op = ValidationRule::Operator(qBound(0, map.value(QStringLiteral("op")).toInt(), int(ValidationRule::Operator::LessThanOrEqual)));
        rule.min = map.value(QStringLiteral("min")).toDouble();
        rule.max = map.value(QStringLiteral("max")).toDouble();
        if (map.contains(QStringLiteral("operand1"))) {
            setOperand(map.value(QStringLiteral("operand1")).toString(), &rule.min, &rule.formula1);
            setOperand(map.value(QStringLiteral("operand2")).toString(), &rule.max, &rule.formula2);
        }
        const QString list = map.value(QStringLiteral("list")).toString().trimmed();
        if (list.startsWith(QLatin1Char('='))) {
            rule.listSource = list.mid(1); // a range or a name: the live source (resolved when the file is read)
        } else {
            for (const QString &entry : list.split(QLatin1Char(','))) {
                const QString trimmed = entry.trimmed();
                if (!trimmed.isEmpty()) {
                    rule.list.append(trimmed);
                }
            }
        }
        rule.message = map.value(QStringLiteral("message")).toString().trimmed();
        rule.errorTitle = map.value(QStringLiteral("errorTitle")).toString().trimmed();
        rule.style = ValidationRule::Style(qBound(0, map.value(QStringLiteral("style")).toInt(), 2));
        rule.prompt = map.value(QStringLiteral("prompt")).toString();
        rule.promptTitle = map.value(QStringLiteral("promptTitle")).toString();
        rule.showInput = map.value(QStringLiteral("showInput"), true).toBool();
        rule.showError = map.value(QStringLiteral("showError"), true).toBool();
        rule.allowBlank = map.value(QStringLiteral("allowBlank"), true).toBool();
        rule.inCellDropdown = map.value(QStringLiteral("inCellDropdown"), true).toBool();
        if (!prepareValidationRule(rule)) {
            showStatusMessage(tr("There's a problem with the formula in this validation."), 8000);
            return;
        }
        rules.append(rule);
    }
    m_document->setValidations(rules);
    syncRulesFromCursor();
    refreshInvalidCircles();
}

// ---- comments ---------------------------------------------------------------------

QString CalcHost::commentAtCursor() const
{
    const CellComment *existing = sheet().commentAt(cursor());
    return existing != nullptr ? existing->text : QString();
}

void CalcHost::setCommentText(const QString &text)
{
    const CellAddr at = cursor();
    const CellComment *existing = sheet().commentAt(at);
    CellComment comment;
    comment.author = existing != nullptr ? existing->author : QString::fromLocal8Bit(qgetenv("USER"));
    comment.text = text.trimmed();
    m_document->setComment(at, comment);
    refreshGrid();
    syncRulesFromCursor();
}

// ---- autofilter -------------------------------------------------------------------

void CalcHost::toggleFilter(bool on)
{
    AutoFilter filter;
    if (on) {
        filter.range = blockAroundSelection();
        filter.active = filter.range.isValid() && filter.range.bottomRight.row > filter.range.topLeft.row;
        if (!filter.active) {
            // Nothing to filter: refuse rather than leave a filter whose
            // header row is also its only row.
            m_syncing = true;
            actions()->setChecked(QString::fromLatin1(ActionId::kCalcAutoFilter), false);
            m_syncing = false;
            return;
        }
    }
    m_document->setAutoFilter(filter);
    refreshGrid();
}

void CalcHost::applyFilterValues(int column, const QStringList &kept)
{
    AutoFilter filter = sheet().autoFilter();
    if (!filter.active || !filter.range.isValid()) {
        return;
    }
    const QStringList values = sheet().filterValuesFor(
        qint32(column), [this](const CellAddr &addr) { return m_document->displayTextAt(addr); });
    if (kept.size() == values.size()) {
        // Everything allowed is the same as no filter on this column.
        filter.allowed.remove(qint32(column));
    } else {
        filter.allowed.insert(qint32(column), kept);
    }
    m_document->setAutoFilter(filter);
    refreshGrid();
}

} // namespace QindaOffice::Apps::QindaCalc
