// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService (M83, D-092): task types and effort driving on the selection,
// an assignment's units and work typed apart (Task Information ▸
// Resources), Project ▸ Custom Fields and the Custom Field filter and sort
// (plan_service.h). Every edit is one undo step through apply().
#include "custom_fields.h"
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_service.h"
#include "plan_time.h"
#include "plan_views.h"
#include "task_types.h"

#include <QRegularExpression>

namespace QindaOffice::Apps::Plan {

namespace {

QString errorOf(const Engine::EditResult &r)
{
    return r.ok ? QString() : r.error;
}

// A value as the field shows it, from the stored spelling (lookups).
QString shown(const Engine::Project &p, const QString &field, const QString &wire, const QLocale &locale)
{
    Engine::Project scratch;
    scratch.info = p.info;
    Engine::Task t;
    t.custom.insert(field, wire);
    scratch.tasks.append(t);
    return Engine::customFieldText(scratch, 0, field, locale);
}

// The stored spelling of a value typed for the field; empty when it is not one.
QString stored(const Engine::Project &p, const QString &field, const QString &text, const QLocale &locale)
{
    Engine::Project scratch;
    scratch.info = p.info;
    scratch.tasks.append(Engine::Task{});
    return Engine::setCustomField(scratch, 0, field, text, locale).ok ? scratch.tasks.first().custom.value(field) : QString();
}

std::optional<Engine::TaskType> typeOf(const QString &key)
{
    if (key == QLatin1String("units")) {
        return Engine::TaskType::FixedUnits;
    }
    if (key == QLatin1String("duration")) {
        return Engine::TaskType::FixedDuration;
    }
    if (key == QLatin1String("work")) {
        return Engine::TaskType::FixedWork;
    }
    return std::nullopt;
}

} // namespace

QVariantMap PlanService::setSelectedTaskType(const QString &type)
{
    const std::optional<Engine::TaskType> t = typeOf(type);
    const QList<int> rows = selectedRows();
    if (!t || rows.isEmpty()) {
        return fail(QStringLiteral("Select the tasks whose type to set."));
    }
    return apply(QStringLiteral("Task Type"), [&](Engine::Project &p) {
        for (int row : rows) {
            if (!p.tasks.at(row).summary) {
                (void)Engine::setTaskType(p, row, *t);
            }
        }
        return QString();
    });
}

QVariantMap PlanService::setSelectedEffortDriven(bool on)
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return fail(QStringLiteral("Select the tasks to make effort driven."));
    }
    return apply(on ? QStringLiteral("Effort Driven") : QStringLiteral("Not Effort Driven"), [&](Engine::Project &p) {
        for (int row : rows) {
            if (!p.tasks.at(row).summary) {
                const Engine::EditResult r = Engine::setEffortDriven(p, row, on);
                if (!r.ok) {
                    return QStringLiteral("“%1”: %2").arg(p.tasks.at(row).name, r.error);
                }
            }
        }
        return QString();
    });
}

QVariantList PlanService::assignmentDetails(const QString &taskId) const
{
    QVariantList out;
    const int row = rowOf(taskId);
    if (row < 0) {
        return out;
    }
    const Engine::Project &p = project();
    const Engine::Task &t = p.tasks.at(row);
    for (int ai : p.assignmentsOfTask(t.uid)) {
        const Engine::Assignment &a = p.assignments.at(ai);
        const int ri = p.resourceIndex(a.resourceUid);
        if (ri < 0) {
            continue;
        }
        const Engine::Resource &r = p.resources.at(ri);
        const bool work = r.type == Engine::ResourceType::Work;
        const QString kind = work ? QStringLiteral("work")
                             : r.type == Engine::ResourceType::Cost ? QStringLiteral("cost") : QStringLiteral("material");
        const QString units = work ? m_locale.toString(a.units) + QLatin1Char('%')
                              : r.type == Engine::ResourceType::Cost
                                  ? Engine::formatMoney(a.costInput, p.info.currency, m_locale)
                                  : m_locale.toString(double(a.units) / 100.0, 'g', 10);
        out.append(QVariantMap{{QStringLiteral("resourceUid"), r.uid},
                               {QStringLiteral("name"), r.name},
                               {QStringLiteral("kind"), kind},
                               {QStringLiteral("units"), units},
                               {QStringLiteral("work"), work ? Engine::formatWork(Engine::assignmentWork(t, a), m_locale)
                                                             : QString()},
                               {QStringLiteral("cost"), Engine::formatMoney(a.cost, p.info.currency, m_locale)}});
    }
    return out;
}

