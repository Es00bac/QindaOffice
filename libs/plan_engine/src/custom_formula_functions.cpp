// SPDX-License-Identifier: GPL-3.0-or-later
// Project's custom-field formulas: the function library (custom_formula_p.h)
// — IIf, Switch, Choose, numbers, text, dates, Project's ProjDate… and
// ProjDur… functions and the pj… unit constants.
#include "custom_formula_p.h"

#include "plan_fields_p.h"
#include "plan_time.h"
#include "work_calendar.h"

#include <QDateTime>
#include <QHash>
#include <QLocale>

#include <cmath>

namespace QindaOffice::Plan::Formula {

const QStringList &functionNames()
{
    static const QStringList names{
        QStringLiteral("iif"), QStringLiteral("switch"), QStringLiteral("choose"), QStringLiteral("abs"),
        QStringLiteral("int"), QStringLiteral("fix"), QStringLiteral("round"), QStringLiteral("sgn"),
        QStringLiteral("sqr"), QStringLiteral("len"), QStringLiteral("left"), QStringLiteral("right"),
        QStringLiteral("mid"), QStringLiteral("ucase"), QStringLiteral("lcase"), QStringLiteral("trim"),
        QStringLiteral("ltrim"), QStringLiteral("rtrim"), QStringLiteral("instr"), QStringLiteral("str"),
        QStringLiteral("cstr"), QStringLiteral("val"), QStringLiteral("cdbl"), QStringLiteral("cint"),
        QStringLiteral("clng"), QStringLiteral("year"), QStringLiteral("month"), QStringLiteral("day"),
        QStringLiteral("weekday"), QStringLiteral("hour"), QStringLiteral("minute"), QStringLiteral("now"),
        QStringLiteral("date"), QStringLiteral("datediff"), QStringLiteral("dateadd"), QStringLiteral("projdatediff"),
        QStringLiteral("projdateadd"), QStringLiteral("projdatesub"), QStringLiteral("projdurconv"),
        QStringLiteral("projdatevalue"), QStringLiteral("projdurvalue"), QStringLiteral("true"), QStringLiteral("false"),
        QStringLiteral("pjminutes"), QStringLiteral("pjhours"), QStringLiteral("pjdays"), QStringLiteral("pjweeks"),
        QStringLiteral("pjmonths")};
    return names;
}

QVariant Evaluator::call(const NodePtr &n)
{
    const QString &f = n->text;
    const auto arg = [&](int k) { return k < n->args.size() ? eval(n->args.at(k)) : QVariant(); };
    const auto need = [&](int count) {
        if (n->args.size() < count) {
            fail(QStringLiteral("%1 needs %2 arguments.").arg(f).arg(count));
            return false;
        }
        return true;
    };
    if (f == QLatin1String("true") || f == QLatin1String("false")) {
        return f == QLatin1String("true");
    }
    static const QHash<QString, int> units{{QStringLiteral("pjminutes"), 3}, {QStringLiteral("pjhours"), 5},
                                           {QStringLiteral("pjdays"), 7}, {QStringLiteral("pjweeks"), 9},
                                           {QStringLiteral("pjmonths"), 11}};
    if (units.contains(f)) {
        return double(units.value(f));
    }
    if (f == QLatin1String("iif")) {
        return need(3) ? (truth(arg(0)) ? arg(1) : arg(2)) : QVariant();
    }
    if (f == QLatin1String("switch")) {
        for (int k = 0; k + 1 < n->args.size(); k += 2) {
            if (truth(arg(k))) {
                return arg(k + 1);
            }
        }
        return QString();
    }
    if (f == QLatin1String("choose")) {
        const int k = need(2) ? int(num(arg(0))) : 0;
        return k >= 1 && k < n->args.size() ? arg(k) : QVariant(QString());
    }
    if (f == QLatin1String("now") || f == QLatin1String("date")) {
        const Minutes now = toMinutes(QDateTime::currentDateTime());
        return dateValue(f == QLatin1String("now") ? now : now - now % 1440);
    }
    if (!need(f == QLatin1String("datediff") || f == QLatin1String("dateadd") ? 3
              : f == QLatin1String("left") || f == QLatin1String("right") || f == QLatin1String("mid")
                      || f == QLatin1String("instr") || f == QLatin1String("projdatediff")
                      || f == QLatin1String("projdateadd") || f == QLatin1String("projdatesub")
                      || f == QLatin1String("projdurconv")
                  ? 2
                  : 1)) {
        return {};
    }
    const QVariant a = arg(0);
    if (f == QLatin1String("abs")) {
        return std::abs(num(a));
    }
    if (f == QLatin1String("int")) {
        return std::floor(num(a));
    }
    if (f == QLatin1String("fix") || f == QLatin1String("clng") || f == QLatin1String("cint")) {
        return f == QLatin1String("fix") ? std::trunc(num(a)) : std::nearbyint(num(a));
    }
    if (f == QLatin1String("round")) {
        const double scale = std::pow(10.0, n->args.size() > 1 ? num(arg(1)) : 0.0);
        return std::nearbyint(num(a) * scale) / scale; // banker's rounding, as VBA
    }
    if (f == QLatin1String("sgn")) {
        return double((num(a) > 0) - (num(a) < 0));
    }
    if (f == QLatin1String("sqr")) {
        return num(a) < 0 ? fail(QStringLiteral("Sqr of a negative number.")) : QVariant(std::sqrt(num(a)));
    }
    if (f == QLatin1String("cdbl") || f == QLatin1String("val")) {
        if (f == QLatin1String("val") && isText(a)) {
            bool ok = false;
            const double v = QLocale::c().toDouble(a.toString().trimmed().section(QLatin1Char(' '), 0, 0), &ok);
            return ok ? v : 0.0;
        }
        return num(a);
    }
    if (f == QLatin1String("str") || f == QLatin1String("cstr")) {
        const QString s = text(a);
        return f == QLatin1String("str") && !isText(a) && num(a) >= 0 ? QLatin1Char(' ') + s : s;
    }
    const QString s = text(a);
    if (f == QLatin1String("len")) {
        return double(s.size());
    }
    if (f == QLatin1String("ucase") || f == QLatin1String("lcase")) {
        return f == QLatin1String("ucase") ? s.toUpper() : s.toLower();
    }
    if (f == QLatin1String("trim") || f == QLatin1String("ltrim") || f == QLatin1String("rtrim")) {
        QString out = s;
        while (f != QLatin1String("rtrim") && out.startsWith(QLatin1Char(' '))) {
            out.remove(0, 1);
        }
        while (f != QLatin1String("ltrim") && out.endsWith(QLatin1Char(' '))) {
            out.chop(1);
        }
        return out;
    }
    if (f == QLatin1String("left") || f == QLatin1String("right")) {
        const int k = std::max(0, int(num(arg(1))));
        return f == QLatin1String("left") ? s.left(k) : s.right(k);
    }
    if (f == QLatin1String("mid")) {
        const int start = std::max(1, int(num(arg(1))));
        return n->args.size() > 2 ? s.mid(start - 1, std::max(0, int(num(arg(2))))) : s.mid(start - 1);
    }
    if (f == QLatin1String("instr")) {
        const bool from = n->args.size() > 2;
        const int start = from ? std::max(1, int(num(a))) : 1;
        const QString hay = from ? text(arg(1)) : s;
        const QString needle = text(arg(from ? 2 : 1));
        return double(hay.indexOf(needle, start - 1) + 1);
    }
    if (f == QLatin1String("projdatevalue") || f == QLatin1String("projdurvalue")) {
        if (f == QLatin1String("projdatevalue") && s.trimmed().compare(QLatin1String("NA"), Qt::CaseInsensitive) == 0) {
            return kNaDate;
        }
        if (f == QLatin1String("projdurvalue")) {
            const std::optional<ParsedDuration> d = parseDuration(s, durationFormatOf(m_p.info), true);
            return d ? QVariant(double(d->minutes)) : fail(QStringLiteral("“%1” is not a duration.").arg(s));
        }
        const std::optional<Minutes> m = parseDate(s, m_p.info.defaultStartMinute, QLocale::c());
        return m ? QVariant(dateValue(*m)) : fail(QStringLiteral("“%1” is not a date.").arg(s));
    }
    const WorkCalendar calendar(m_p, m_p.info.calendarUid);
    if (f == QLatin1String("projdatediff")) {
        return double(calendar.workBetween(minutesOfDate(num(a)), minutesOfDate(num(arg(1)))));
    }
    if (f == QLatin1String("projdateadd") || f == QLatin1String("projdatesub")) {
        const Minutes w = std::llround(num(arg(1)));
        const Minutes from = minutesOfDate(num(a));
        return dateValue(f == QLatin1String("projdateadd") ? calendar.add(from, w) : calendar.subtract(from, w));
    }
    if (f == QLatin1String("projdurconv")) {
        const int c = int(num(arg(1))); // Project's pjMinutes 3 … pjMonths 11 (+1 elapsed)
        const DurationUnit unit = c <= 4 ? DurationUnit::Minutes : c <= 6 ? DurationUnit::Hours : c <= 8 ? DurationUnit::Days
                                  : c <= 10 ? DurationUnit::Weeks : DurationUnit::Months;
        return num(a) / double(minutesPerUnit(unit, durationFormatOf(m_p.info), c % 2 == 0));
    }
    const QDateTime when = toDateTime(minutesOfDate(num(f == QLatin1String("datediff") || f == QLatin1String("dateadd")
                                                            ? arg(f == QLatin1String("dateadd") ? 2 : 1)
                                                            : a)));
    if (f == QLatin1String("year")) {
        return double(when.date().year());
    }
    if (f == QLatin1String("month")) {
        return double(when.date().month());
    }
    if (f == QLatin1String("day")) {
        return double(when.date().day());
    }
    if (f == QLatin1String("weekday")) {
        return double(when.date().dayOfWeek() % 7 + 1); // Sunday = 1, as in VBA
    }
    if (f == QLatin1String("hour") || f == QLatin1String("minute")) {
        return double(f == QLatin1String("hour") ? when.time().hour() : when.time().minute());
    }
    const QString interval = s.toLower();
    if (f == QLatin1String("dateadd")) {
        const int k = int(num(arg(1)));
        const QDateTime out = interval == QLatin1String("yyyy") ? when.addYears(k)
                              : interval == QLatin1String("m")  ? when.addMonths(k)
                              : interval == QLatin1String("ww") ? when.addDays(7LL * k)
                              : interval == QLatin1String("h")  ? when.addSecs(3600LL * k)
                              : interval == QLatin1String("n")  ? when.addSecs(60LL * k)
                                                                : when.addDays(k);
        return dateValue(toMinutes(out));
    }
    const QDateTime later = toDateTime(minutesOfDate(num(arg(2))));
    const qint64 minutes = when.secsTo(later) / 60;
    if (interval == QLatin1String("yyyy")) {
        return double(later.date().year() - when.date().year());
    }
    if (interval == QLatin1String("m")) {
        return double((later.date().year() - when.date().year()) * 12 + later.date().month() - when.date().month());
    }
    return double(interval == QLatin1String("h")    ? minutes / 60
                  : interval == QLatin1String("n")  ? minutes
                  : interval == QLatin1String("ww") ? when.date().daysTo(later.date()) / 7
                                                    : when.date().daysTo(later.date()));
}

} // namespace QindaOffice::Plan::Formula
