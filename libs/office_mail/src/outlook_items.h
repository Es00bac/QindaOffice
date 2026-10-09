// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook items as QindaMail's values (M76, D-085): one Mapi::Message — from
// a .msg (msg_objects.h) or a .pst (pst_reader.h) — read by its message
// class. Mail (IPM.Note, meeting requests, reports, posts) becomes
// MessageContent through mapi_mail.h; IPM.Contact a Contact; IPM.DistList a
// group Contact plus its members; IPM.Appointment an OutlookAppointment;
// IPM.Task an OutlookTask. Sticky notes and journal entries are "Other" and
// not read.
//
// AGENT-CONTRACT (QindaMail's importers: mail_service_files.cpp for .msg,
// pst_import.cpp for .pst): the values carry what the file says, in UTC and
// Windows zone names; the app turns them into store records (IANA zones,
// local wall times, recurrence rules through mail_exchange's pattern
// mapping) and assigns ids. Identity (a contact's uid, an event's id) is the
// importer's: these readers never invent one.
//
// Threading: like the bags they read, one thread at a time.

#include "contact.h"
#include "mapi_object.h"
#include "message_content.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Mail {

enum class OutlookItemKind { Mail, Contact, DistList, Appointment, Task, Other };

[[nodiscard]] OutlookItemKind outlookItemKind(const QString &messageClass);

// One member of an Outlook contact group. A member that is a contact of the
// same .pst names it by store and node (storeUid, contactNid); every member
// also has the name and address of its one-off entry when Outlook kept one.
struct DistListMember {
    QString name;
    QString address;          // SMTP; "" when only an Exchange address is known
    QByteArray storeUid;      // 16 bytes when the member is a contact of a store
    quint32 contactNid = 0;   // that contact's node id
};

// MS-OXOCAL 2.2.1.44 recurrence, in the vocabulary of mail_exchange's
// RecurrencePattern (Exchange keeps the same pattern) so the app maps it
// with patternToRule().
struct OutlookRecurrence {
    QString type;              // daily weekly absoluteMonthly relativeMonthly absoluteYearly relativeYearly
    int interval = 1;
    QList<int> days;           // 1 = Monday … 7 = Sunday
    int dayOfMonth = 0;
    int month = 0;             // 1–12, yearly patterns
    QString index;             // first second third fourth last (relative patterns)
    QString firstDayOfWeek;    // monday … sunday
    QString range = QStringLiteral("noEnd");   // noEnd endDate numbered
    QDate startDate;
    QDate endDate;
    int count = 0;
    QList<QDate> deletedDates; // occurrences deleted from the series (original dates)
};

struct OutlookAppointment {
    QString subject;
    QString location;
    QString body;
    QDateTime start;           // UTC
    QDateTime end;             // UTC
    bool allDay = false;
    QString windowsZone;       // the start's Windows zone ("Pacific Standard Time"), "" when unknown
    QString uid;               // hex of the global object id, "" when the item has none
    QString organizer;         // "Name <addr>" or ""
    QStringList attendees;     // "Name <addr>"
    QStringList categories;
    bool recurring = false;
    OutlookRecurrence recurrence;
};

struct OutlookTask {
    QString subject;
    QString body;
    QDate start;
    QDate due;
    QDate completed;
    int status = 0;            // MS-OXOTASK: 0 not started, 1 in progress, 2 complete, 3 waiting, 4 deferred
    double percentComplete = 0;
    bool complete = false;
    int importance = 1;        // 0 low, 1 normal, 2 high
    QStringList categories;
};

struct OutlookItem {
    OutlookItemKind kind = OutlookItemKind::Mail;
    QString messageClass;
    bool unread = false;       // PidTagMessageFlags without mfRead
    bool flagged = false;      // PidTagFlagStatus followupFlagged
    MessageContent mail;
    Contact contact;           // Contact, and DistList (kind "group", no members yet)
    QList<DistListMember> members;
    OutlookAppointment appointment;
    OutlookTask task;
};

// Reads `message` by its class; Other is reported without reading further.
// False with `*error` when the item is corrupt.
bool readOutlookItem(Mapi::Message &message, OutlookItem *out, QString *error);
// The same for a whole .msg file.
bool readOutlookMsg(const QByteArray &file, OutlookItem *out, QString *error);

// Internal to outlook_*.cpp.
namespace OutlookDetail {
bool readContact(Mapi::Message &message, Contact *out, QString *error);
bool readDistList(Mapi::Message &message, Contact *group, QList<DistListMember> *members, QString *error);
bool readAppointment(Mapi::Message &message, OutlookAppointment *out, QString *error);
bool readTask(Mapi::Message &message, OutlookTask *out, QString *error);
// The date a "local midnight stored as UTC" value means: the UTC date of the
// instant twelve hours later (right for every zone from UTC−11 to UTC+12).
[[nodiscard]] QDate localDate(const QDateTime &utc);
[[nodiscard]] std::optional<quint16> named(const Mapi::Message &message, const QUuid &set, quint32 lid);
} // namespace OutlookDetail

} // namespace QindaOffice::Mail
