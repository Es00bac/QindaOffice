// SPDX-License-Identifier: GPL-3.0-or-later
// Outlook appointments (MS-OXOCAL) and tasks (MS-OXOTASK), and reading any
// item by its class (outlook_items.h).

#include "outlook_items.h"

#include "mapi_mail.h"
#include "mapi_named.h"
#include "msg_headers.h"
#include "msg_objects.h"

namespace QindaOffice::Mail {
namespace OutlookDetail {
namespace {

using MsgDetail::oneLine;

enum : quint16 {
    PidImportance = 0x0017,
    PidMessageClass = 0x001A,
    PidSubject = 0x0037,
    PidSentRepresentingName = 0x0042,
    PidStartDate = 0x0060,
    PidEndDate = 0x0061,
    PidSentRepresentingAddressType = 0x0064,
    PidSentRepresentingEmailAddress = 0x0065,
    PidSenderName = 0x0C1A,
    PidSenderAddressType = 0x0C1E,
    PidSenderEmailAddress = 0x0C1F,
    PidMessageFlags = 0x0E07,
    PidBody = 0x1000,
    PidFlagStatus = 0x1090,
    PidDisplayName = 0x3001,
    PidAddressType = 0x3002,
    PidEmailAddress = 0x3003,
    PidSmtpAddress = 0x39FE,
    PidInternetCodepage = 0x3FDE,
    PidMessageCodepage = 0x3FFD,
    PidSenderSmtpAddress = 0x5D01,
    PidSentRepresentingSmtpAddress = 0x5D02,
    PidRecipientFlags = 0x5FFD,
};

// PSETID_Appointment (MS-OXOCAL 2.2.1) and PSETID_Meeting.
enum : quint32 {
    LidLocation = 0x8208,
    LidAppointmentStartWhole = 0x820D,
    LidAppointmentEndWhole = 0x820E,
    LidAppointmentSubType = 0x8215,
    LidAppointmentRecur = 0x8216,
    LidRecurring = 0x8223,
    LidTimeZoneDefinitionStartDisplay = 0x825E,
    LidGlobalObjectId = 0x0003,
    LidCleanGlobalObjectId = 0x0023,
};

// PSETID_Task (MS-OXOTASK 2.2.2).
enum : quint32 {
    LidTaskStatus = 0x8101,
    LidPercentComplete = 0x8102,
    LidTaskStartDate = 0x8104,
    LidTaskDueDate = 0x8105,
    LidTaskDateCompleted = 0x810F,
    LidTaskComplete = 0x811C,
};

constexpr int kMaxListedDates = 10000;

class Cursor {
public:
    explicit Cursor(const QByteArray &bytes) : m_bytes(bytes) {}
    bool u16(quint16 *out)
    {
        if (m_at + 2 > m_bytes.size()) return false;
        *out = quint16(uchar(m_bytes.at(m_at)) | (uchar(m_bytes.at(m_at + 1)) << 8));
        m_at += 2;
        return true;
    }
    bool u32(quint32 *out)
    {
        quint16 low = 0, high = 0;
        if (!u16(&low) || !u16(&high)) return false;
        *out = quint32(low) | (quint32(high) << 16);
        return true;
    }

private:
    const QByteArray &m_bytes;
    qsizetype m_at = 0;
};

QDate minutesToDate(quint32 minutes)
{
    return QDate(1601, 1, 1).addDays(minutes / 1440);
}

QList<int> daysFromMask(quint32 mask)
{
    QList<int> days;
    for (int bit = 1; bit <= 6; ++bit) {
        if (mask & (1u << bit)) days.append(bit);   // Monday … Saturday
    }
    if (mask & 1u) days.append(7);                  // Sunday
    return days;
}

// MS-OXOCAL 2.2.1.44.1 RecurrencePattern (the start of AppointmentRecurrencePattern).
bool parseRecurrence(const QByteArray &blob, OutlookRecurrence *out)
{
    Cursor c(blob);
    quint16 reader = 0, writer = 0, frequency = 0, pattern = 0, calendar = 0;
    quint32 firstDateTime = 0, period = 0, sliding = 0, day = 0, mask = 0, nth = 0;
    if (!c.u16(&reader) || !c.u16(&writer) || !c.u16(&frequency) || !c.u16(&pattern) || !c.u16(&calendar)
        || !c.u32(&firstDateTime) || !c.u32(&period) || !c.u32(&sliding))
        return false;
    if (reader != 0x3004 || (calendar != 0 && calendar != 1)) return false;   // Gregorian only
    if (pattern == 1 && !c.u32(&mask)) return false;
    if ((pattern == 2 || pattern == 4) && !c.u32(&day)) return false;
    if (pattern == 3 && (!c.u32(&mask) || !c.u32(&nth))) return false;
    if (pattern > 4) return false;   // Hijri patterns
    quint32 endType = 0, count = 0, firstDay = 0, deleted = 0, modified = 0, startDate = 0, endDate = 0;
    if (!c.u32(&endType) || !c.u32(&count) || !c.u32(&firstDay) || !c.u32(&deleted) || deleted > kMaxListedDates)
        return false;
    QList<QDate> deletedDates;
    for (quint32 i = 0; i < deleted; ++i) {
        quint32 minutes = 0;
        if (!c.u32(&minutes)) return false;
        deletedDates.append(minutesToDate(minutes));
    }
    if (!c.u32(&modified) || modified > kMaxListedDates) return false;
    QList<QDate> modifiedDates;
    for (quint32 i = 0; i < modified; ++i) {
        quint32 minutes = 0;
        if (!c.u32(&minutes)) return false;
        modifiedDates.append(minutesToDate(minutes));
    }
    if (!c.u32(&startDate) || !c.u32(&endDate)) return false;

    OutlookRecurrence r;
    r.startDate = minutesToDate(startDate);
    static const char *const kIndexes[] = {"first", "second", "third", "fourth", "last"};
    static const char *const kDays[] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
    r.index = nth >= 1 && nth <= 5 ? QString::fromLatin1(kIndexes[nth - 1]) : QString();
    r.firstDayOfWeek = firstDay <= 6 ? QString::fromLatin1(kDays[firstDay]) : QString();
    if (frequency == 0x200A && pattern == 0) {
        r.type = QStringLiteral("daily");
        r.interval = int(qMax<quint32>(1, period / 1440));
    } else if ((frequency == 0x200A || frequency == 0x200B) && pattern == 1) {
        r.type = QStringLiteral("weekly");   // a daily "every weekday" is a weekly Mon–Fri
        r.interval = int(qMax<quint32>(1, period));
        r.days = daysFromMask(mask);
    } else if (frequency == 0x200C && (pattern == 2 || pattern == 4)) {
        r.type = QStringLiteral("absoluteMonthly");
        r.interval = int(qMax<quint32>(1, period));
        r.dayOfMonth = pattern == 4 ? 31 : int(day);
    } else if (frequency == 0x200C && pattern == 3) {
        r.type = QStringLiteral("relativeMonthly");
        r.interval = int(qMax<quint32>(1, period));
        r.days = daysFromMask(mask);
    } else if (frequency == 0x200D && (pattern == 2 || pattern == 4)) {
        r.type = QStringLiteral("absoluteYearly");
        r.interval = int(qMax<quint32>(1, period / 12));
        r.dayOfMonth = pattern == 4 ? 31 : int(day);
        r.month = r.startDate.month();
    } else if (frequency == 0x200D && pattern == 3) {
        r.type = QStringLiteral("relativeYearly");
        r.interval = int(qMax<quint32>(1, period / 12));
        r.days = daysFromMask(mask);
        r.month = r.startDate.month();
    } else {
        return false;
    }
    if (endType == 0x2021) {
        r.range = QStringLiteral("endDate");
        r.endDate = minutesToDate(endDate);
    } else if (endType == 0x2022) {
        r.range = QStringLiteral("numbered");
        r.count = int(qMin<quint32>(count, 999));
    }
    // A changed occurrence is listed as deleted AND modified on its date;
    // it stays in the series (with the series' values) rather than vanish.
    for (const QDate &date : std::as_const(deletedDates)) {
        if (!modifiedDates.contains(date)) r.deletedDates.append(date);
    }
    *out = r;
    return true;
}

// MS-OXOCAL 2.2.1.41 TZDEFINITION: the Windows zone's key name.
QString zoneKeyName(const QByteArray &definition)
{
    if (definition.size() < 8 || quint8(definition.at(0)) != 0x02) return {};
    const int length = quint8(definition.at(6)) | (quint8(definition.at(7)) << 8);
    if (length <= 0 || length > 256 || 8 + 2 * length > definition.size()) return {};
    return MsgDetail::utf16LeToString(definition.mid(8, 2 * length));
}

QString mailbox(Mapi::PropertyBag &bag, quint16 name, quint16 smtp, quint16 type, quint16 address)
{
    const QString who = oneLine(bag.string(name));
    const QString where = Mapi::smtpAddress(bag, smtp, type, address);
    return where.isEmpty() ? who : MsgDetail::formatAddress(who, where);
}

} // namespace

bool readAppointment(Mapi::Message &message, OutlookAppointment *out, QString *error)
{
    OutlookAppointment a;
    const auto id = [&message](quint32 lid) { return named(message, Mapi::kPsetidAppointment, lid); };
    a.subject = oneLine(message.string(PidSubject));
    a.body = message.string(PidBody);
    if (const auto location = id(LidLocation)) a.location = oneLine(message.string(*location));
    if (const auto start = id(LidAppointmentStartWhole)) a.start = message.timeValue(*start);
    if (const auto end = id(LidAppointmentEndWhole)) a.end = message.timeValue(*end);
    if (!a.start.isValid()) a.start = message.timeValue(PidStartDate);
    if (!a.end.isValid()) a.end = message.timeValue(PidEndDate);
    if (const auto allDay = id(LidAppointmentSubType)) a.allDay = message.boolValue(*allDay);
    if (const auto zone = id(LidTimeZoneDefinitionStartDisplay)) a.windowsZone = zoneKeyName(message.binary(*zone));
    for (const quint32 lid : {LidCleanGlobalObjectId, LidGlobalObjectId}) {
        const auto object = named(message, Mapi::kPsetidMeeting, lid);
        const QByteArray bytes = object ? message.binary(*object) : QByteArray();
        if (!bytes.isEmpty()) {
            a.uid = QString::fromLatin1(bytes.toHex().toUpper());
            break;
        }
    }
    a.organizer = mailbox(message, PidSentRepresentingName, PidSentRepresentingSmtpAddress, PidSentRepresentingAddressType,
                          PidSentRepresentingEmailAddress);
    if (a.organizer.isEmpty())
        a.organizer = mailbox(message, PidSenderName, PidSenderSmtpAddress, PidSenderAddressType, PidSenderEmailAddress);
    std::vector<std::unique_ptr<Mapi::PropertyBag>> recipients;
    QString why;
    if (!message.recipients(&recipients, &why)) {
        if (error) *error = why;
        return false;
    }
    for (const std::unique_ptr<Mapi::PropertyBag> &recipient : recipients) {
        if (recipient->longValue(PidRecipientFlags).value_or(0) & 0x0002) continue;   // recipOrganizer
        const QString attendee = mailbox(*recipient, PidDisplayName, PidSmtpAddress, PidAddressType, PidEmailAddress);
        if (!attendee.isEmpty() && attendee != a.organizer) a.attendees.append(attendee);
    }
    if (const auto keywords = message.namedProperties().find(Mapi::kPsPublicStrings, QStringLiteral("Keywords")))
        a.categories = message.strings(*keywords);
    const auto recurring = id(LidRecurring);
    const auto pattern = id(LidAppointmentRecur);
    if (recurring && message.boolValue(*recurring) && pattern)
        a.recurring = parseRecurrence(message.binary(*pattern), &a.recurrence);
    if (message.failed()) {
        if (error) *error = message.error();
        return false;
    }
    *out = a;
    return true;
}

bool readTask(Mapi::Message &message, OutlookTask *out, QString *error)
{
    OutlookTask t;
    const auto id = [&message](quint32 lid) { return named(message, Mapi::kPsetidTask, lid); };
    t.subject = oneLine(message.string(PidSubject));
    t.body = message.string(PidBody);
    if (const auto status = id(LidTaskStatus)) t.status = int(message.longValue(*status).value_or(0));
    if (const auto percent = id(LidPercentComplete)) t.percentComplete = message.doubleValue(*percent).value_or(0);
    if (const auto complete = id(LidTaskComplete)) t.complete = message.boolValue(*complete);
    // AGENT-NOTE (MS-OXOTASK 2.2.2.2): the task dates are the user's local
    // dates stored as midnight (UTC); localDate() reads either convention.
    if (const auto start = id(LidTaskStartDate)) t.start = localDate(message.timeValue(*start));
    if (const auto due = id(LidTaskDueDate)) t.due = localDate(message.timeValue(*due));
    if (const auto done = id(LidTaskDateCompleted)) t.completed = localDate(message.timeValue(*done));
    t.importance = int(message.longValue(PidImportance).value_or(1));
    if (const auto keywords = message.namedProperties().find(Mapi::kPsPublicStrings, QStringLiteral("Keywords")))
        t.categories = message.strings(*keywords);
    if (message.failed()) {
        if (error) *error = message.error();
        return false;
    }
    *out = t;
    return true;
}

} // namespace OutlookDetail

OutlookItemKind outlookItemKind(const QString &messageClass)
{
    const auto is = [&messageClass](const char *prefix) {
        const QLatin1String p(prefix);
        return messageClass.compare(p, Qt::CaseInsensitive) == 0
            || messageClass.startsWith(QString(p) + QLatin1Char('.'), Qt::CaseInsensitive);
    };
    if (is("IPM.Contact")) return OutlookItemKind::Contact;
    if (is("IPM.DistList")) return OutlookItemKind::DistList;
    if (is("IPM.Appointment")) return OutlookItemKind::Appointment;
    if (is("IPM.Task")) return OutlookItemKind::Task;
    if (is("IPM.StickyNote") || is("IPM.Activity") || is("IPM.Document") || is("IPM.OLE.CLASS"))
        return OutlookItemKind::Other;
    return OutlookItemKind::Mail;
}

bool readOutlookItem(Mapi::Message &message, OutlookItem *out, QString *error)
{
    using namespace OutlookDetail;
    OutlookItem item;
    item.messageClass = oneLine(message.string(PidMessageClass));
    item.kind = outlookItemKind(item.messageClass);
    item.unread = (message.longValue(PidMessageFlags).value_or(1) & 0x1) == 0;   // mfRead
    item.flagged = message.longValue(PidFlagStatus).value_or(0) == 2;           // followupFlagged
    const int internet = int(message.longValue(PidInternetCodepage).value_or(0));
    const int codePage = int(message.longValue(PidMessageCodepage).value_or(0));
    message.setString8CodePage(codePage > 0 ? codePage : internet > 0 ? internet : 1252);
    QString why;
    bool ok = true;
    switch (item.kind) {
    case OutlookItemKind::Mail: ok = Mapi::readMailContent(message, &item.mail, &why); break;
    case OutlookItemKind::Contact: ok = readContact(message, &item.contact, &why); break;
    case OutlookItemKind::DistList: ok = readDistList(message, &item.contact, &item.members, &why); break;
    case OutlookItemKind::Appointment: ok = readAppointment(message, &item.appointment, &why); break;
    case OutlookItemKind::Task: ok = readTask(message, &item.task, &why); break;
    case OutlookItemKind::Other: break;
    }
    if (!ok) {
        if (error) *error = why;
        return false;
    }
    *out = std::move(item);
    return true;
}

bool readOutlookMsg(const QByteArray &file, OutlookItem *out, QString *error)
{
    QString why;
    std::unique_ptr<MsgDetail::MsgMessage> message = MsgDetail::openMsgFile(file, &why);
    if (!message) {
        if (error) *error = why;
        return false;
    }
    if (!readOutlookItem(*message, out, &why)) {
        if (error) *error = QStringLiteral("Cannot read the Outlook message: %1").arg(why);
        return false;
    }
    return true;
}

} // namespace QindaOffice::Mail
