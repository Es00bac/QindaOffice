// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include <QSet>

#include <QDateTime>

namespace QindaOffice::Calc {

namespace {

int isoWeekNumber(const QDate &date)
{
    // ISO 8601: week 1 contains the year's first Thursday. A year has 53
    // ISO weeks iff Jan 1 is a Thursday, or a Wednesday in a leap year.
    const int dow = date.dayOfWeek();
    const int week = (date.dayOfYear() - dow + 10) / 7;
    const auto yearHas53 = [](int year) {
        const int jan1 = QDate(year, 1, 1).dayOfWeek();
        return jan1 == 4 || (QDate::isLeapYear(year) && jan1 == 3);
    };
    if (week < 1) {
        return yearHas53(date.year() - 1) ? 53 : 52;
    }
    if (week > 52) {
        return yearHas53(date.year()) ? 53 : 1;
    }
    return week;
}

} // namespace

void registerDateFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("TODAY"), FunctionSpec{0, 0, true, [](EvalContext &, const QVector<Arg> &) {
                  return serialFromDate(QDate::currentDate());
              }});
    r.insert(QStringLiteral("NOW"), FunctionSpec{0, 0, true, [](EvalContext &, const QVector<Arg> &) {
                  const QDateTime now = QDateTime::currentDateTime();
                  return serialFromDate(now.date())
                      + (now.time().msecsSinceStartOfDay() / 86400000.0);
              }});
    r.insert(QStringLiteral("DATE"),
             FunctionSpec{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto y = scalarNumber(args.at(0));
                              const auto m = scalarNumber(args.at(1));
                              const auto d = scalarNumber(args.at(2));
                              if (!y || !m || !d) return Err::Value;
                              int year = int(*y);
                              int month = int(*m);
                              int day = int(*d);
                              // Excel normalizes overflow months/days.
                              year += std::floor(double(month - 1) / 12.0);
                              month = ((month - 1) % 12 + 12) % 12 + 1;
                              const QDate base(year, month, 1);
                              const QDate date = base.addDays(day - 1);
                              return date.isValid() ? Value(serialFromDate(date)) : Value(Err::Num);
                          }});
    r.insert(QStringLiteral("TIME"),
             FunctionSpec{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto h = scalarNumber(args.at(0));
                              const auto m = scalarNumber(args.at(1));
                              const auto s = scalarNumber(args.at(2));
                              if (!h || !m || !s) return Err::Value;
                              const double total = (*h * 3600.0 + *m * 60.0 + *s) / 86400.0;
                              return total - std::floor(total);
                          }});
    const auto part = [](char which) {
        return FunctionSpec{1, 1, false,
                            [which](EvalContext &, const QVector<Arg> &args) -> Value {
                                const auto serial = scalarNumber(args.at(0));
                                if (!serial) return Err::Value;
                                const QDate date = dateFromSerial(*serial);
                                if (!date.isValid()) return Err::Value;
                                switch (which) {
                                case 'y': return double(date.year());
                                case 'm': return double(date.month());
                                case 'd': return double(date.day());
                                case 'h': return double(qint64(*serial * 24.0) % 24);
                                case 'n': return double(qint64(*serial * 24.0 * 60.0) % 60);
                                case 's': return double(qint64(*serial * 24.0 * 3600.0) % 60);
                                }
                                return Err::Value;
                            }};
    };
    r.insert(QStringLiteral("YEAR"), part('y'));
    r.insert(QStringLiteral("MONTH"), part('m'));
    r.insert(QStringLiteral("DAY"), part('d'));
    r.insert(QStringLiteral("HOUR"), part('h'));
    r.insert(QStringLiteral("MINUTE"), part('n'));
    r.insert(QStringLiteral("SECOND"), part('s'));
    r.insert(QStringLiteral("WEEKDAY"),
             FunctionSpec{1, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto serial = scalarNumber(args.at(0));
                              if (!serial) return Err::Value;
                              int type = 1;
                              if (args.size() > 1) {
                                  const auto t = scalarNumber(args.at(1));
                                  if (t) type = int(*t);
                              }
                              const int day = dateFromSerial(*serial).dayOfWeek(); // 1=Mon
                              if (type == 1) return double(day % 7 + 1);   // 1=Sun..7=Sat
                              if (type == 2) return double(day);           // 1=Mon..7=Sun
                              if (type == 3) return double((day + 6) % 7); // 0=Mon..6=Sun
                              // Types 11-17 are 1-based weekday numbers with different start days
                              if (type >= 11 && type <= 17) {
                                  const int startDow = type - 10; // 11=Mon..17=Sun
                                  return double((day - startDow + 7) % 7 + 1);
                              }
                              return Err::Value;
                          }});
    r.insert(QStringLiteral("WEEKNUM"),
             FunctionSpec{1, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto serial = scalarNumber(args.at(0));
                              if (!serial) return Err::Value;
                              int type = 1;
                              if (args.size() > 1) {
                                  const auto t = scalarNumber(args.at(1));
                                  if (t) type = int(*t);
                              }
                              const QDate date = dateFromSerial(*serial);
                              if (type == 21) {
                                  return Value(double(isoWeekNumber(date)));
                              }
                              int startDow;
                              if (type == 1) {
                                  startDow = 7; // Sunday
                              } else if (type >= 11 && type <= 17) {
                                  startDow = type - 10; // 11=Mon(1), ..., 17=Sun(7)
                              } else {
                                  startDow = 1; // Monday (type 2 and default)
                              }
                              const QDate jan1(date.year(), 1, 1);
                              const int offset = (jan1.dayOfWeek() - startDow + 7) % 7;
                              const int week = (jan1.daysTo(date) + offset) / 7 + 1;
                              return double(week);
                          }});
    r.insert(QStringLiteral("DAYS"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto end = scalarNumber(args.at(0));
                              const auto start = scalarNumber(args.at(1));
                              if (!end || !start) return Err::Value;
                              return *end - *start;
                          }});
    r.insert(QStringLiteral("DATEDIF"),
             FunctionSpec{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto start = scalarNumber(args.at(0));
                              const auto end = scalarNumber(args.at(1));
                              const QString unit = toText(scalarOrValue(args.at(2))).toUpper();
                              if (!start || !end) return Err::Value;
                              if (*end < *start) return Err::Num;
                              const QDate a = dateFromSerial(*start);
                              const QDate b = dateFromSerial(*end);
                              if (unit == QLatin1String("Y")) {
                                  int years = b.year() - a.year();
                                  if (b < a.addYears(years)) --years;
                                  return double(years);
                              }
                              if (unit == QLatin1String("M")) {
                                  int months = (b.year() - a.year()) * 12 + (b.month() - a.month());
                                  if (b.day() < a.day()) --months;
                                  return double(months);
                              }
                              if (unit == QLatin1String("D")) {
                                  return double(a.daysTo(b));
                              }
                              return Err::Value;
                          }});
    r.insert(QStringLiteral("EDATE"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto serial = scalarNumber(args.at(0));
                              const auto months = scalarNumber(args.at(1));
                              if (!serial || !months) return Err::Value;
                              const QDate date = dateFromSerial(*serial).addMonths(int(*months));
                              return date.isValid() ? Value(serialFromDate(date)) : Value(Err::Num);
                          }});
    r.insert(QStringLiteral("EOMONTH"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto serial = scalarNumber(args.at(0));
                              const auto months = scalarNumber(args.at(1));
                              if (!serial || !months) return Err::Value;
                              const QDate first = dateFromSerial(*serial).addMonths(int(*months));
                              if (!first.isValid()) return Err::Num;
                              const QDate end = QDate(first.year(), first.month(), 1)
                                                    .addMonths(1)
                                                    .addDays(-1);
                              return serialFromDate(end);
                          }});
    r.insert(QStringLiteral("DATEVALUE"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString s = toText(scalarOrValue(args.at(0))).trimmed();
                              static const QList<QString> kPatterns = {
                                  QStringLiteral("yyyy-MM-dd"), QStringLiteral("yyyy/MM/dd"),
                                  QStringLiteral("dd.MM.yyyy"), QStringLiteral("d.M.yyyy"),
                                  QStringLiteral("MM/dd/yyyy"), QStringLiteral("dd/MM/yyyy"),
                                  QStringLiteral("d. MMMM yyyy"), QStringLiteral("MMMM d, yyyy"),
                              };
                              for (const QString &pattern : kPatterns) {
                                  const QDate date = QDate::fromString(s, pattern);
                                  if (date.isValid()) {
                                      return serialFromDate(date);
                                  }
                              }
                              return Err::Value;
                          }});
    r.insert(QStringLiteral("TIMEVALUE"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString s = toText(scalarOrValue(args.at(0))).trimmed();
                              static const QList<QString> kPatterns = {
                                  QStringLiteral("hh:mm"), QStringLiteral("hh:mm:ss"),
                                  QStringLiteral("h:mm AM"), QStringLiteral("h:mm:ss AP"),
                                  QStringLiteral("h:mm:ss"),
                              };
                              for (const QString &pattern : kPatterns) {
                                  const QTime time = QTime::fromString(s, pattern);
                                  if (time.isValid()) {
                                      return time.msecsSinceStartOfDay() / 86400000.0;
                                  }
                              }
                              return Err::Value;
                          }});
}

