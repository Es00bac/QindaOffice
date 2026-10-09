// SPDX-License-Identifier: GPL-3.0-or-later
// Meeting requests received by mail (M64 P3, iMIP RFC 6047, D-072): an
// invitation synced from a local IMAP server shows as a meeting (what, when,
// organizer, me among the attendees), Accept puts it into the calendar and
// sends a METHOD:REPLY with PARTSTAT=ACCEPTED to the organizer through the
// account's SMTP server (a local fake), Decline takes it out again; an
// invitation opened from a file (no account to answer from) still updates
// the calendar; an event with invitees goes out as METHOD:REQUEST (and as
// an update the second time). Headless, 127.0.0.1 only.

#include "fake_imap_server.h"
#include "fake_mail_server.h"
#include "mail_test_support.h"

#include <qindatk/local_productivity_store.h>

using namespace QindaOffice::Apps::Mail;

namespace {

QByteArray invitation(const QByteArray &attendee)
{
    QByteArray ics = "BEGIN:VCALENDAR\r\nPRODID:-//Example//Calendar//EN\r\nVERSION:2.0\r\nMETHOD:REQUEST\r\n"
                     "BEGIN:VTIMEZONE\r\nTZID:Europe/Berlin\r\nBEGIN:STANDARD\r\nDTSTART:19701025T030000\r\n"
                     "TZOFFSETFROM:+0200\r\nTZOFFSETTO:+0100\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n"
                     "BEGIN:VEVENT\r\nUID:planning-42@example.org\r\nSEQUENCE:1\r\nDTSTAMP:20261001T080000Z\r\n"
                     "DTSTART;TZID=Europe/Berlin:20261015T100000\r\nDTEND;TZID=Europe/Berlin:20261015T110000\r\n"
                     "SUMMARY:Quarterly planning\r\nLOCATION:Room 4\r\n"
                     "ORGANIZER;CN=Grace Hopper:mailto:grace@example.org\r\n"
                     "ATTENDEE;CN=Ada;PARTSTAT=NEEDS-ACTION;RSVP=TRUE:mailto:" + attendee + "\r\n"
                     "ATTENDEE;CN=Alan;PARTSTAT=ACCEPTED:mailto:alan@example.org\r\n"
                     "END:VEVENT\r\nEND:VCALENDAR\r\n";
    return "From: Grace Hopper <grace@example.org>\r\nTo: " + attendee + "\r\nSubject: Invitation: Quarterly planning\r\n"
           "Date: Thu, 1 Oct 2026 08:00:00 +0000\r\nMessage-ID: <invite-42@example.org>\r\nMIME-Version: 1.0\r\n"
           "Content-Type: multipart/alternative; boundary=\"a\"\r\n\r\n"
           "--a\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nYou are invited to Quarterly planning.\r\n"
           "--a\r\nContent-Type: text/calendar; charset=utf-8; method=REQUEST\r\n\r\n" + ics + "--a--\r\n";
}

QVariantMap server(quint16 port, const char *protocol)
{
    QVariantMap map{{QStringLiteral("host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), port},
                    {QStringLiteral("security"), QStringLiteral("none")}, {QStringLiteral("userName"), QStringLiteral("ada")}};
    if (protocol != nullptr) map.insert(QStringLiteral("protocol"), QString::fromLatin1(protocol));
    return map;
}

int meetingEvents(MailService *service)
{
    int count = 0;
    for (const QVariant &value : service->store()->events())
        count += value.toMap().value(QStringLiteral("meetingUid")).toString() == QStringLiteral("planning-42@example.org");
    return count;
}

} // namespace

class TestMailMeetings : public QObject {
    Q_OBJECT

private slots:
    void acceptRepliesAndDeclineRemoves()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeImapServer imap("pw");
        MailTest::FakeMailServer smtp(MailTest::FakeMailServer::Kind::Smtp, "pw");
        imap.inbox.append({3, invitation("ada@work.example"), false});
        const QVariantMap added = service->addAccount({{QStringLiteral("kind"), QStringLiteral("imap")},
                                                       {QStringLiteral("displayName"), QStringLiteral("Work")},
                                                       {QStringLiteral("emailAddress"), QStringLiteral("ada@work.example")},
                                                       {QStringLiteral("fullName"), QStringLiteral("Ada Lovelace")},
                                                       {QStringLiteral("incoming"), server(imap.port(), "imap")},
                                                       {QStringLiteral("outgoing"), server(smtp.port(), nullptr)}},
                                                      QStringLiteral("pw"));
        QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(added.value(QStringLiteral("error")).toString()));
        const QString account = added.value(QStringLiteral("id")).toString();
        QSignalSpy finished(service, &MailService::operationFinished);
        QCOMPARE(MailTest::waitState(finished, service->syncFolder(QStringLiteral("acct:") + account + QStringLiteral(":INBOX"))).first,
                 QStringLiteral("accepted"));
        const QString id = QStringLiteral("imap:%1:INBOX:4242:3").arg(account);
        bool loaded = false;
        service->loadSource(id, [&loaded](const QByteArray &, const QString &) { loaded = true; });
        QTRY_VERIFY(loaded);

        QVariantMap meeting = service->meetingOf(id);
        QCOMPARE(meeting.value(QStringLiteral("method")).toString(), QStringLiteral("REQUEST"));
        QCOMPARE(meeting.value(QStringLiteral("title")).toString(), QStringLiteral("Quarterly planning"));
        QCOMPARE(meeting.value(QStringLiteral("location")).toString(), QStringLiteral("Room 4"));
        QCOMPARE(meeting.value(QStringLiteral("zone")).toString(), QStringLiteral("Europe/Berlin"));
        QVERIFY(meeting.value(QStringLiteral("startLocal")).toString().startsWith(QStringLiteral("2026-10-15 10:00")));
        QCOMPARE(meeting.value(QStringLiteral("organizer")).toString(), QStringLiteral("grace@example.org"));
        QCOMPARE(meeting.value(QStringLiteral("myAddress")).toString(), QStringLiteral("ada@work.example"));
        QCOMPARE(meeting.value(QStringLiteral("myStatus")).toString(), QStringLiteral("needs-action"));
        QVERIFY(meeting.value(QStringLiteral("canReply")).toBool());
        QCOMPARE(meeting.value(QStringLiteral("attendees")).toList().size(), 2);

        const auto accept = MailTest::waitState(finished, service->respondToMeeting(id, QStringLiteral("accepted")), 20000);
        QVERIFY2(accept.first == QLatin1String("accepted"), qPrintable(accept.second));
        QCOMPARE(meetingEvents(service), 1);
        QCOMPARE(service->meetingOf(id).value(QStringLiteral("myStatus")).toString(), QStringLiteral("accepted"));
        QCOMPARE(smtp.delivered.size(), 1);
        const QByteArray reply = smtp.delivered.first();
        QVERIFY(reply.contains("Subject: Accepted: Quarterly planning"));
        QVERIFY(smtp.recipients.join(' ').contains("grace@example.org"));
        // The reply.ics travels base64-encoded; decode and check it.
        const qsizetype at = reply.indexOf("reply.ics");
        QVERIFY(at > 0);
        const qsizetype bodyStart = reply.indexOf("\r\n\r\n", at) + 4;
        const QByteArray ics = QByteArray::fromBase64(reply.mid(bodyStart, reply.indexOf("--", bodyStart) - bodyStart));
        QVERIFY2(ics.contains("METHOD:REPLY"), ics.constData());
        QVERIFY(ics.contains("UID:planning-42@example.org"));
        QVERIFY(ics.contains("SEQUENCE:1"));
        QVERIFY(ics.contains("PARTSTAT=ACCEPTED"));
        QVERIFY(ics.contains("mailto:ada@work.example"));
        QVERIFY(ics.contains("BEGIN:VTIMEZONE"));
        QVERIFY(!ics.contains("alan@example.org"));                 // only the replying attendee

        // Accepting again keeps one event; declining removes it and says so.
        QCOMPARE(MailTest::waitState(finished, service->respondToMeeting(id, QStringLiteral("tentative")), 20000).first,
                 QStringLiteral("accepted"));
        QCOMPARE(meetingEvents(service), 1);
        QCOMPARE(MailTest::waitState(finished, service->respondToMeeting(id, QStringLiteral("declined")), 20000).first,
                 QStringLiteral("accepted"));
        QTRY_COMPARE(meetingEvents(service), 0);
        QTRY_COMPARE(smtp.delivered.size(), 3);
        QVERIFY(smtp.delivered.last().contains("Subject: Declined: Quarterly planning"));
    }