QVariantMap PlanService::setAssignmentField(const QString &taskId, int resourceUid, const QString &field,
                                            const QString &text)
{
    const int row = rowOf(taskId);
    if (row < 0) {
        return fail(QStringLiteral("There is no such task."));
    }
    const QLocale locale = m_locale;
    return apply(field == QLatin1String("work") ? QStringLiteral("Assignment Work") : QStringLiteral("Assignment Units"),
                 [&](Engine::Project &p) {
        const int ai = p.assignmentIndex(p.tasks.at(row).uid, resourceUid);
        const int ri = p.resourceIndex(resourceUid);
        if (ai < 0 || ri < 0) {
            return QStringLiteral("That resource is not assigned to this task.");
        }
        const Engine::ResourceType type = p.resources.at(ri).type;
        if (field == QLatin1String("work")) {
            const std::optional<Engine::Minutes> work = Engine::parseWorkText(text, p.info);
            return work ? errorOf(Engine::setAssignmentWork(p, ai, *work))
                        : QStringLiteral("“%1” is not work. Try 16h or 2d.").arg(text.trimmed());
        }
        if (type == Engine::ResourceType::Cost) {
            const std::optional<qint64> amount = Engine::parseMoney(text, p.info.currency, locale);
            if (!amount || *amount < 0) {
                return QStringLiteral("“%1” is not an amount.").arg(text.trimmed());
            }
            p.assignments[ai].costInput = *amount;
            return QString();
        }
        static const QRegularExpression re(QStringLiteral("^\\s*(\\d+(?:[.,]\\d+)?)\\s*%?\\s*[^\\d%]*$"));
        const QRegularExpressionMatch m = re.match(text);
        const double value = m.hasMatch() ? m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble() : 0;
        const int units = type == Engine::ResourceType::Work ? qRound(value) : qRound(value * 100.0);
        if (units <= 0) {
            return QStringLiteral("“%1” is not an amount of work or material.").arg(text.trimmed());
        }
        return errorOf(Engine::setAssignmentUnits(p, ai, units));
    });
}

QVariantList PlanService::customFields(const QString &kind) const
{
    QVariantList out;
    const std::optional<Engine::CustomFieldKind> k = Engine::customFieldKindFromName(kind);
    if (!k) {
        return out;
    }
    const Engine::Project &p = project();
    const QStringList used = Engine::customFieldsInUse(p);
    for (const QString &field : Engine::customFieldsOfKind(*k)) {
        const Engine::CustomFieldDef *d = Engine::customFieldDef(p, field);
        out.append(QVariantMap{{QStringLiteral("field"), field},
                               {QStringLiteral("title"), d != nullptr && !d->alias.isEmpty()
                                                             ? QStringLiteral("%1 (%2)").arg(d->alias, field) : field},
                               {QStringLiteral("alias"), d != nullptr ? d->alias : QString()},
                               {QStringLiteral("lookup"), d != nullptr && !d->lookup.isEmpty()},
                               {QStringLiteral("formula"), d != nullptr ? d->formula : QString()},
                               {QStringLiteral("used"), used.contains(field)}});
    }
    return out;
}

QVariantMap PlanService::customFieldDefinition(const QString &field) const
{
    const std::optional<Engine::CustomFieldInfo> info = Engine::customFieldInfo(field);
    if (!info) {
        return {};
    }
    const Engine::Project &p = project();
    const Engine::CustomFieldDef *d = Engine::customFieldDef(p, info->field);
    QVariantList lookup;
    for (const Engine::CustomLookupEntry &e : d != nullptr ? d->lookup : QList<Engine::CustomLookupEntry>()) {
        lookup.append(QVariantMap{{QStringLiteral("value"), shown(p, info->field, e.value, m_locale)},
                                  {QStringLiteral("description"), e.description}});
    }
    return {{QStringLiteral("field"), info->field},
            {QStringLiteral("kind"), Engine::customFieldKindName(info->kind)},
            {QStringLiteral("alias"), d != nullptr ? d->alias : QString()},
            {QStringLiteral("lookup"), lookup},
            {QStringLiteral("restrict"), d != nullptr && d->restrictToList},
            {QStringLiteral("formula"), d != nullptr ? d->formula : QString()}};
}

