// SPDX-License-Identifier: GPL-3.0-or-later
// Project's custom-field formulas, the cheap and common part (custom_fields.h
// lists what is understood): the evaluator's core — values, operators and
// the [Field] references — and the public entry points. The parser and the
// function library are custom_formula_parse.cpp and
// custom_formula_functions.cpp (custom_formula_p.h joins them).
#include "custom_formula_p.h"

#include "baselines.h"
#include "plan_fields_p.h"
#include "plan_time.h"

#include <QHash>
#include <QLocale>

#include <cmath>

namespace QindaOffice::Plan {

namespace Formula {

namespace {

enum class Ref {
    Duration, Work, Cost, FixedCost, ActualCost, RemainingCost, BaselineCost, CostVariance, Percent, Start, Finish,
    BaselineStart, BaselineFinish, ActualStart, ActualFinish, Deadline, EarlyStart, EarlyFinish, LateStart, LateFinish,
    ConstraintDate, Name, Notes, Wbs, ResourceNames, Predecessors, Priority, Id, UniqueId, OutlineLevel, Milestone,
    Summary, Critical, Active, TotalSlack, FreeSlack, BaselineDuration, BaselineWork, ActualDuration, RemainingDuration,
    ActualWork, RemainingWork, StartVariance, FinishVariance, DurationVariance, WorkVariance, Type, EffortDriven,
};

const QHash<QString, Ref> &refs()
{
    static const QHash<QString, Ref> table{
        {QStringLiteral("duration"), Ref::Duration}, {QStringLiteral("work"), Ref::Work},
        {QStringLiteral("cost"), Ref::Cost}, {QStringLiteral("fixed cost"), Ref::FixedCost},
        {QStringLiteral("actual cost"), Ref::ActualCost}, {QStringLiteral("remaining cost"), Ref::RemainingCost},
        {QStringLiteral("baseline cost"), Ref::BaselineCost}, {QStringLiteral("cost variance"), Ref::CostVariance},
        {QStringLiteral("% complete"), Ref::Percent}, {QStringLiteral("percent complete"), Ref::Percent},
        {QStringLiteral("start"), Ref::Start}, {QStringLiteral("finish"), Ref::Finish},
        {QStringLiteral("baseline start"), Ref::BaselineStart}, {QStringLiteral("baseline finish"), Ref::BaselineFinish},
        {QStringLiteral("actual start"), Ref::ActualStart}, {QStringLiteral("actual finish"), Ref::ActualFinish},
        {QStringLiteral("deadline"), Ref::Deadline}, {QStringLiteral("early start"), Ref::EarlyStart},
        {QStringLiteral("early finish"), Ref::EarlyFinish}, {QStringLiteral("late start"), Ref::LateStart},
        {QStringLiteral("late finish"), Ref::LateFinish}, {QStringLiteral("constraint date"), Ref::ConstraintDate},
        {QStringLiteral("name"), Ref::Name}, {QStringLiteral("task name"), Ref::Name}, {QStringLiteral("notes"), Ref::Notes},
        {QStringLiteral("wbs"), Ref::Wbs}, {QStringLiteral("resource names"), Ref::ResourceNames},
        {QStringLiteral("predecessors"), Ref::Predecessors}, {QStringLiteral("priority"), Ref::Priority},
        {QStringLiteral("id"), Ref::Id}, {QStringLiteral("unique id"), Ref::UniqueId},
        {QStringLiteral("outline level"), Ref::OutlineLevel}, {QStringLiteral("milestone"), Ref::Milestone},
        {QStringLiteral("summary"), Ref::Summary}, {QStringLiteral("critical"), Ref::Critical},
        {QStringLiteral("active"), Ref::Active}, {QStringLiteral("total slack"), Ref::TotalSlack},
        {QStringLiteral("free slack"), Ref::FreeSlack}, {QStringLiteral("baseline duration"), Ref::BaselineDuration},
        {QStringLiteral("baseline work"), Ref::BaselineWork}, {QStringLiteral("actual duration"), Ref::ActualDuration},
        {QStringLiteral("remaining duration"), Ref::RemainingDuration}, {QStringLiteral("actual work"), Ref::ActualWork},
        {QStringLiteral("remaining work"), Ref::RemainingWork}, {QStringLiteral("start variance"), Ref::StartVariance},
        {QStringLiteral("finish variance"), Ref::FinishVariance},
        {QStringLiteral("duration variance"), Ref::DurationVariance}, {QStringLiteral("work variance"), Ref::WorkVariance},
        {QStringLiteral("type"), Ref::Type}, {QStringLiteral("effort driven"), Ref::EffortDriven},
    };
    return table;
}

// A custom field by its name or alias ("Text1", "Department").
QString customByName(const Project &project, const QString &name)
{
    if (const std::optional<CustomFieldInfo> info = customFieldInfo(name)) {
        return info->field;
    }
    for (const CustomFieldDef &d : project.customFields) {
        if (!d.alias.isEmpty() && d.alias.compare(name, Qt::CaseInsensitive) == 0) {
            return d.field;
        }
    }
    return {};
}

QString problemIn(const Project &project, const NodePtr &n)
{
    if (n->kind == Node::Field && !refs().contains(n->text.toLower()) && customByName(project, n->text).isEmpty()) {
        return QStringLiteral("There is no field [%1].").arg(n->text);
    }
    if (n->kind == Node::Call && !functionNames().contains(n->text)) {
        return QStringLiteral("QindaPlan's formulas have no function %1.").arg(n->text);
    }
    for (const NodePtr &a : n->args) {
        const QString p = problemIn(project, a);
        if (!p.isEmpty()) {
            return p;
        }
    }
    return {};
}

} // namespace

double dateValue(Minutes m)
{
    return m == kNoDate ? kNaDate : kEpochDays + double(m) / 1440.0;
}

Minutes minutesOfDate(double days)
{
    return std::llround((days - kEpochDays) * 1440.0);
}

QString numberText(double v)
{
    QString s = QString::number(v, 'f', 6);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) {
            s.chop(1);
        }
        if (s.endsWith(QLatin1Char('.'))) {
            s.chop(1);
        }
    }
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

QVariant Evaluator::eval(const NodePtr &n)
{
    if (!error.isEmpty()) {
        return {};
    }
    switch (n->kind) {
    case Node::Number: return n->number;
    case Node::Text: return n->text;
    case Node::Field: return field(n->text);
    case Node::Call: return call(n);
    case Node::Unary: {
        const QVariant v = eval(n->args.at(0));
        return n->text == QLatin1String("not") ? QVariant(!truth(v)) : QVariant(-num(v));
    }
    case Node::Binary: return binary(n->text, eval(n->args.at(0)), eval(n->args.at(1)));
    }
    return {};
}

QVariant Evaluator::fail(const QString &why)
{
    if (error.isEmpty()) {
        error = why;
    }
    return {};
}

double Evaluator::num(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool) {
        return v.toBool() ? -1.0 : 0.0;
    }
    if (v.typeId() == QMetaType::QString) {
        bool ok = false;
        const double d = QLocale::c().toDouble(v.toString().trimmed(), &ok);
        if (!ok) {
            fail(QStringLiteral("“%1” is not a number.").arg(v.toString()));
        }
        return ok ? d : 0.0;
    }
    return v.toDouble();
}