    void invitationsGoOutAsRequests()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeImapServer imap("pw");
        MailTest::FakeMailServer smtp(MailTest::FakeMailServer::Kind::Smtp, "pw");
        const QVariantMap added = service->addAccount({{QStringLiteral("kind"), QStringLiteral("imap")},
                                                       {QStringLiteral("displayName"), QStringLiteral("Work")},
                                                       {QStringLiteral("emailAddress"), QStringLiteral("ada@work.example")},
                                                       {QStringLiteral("fullName"), QStringLiteral("Ada Lovelace")},
                                                       {QStringLiteral("incoming"), server(imap.port(), "imap")},
                                                       {QStringLiteral("outgoing"), server(smtp.port(), nullptr)}},
                                                      QStringLiteral("pw"));
        QVERIFY(added.value(QStringLiteral("ok")).toBool());
        QSignalSpy changes(service, &MailService::eventChangeFinished);
        service->createEvent({{QStringLiteral("id"), QStringLiteral("event:plan-7")}, {QStringLiteral("title"), QStringLiteral("Planning")},
                              {QStringLiteral("startLocal"), QStringLiteral("2026-10-20T14:00:00")},
                              {QStringLiteral("endLocal"), QStringLiteral("2026-10-20T15:00:00")},
                              {QStringLiteral("zone"), QStringLiteral("Europe/Berlin")}, {QStringLiteral("location"), QStringLiteral("Room 4")}});
        QSignalSpy finished(service, &MailService::operationFinished);
        const QStringList invitees{QStringLiteral("Grace Hopper <grace@example.org>"), QStringLiteral("alan@example.org")};
        const auto first = MailTest::waitState(finished, service->sendInvitations(QStringLiteral("event:plan-7"), invitees), 20000);
        QVERIFY2(first.first == QLatin1String("accepted"), qPrintable(first.second));
        QTRY_COMPARE(smtp.delivered.size(), 1);
        const QByteArray mail = smtp.delivered.first();
        QVERIFY(mail.contains("Subject: Invitation: Planning"));
        const QByteArray rcpt = smtp.recipients.join(' ');
        QVERIFY(rcpt.contains("grace@example.org") && rcpt.contains("alan@example.org"));
        const qsizetype at = mail.indexOf("invite.ics");
        const qsizetype bodyStart = mail.indexOf("\r\n\r\n", at) + 4;
        QByteArray ics = QByteArray::fromBase64(mail.mid(bodyStart, mail.indexOf("--", bodyStart) - bodyStart));
        QVERIFY(ics.contains("\r\n "));                    // long lines are folded (RFC 5545 §3.1)…
        ics.replace("\r\n ", "");                          // …and checked unfolded
        QVERIFY2(ics.contains("METHOD:REQUEST"), ics.constData());
        QVERIFY(ics.contains("UID:plan-7@qindamail"));
        QVERIFY(ics.contains("SEQUENCE:0"));
        QVERIFY(ics.contains("ORGANIZER;CN=Ada Lovelace:mailto:ada@work.example"));
        QVERIFY(ics.contains("ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=NEEDS-ACTION;RSVP=TRUE;CN=Grace Hopper:mailto:grace@example.org"));
        QVERIFY(ics.contains("mailto:alan@example.org"));
        QVERIFY(ics.contains("SUMMARY:Planning"));
        QCOMPARE(ics.count("METHOD:"), 1);
        // The event remembers the meeting; a second send is an update.
        QVariantMap event = service->eventById(QStringLiteral("event:plan-7"));
        QCOMPARE(event.value(QStringLiteral("meetingUid")).toString(), QStringLiteral("plan-7@qindamail"));
        QCOMPARE(event.value(QStringLiteral("attendees")).toList().size(), 2);
        QCOMPARE(MailTest::waitState(finished, service->sendInvitations(QStringLiteral("event:plan-7"), invitees), 20000).first,
                 QStringLiteral("accepted"));
        QTRY_COMPARE(smtp.delivered.size(), 2);
        QVERIFY(smtp.delivered.last().contains("Subject: Updated invitation: Planning"));
        // Nobody to invite, or an event that is gone: refused.
        QCOMPARE(MailTest::waitState(finished, service->sendInvitations(QStringLiteral("event:plan-7"), {})).first,
                 QStringLiteral("rejected"));
        QCOMPARE(MailTest::waitState(finished, service->sendInvitations(QStringLiteral("event:none"), invitees)).first,
                 QStringLiteral("rejected"));
    }

    void invitationFromAFileUpdatesTheCalendarOnly()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QString path = env.dir.filePath(QStringLiteral("invite.eml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(invitation("ada@example.org"));
        file.close();
        const QString id = service->importMessageFile(path);
        QVERIFY(!id.isEmpty());
        const QVariantMap meeting = service->meetingOf(id);
        QCOMPARE(meeting.value(QStringLiteral("method")).toString(), QStringLiteral("REQUEST"));
        QVERIFY(!meeting.value(QStringLiteral("canReply")).toBool());
        QSignalSpy finished(service, &MailService::operationFinished);
        QCOMPARE(MailTest::waitState(finished, service->respondToMeeting(id, QStringLiteral("accepted"))).first,
                 QStringLiteral("accepted"));
        QCOMPARE(meetingEvents(service), 1);
        QCOMPARE(MailTest::waitState(finished, service->respondToMeeting(id, QStringLiteral("maybe"))).first,
                 QStringLiteral("rejected"));
        // The organizer cancels: the card offers to remove it, nothing is sent.
        QByteArray cancel = invitation("ada@example.org");
        cancel.replace("METHOD:REQUEST", "METHOD:CANCEL").replace("method=REQUEST", "method=CANCEL")
            .replace("Subject: Invitation:", "Subject: Cancelled:").replace("invite-42@", "cancel-42@");
        const QString cancelPath = env.dir.filePath(QStringLiteral("cancel.eml"));
        QFile cancelFile(cancelPath);
        QVERIFY(cancelFile.open(QIODevice::WriteOnly));
        cancelFile.write(cancel);
        cancelFile.close();
        const QString cancelled = service->importMessageFile(cancelPath);
        QCOMPARE(service->meetingOf(cancelled).value(QStringLiteral("method")).toString(), QStringLiteral("CANCEL"));
        QVERIFY(!service->meetingOf(cancelled).value(QStringLiteral("eventId")).toString().isEmpty());
        QCOMPARE(MailTest::waitState(finished, service->respondToMeeting(cancelled, QStringLiteral("removed"))).first,
                 QStringLiteral("accepted"));
        QTRY_COMPARE(meetingEvents(service), 0);
        // A message that is not an invitation has no meeting.
        env.service->store()->upsert(QStringLiteral("messages"),
                                     MailTest::message(QStringLiteral("plain"), QStringLiteral("Hi"), QDateTime::currentDateTimeUtc()));
        QVERIFY(service->meetingOf(QStringLiteral("plain")).isEmpty());
    }
};

MAIL_TEST_MAIN(TestMailMeetings)

#include "tst_qindamail_meetings.moc"