QVariantMap PlanService::saveCustomField(const QString &field, const QVariantMap &definition)
{
    const std::optional<Engine::CustomFieldInfo> info = Engine::customFieldInfo(field);
    if (!info) {
        return fail(QStringLiteral("There is no custom field %1.").arg(field));
    }
    const QLocale locale = m_locale;
    return apply(QStringLiteral("Custom Fields"), [&](Engine::Project &p) {
        Engine::EditResult r = Engine::setCustomFieldAlias(p, info->field, definition.value(QStringLiteral("alias")).toString());
        if (!r.ok) {
            return r.error;
        }
        QList<Engine::CustomLookupEntry> entries;
        for (const QVariant &v : definition.value(QStringLiteral("lookup")).toList()) {
            const QVariantMap e = v.toMap();
            const QString text = e.value(QStringLiteral("value")).toString().trimmed();
            if (text.isEmpty()) {
                continue;
            }
            const QString wire = stored(p, info->field, text, locale);
            if (wire.isEmpty()) {
                return QStringLiteral("The lookup value “%1” is not a %2.")
                    .arg(text, Engine::customFieldKindName(info->kind).toLower());
            }
            entries.append({wire, e.value(QStringLiteral("description")).toString().trimmed()});
        }
        r = Engine::setCustomFieldLookup(p, info->field, entries, definition.value(QStringLiteral("restrict")).toBool());
        if (!r.ok) {
            return r.error;
        }
        return errorOf(Engine::setCustomFieldFormula(p, info->field, definition.value(QStringLiteral("formula")).toString()));
    });
}

QVariantList PlanService::usedCustomFields() const
{
    QVariantList out;
    for (const QString &field : Engine::customFieldsInUse(project())) {
        out.append(QVariantMap{{QStringLiteral("field"), field},
                               {QStringLiteral("title"), Engine::customFieldTitle(project(), field)}});
    }
    return out;
}

QVariantList PlanService::taskCustomValues(const QString &taskId) const
{
    QVariantList out;
    const int row = rowOf(taskId);
    if (row < 0) {
        return out;
    }
    const Engine::Project &p = project();
    for (const QString &field : Engine::customFieldsInUse(p)) {
        const Engine::CustomFieldDef *d = Engine::customFieldDef(p, field);
        QVariantList choices;
        for (const Engine::CustomLookupEntry &e : d != nullptr ? d->lookup : QList<Engine::CustomLookupEntry>()) {
            choices.append(shown(p, field, e.value, m_locale));
        }
        out.append(QVariantMap{{QStringLiteral("field"), field},
                               {QStringLiteral("title"), Engine::customFieldTitle(p, field)},
                               {QStringLiteral("value"), Engine::customFieldEditText(p, row, field, m_locale)},
                               {QStringLiteral("choices"), choices},
                               {QStringLiteral("editable"), d == nullptr || d->formula.isEmpty()}});
    }
    return out;
}

QVariantMap PlanService::applyCustomFilter(const QString &field, const QString &test, const QString &value)
{
    static const QStringList tests{QStringLiteral("equals"),  QStringLiteral("notEquals"), QStringLiteral("contains"),
                                   QStringLiteral("greater"), QStringLiteral("less"),      QStringLiteral("isSet"),
                                   QStringLiteral("notSet")};
    if (!Engine::isCustomField(field) || !tests.contains(test)) {
        return fail(QStringLiteral("Choose a custom field and a test."));
    }
    Engine::TaskFilter filter;
    filter.id = QStringLiteral("custom");
    filter.field = field;
    filter.test = test;
    if (test != QLatin1String("isSet") && test != QLatin1String("notSet")) {
        const std::optional<QVariant> v = test == QLatin1String("contains")
                                              ? std::optional<QVariant>(QVariant(value.trimmed()))
                                              : Engine::parseCustomFieldValue(project(), field, value, m_locale);
        if (!v) {
            return fail(QStringLiteral("“%1” is not a value of %2.").arg(value.trimmed(), Engine::customFieldTitle(project(), field)));
        }
        filter.value = *v;
    }
    m_filter = filter;
    rebuild();
    emit viewChanged();
    return ok({{QStringLiteral("rows"), int(m_visible.size())}});
}

QVariantMap PlanService::setCustomSort(const QString &field, bool descending)
{
    if (!Engine::isCustomField(field)) {
        return fail(QStringLiteral("Choose a custom field to sort by."));
    }
    m_sortKey = QStringLiteral("custom:") + field + (descending ? QStringLiteral(":desc") : QString());
    rebuild();
    emit viewChanged();
    return ok();
}

} // namespace QindaOffice::Apps::Plan