QString Evaluator::text(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool) {
        return v.toBool() ? QStringLiteral("Yes") : QStringLiteral("No");
    }
    return isText(v) ? v.toString() : numberText(v.toDouble());
}

bool Evaluator::truth(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool) {
        return v.toBool();
    }
    if (isText(v)) {
        const QString t = v.toString().trimmed().toLower();
        return t == QLatin1String("yes") || t == QLatin1String("true") || (t != QLatin1String("no") && num(v) != 0);
    }
    return v.toDouble() != 0;
}

double Evaluator::money(qint64 minor) const
{
    return double(minor) / std::pow(10.0, currencyDecimals(m_p.info.currency));
}

QVariant Evaluator::binary(const QString &o, const QVariant &a, const QVariant &b)
{
    if (!error.isEmpty()) {
        return {};
    }
    if (o == QLatin1String("&") || (o == QLatin1String("+") && isText(a) && isText(b))) {
        return text(a) + text(b);
    }
    if (o == QLatin1String("and") || o == QLatin1String("or") || o == QLatin1String("xor")) {
        const bool x = truth(a);
        const bool y = truth(b);
        return o == QLatin1String("and") ? x && y : o == QLatin1String("or") ? x || y : x != y;
    }
    if (o == QLatin1String("=") || o == QLatin1String("<>") || o == QLatin1String("<") || o == QLatin1String(">")
        || o == QLatin1String("<=") || o == QLatin1String(">=")) {
        int c = 0;
        bool numeric = !isText(a) || !isText(b);
        if (numeric) {
            const double x = num(a);
            const double y = num(b);
            if (!error.isEmpty()) {
                error.clear(); // a text that is no number compares as text
                numeric = false;
            } else {
                c = x < y ? -1 : x > y ? 1 : 0;
            }
        }
        if (!numeric) {
            c = QString::compare(text(a), text(b));
            c = c < 0 ? -1 : c > 0 ? 1 : 0;
        }
        return o == QLatin1String("=") ? c == 0 : o == QLatin1String("<>") ? c != 0 : o == QLatin1String("<") ? c < 0
               : o == QLatin1String(">") ? c > 0 : o == QLatin1String("<=") ? c <= 0 : c >= 0;
    }
    const double x = num(a);
    const double y = num(b);
    if (o == QLatin1String("+")) {
        return x + y;
    }
    if (o == QLatin1String("-")) {
        return x - y;
    }
    if (o == QLatin1String("*")) {
        return x * y;
    }
    if (o == QLatin1String("^")) {
        return std::pow(x, y);
    }
    const double ix = std::nearbyint(x);
    const double iy = std::nearbyint(y);
    if ((o == QLatin1String("/") && y == 0) || (o != QLatin1String("/") && iy == 0)) {
        return fail(QStringLiteral("The formula divides by zero."));
    }
    if (o == QLatin1String("/")) {
        return x / y;
    }
    return o == QLatin1String("\\") ? std::trunc(ix / iy) : std::fmod(ix, iy);
}