void registerDateExtraFunctions(QHash<QString, FunctionSpec> &r)
{
    // DAYS360 (US 30/360): both dates shifted per the NASD convention.
    r.insert(QStringLiteral("DAYS360"),
             FunctionSpec{2, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto s = scalarNumber(args.at(0));
                              const auto e = scalarNumber(args.at(1));
                              if (!s || !e) return Err::Value;
                              QDate start = dateFromSerial(*s);
                              QDate end = dateFromSerial(*e);
                              bool euMethod = false;
                              if (args.size() > 2) {
                                  const auto m = toBool(scalarOrValue(args.at(2)));
                                  if (!m) return Err::Value;
                                  euMethod = *m;
                              }
                              int d1 = start.day();
                              int d2 = end.day();
                              if (!euMethod) {
                                  // US/NASD method: last day of Feb -> 30
                                  if (d1 == 31 || (start.month() == 2 && d1 == start.daysInMonth())) {
                                      d1 = 30;
                                  }
                                  if (d2 == 31 && d1 >= 30) {
                                      d2 = 30;
                                  }
                                  // If both dates are last day of Feb, d2=30
                                  if (start.month() == 2 && d1 == start.daysInMonth()
                                      && end.month() == 2 && d2 == end.daysInMonth()) {
                                      d2 = 30;
                                  }
                              } else {
                                  d1 = qMin(d1, 30);
                                  d2 = qMin(d2, 30);
                              }
                              return double((end.year() - start.year()) * 360
                                            + (end.month() - start.month()) * 30 + (d2 - d1));
                          }});
    r.insert(QStringLiteral("ISOWEEKNUM"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto s = scalarNumber(args.at(0));
                              if (!s) return Err::Value;
                              return double(dateFromSerial(*s).weekNumber());
                          }});
    r.insert(QStringLiteral("YEARFRAC"),
             FunctionSpec{2, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto s = scalarNumber(args.at(0));
                              const auto e = scalarNumber(args.at(1));
                              if (!s || !e) return Err::Value;
                              int basis = 0;
                              if (args.size() > 2) {
                                  const auto b = scalarNumber(args.at(2));
                                  if (!b) return Err::Value;
                                  basis = int(*b);
                                  if (basis < 0 || basis > 4) return Err::Num;
                              }
                              QDate start = dateFromSerial(*s);
                              QDate end = dateFromSerial(*e);
                              // Excel swaps reversed dates and returns positive fraction
                              if (start > end) {
                                  std::swap(start, end);
                              }
                              if (basis == 0 || basis == 4) {
                                  // 30/360 US (basis 0) or 30E/360 (4).
                                  int d1 = start.day();
                                  int d2 = end.day();
                                  if (basis == 0) {
                                      // US method: last day of Feb -> 30
                                      if (d1 == 31) d1 = 30;
                                      if (start.month() == 2 && d1 == start.daysInMonth()) d1 = 30;
                                      if (d2 == 31 && d1 == 30) d2 = 30;
                                      if (end.month() == 2 && d2 == end.daysInMonth() && d1 == 30) d2 = 30;
                                  } else {
                                      d1 = qMin(d1, 30);
                                      d2 = qMin(d2, 30);
                                  }
                                  return double((end.year() - start.year()) * 360
                                                + (end.month() - start.month()) * 30
                                                + (d2 - d1))
                                      / 360.0;
                              }
                              if (basis == 1) {
                                  // Actual/actual: the year count is 366
                                  // when the interval contains Feb 29.
                                  double years = 0;
                                  QDate cursor = start;
                                  while (cursor < end) {
                                      const int y = cursor.year();
                                      const QDate feb29(y, 2, 29);
                                      const QDate boundary = feb29.isValid() && feb29 > cursor
                                          ? feb29
                                          : QDate(y + 1, 1, 1);
                                      const QDate stop = qMin(boundary, end);
                                      years += double(stop.toJulianDay() - cursor.toJulianDay())
                                          / (feb29.isValid() && cursor <= feb29 && stop > feb29
                                                 || (feb29.isValid() && boundary == feb29
                                                     && feb29.isValid() && cursor.year() == y
                                                     && feb29 > cursor && stop >= feb29)
                                                 ? 366.0
                                                 : 365.0);
                                      cursor = stop;
                                  }
                                  return years;
                              }
                              if (basis == 2) {
                                  return (end.toJulianDay() - start.toJulianDay()) / 360.0;
                              }
                              return (end.toJulianDay() - start.toJulianDay()) / 365.0;
                          }});
    const auto holidaySet = [](EvalContext &ctx, const QVector<Arg> &args, int index) {
        QSet<qint64> holidays;
        if (args.size() > index
            && std::holds_alternative<RangeView>(args.at(index))) {
            QVector<Value> values;
            const Value err =
                sparseRangeValues(ctx, std::get<RangeView>(args.at(index)), &values);
            if (!isErr(err)) {
                for (const Value &v : values) {
                    if (const auto d = toNumber(v); d.has_value()) {
                        holidays.insert(qint64(std::floor(*d)));
                    }
                }
            }
        }
        return holidays;
    };
    r.insert(QStringLiteral("NETWORKDAYS"),
             FunctionSpec{2, 3, false, [holidaySet](EvalContext &ctx,
                                                    const QVector<Arg> &args) -> Value {
                              const auto s = scalarNumber(args.at(0));
                              const auto e = scalarNumber(args.at(1));
                              if (!s || !e) return Err::Value;
                              QDate start = dateFromSerial(*s);
                              QDate end = dateFromSerial(*e);
                              const QSet<qint64> holidays = holidaySet(ctx, args, 2);
                              int days = 0;
                              for (QDate d = qMin(start, end); d <= qMax(start, end);
                                   d = d.addDays(1)) {
                                  const int dow = d.dayOfWeek();
                                  if (dow >= 6) continue;
                                  if (holidays.contains(qint64(std::floor(
                                          serialFromDate(d)))))
                                      continue;
                                  ++days;
                              }
                              return double(days);
                          }});
    r.insert(QStringLiteral("WORKDAY"),
             FunctionSpec{2, 3, false, [holidaySet](EvalContext &ctx,
                                                    const QVector<Arg> &args) -> Value {
                              const auto s = scalarNumber(args.at(0));
                              const auto days = scalarNumber(args.at(1));
                              if (!s || !days) return Err::Value;
                              QDate date = dateFromSerial(*s);
                              const QSet<qint64> holidays = holidaySet(ctx, args, 2);
                              int remaining = int(*days);
                              int step = remaining < 0 ? -1 : 1;
                              remaining = std::abs(remaining);
                              while (remaining > 0) {
                                  date = date.addDays(step);
                                  const int dow = date.dayOfWeek();
                                  if (dow < 6
                                      && !holidays.contains(qint64(std::floor(
                                          serialFromDate(date))))) {
                                      --remaining;
                                  }
                              }
                              return serialFromDate(date);
                          }});
}

} // namespace QindaOffice::Calc
