// SPDX-License-Identifier: GPL-3.0-or-later
// The resource sheet's fields as text (plan_edit.h).
#include "plan_edit.h"
#include "plan_time.h"

#include <QRegularExpression>

namespace QindaOffice::Plan {

namespace {

struct FieldInfo {
    const char *id;
    const char *title;
    bool editable;
};

constexpr FieldInfo kResourceFields[] = {
    {"id", "ID", false},
    {"indicators", "Indicators", false},
    {"name", "Resource Name", true},
    {"type", "Type", true},
    {"materialLabel", "Material Label", true},
    {"initials", "Initials", true},
    {"group", "Group", true},
    {"maxUnits", "Max. Units", true},
    {"standardRate", "Std. Rate", true},
    {"costPerUse", "Cost/Use", true},
    {"calendar", "Base Calendar", false},
    {"email", "Email", true},
    {"work", "Work", false},
    {"cost", "Cost", false},
    {"overallocated", "Overallocated", false},
};

const FieldInfo *resourceField(const QString &id)
{
    for (const FieldInfo &f : kResourceFields) {
        if (id == QLatin1String(f.id)) {
            return &f;
        }
    }
    return nullptr;
}

QString initialsOf(const QString &name)
{
    QString out;
    for (const QString &word : name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        out.append(word.at(0).toUpper());
    }
    return out.left(3);
}

} // namespace

QStringList resourceFieldIds()
{
    QStringList out;
    for (const FieldInfo &f : kResourceFields) {
        out.append(QLatin1String(f.id));
    }
    return out;
}

bool isResourceFieldEditable(const QString &field)
{
    const FieldInfo *f = resourceField(field);
    return f != nullptr && f->editable;
}

QString resourceFieldTitle(const QString &field)
{
    const FieldInfo *f = resourceField(field);
    return f != nullptr ? QString::fromUtf8(f->title) : field;
}

QString rateText(const Project &project, const Resource &resource, const QLocale &locale)
{
    const QString money = formatMoney(resource.standardRate, project.info.currency, locale);
    return resource.type == ResourceType::Work ? money + QStringLiteral("/hr") : money;
}

QString resourceFieldText(const Project &project, int index, const QString &field, const QLocale &locale)
{
    if (index < 0 || index >= project.resources.size()) {
        return {};
    }
    const Resource &r = project.resources.at(index);
    if (field == QLatin1String("id")) {
        return QString::number(index + 1);
    }
    if (field == QLatin1String("indicators")) {
        return r.overallocated ? QStringLiteral("overallocated") : QString();
    }
    if (field == QLatin1String("name")) {
        return r.name;
    }
    if (field == QLatin1String("type")) {
        return r.type == ResourceType::Work ? QStringLiteral("Work") : QStringLiteral("Material");
    }
    if (field == QLatin1String("materialLabel")) {
        return r.materialLabel;
    }
    if (field == QLatin1String("initials")) {
        return r.initials;
    }
    if (field == QLatin1String("group")) {
        return r.group;
    }
    if (field == QLatin1String("maxUnits")) {
        return r.type == ResourceType::Work ? locale.toString(r.maxUnits) + QLatin1Char('%') : QString();
    }
    if (field == QLatin1String("standardRate")) {
        return rateText(project, r, locale);
    }
    if (field == QLatin1String("costPerUse")) {
        return formatMoney(r.costPerUse, project.info.currency, locale);
    }
    if (field == QLatin1String("calendar")) {
        const int c = project.calendarIndex(r.calendarUid >= 0 ? r.calendarUid : project.info.calendarUid);
        return r.type == ResourceType::Work && c >= 0 ? project.calendars.at(c).name : QString();
    }
    if (field == QLatin1String("email")) {
        return r.email;
    }
    if (field == QLatin1String("work")) {
        if (r.type == ResourceType::Material) {
            const QString q = locale.toString(double(r.work) / 100.0, 'g', 12);
            return r.materialLabel.isEmpty() ? q : q + QLatin1Char(' ') + r.materialLabel;
        }
        return formatWork(r.work, locale);
    }
    if (field == QLatin1String("cost")) {
        return formatMoney(r.cost, project.info.currency, locale);
    }
    if (field == QLatin1String("overallocated")) {
        return r.overallocated ? QStringLiteral("Yes") : QStringLiteral("No");
    }
    return {};
}

EditResult setResourceField(Project &project, int index, const QString &field, const QString &text, const QLocale &locale)
{
    if (index < 0 || index >= project.resources.size()) {
        return EditResult::fail(QStringLiteral("There is no such resource."));
    }
    if (!isResourceFieldEditable(field)) {
        return EditResult::fail(QStringLiteral("%1 is calculated and cannot be typed.").arg(resourceFieldTitle(field)));
    }
    Resource &r = project.resources[index];
    const QString t = text.trimmed();
    if (field == QLatin1String("name")) {
        if (t.isEmpty()) {
            return EditResult::fail(QStringLiteral("A resource needs a name."));
        }
        if (t.contains(QLatin1Char(',')) || t.contains(QLatin1Char('[')) || t.contains(QLatin1Char(']'))) {
            return EditResult::fail(QStringLiteral("A resource name cannot contain commas or square brackets."));
        }
        const int other = project.resourceIndexByName(t);
        if (other >= 0 && other != index) {
            return EditResult::fail(QStringLiteral("There is already a resource called “%1”.").arg(t));
        }
        const bool autoInitials = r.initials.isEmpty() || r.initials == initialsOf(r.name);
        r.name = t;
        if (autoInitials) {
            r.initials = initialsOf(t);
        }
        return {};
    }
    if (field == QLatin1String("type")) {
        if (t.compare(QLatin1String("Work"), Qt::CaseInsensitive) == 0) {
            r.type = ResourceType::Work;
        } else if (t.compare(QLatin1String("Material"), Qt::CaseInsensitive) == 0) {
            r.type = ResourceType::Material;
        } else {
            return EditResult::fail(QStringLiteral("Type is Work or Material."));
        }
        return {};
    }
    if (field == QLatin1String("materialLabel")) {
        r.materialLabel = t;
        return {};
    }
    if (field == QLatin1String("initials")) {
        r.initials = t;
        return {};
    }
    if (field == QLatin1String("group")) {
        r.group = t;
        return {};
    }
    if (field == QLatin1String("email")) {
        r.email = t;
        return {};
    }
    if (field == QLatin1String("maxUnits")) {
        static const QRegularExpression re(QStringLiteral("^(\\d{1,5})\\s*%?$"));
        const QRegularExpressionMatch m = re.match(t);
        const int units = m.hasMatch() ? m.captured(1).toInt() : -1;
        if (units <= 0 || units > 10000) {
            return EditResult::fail(QStringLiteral("Max. Units is a percentage, like 100% or 50%."));
        }
        r.maxUnits = units;
        return {};
    }
    if (field == QLatin1String("costPerUse")) {
        const std::optional<qint64> money = parseMoney(t, project.info.currency, locale);
        if (!money || *money < 0) {
            return EditResult::fail(QStringLiteral("“%1” is not an amount.").arg(t));
        }
        r.costPerUse = *money;
        return {};
    }
    if (field == QLatin1String("standardRate")) {
        // "$50/hr", "400/d", "2000/wk", "12.50".
        static const QRegularExpression re(QStringLiteral("^(.*?)\\s*(?:/\\s*([a-zA-Z]+))?$"));
        const QRegularExpressionMatch m = re.match(t);
        const std::optional<qint64> money = parseMoney(m.captured(1), project.info.currency, locale);
        if (!money || *money < 0) {
            return EditResult::fail(QStringLiteral("“%1” is not a rate. Try 50/hr or 400/d.").arg(t));
        }
        const QString unit = m.captured(2).toLower();
        qint64 perHour = *money;
        if (r.type == ResourceType::Work && !unit.isEmpty()) {
            if (unit == QLatin1String("d") || unit == QLatin1String("day")) {
                perHour = divRound(*money * 60, project.info.minutesPerDay);
            } else if (unit == QLatin1String("w") || unit == QLatin1String("wk") || unit == QLatin1String("week")) {
                perHour = divRound(*money * 60, project.info.minutesPerWeek);
            } else if (unit == QLatin1String("m") || unit == QLatin1String("min")) {
                perHour = *money * 60;
            } else if (unit != QLatin1String("h") && unit != QLatin1String("hr") && unit != QLatin1String("hour")) {
                return EditResult::fail(QStringLiteral("“/%1” is not a rate unit (use /hr, /d or /wk).").arg(unit));
            }
        }
        r.standardRate = perHour;
        return {};
    }
    return EditResult::fail(QStringLiteral("Unknown field %1.").arg(field));
}

} // namespace QindaOffice::Plan
