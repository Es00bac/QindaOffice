// SPDX-License-Identifier: GPL-3.0-or-later
// Follow-up flags: presets, values, wording (follow_up.h).
#include "follow_up.h"

#include <QCoreApplication>

#include <algorithm>

namespace QindaOffice::Mail::FollowUps {

using namespace Qt::StringLiterals;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Mail::FollowUps", text);
}

QDate weekStart(const QDate &day, Qt::DayOfWeek first)
{
    return day.addDays(-((day.dayOfWeek() - int(first) + 7) % 7));
}

// The first / last work day in [from, from + 6]; invalid when none.
QDate firstWorkDay(const QDate &from, const QList<int> &workDays)
{
    for (int i = 0; i < 7; ++i) {
        if (workDays.contains(from.addDays(i).dayOfWeek())) return from.addDays(i);
    }
    return {};
}

QDate lastWorkDay(const QDate &from, const QList<int> &workDays)
{
    for (int i = 6; i >= 0; --i) {
        if (workDays.contains(from.addDays(i).dayOfWeek())) return from.addDays(i);
    }
    return {};
}

} // namespace

const QStringList &presets()
{
    static const QStringList all{u"today"_s, u"tomorrow"_s, u"thisWeek"_s, u"nextWeek"_s, u"noDate"_s, u"custom"_s};
    return all;
}

QList<int> defaultWorkDays()
{
    return {1, 2, 3, 4, 5};
}

FollowUp forPreset(const QString &preset, const QDate &today, Qt::DayOfWeek firstDayOfWeek, const QList<int> &workDays)
{
    FollowUp flag;
    if (!presets().contains(preset) || preset == "custom"_L1 || !today.isValid()) return flag;
    const QList<int> days = workDays.isEmpty() ? defaultWorkDays() : workDays;
    flag.status = u"flagged"_s;
    flag.preset = preset;
    flag.column = u"todo"_s;
    const QDate thisWeek = weekStart(today, firstDayOfWeek);
    if (preset == "today"_L1) {
        flag.start = flag.due = today;
    } else if (preset == "tomorrow"_L1) {
        flag.start = flag.due = today.addDays(1);
    } else if (preset == "thisWeek"_L1) {
        QDate due = lastWorkDay(thisWeek, days);
        if (!due.isValid() || due < today) due = today;      // a weekend: the week's work is over
        flag.due = due;
        flag.start = std::max(today, std::min(today.addDays(2), due));
    } else if (preset == "nextWeek"_L1) {
        const QDate next = thisWeek.addDays(7);
        flag.start = firstWorkDay(next, days);
        flag.due = lastWorkDay(next, days);
        if (!flag.start.isValid()) flag.start = flag.due = next;
    }
    return flag;
}

FollowUp custom(const QDate &start, const QDate &due)
{
    FollowUp flag;
    flag.status = u"flagged"_s;
    flag.preset = u"custom"_s;
    flag.column = u"todo"_s;
    flag.due = due;
    flag.start = start.isValid() ? start : due;
    if (flag.start.isValid() && flag.due.isValid() && flag.start > flag.due) flag.start = flag.due;
    return flag;
}

FollowUp completed(const FollowUp &flag, const QDateTime &nowUtc)
{
    FollowUp done = flag;
    done.status = u"complete"_s;
    done.completed = nowUtc.toUTC();
    if (done.preset.isEmpty()) done.preset = u"noDate"_s;
    return done;
}

FollowUp fromVariantMap(const QVariantMap &map)
{
    FollowUp flag;
    flag.status = map.value(u"status"_s).toString();
    if (flag.status != "flagged"_L1 && flag.status != "complete"_L1) flag.status.clear();
    flag.preset = map.value(u"preset"_s).toString();
    flag.start = QDate::fromString(map.value(u"start"_s).toString(), Qt::ISODate);
    flag.due = QDate::fromString(map.value(u"due"_s).toString(), Qt::ISODate);
    flag.completed = QDateTime::fromString(map.value(u"completed"_s).toString(), Qt::ISODate);
    flag.column = map.value(u"column"_s).toString() == "doing"_L1 ? u"doing"_s : u"todo"_s;
    static const QStringList known{u"status"_s, u"preset"_s, u"start"_s, u"due"_s, u"completed"_s, u"column"_s};
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!known.contains(it.key())) flag.extra.insert(it.key(), it.value());
    }
    return flag;
}

QVariantMap toVariantMap(const FollowUp &flag)
{
    if (flag.status.isEmpty()) return {};
    QVariantMap map = flag.extra;
    map.insert(u"status"_s, flag.status);
    map.insert(u"preset"_s, flag.preset);
    map.insert(u"start"_s, flag.start.isValid() ? flag.start.toString(Qt::ISODate) : QString());
    map.insert(u"due"_s, flag.due.isValid() ? flag.due.toString(Qt::ISODate) : QString());
    map.insert(u"completed"_s, flag.completed.isValid() ? flag.completed.toUTC().toString(Qt::ISODate) : QString());
    map.insert(u"column"_s, flag.column.isEmpty() ? u"todo"_s : flag.column);
    return map;
}

QString describe(const FollowUp &flag, const QLocale &locale)
{
    if (flag.isComplete()) {
        return flag.completed.isValid()
            ? tr("Completed on %1.").arg(locale.toString(flag.completed.toLocalTime().date(), QLocale::LongFormat))
            : tr("Completed.");
    }
    if (!flag.isFlagged()) return {};
    QString text = tr("Follow up.");
    if (flag.start.isValid()) text += u' ' + tr("Start by %1.").arg(locale.toString(flag.start, QLocale::LongFormat));
    if (flag.due.isValid()) text += u' ' + tr("Due by %1.").arg(locale.toString(flag.due, QLocale::LongFormat));
    return text;
}

QString dueLabel(const FollowUp &flag, const QDate &today, const QLocale &locale)
{
    if (flag.isComplete()) return tr("Done");
    if (!flag.isFlagged() || !flag.due.isValid()) return {};
    if (flag.due < today) return tr("Overdue");
    if (flag.due == today) return tr("Today");
    if (flag.due == today.addDays(1)) return tr("Tomorrow");
    if (flag.due < today.addDays(7)) return locale.dayName(flag.due.dayOfWeek(), QLocale::ShortFormat);
    return locale.toString(flag.due, QLocale::ShortFormat);
}

} // namespace QindaOffice::Mail::FollowUps
