// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: meeting requests received by mail (M64 P3, iMIP RFC 6047,
// D-072) — what a `text/calendar; method=REQUEST` part proposes, and
// Accept / Tentative / Decline: the meeting goes into (or out of) the local
// calendar and a METHOD:REPLY goes back to the organizer through the
// account the request came to.
//
// M95 uses QindaTK CalendarMail and prepared SMTP for a proper calendar MIME part.
#include "mail_service.h"

#include "contact.h"
#include "message_content.h"
#include "provider_calls.h"

#include <qindatk/ical_codec.h>
#include <qindatk/calendar_mail.h>
#include "rfc822_writer.h"
#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/mail_mime.h>

#include <QTimeZone>
#include <QUuid>

namespace QindaOffice::Apps::Mail {

namespace {

QString partstatFor(const QString &response)
{
    return response == QLatin1String("accepted") ? QStringLiteral("ACCEPTED")
        : response == QLatin1String("tentative") ? QStringLiteral("TENTATIVE")
                                                 : QStringLiteral("DECLINED");
}

} // namespace

QVariantMap MailService::meetingOf(const QString &messageId) const
{
    const QindaOffice::Mail::MessageContent content = contentOf(messageId);
    if (content.calendar.trimmed().isEmpty()) return {};
    const QByteArray ics = content.calendar.toUtf8();
    QindaTK::CalendarMail codec;
    const auto parsed=codec.parse(QindaOffice::Mail::writeRfc822(content));
    if (!parsed.value(QStringLiteral("ok")).toBool()) return {};
    QVariantMap meeting=parsed.value(QStringLiteral("event")).toMap();
    meeting.insert(QStringLiteral("method"),parsed.value(QStringLiteral("method")));
    meeting.insert(QStringLiteral("uid"),parsed.value(QStringLiteral("uid")));
    meeting.insert(QStringLiteral("recurrenceIdRaw"),parsed.value(QStringLiteral("recurrenceIdRaw")));
    meeting.insert(QStringLiteral("authenticated"),false);
    const QString organizer=parsed.value(QStringLiteral("organizer")).toString();
    QString organizerName;
    for (const auto &property:QindaTK::ICal::parseLines(ics))
        if(property.name==QLatin1String("ORGANIZER")) { organizerName=property.param(QStringLiteral("CN"));break; }
    QVariantList attendees;
    for(const auto &value:parsed.value(QStringLiteral("attendees")).toList()) {
        auto attendee=value.toMap();attendee.insert(QStringLiteral("status"),attendee.value(QStringLiteral("partstat")).toString().toLower());attendees.append(attendee);
    }
    meeting.insert(QStringLiteral("organizer"),organizer);
    meeting.insert(QStringLiteral("organizerName"),organizerName);
    meeting.insert(QStringLiteral("attendees"),attendees);
    // Who I am in it: one of my addresses, or the address of the account it came to.
    QStringList mine = ownAddresses();
    if (const AccountRuntime *rt = runtime(ownerAccountOf(rawMessage(messageId))))
        mine.append(rt->account().emailAddress);
    QString myAddress;
    QString myStatus;
    for (const QVariant &value : std::as_const(attendees)) {
        const QVariantMap attendee = value.toMap();
        if (!myAddress.isEmpty() || !mine.contains(attendee.value(QStringLiteral("address")).toString(), Qt::CaseInsensitive))
            continue;
        myAddress = attendee.value(QStringLiteral("address")).toString();
        myStatus = attendee.value(QStringLiteral("status")).toString();
    }
    for (const QVariant &value : m_store->events()) {
        const QVariantMap event = value.toMap();
        if (event.value(QStringLiteral("meetingUid")).toString() != meeting.value(QStringLiteral("uid")).toString()) continue;
        meeting.insert(QStringLiteral("eventId"), event.value(QStringLiteral("id")));
        myStatus = event.value(QStringLiteral("meetingStatus")).toString();
    }
    const AccountRuntime *owner = runtime(ownerAccountOf(rawMessage(messageId)));
    meeting.insert(QStringLiteral("myAddress"), myAddress);
    meeting.insert(QStringLiteral("myStatus"), myStatus);
    meeting.insert(QStringLiteral("canReply"), owner != nullptr && owner->canSend() && !organizer.isEmpty()
                                                   && !myAddress.isEmpty());
    return meeting;
}

QString MailService::respondToMeeting(const QString &messageId, const QString &response)
{
    const QString requestId = ProviderCalls::newRequestId(QStringLiteral("meeting"));
    const QVariantMap meeting = meetingOf(messageId);
    // A cancellation (RFC 5546 §3.2.5) needs no answer: "removed" takes the
    // meeting out of the calendar and nothing is sent.
    if (meeting.value(QStringLiteral("method")).toString() == QLatin1String("CANCEL") && response == QLatin1String("removed")) {
        const QString cancelled = meeting.value(QStringLiteral("eventId")).toString();
        if (!cancelled.isEmpty()) static_cast<void>(deleteEvent(cancelled, QString(), QStringLiteral("series")));
        finishNow(requestId, cancelled.isEmpty() ? QStringLiteral("rejected") : QStringLiteral("accepted"),
                  cancelled.isEmpty() ? tr("The meeting is not in your calendar") : QString());
        emit meetingChanged(messageId);
        return requestId;
    }
    static const QStringList responses{QStringLiteral("accepted"), QStringLiteral("tentative"), QStringLiteral("declined")};
    if (meeting.value(QStringLiteral("method")).toString() != QLatin1String("REQUEST") || !responses.contains(response)) {
        finishNow(requestId, QStringLiteral("rejected"), tr("This message holds no meeting request to answer"));
        return requestId;
    }
    // 1. The calendar: the meeting in the local calendar, marked with its
    //    iCalendar UID and my answer; a decline takes it out again.
    const QString eventId = meeting.value(QStringLiteral("eventId")).toString();
    if (response == QLatin1String("declined")) {
        if (!eventId.isEmpty()) static_cast<void>(deleteEvent(eventId, QString(), QStringLiteral("series")));
    } else if (!eventId.isEmpty()) {
        QVariantMap record;
        for (const QVariant &value : m_store->events()) {
            if (value.toMap().value(QStringLiteral("id")).toString() == eventId) record = value.toMap();
        }
        record.insert(QStringLiteral("meetingStatus"), response);
        static_cast<void>(m_store->upsert(QStringLiteral("events"), record));
        static_cast<void>(saveStore());
    } else {
        QVariantMap event;
        for (const char *key : {"title", "description", "location", "startLocal", "endLocal", "zone", "allDay",
                                "recurrenceRule", "recurrenceRaw", "exceptions", "overrides"}) {
            if (meeting.contains(QString::fromLatin1(key))) event.insert(QString::fromLatin1(key), meeting.value(QString::fromLatin1(key)));
        }
        event.insert(QStringLiteral("calendarId"), QString::fromLatin1(kLocalCalendarId));
        event.insert(QStringLiteral("meetingUid"), meeting.value(QStringLiteral("uid")));
        event.insert(QStringLiteral("meetingStatus"), response);
        event.insert(QStringLiteral("organizer"), meeting.value(QStringLiteral("organizer")));
        static_cast<void>(createEvent(event));
    }
    // 2. The reply, through the account the request came to.
    if (!meeting.value(QStringLiteral("canReply")).toBool()) {
        finishNow(requestId, QStringLiteral("accepted"), tr("Your calendar is updated; no account can send the reply"));
        emit meetingChanged(messageId);
        return requestId;
    }
    const QVariantMap message = messageById(messageId);
    const QString myAddress = meeting.value(QStringLiteral("myAddress")).toString();
    QindaTK::CalendarMail codec;
    const auto responseCalendar=codec.reply(contentOf(messageId).calendar.toUtf8(),myAddress,partstatFor(response));
    if (!responseCalendar.value(QStringLiteral("ok")).toBool()) {
        finishNow(requestId,QStringLiteral("rejected"),responseCalendar.value(QStringLiteral("error")).toString());return requestId;
    }
    const QByteArray reply=responseCalendar.value(QStringLiteral("calendar")).toByteArray();
    QVariantMap draft = newDraft(ownerAccountOf(rawMessage(messageId)));
    const QString title = meeting.value(QStringLiteral("title")).toString();
    const QString verb = response == QLatin1String("accepted") ? tr("Accepted") : response == QLatin1String("tentative")
        ? tr("Tentative") : tr("Declined");
    draft.insert(QStringLiteral("to"), QindaOffice::Mail::formatMailbox(meeting.value(QStringLiteral("organizerName")).toString(),
                                                                         meeting.value(QStringLiteral("organizer")).toString()));
    draft.insert(QStringLiteral("subject"), verb + QStringLiteral(": ") + title);
    draft.insert(QStringLiteral("body"), tr("%1 has answered your invitation \"%2\": %3.").arg(myAddress, title, verb.toLower()));
    draft.insert(QStringLiteral("inReplyTo"), message.value(QStringLiteral("internetMessageId")));
    draft.insert(QStringLiteral("calendarText"),QString::fromUtf8(reply));
    draft.insert(QStringLiteral("calendarMethod"),QStringLiteral("REPLY"));
    const QString sendId = send(draft);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(this, &MailService::sendFinished, this,
                          [this, connection, sendId, requestId, messageId](const QString &id, const QVariantMap &result) {
        if (id != sendId) return;
        disconnect(*connection);
        finishNow(requestId, result.value(QStringLiteral("state")).toString(), result.value(QStringLiteral("error")).toString());
        emit meetingChanged(messageId);
    });
    emit meetingChanged(messageId);
    return requestId;
}

} // namespace QindaOffice::Apps::Mail
