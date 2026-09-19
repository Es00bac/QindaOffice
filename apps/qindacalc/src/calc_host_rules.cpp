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

#include "chart_spec.h"
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

// "A1:B9" → range; a single address is a one-cell range.
CellRange parseRangeText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return parseChartRange(trimmed.contains(QLatin1Char(':')) ? trimmed
                                                             : trimmed + QLatin1Char(':') + trimmed);
}

QString colorText(const std::optional<Rgb> &rgb)
{
    return rgb.has_value() ? QColor(rgb->r, rgb->g, rgb->b).name() : QString();
}

std::optional<Rgb> colorFromText(const QString &text)
{
    const QColor color = QColor::fromString(text);
    if (text.isEmpty() || !color.isValid()) {
        return std::nullopt;
    }
    return Rgb{quint8(color.red()), quint8(color.green()), quint8(color.blue())};
}

} // namespace

void CalcHost::createRuleActions()
{
    Quick::ActionModel *model = actions();
    connect(model->add(QString::fromLatin1(ActionId::kCalcCondFormat), tr("Con&ditional Formatting…"),
                       {}, QStringLiteral("paint-bucket")),
            &ActionEntry::triggered, this, [this] { emit condFormatRequested(); });
    connect(model->add(QString::fromLatin1(ActionId::kCalcValidation), tr("&Validity…")),
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
}

// ---- conditional formatting ---------------------------------------------------------

QString CalcHost::selectionRangeText() const
{
    return rangeText(selection());
}

QVariantList CalcHost::condFormatRules() const
{
    QVariantList list;
    for (const CondFormatRule &rule : sheet().condFormats()) {
        // Ops the dialog cannot author (import-only) travel marked: the
        // dialog shows them read-only and the host re-inserts them at their
        // original positions on Apply, so precedence survives the visit.
        const bool imported = rule.op == CondFormatRule::Op::Expression
            || rule.op == CondFormatRule::Op::DataBar
            || rule.op == CondFormatRule::Op::GreaterThanOrEqual
            || rule.op == CondFormatRule::Op::LessThanOrEqual;
        list.append(QVariantMap{
            {QStringLiteral("range"), rangeText(rule.range)},
            {QStringLiteral("op"), int(rule.op)},
            {QStringLiteral("first"), rule.first},
            {QStringLiteral("second"), rule.second},
            {QStringLiteral("text"), rule.text},
            {QStringLiteral("formula"), rule.formula},
            {QStringLiteral("imported"), imported},
            {QStringLiteral("background"), colorText(rule.format.background)},
            {QStringLiteral("foreground"), colorText(rule.format.foreground)},
            {QStringLiteral("bold"), rule.format.bold.value_or(false)},
            {QStringLiteral("italic"), rule.format.italic.value_or(false)},
        });
    }
    return list;
}

void CalcHost::setCondFormatRules(const QVariantList &rules)
{
    QList<CondFormatRule> fromDialog;
    for (const QVariant &value : rules) {
        const QVariantMap map = value.toMap();
        CondFormatRule rule;
        rule.range = parseRangeText(map.value(QStringLiteral("range")).toString());
        if (!rule.range.isValid()) {
            continue;
        }
        rule.op = CondFormatRule::Op(qBound(0, map.value(QStringLiteral("op")).toInt(), int(CondFormatRule::Op::NotEmpty)));
        rule.first = map.value(QStringLiteral("first")).toDouble();
        rule.second = map.value(QStringLiteral("second")).toDouble();
        rule.text = map.value(QStringLiteral("text")).toString();
        rule.format.background = colorFromText(map.value(QStringLiteral("background")).toString());
        rule.format.foreground = colorFromText(map.value(QStringLiteral("foreground")).toString());
        if (map.value(QStringLiteral("bold")).toBool()) {
            rule.format.bold = true;
        }
        if (map.value(QStringLiteral("italic")).toBool()) {
            rule.format.italic = true;
        }
        fromDialog.append(rule);
    }
    // Re-insert the import-only rules at their original positions (the
    // dialog returned only the rows it could edit). Rules the user added
    // land at the end — the lowest precedence.
    QList<CondFormatRule> out;
    int index = 0;
    for (const CondFormatRule &original : sheet().condFormats()) {
        if (original.op == CondFormatRule::Op::Expression
            || original.op == CondFormatRule::Op::DataBar
            || original.op == CondFormatRule::Op::GreaterThanOrEqual
            || original.op == CondFormatRule::Op::LessThanOrEqual) {
            out.append(original);
        } else if (index < fromDialog.size()) {
            out.append(fromDialog.at(index++));
        }
    }
    for (; index < fromDialog.size(); ++index) {
        out.append(fromDialog.at(index));
    }
    // The whole list comes back, so adding three rules and removing one is
    // ONE undo step — which is what the user did.
    m_document->setCondFormats(out);
    refreshGrid();
}

// ---- validation -------------------------------------------------------------------

QVariantMap CalcHost::validationAtCursor() const
{
    const ValidationRule *existing = sheet().validationAt(cursor());
    QVariantMap map{{QStringLiteral("kind"), -1},
                    {QStringLiteral("min"), 0.0},
                    {QStringLiteral("max"), 100.0},
                    {QStringLiteral("list"), QString()},
                    {QStringLiteral("message"), QString()},
                    {QStringLiteral("range"), rangeText(selection())}};
    if (existing != nullptr) {
        map.insert(QStringLiteral("kind"), int(existing->kind));
        map.insert(QStringLiteral("min"), existing->min);
        map.insert(QStringLiteral("max"), existing->max);
        map.insert(QStringLiteral("list"), existing->list.join(QStringLiteral(", ")));
        map.insert(QStringLiteral("message"), existing->message);
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
        if (!(rule.range == current)) {
            rules.append(rule);
        }
    }
    const int kind = map.value(QStringLiteral("kind"), -1).toInt();
    if (kind >= 0) {
        ValidationRule rule;
        rule.range = current;
        rule.kind = ValidationRule::Kind(qBound(0, kind, int(ValidationRule::Kind::NotEmpty)));
        rule.min = map.value(QStringLiteral("min")).toDouble();
        rule.max = map.value(QStringLiteral("max")).toDouble();
        for (const QString &entry : map.value(QStringLiteral("list")).toString().split(QLatin1Char(','))) {
            const QString trimmed = entry.trimmed();
            if (!trimmed.isEmpty()) {
                rule.list.append(trimmed);
            }
        }
        rule.message = map.value(QStringLiteral("message")).toString().trimmed();
        rules.append(rule);
    }
    m_document->setValidations(rules);
    syncRulesFromCursor();
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
