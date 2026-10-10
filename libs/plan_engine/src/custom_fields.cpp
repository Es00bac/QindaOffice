// SPDX-License-Identifier: GPL-3.0-or-later
// Project's task custom fields: values and definitions (custom_fields.h;
// the catalog is custom_field_catalog.cpp). Values are stored in Microsoft
// Project XML's spelling, so this file converts with the MSPDI wire helpers
// (mspdi_p.h).
#include "custom_fields.h"

#include "mspdi_p.h"
#include "plan_time.h"

#include <cmath>

namespace QindaOffice::Plan {

namespace {

// "12.5", "3", "-0.25": a decimal without exponent or trailing zeros.
QString decimalWire(double value)
{
    QString s = QString::number(value, 'f', 6);
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

std::optional<double> parseNumber(const QString &text, const QLocale &locale)
{
    bool ok = false;
    double v = locale.toDouble(text.trimmed(), &ok);
    if (!ok) {
        v = QLocale::c().toDouble(text.trimmed(), &ok);
    }
    return ok && std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
}

// A Duration value: minutes and Project's format code.
struct WireDuration {
    Minutes minutes = 0;
    int format = 7;
};
WireDuration splitDuration(const QString &wire)
{
    const qsizetype bar = wire.indexOf(QLatin1Char('|'));
    WireDuration d;
    d.minutes = Mspdi::parseWireDuration(bar < 0 ? wire : wire.left(bar)).value_or(0);
    d.format = bar < 0 ? 7 : wire.mid(bar + 1).toInt();
    return d;
}

bool isDateKind(CustomFieldKind kind)
{
    return kind == CustomFieldKind::Date || kind == CustomFieldKind::Start || kind == CustomFieldKind::Finish;
}

// The wire value of a formula's result for a field of `kind`.
QString wireOf(const Project &project, CustomFieldKind kind, const QVariant &value)
{
    const auto number = [&] { return value.toDouble(); };
    switch (kind) {
    case CustomFieldKind::Text: return value.toString();
    case CustomFieldKind::Number: return decimalWire(number());
    case CustomFieldKind::Flag: return value.toBool() || number() != 0 ? QStringLiteral("1") : QStringLiteral("0");
    case CustomFieldKind::Cost: {
        const int decimals = currencyDecimals(project.info.currency);
        const qint64 minor = std::llround(number() * std::pow(10.0, decimals));
        return QString::number(Mspdi::minorToCents(minor, decimals));
    }
    case CustomFieldKind::Duration: return Mspdi::wireDuration(std::llround(number())) + QStringLiteral("|7");
    case CustomFieldKind::Date:
    case CustomFieldKind::Start:
    case CustomFieldKind::Finish:
        // Formula dates are days since 30 Dec 1899 (25569 = 1 Jan 1970).
        return Mspdi::wireDateTime(std::llround((number() - 25569.0) * 1440.0));
    }
    return {};
}

CustomFieldDef &defFor(Project &project, const QString &field)
{
    for (CustomFieldDef &d : project.customFields) {
        if (d.field == field) {
            return d;
        }
    }
    CustomFieldDef d;
    d.field = field;
    project.customFields.append(d);
    return project.customFields.last();
}

void dropIfEmpty(Project &project, const QString &field)
{
    project.customFields.removeIf([&](const CustomFieldDef &d) {
        // A definition read from a file keeps its kept parts (MSPDI).
        return d.field == field && d.alias.isEmpty() && d.lookup.isEmpty() && d.formula.isEmpty() && d.extra.isEmpty();
    });
}

} // namespace

const CustomFieldDef *customFieldDef(const Project &project, const QString &field)
{
    for (const CustomFieldDef &d : project.customFields) {
        if (d.field == field) {
            return &d;
        }
    }
    return nullptr;
}

QString customFieldTitle(const Project &project, const QString &field)
{
    const CustomFieldDef *d = customFieldDef(project, field);
    return d != nullptr && !d->alias.isEmpty() ? d->alias : field;
}

QStringList customFieldsInUse(const Project &project)
{
    QStringList out;
    for (const CustomFieldInfo &info : customFieldCatalog()) {
        const CustomFieldDef *d = customFieldDef(project, info.field);
        bool used = d != nullptr && (!d->alias.isEmpty() || !d->lookup.isEmpty() || !d->formula.isEmpty());
        for (int i = 0; i < project.tasks.size() && !used; ++i) {
            used = project.tasks.at(i).custom.contains(info.field);
        }
        if (used) {
            out.append(info.field);
        }
    }
    return out;
}

QString customFieldWireValue(const Project &project, int index, const QString &field)
{
    const std::optional<CustomFieldInfo> info = customFieldInfo(field);
    if (!info || index < 0 || index >= project.tasks.size()) {
        return {};
    }
    const CustomFieldDef *d = customFieldDef(project, info->field);
    if (d != nullptr && !d->formula.isEmpty()) {
        const FormulaResult r = evaluateFormula(project, index, d->formula);
        return r.ok ? wireOf(project, info->kind, r.value) : QString();
    }
    return project.tasks.at(index).custom.value(info->field);
}

std::optional<QVariant> customFieldValue(const Project &project, int index, const QString &field)
{
    const std::optional<CustomFieldInfo> info = customFieldInfo(field);
    if (!info || index < 0 || index >= project.tasks.size()) {
        return std::nullopt;
    }
    const QString wire = customFieldWireValue(project, index, field);
    if (wire.isEmpty()) {
        // As in Project, an unset number, cost, flag or duration is 0 (No);
        // unset text and dates (NA) and a formula's error have no value.
        const CustomFieldDef *d = customFieldDef(project, info->field);
        const bool failed = d != nullptr && !d->formula.isEmpty();
        return info->kind == CustomFieldKind::Text || isDateKind(info->kind) || failed ? std::nullopt
                                                                                        : std::optional<QVariant>(0.0);
    }
    switch (info->kind) {
    case CustomFieldKind::Text: return QVariant(wire);
    case CustomFieldKind::Number: return QVariant(QLocale::c().toDouble(wire));
    case CustomFieldKind::Flag: return QVariant(wire == QLatin1String("1") ? 1.0 : 0.0);
    case CustomFieldKind::Cost:
        return QVariant(double(Mspdi::centsToMinor(Mspdi::scaledDecimal(wire, 0).value_or(0),
                                                   currencyDecimals(project.info.currency))));
    case CustomFieldKind::Duration: return QVariant(double(splitDuration(wire).minutes));
    case CustomFieldKind::Date:
    case CustomFieldKind::Start:
    case CustomFieldKind::Finish: {
        const Minutes m = Mspdi::parseWireDateTime(wire);
        return m == kNoDate ? std::nullopt : std::optional<QVariant>(QVariant(double(m)));
    }
    }
    return std::nullopt;
}

std::optional<QVariant> parseCustomFieldValue(const Project &project, const QString &field, const QString &text,
                                              const QLocale &locale)
{
    // Typed into a scratch task of a project with the same currency and
    // hours, without the field's formula or list restriction.
    Project scratch;
    scratch.info = project.info;
    scratch.tasks.append(Task{});
    const EditResult r = setCustomField(scratch, 0, field, text, locale);
    return r.ok ? customFieldValue(scratch, 0, field) : std::nullopt;
}

QString customFieldText(const Project &project, int index, const QString &field, const QLocale &locale)
{
    const std::optional<CustomFieldInfo> info = customFieldInfo(field);
    if (!info || index < 0 || index >= project.tasks.size()) {
        return {};
    }
    const CustomFieldDef *d = customFieldDef(project, info->field);
    if (d != nullptr && !d->formula.isEmpty() && !evaluateFormula(project, index, d->formula).ok) {
        return QStringLiteral("#ERROR");
    }
    const QString wire = customFieldWireValue(project, index, field);
    switch (info->kind) {
    case CustomFieldKind::Text: return wire;
    case CustomFieldKind::Number: {
        QString s = locale.toString(QLocale::c().toDouble(wire), 'f', 6);
        const QString dot = locale.decimalPoint();
        if (s.contains(dot)) {
            while (s.endsWith(QLatin1Char('0'))) {
                s.chop(1);
            }
            if (s.endsWith(dot)) {
                s.chop(dot.size());
            }
        }
        return wire.isEmpty() ? locale.toString(0) : s;
    }
    case CustomFieldKind::Flag: return wire == QLatin1String("1") ? QStringLiteral("Yes") : QStringLiteral("No");
    case CustomFieldKind::Cost: {
        const qint64 cents = Mspdi::scaledDecimal(wire, 0).value_or(0);
        return formatMoney(Mspdi::centsToMinor(cents, currencyDecimals(project.info.currency)), project.info.currency,
                           locale);
    }
    case CustomFieldKind::Duration: {
        const WireDuration dur = splitDuration(wire);
        const Mspdi::DurationCode code = Mspdi::durationCode(dur.format);
        return formatDuration(dur.minutes, code.known && !code.percent ? code.unit : DurationUnit::Days,
                              durationFormatOf(project.info), code.estimated, locale, code.elapsed);
    }
    case CustomFieldKind::Date:
    case CustomFieldKind::Start:
    case CustomFieldKind::Finish: {
        const Minutes m = Mspdi::parseWireDateTime(wire);
        return m == kNoDate ? QStringLiteral("NA") : formatDate(m, locale);
    }
    }
    return {};
}

QString customFieldEditText(const Project &project, int index, const QString &field, const QLocale &locale)
{
    const std::optional<CustomFieldInfo> info = customFieldInfo(field);
    if (info && isDateKind(info->kind)) {
        const Minutes m = Mspdi::parseWireDateTime(customFieldWireValue(project, index, field));
        if (m == kNoDate) {
            return {};
        }
        const int standard = info->kind == CustomFieldKind::Finish ? project.info.defaultFinishMinute
                                                                   : project.info.defaultStartMinute;
        return minuteOfDay(m) == standard ? formatDate(m, locale) : formatDateTime(m, locale);
    }
    if (info && info->kind == CustomFieldKind::Number && customFieldWireValue(project, index, field).isEmpty()) {
        return {};
    }
    return customFieldText(project, index, field, locale);
}

EditResult setCustomField(Project &project, int index, const QString &field, const QString &text, const QLocale &locale)
{
    const std::optional<CustomFieldInfo> info = customFieldInfo(field);
    if (!info || index < 0 || index >= project.tasks.size()) {
        return EditResult::fail(QStringLiteral("There is no such task or field."));
    }
    const CustomFieldDef *d = customFieldDef(project, info->field);
    const QString title = customFieldTitle(project, info->field);
    if (d != nullptr && !d->formula.isEmpty()) {
        return EditResult::fail(QStringLiteral("%1 is calculated by its formula.").arg(title));
    }
    const QString t = text.trimmed();
    Task &task = project.tasks[index];
    if (t.isEmpty() || (isDateKind(info->kind) && t.compare(QLatin1String("NA"), Qt::CaseInsensitive) == 0)) {
        task.custom.remove(info->field);
        return {};
    }
    QString wire;
    switch (info->kind) {
    case CustomFieldKind::Text: wire = text; break;
    case CustomFieldKind::Number: {
        const std::optional<double> v = parseNumber(t, locale);
        if (!v) {
            return EditResult::fail(QStringLiteral("%1 is a number.").arg(title));
        }
        wire = decimalWire(*v);
        break;
    }
    case CustomFieldKind::Flag: {
        const QString l = t.toLower();
        if (l == QLatin1String("yes") || l == QLatin1String("y") || l == QLatin1String("true") || l == QLatin1String("1")) {
            wire = QStringLiteral("1");
        } else if (l == QLatin1String("no") || l == QLatin1String("n") || l == QLatin1String("false") || l == QLatin1String("0")) {
            wire = QStringLiteral("0");
        } else {
            return EditResult::fail(QStringLiteral("%1 is Yes or No.").arg(title));
        }
        break;
    }
    case CustomFieldKind::Cost: {
        const std::optional<qint64> money = parseMoney(t, project.info.currency, locale);
        if (!money) {
            return EditResult::fail(QStringLiteral("“%1” is not an amount.").arg(t));
        }
        wire = QString::number(Mspdi::minorToCents(*money, currencyDecimals(project.info.currency)));
        break;
    }
    case CustomFieldKind::Duration: {
        const std::optional<ParsedDuration> dur = parseDuration(t, durationFormatOf(project.info), true);
        if (!dur) {
            return EditResult::fail(QStringLiteral("“%1” is not a duration. Try 3d, 4h or 2ed.").arg(t));
        }
        wire = Mspdi::wireDuration(dur->minutes) + QLatin1Char('|')
            + QString::number(Mspdi::durationFormat(dur->unit, dur->estimated, dur->elapsed));
        break;
    }
    case CustomFieldKind::Date:
    case CustomFieldKind::Start:
    case CustomFieldKind::Finish: {
        const int minute = info->kind == CustomFieldKind::Finish ? project.info.defaultFinishMinute
                                                                 : project.info.defaultStartMinute;
        const std::optional<Minutes> m = parseDate(t, minute, locale);
        if (!m) {
            return EditResult::fail(QStringLiteral("“%1” is not a date.").arg(t));
        }
        wire = Mspdi::wireDateTime(*m);
        break;
    }
    }
    if (d != nullptr && d->restrictToList && !d->lookup.isEmpty()) {
        const bool listed = std::any_of(d->lookup.cbegin(), d->lookup.cend(),
                                        [&](const CustomLookupEntry &e) { return e.value == wire; });
        if (!listed) {
            return EditResult::fail(QStringLiteral("%1 only takes a value from its lookup table.").arg(title));
        }
    }
    // 0, No, $0 and 0 days are what an untouched field already is: stored
    // as unset, so a task carries (and Project XML writes) only real values.
    const bool fallback = (info->kind == CustomFieldKind::Duration && splitDuration(wire).minutes == 0)
                       || ((info->kind == CustomFieldKind::Number || info->kind == CustomFieldKind::Flag
                            || info->kind == CustomFieldKind::Cost) && wire == QLatin1String("0"));
    if (fallback) {
        task.custom.remove(info->field);
    } else {
        task.custom.insert(info->field, wire);
    }
    return {};
}

EditResult setCustomFieldAlias(Project &project, const QString &field, const QString &alias)
{
    const std::optional<CustomFieldInfo> info = customFieldInfo(field);
    if (!info) {
        return EditResult::fail(QStringLiteral("There is no custom field %1.").arg(field));
    }
    const QString a = alias.trimmed();
    if (a.size() > 50) {
        return EditResult::fail(QStringLiteral("A field's new name is at most 50 characters, as in Project."));
    }
    for (const CustomFieldDef &other : std::as_const(project.customFields)) {
        if (!a.isEmpty() && other.field != info->field && other.alias.compare(a, Qt::CaseInsensitive) == 0) {
            return EditResult::fail(QStringLiteral("%1 is already called “%2”.").arg(other.field, a));
        }
    }
    defFor(project, info->field).alias = a;
    dropIfEmpty(project, info->field);
    return {};
}

EditResult setCustomFieldLookup(Project &project, const QString &field, const QList<CustomLookupEntry> &entries,
                                bool restrictToList)
{
    const std::optional<CustomFieldInfo> info = customFieldInfo(field);
    if (!info) {
        return EditResult::fail(QStringLiteral("There is no custom field %1.").arg(field));
    }
    if (info->kind == CustomFieldKind::Flag) {
        return EditResult::fail(QStringLiteral("A flag is Yes or No; it has no lookup table."));
    }
    QStringList seen;
    for (const CustomLookupEntry &e : entries) {
        if (e.value.isEmpty() || seen.contains(e.value)) {
            return EditResult::fail(QStringLiteral("Every lookup value must be filled in and different."));
        }
        seen.append(e.value);
    }
    CustomFieldDef &d = defFor(project, info->field);
    d.lookup = entries;
    d.restrictToList = restrictToList && !entries.isEmpty();
    dropIfEmpty(project, info->field);
    return {};
}

EditResult setCustomFieldFormula(Project &project, const QString &field, const QString &formula)
{
    const std::optional<CustomFieldInfo> info = customFieldInfo(field);
    if (!info) {
        return EditResult::fail(QStringLiteral("There is no custom field %1.").arg(field));
    }
    const QString f = formula.trimmed();
    if (!f.isEmpty()) {
        const QString problem = formulaProblem(project, f);
        if (!problem.isEmpty()) {
            return EditResult::fail(problem);
        }
    }
    const CustomFieldDef *old = customFieldDef(project, info->field);
    if (f.isEmpty() && old != nullptr && !old->formula.isEmpty()) {
        // Project keeps what the formula showed as the field's values.
        for (int i = 0; i < project.tasks.size(); ++i) {
            const QString wire = customFieldWireValue(project, i, info->field);
            if (!wire.isEmpty()) {
                project.tasks[i].custom.insert(info->field, wire);
            }
        }
    }
    if (!f.isEmpty()) {
        for (Task &t : project.tasks) {
            t.custom.remove(info->field); // the formula decides from now on
        }
    }
    defFor(project, info->field).formula = f;
    dropIfEmpty(project, info->field);
    return {};
}

} // namespace QindaOffice::Plan
