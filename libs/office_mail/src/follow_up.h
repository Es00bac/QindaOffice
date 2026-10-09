// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook's follow-up flags (M81, D-090; docs/mail.md#follow-up-flags):
// Today, Tomorrow, This Week, Next Week, No Date, Custom, Mark Complete and
// Clear Flag, with the start and due dates Outlook gives each preset
// ("Flag email messages for follow up", Microsoft Support):
//   today      start = due = today
//   tomorrow   start = due = today + 1
//   thisWeek   due = the last work day of this week; start = today + 2,
//              but no later than the due date (and never before today)
//   nextWeek   start = the first, due = the last work day of next week
//   noDate     no dates
//   custom     the dates the user picks
// Weeks start on the locale's first day; work days default to Mon–Fri.
//
// AGENT-CONTRACT (store records' `followUp`, the task board, the IMAP,
// Graph and EWS mappings): {status "flagged" | "complete", preset, start
// "yyyy-MM-dd" | "", due "yyyy-MM-dd" | "", completed ISO-8601 UTC | "",
// column "todo" | "doing"}; no `followUp` (or an empty status) = not
// flagged. A record's `flagged` mirrors status == "flagged" (it is what
// IMAP's \Flagged carries; Exchange keeps the dates and the completion).

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Mail {

struct FollowUp {
    QString status;              // "" | flagged | complete
    QString preset;              // today | tomorrow | thisWeek | nextWeek | noDate | custom
    QDate start, due;
    QDateTime completed;         // UTC
    QString column;              // todo | doing (the task board, while flagged)
    QVariantMap extra;

    [[nodiscard]] bool isFlagged() const { return status == QLatin1String("flagged"); }
    [[nodiscard]] bool isComplete() const { return status == QLatin1String("complete"); }
};

namespace FollowUps {

const QStringList &presets();    // today tomorrow thisWeek nextWeek noDate custom

// Mon..Fri as Qt day numbers (1 = Monday).
QList<int> defaultWorkDays();

// A flag for `preset` (not custom) on `today`; an invalid preset gives an
// empty status.
FollowUp forPreset(const QString &preset, const QDate &today, Qt::DayOfWeek firstDayOfWeek = Qt::Monday,
                   const QList<int> &workDays = defaultWorkDays());
// Custom dates; a missing start takes the due date, a start after the due
// date is moved to it.
FollowUp custom(const QDate &start, const QDate &due);
// Marked complete now (dates kept).
FollowUp completed(const FollowUp &flag, const QDateTime &nowUtc);

FollowUp fromVariantMap(const QVariantMap &map);
QVariantMap toVariantMap(const FollowUp &flag);   // {} for no flag

// What the reading pane and the task card say: "Follow up. Start by
// Friday, October 16. Due by Friday, October 16." / "Follow up." /
// "Completed on …".
QString describe(const FollowUp &flag, const QLocale &locale = QLocale());
// A short due label for the message list: "Today", "Tomorrow", a weekday,
// a date, "Overdue", "Done" or "" (no date).
QString dueLabel(const FollowUp &flag, const QDate &today, const QLocale &locale = QLocale());

} // namespace FollowUps
} // namespace QindaOffice::Mail
