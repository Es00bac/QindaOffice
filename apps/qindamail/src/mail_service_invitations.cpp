// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: meeting invitations sent from the event dialog (M64 P3, iMIP
// RFC 6047 / iTIP RFC 5546 §3.2.2, the organizer's side of D-072): the
// event's VEVENT as METHOD:REQUEST with ORGANIZER and one ATTENDEE per
// invitee (RSVP=TRUE), mailed to them through a sending account. The event
// remembers its iCalendar UID, invitees and SEQUENCE, so a later change goes
// out as an update of the same meeting.
//
// AGENT-NOTE (toolkit follow-up in D-072): as for replies
// (mail_service_meetings.cpp), the toolkit's SmtpMailProvider attaches the
// request as an `invite.ics` file; it cannot write the inline
// `text/calendar; method=REQUEST` part iMIP prefers.
#include "mail_service.h"

#include "contact.h"
#include "provider_calls.h"

#include <qindatk/ical_codec.h>
#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/mail_mime.h>

#include <algorithm>

namespace QindaOffice::Apps::Mail {

namespace {

QString quotedParam(QString value)
{
    value.remove(QLatin1Char('"'));
    return value.contains(QLatin1Char(',')) || value.contains(QLatin1Char(';')) || value.contains(QLatin1Char(':'))
        ? QLatin1Char('"') + value + QLatin1Char('"') : value;
}

QByteArray personLine(const char *property, const QString &params, const QString &name, const QString &address)
{
    QString line = QString::fromLatin1(property) + params;
    if (!name.trimmed().isEmpty()) line += QStringLiteral(";CN=") + quotedParam(name.trimmed());
    return QindaTK::ICal::foldLine(line + QStringLiteral(":mailto:") + address);
}

// Drops every METHOD, SEQUENCE, ORGANIZER and ATTENDEE content line (with
// its folded continuation lines): this file writes those itself.
QByteArray withoutItipLines(const QByteArray &ics)
{
    static const QList<QByteArray> names{"METHOD", "SEQUENCE", "ORGANIZER", "ATTENDEE"};
    QByteArray out;
    bool skipping = false;
    for (const QByteArray &line : ics.split('\n')) {
        if (line.isEmpty()) continue;
        if (line.startsWith(' ') || line.startsWith('\t')) {
            if (!skipping) out += line + '\n';
            continue;
        }
        const qsizetype end = std::min(line.indexOf(':') < 0 ? line.size() : line.indexOf(':'),
                                       line.indexOf(';') < 0 ? line.size() : line.indexOf(';'));
        skipping = names.contains(line.left(end).trimmed().toUpper());
        if (!skipping) out += line + '\n';
    }
    return out;
}

} // namespace

QString MailService::sendInvitations(const QString &eventId, const QStringList &invitees)
{
    const QString requestId = ProviderCalls::newRequestId(QStringLiteral("invite"));
    QVariantMap event;
    for (const QVariant &value : m_store->events()) {
        if (value.toMap().value(QStringLiteral("id")).toString() == eventId) event = value.toMap();
    }
    QString error;
    const QList<QindaTK::Mime::Address> people = QindaTK::Mime::parseAddressList(invitees.join(QStringLiteral(", ")), &error);
    // The calendar's own account sends when it can, else the default one.
    const AccountRuntime *rt = runtime(event.value(QStringLiteral("remoteAccount")).toString());
    if (rt == nullptr || !rt->canSend()) rt = runtime(defaultAccountId());
    if (event.isEmpty() || people.isEmpty() || !error.isEmpty() || rt == nullptr || !rt->canSend()) {
        finishNow(requestId, QStringLiteral("rejected"),
                  event.isEmpty()                ? tr("The event no longer exists")
                  : !error.isEmpty()             ? error
                  : people.isEmpty()             ? tr("Add the people to invite")
                                                 : tr("Add an account that can send mail to invite people"));
        return requestId;
    }
    QString uid = event.value(QStringLiteral("meetingUid")).toString();
    if (uid.isEmpty()) uid = eventId.section(QLatin1Char(':'), -1) + QStringLiteral("@qindamail");
    const int sequence = event.contains(QStringLiteral("meetingSequence"))
        ? event.value(QStringLiteral("meetingSequence")).toInt() + 1 : 0;
    const QString organizer = rt->account().emailAddress;
    const QString organizerName = rt->account().fullName;

    // The VEVENT as the toolkit maps it, then the iTIP additions.
    QByteArray ics = withoutItipLines(QindaTK::ICal::fromRecord(QStringLiteral("VEVENT"), uid, event));
    const qsizetype begin = ics.indexOf("BEGIN:VCALENDAR\r\n");
    if (begin >= 0) ics.insert(begin + 17, "METHOD:REQUEST\r\n");
    QByteArray lines;
    lines += QindaTK::ICal::foldLine(QStringLiteral("SEQUENCE:") + QString::number(sequence));
    lines += personLine("ORGANIZER", QString(), organizerName, organizer);
    QVariantList stored;
    for (const QindaTK::Mime::Address &person : people) {
        lines += personLine("ATTENDEE", QStringLiteral(";ROLE=REQ-PARTICIPANT;PARTSTAT=NEEDS-ACTION;RSVP=TRUE"),
                              person.name, person.address);
        stored.append(QVariantMap{{QStringLiteral("address"), person.address}, {QStringLiteral("name"), person.name},
                                  {QStringLiteral("status"), QStringLiteral("needs-action")}});
    }
    const qsizetype end = ics.lastIndexOf("END:VEVENT\r\n");
    if (end < 0) {
        finishNow(requestId, QStringLiteral("rejected"), tr("The event could not be written as an invitation"));
        return requestId;
    }
    ics.insert(end, lines);

    QVariantMap draft = newDraft(rt->id());
    const QString draftId = draft.value(QStringLiteral("id")).toString();
    const QVariantMap file = m_attachments->importBytes(draftId + QStringLiteral("/invite"), QStringLiteral("invite.ics"),
                                                        QStringLiteral("text/calendar"), ics);
    QStringList to;
    for (const QindaTK::Mime::Address &person : people) to.append(QindaOffice::Mail::formatMailbox(person.name, person.address));
    const QString title = event.value(QStringLiteral("title")).toString();
    draft.insert(QStringLiteral("to"), to.join(QStringLiteral(", ")));
    draft.insert(QStringLiteral("subject"), (sequence > 0 ? tr("Updated invitation: ") : tr("Invitation: ")) + title);
    QString body = tr("%1 invites you to \"%2\".").arg(organizerName.isEmpty() ? organizer : organizerName, title)
        + QStringLiteral("\n\n") + tr("When: %1 – %2 (%3)").arg(event.value(QStringLiteral("startLocal")).toString(),
                                                              event.value(QStringLiteral("endLocal")).toString(),
                                                              event.value(QStringLiteral("zone")).toString());
    if (!event.value(QStringLiteral("location")).toString().isEmpty())
        body += QLatin1Char('\n') + tr("Where: %1").arg(event.value(QStringLiteral("location")).toString());
    draft.insert(QStringLiteral("body"), body);
    if (file.value(QStringLiteral("ok")).toBool()) {
        draft.insert(QStringLiteral("attachments"), QVariantList{QVariantMap{
            {QStringLiteral("id"), file.value(QStringLiteral("id"))}, {QStringLiteral("name"), QStringLiteral("invite.ics")},
            {QStringLiteral("mimeType"), QStringLiteral("text/calendar")}, {QStringLiteral("size"), ics.size()},
            {QStringLiteral("state"), QStringLiteral("local")}}});
    }
    event.insert(QStringLiteral("meetingUid"), uid);
    event.insert(QStringLiteral("meetingSequence"), sequence);
    event.insert(QStringLiteral("organizer"), organizer);
    event.insert(QStringLiteral("attendees"), stored);
    static_cast<void>(m_store->upsert(QStringLiteral("events"), event));
    static_cast<void>(saveStore());

    const QString sendId = send(draft);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(this, &MailService::sendFinished, this,
                          [this, connection, sendId, requestId](const QString &id, const QVariantMap &result) {
        if (id != sendId) return;
        disconnect(*connection);
        finishNow(requestId, result.value(QStringLiteral("state")).toString(), result.value(QStringLiteral("error")).toString());
    });
    return requestId;
}

} // namespace QindaOffice::Apps::Mail
