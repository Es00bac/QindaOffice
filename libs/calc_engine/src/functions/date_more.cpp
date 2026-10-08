// SPDX-License-Identifier: GPL-3.0-or-later
// Working-day arithmetic with a chosen weekend (M61, D-069):
// NETWORKDAYS.INTL and WORKDAY.INTL, and NETWORKDAYS/WORKDAY re-registered
// on the same core (the old NETWORKDAYS counted a backwards span as
// positive; Excel's answer is negative).
//
// The weekend argument is Excel's: a code 1-7 for two-day weekends
// (1 = Saturday+Sunday … 7 = Friday+Saturday), 11-17 for one day
// (11 = Sunday … 17 = Saturday), or seven 0/1 characters Monday first
// ("0000011" = Saturday+Sunday).

#include "functions.h"

#include "../eval_array.h"

#include <QSet>

#include <array>
#include <cmath>

namespace QindaOffice::Calc {

namespace {

using Weekend = std::array<bool, 7>; // Monday..Sunday, true = not a working day

// `allDays`: a weekend of all seven days is a valid question for
// NETWORKDAYS.INTL (no working days: 0) but has no answer for WORKDAY.INTL.
Value weekendFrom(const QVector<Arg> &args, int index, bool allDays, Weekend *out)
{
    out->fill(false);
    if (index >= args.size()) {
        (*out)[5] = (*out)[6] = true;
        return {};
    }
    const Value v = scalarOrValue(args.at(index));
    if (isErr(v)) return v;
    if (isText(v)) {
        const QString mask = std::get<QString>(v);
        if (mask.size() != 7) return Err::Value;
        for (int day = 0; day < 7; ++day) {
            if (mask.at(day) != QLatin1Char('0') && mask.at(day) != QLatin1Char('1')) return Err::Value;
            (*out)[day] = mask.at(day) == QLatin1Char('1');
        }
        return !allDays && mask == QLatin1String("1111111") ? Value(Err::Value) : Value();
    }
    const auto number = toNumber(v);
    if (!number) return Err::Value;
    const int code = isBlank(v) ? 1 : int(*number);
    if (code >= 1 && code <= 7) {
        (*out)[(code + 4) % 7] = true; // 1 → Saturday (index 5)
        (*out)[(code + 5) % 7] = true; // 1 → Sunday (index 6)
        return {};
    }
    if (code >= 11 && code <= 17) {
        (*out)[(code - 5) % 7] = true; // 11 → Sunday (6), 12 → Monday (0)
        return {};
    }
    return Err::Num;
}

Value holidaysFrom(const EvalContext &ctx, const QVector<Arg> &args, int index, QSet<qint64> *out)
{
    if (index >= args.size()) return {};
    QVector<Value> values;
    if (const Value err = flatArgValues(ctx, args.at(index), &values); isErr(err)) return err;
    for (const Value &v : std::as_const(values)) {
        if (isBlank(v)) continue;
        if (isText(v)) return Err::Value;
        if (const auto d = toNumber(v)) out->insert(qint64(std::floor(*d)));
    }
    return {};
}

bool workingDay(qint64 serial, const Weekend &weekend, const QSet<qint64> &holidays)
{
    const int dayOfWeek = dateFromSerial(double(serial)).dayOfWeek(); // 1 = Monday
    return !weekend.at(dayOfWeek - 1) && !holidays.contains(serial);
}

Value serialArg(const QVector<Arg> &args, int index, qint64 *out)
{
    const Value v = scalarOrValue(args.at(index));
    if (isErr(v)) return v;
    const auto d = toNumber(v);
    if (!d) return Err::Value;
    if (*d < 0.0) return Err::Num;
    *out = qint64(std::floor(*d));
    return {};
}

// (start, end, weekend at `weekendIndex` or none, holidays at the next).
FunctionSpec networkDays(int weekendIndex)
{
    const int maxArgs = weekendIndex < 0 ? 3 : 4;
    return FunctionSpec{2, maxArgs, false, [weekendIndex](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        qint64 start = 0;
        qint64 end = 0;
        if (const Value err = serialArg(args, 0, &start); isErr(err)) return err;
        if (const Value err = serialArg(args, 1, &end); isErr(err)) return err;
        Weekend weekend;
        if (const Value err = weekendFrom(args, weekendIndex < 0 ? 99 : weekendIndex, true, &weekend); isErr(err)) {
            return err;
        }
        QSet<qint64> holidays;
        const int holidayIndex = weekendIndex < 0 ? 2 : weekendIndex + 1;
        if (const Value err = holidaysFrom(ctx, args, holidayIndex, &holidays); isErr(err)) return err;
        const double sign = start <= end ? 1.0 : -1.0;
        double count = 0.0;
        for (qint64 day = qMin(start, end); day <= qMax(start, end); ++day) {
            if (workingDay(day, weekend, holidays)) count += 1.0;
        }
        return sign * count;
    }};
}

FunctionSpec workDay(int weekendIndex)
{
    const int maxArgs = weekendIndex < 0 ? 3 : 4;
    return FunctionSpec{2, maxArgs, false, [weekendIndex](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        qint64 start = 0;
        if (const Value err = serialArg(args, 0, &start); isErr(err)) return err;
        const Value daysValue = scalarOrValue(args.at(1));
        if (isErr(daysValue)) return daysValue;
        const auto days = toNumber(daysValue);
        if (!days) return Err::Value;
        Weekend weekend;
        if (const Value err = weekendFrom(args, weekendIndex < 0 ? 99 : weekendIndex, false, &weekend); isErr(err)) {
            return err;
        }
        QSet<qint64> holidays;
        const int holidayIndex = weekendIndex < 0 ? 2 : weekendIndex + 1;
        if (const Value err = holidaysFrom(ctx, args, holidayIndex, &holidays); isErr(err)) return err;
        const auto wanted = qint64(std::trunc(std::abs(*days)));
        const qint64 step = *days < 0 ? -1 : 1;
        qint64 day = start;
        for (qint64 found = 0; found < wanted;) {
            day += step;
            if (day < 0) return Err::Num;
            if (workingDay(day, weekend, holidays)) ++found;
        }
        return double(day);
    }};
}

} // namespace

void registerDateMoreFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("NETWORKDAYS"), networkDays(-1));
    r.insert(QStringLiteral("NETWORKDAYS.INTL"), networkDays(2));
    r.insert(QStringLiteral("WORKDAY"), workDay(-1));
    r.insert(QStringLiteral("WORKDAY.INTL"), workDay(2));
}

} // namespace QindaOffice::Calc