QVariant Evaluator::field(const QString &name)
{
    if (m_i < 0 || m_i >= m_p.tasks.size()) {
        return 0.0;
    }
    const Task &t = m_p.tasks.at(m_i);
    const QString custom = customByName(m_p, name);
    if (!custom.isEmpty()) {
        const std::optional<CustomFieldInfo> info = customFieldInfo(custom);
        const CustomFieldDef *def = customFieldDef(m_p, custom);
        if (def != nullptr && !def->formula.isEmpty()) {
            if (m_depth >= kMaxDepth) {
                return fail(QStringLiteral("Formulas refer to each other in a circle."));
            }
            QString parseError;
            const NodePtr tree = parseFormula(def->formula, &parseError);
            Evaluator inner(m_p, m_i, m_depth + 1);
            const QVariant v = tree ? inner.eval(tree) : QVariant();
            if (!tree || !inner.error.isEmpty()) {
                return fail(tree ? inner.error : parseError);
            }
            return v;
        }
        const std::optional<QVariant> v = customFieldValue(m_p, m_i, custom);
        switch (info->kind) {
        case CustomFieldKind::Text: return v ? v->toString() : QString();
        case CustomFieldKind::Flag: return v && v->toDouble() != 0;
        case CustomFieldKind::Cost: return money(v ? qint64(v->toDouble()) : 0);
        case CustomFieldKind::Date:
        case CustomFieldKind::Start:
        case CustomFieldKind::Finish: return v ? dateValue(Minutes(v->toDouble())) : kNaDate;
        default: return v ? v->toDouble() : 0.0;
        }
    }
    const auto it = refs().constFind(name.toLower());
    if (it == refs().cend()) {
        return fail(QStringLiteral("There is no field [%1].").arg(name));
    }
    const BaselineData &base = t.baselines[0];
    const int mpd = m_p.info.minutesPerDay;
    const Minutes actual = actualDurationOf(t, mpd);
    const qint64 actualCost = t.actualCostInput >= 0 && m_p.info.editableActualCosts ? t.actualCostInput
                                                                                     : divRound(t.cost * t.percentComplete, 100);
    const Variance var = variance(m_p, t, 0);
    switch (*it) {
    case Ref::Duration: return double(t.duration);
    case Ref::Work: return double(t.work);
    case Ref::Cost: return money(t.cost);
    case Ref::FixedCost: return money(t.fixedCost);
    case Ref::ActualCost: return money(actualCost);
    case Ref::RemainingCost: return money(t.cost - actualCost);
    case Ref::BaselineCost: return money(base.set ? base.cost : 0);
    case Ref::CostVariance: return money(var.valid ? var.cost : t.cost);
    case Ref::Percent: return double(t.percentComplete);
    case Ref::Start: return dateValue(t.start);
    case Ref::Finish: return dateValue(t.finish);
    case Ref::BaselineStart: return dateValue(base.set ? base.start : kNoDate);
    case Ref::BaselineFinish: return dateValue(base.set ? base.finish : kNoDate);
    case Ref::ActualStart: return dateValue(t.actualStart);
    case Ref::ActualFinish: return dateValue(t.actualFinish);
    case Ref::Deadline: return dateValue(t.deadline);
    case Ref::EarlyStart: return dateValue(t.earlyStart);
    case Ref::EarlyFinish: return dateValue(t.earlyFinish);
    case Ref::LateStart: return dateValue(t.lateStart);
    case Ref::LateFinish: return dateValue(t.lateFinish);
    case Ref::ConstraintDate: return dateValue(t.constraintDate);
    case Ref::Name: return t.name;
    case Ref::Notes: return t.notes;
    case Ref::Wbs: return t.wbs;
    case Ref::ResourceNames: return resourceNamesText(m_p, m_i, QLocale::c());
    case Ref::Predecessors: return predecessorsText(m_p, m_i, QLocale::c());
    case Ref::Priority: return double(t.priority);
    case Ref::Id: return double(t.id);
    case Ref::UniqueId: return double(t.uid);
    case Ref::OutlineLevel: return double(t.outlineLevel);
    case Ref::Milestone: return t.isMilestone();
    case Ref::Summary: return t.summary;
    case Ref::Critical: return t.critical;
    case Ref::Active: return t.active;
    case Ref::TotalSlack: return double(t.totalSlack);
    case Ref::FreeSlack: return double(t.freeSlack);
    case Ref::BaselineDuration: return double(base.set ? base.duration : 0);
    case Ref::BaselineWork: return double(base.set ? base.work : 0);
    case Ref::ActualDuration: return double(actual);
    case Ref::RemainingDuration: return double(t.duration - actual);
    case Ref::ActualWork: return double(divRound(t.work * t.percentComplete, 100));
    case Ref::RemainingWork: return double(t.work - divRound(t.work * t.percentComplete, 100));
    case Ref::StartVariance: return double(var.valid ? var.start : 0);
    case Ref::FinishVariance: return double(var.valid ? var.finish : 0);
    case Ref::DurationVariance: return double(var.valid ? var.duration : 0);
    case Ref::WorkVariance: return double(var.valid ? var.work : 0);
    case Ref::Type: return double(int(t.type));
    case Ref::EffortDriven: return t.effortDriven || t.type == TaskType::FixedWork;
    }
    return {};
}

} // namespace Formula

FormulaResult evaluateFormula(const Project &project, int index, const QString &formula)
{
    FormulaResult result;
    QString error;
    const Formula::NodePtr tree = Formula::parseFormula(formula, &error);
    if (!tree) {
        result.error = error;
        return result;
    }
    Formula::Evaluator evaluator(project, index, 0);
    result.value = evaluator.eval(tree);
    result.error = evaluator.error;
    result.ok = evaluator.error.isEmpty() && result.value.isValid();
    return result;
}

QString formulaProblem(const Project &project, const QString &formula)
{
    QString error;
    const Formula::NodePtr tree = Formula::parseFormula(formula, &error);
    if (!tree) {
        return QStringLiteral("The formula cannot be read: %1.").arg(error);
    }
    return Formula::problemIn(project, tree);
}

} // namespace QindaOffice::Plan
