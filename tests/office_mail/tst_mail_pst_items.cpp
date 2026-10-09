// SPDX-License-Identifier: GPL-3.0-or-later
// tst_mail_pst (M76): every item of the standard fixture mapped — mail,
// an embedded message, a contact, a contact group, appointments, tasks —
// and an Outlook contact group opened from a .msg. See tst_mail_pst.h.

#include "tst_mail_pst.h"

#include "outlook_items.h"
#include "pst_fixture.h"
#include "rfc822_reader.h"
#include "rfc822_writer.h"

#include <QtTest>

using namespace QindaOffice::Mail;
using namespace QindaOffice::Mail::Test;

namespace {

// Item order of buildStandardPst (folder order).
enum Item { Report, Hello, Memo, Forward, Kickoff, Reply, Grace, Team, Review, Holiday, BrandBook, Invoices, Sticky };

struct Fixture {
    PstBuilder builder{true};
    std::unique_ptr<OpenedPst> pst = std::make_unique<OpenedPst>();
    QString error;

    explicit Fixture(bool unicode, quint8 encoding = 1)
    {
        const QByteArray bytes = buildStandardPst(unicode, encoding, nullptr, &builder);
        if (!pst->open(bytes, &error)) qWarning().noquote() << "open failed:" << error;
    }
    OutlookItem item(int index)
    {
        OutlookItem out;
        std::unique_ptr<Mapi::Message> message = pst->file.openMessage(builder.messageNids.at(index), &error);
        if (!message || !readOutlookItem(*message, &out, &error)) qWarning().noquote() << "item failed:" << error;
        return out;
    }
};

} // namespace

void TestMailPst::mailItems_data()
{
    QTest::addColumn<bool>("unicode");
    QTest::newRow("unicode") << true;
    QTest::newRow("ansi") << false;
}

void TestMailPst::mailItems()
{
    QFETCH(bool, unicode);
    Fixture f(unicode);
    const OutlookItem report = f.item(Report);
    QCOMPARE(report.kind, OutlookItemKind::Mail);
    QCOMPARE(report.messageClass, QStringLiteral("IPM.Note"));
    QVERIFY(!report.unread && !report.flagged);
    const MessageContent &m = report.mail;
    QCOMPARE(m.subject, QStringLiteral("Quarterly report"));
    QCOMPARE(m.fromName, QStringLiteral("Ada Lovelace"));
    QCOMPARE(m.fromAddress, QStringLiteral("ada@example.org"));
    QCOMPARE(m.date, pstUtc(2026, 9, 30, 14));
    QCOMPARE(m.received, pstUtc(2026, 9, 30, 14, 1));
    QCOMPARE(m.messageId, QStringLiteral("q3@example.org"));
    QCOMPARE(m.to, QStringLiteral("Bob Example <bob@example.org>"));
    QCOMPARE(m.cc, QStringLiteral("Cy Young <cy@example.org>"));   // the SMTP form, never the X.500 DN
    QCOMPARE(m.bcc, QStringLiteral("Dee <dee@example.org>"));
    QVERIFY(m.html.contains(QLatin1String("Numbers <b>up</b>")));
    QCOMPARE(m.plain, QStringLiteral("Numbers up"));
    QCOMPARE(m.attachments.size(), qsizetype(2));
    QCOMPARE(m.attachments.at(0).name, QStringLiteral("report.pdf"));
    QCOMPARE(m.attachments.at(0).mimeType, QStringLiteral("application/pdf"));
    QCOMPARE(m.attachments.at(0).data.size(), qsizetype(6009));   // a value in a subnode
    QVERIFY(!m.attachments.at(0).inlinePart);
    QCOMPARE(m.attachments.at(1).contentId, QStringLiteral("chart@example.org"));
    QVERIFY(m.attachments.at(1).inlinePart);
    QCOMPARE(m.attachments.at(1).data, pstPicture());

    const OutlookItem hello = f.item(Hello);
    QVERIFY(hello.unread);
    QVERIFY(hello.flagged);
    QCOMPARE(hello.mail.plain, QStringLiteral("Hello there,\nplain text only."));
    QVERIFY(hello.mail.html.isEmpty());
    QCOMPARE(hello.mail.to, QStringLiteral("Ada Lovelace <ada@example.org>"));

    const OutlookItem memo = f.item(Memo);
    QVERIFY2(memo.mail.plain.contains(QLatin1String("Hello from RTF")), qPrintable(memo.mail.plain));

    const OutlookItem kickoff = f.item(Kickoff);
    QCOMPARE(kickoff.mail.plain.size(), qsizetype(800 * 32));
    QVERIFY(kickoff.mail.plain.endsWith(QLatin1String("Line 0799 of the kickoff notes.\n")));
    QCOMPARE(f.item(Reply).mail.to, QStringLiteral("Bob Example <bob@example.org>"));

    // Through the deterministic writer: the same item twice is the same message.
    const QByteArray first = writeRfc822(report.mail);
    QCOMPARE(writeRfc822(f.item(Report).mail), first);
    const MessageContent back = readRfc822(first);
    QCOMPARE(back.subject, m.subject);
    QCOMPARE(back.attachments.size(), qsizetype(2));
}

void TestMailPst::embeddedMessage()
{
    Fixture f(true);
    const OutlookItem forward = f.item(Forward);
    QCOMPARE(forward.mail.subject, QStringLiteral("Fwd: Original"));
    QCOMPARE(forward.mail.attachments.size(), qsizetype(1));
    const MessageAttachment &attached = forward.mail.attachments.first();
    QCOMPARE(attached.mimeType, QStringLiteral("message/rfc822"));
    QCOMPARE(attached.name, QStringLiteral("Original.eml"));
    QVERIFY(attached.embedded);
    QCOMPARE(attached.embedded->subject, QStringLiteral("Original"));
    QCOMPARE(attached.embedded->plain, QStringLiteral("The original text"));
    QCOMPARE(attached.embedded->fromAddress, QStringLiteral("eve@example.org"));
    QCOMPARE(attached.embedded->to, QStringLiteral("Ada Lovelace <ada@example.org>"));
    QVERIFY(writeRfc822(forward.mail).contains("message/rfc822"));
}

void TestMailPst::contactItem_data()
{
    mailItems_data();
}

void TestMailPst::contactItem()
{
    QFETCH(bool, unicode);
    Fixture f(unicode);
    const OutlookItem item = f.item(Grace);
    QCOMPARE(item.kind, OutlookItemKind::Contact);
    const Contact &c = item.contact;
    QCOMPARE(c.displayName, QStringLiteral("Grace Hopper"));
    QCOMPARE(c.givenName, QStringLiteral("Grace"));
    QCOMPARE(c.familyName, QStringLiteral("Hopper"));
    QCOMPARE(c.organization, QStringLiteral("Navy"));
    QCOMPARE(c.department, QStringLiteral("Computing"));
    QCOMPARE(c.title, QStringLiteral("Rear Admiral"));
    QCOMPARE(c.emails.size(), qsizetype(2));
    QCOMPARE(c.emails.at(0).value, QStringLiteral("grace@example.org"));
    QVERIFY(c.emails.at(0).preferred);
    QCOMPARE(c.emails.at(1).value, QStringLiteral("grace.hopper@navy.example"));   // EX address → its SMTP form
    QCOMPARE(c.phones, (QList<ContactValue>{{QStringLiteral("cell"), QStringLiteral("+1 555 0101"), false},
                                            {QStringLiteral("work"), QStringLiteral("+1 555 0100"), false}}));
    QCOMPARE(c.addresses.size(), qsizetype(2));
    QCOMPARE(c.addresses.at(0).type, QStringLiteral("work"));
    QCOMPARE(c.addresses.at(0).street, QStringLiteral("2 Navy Way"));
    QCOMPARE(c.addresses.at(1).type, QStringLiteral("home"));
    QCOMPARE(c.addresses.at(1).locality, QStringLiteral("Arlington"));
    QCOMPARE(c.addresses.at(1).postalCode, QStringLiteral("22201"));
    QCOMPARE(c.birthday, QStringLiteral("1906-12-09"));
    QCOMPARE(c.urls.first().value, QStringLiteral("https://grace.example.org"));
    QCOMPARE(c.notes, QStringLiteral("Pioneer of compilers."));
    QCOMPARE(c.categories, (QStringList{QStringLiteral("Navy"), QStringLiteral("Computing")}));
    QCOMPARE(c.photo, pstPicture());
    QCOMPARE(c.photoMimeType, QStringLiteral("image/png"));
}

void TestMailPst::groupItem()
{
    Fixture f(true);
    const OutlookItem item = f.item(Team);
    QCOMPARE(item.kind, OutlookItemKind::DistList);
    QVERIFY(item.contact.isGroup());
    QCOMPARE(item.contact.displayName, QStringLiteral("Team"));
    QCOMPARE(item.contact.notes, QStringLiteral("Everyone on the project"));
    QCOMPARE(item.members.size(), qsizetype(2));
    QCOMPARE(item.members.at(0).name, QStringLiteral("Grace Hopper"));
    QCOMPARE(item.members.at(0).address, QStringLiteral("grace@example.org"));
    QCOMPARE(item.members.at(0).storeUid, f.builder.recordKey);   // a contact of this file …
    QCOMPARE(item.members.at(0).contactNid, f.builder.messageNids.at(Grace));
    QCOMPARE(item.members.at(1).name, QStringLiteral("Alan Turing"));   // … and a one-off address
    QCOMPARE(item.members.at(1).address, QStringLiteral("alan@example.org"));
    QCOMPARE(item.members.at(1).contactNid, 0u);
}

void TestMailPst::appointmentItems()
{
    Fixture f(true, 2);
    const OutlookItem review = f.item(Review);
    QCOMPARE(review.kind, OutlookItemKind::Appointment);
    const OutlookAppointment &a = review.appointment;
    QCOMPARE(a.subject, QStringLiteral("Design review"));
    QCOMPARE(a.location, QStringLiteral("Room 4"));
    QCOMPARE(a.body, QStringLiteral("Agenda: wireframes"));
    QCOMPARE(a.start, pstUtc(2026, 10, 5, 16));
    QCOMPARE(a.end, pstUtc(2026, 10, 5, 17));
    QVERIFY(!a.allDay);
    QCOMPARE(a.windowsZone, QStringLiteral("Pacific Standard Time"));
    QCOMPARE(a.uid, QStringLiteral("040000008200E00074C5B7101A82E008ABCDEF"));
    QCOMPARE(a.organizer, QStringLiteral("Ada Lovelace <ada@example.org>"));
    QCOMPARE(a.attendees, QStringList{QStringLiteral("Bob Example <bob@example.org>")});
    QVERIFY(a.recurring);
    QCOMPARE(a.recurrence.type, QStringLiteral("weekly"));
    QCOMPARE(a.recurrence.interval, 1);
    QCOMPARE(a.recurrence.days, (QList<int>{1, 3}));
    QCOMPARE(a.recurrence.range, QStringLiteral("numbered"));
    QCOMPARE(a.recurrence.count, 10);
    QCOMPARE(a.recurrence.startDate, QDate(2026, 10, 5));
    QCOMPARE(a.recurrence.firstDayOfWeek, QStringLiteral("sunday"));
    QCOMPARE(a.recurrence.deletedDates, QList<QDate>{QDate(2026, 10, 12)});

    const OutlookItem holiday = f.item(Holiday);
    QVERIFY(holiday.appointment.allDay);
    QVERIFY(!holiday.appointment.recurring);
    QCOMPARE(holiday.appointment.start, pstUtc(2026, 12, 25, 8));
    QCOMPARE(holiday.appointment.windowsZone, QStringLiteral("Pacific Standard Time"));
}

void TestMailPst::taskItems()
{
    Fixture f(false, 2);
    const OutlookItem brandBook = f.item(BrandBook);
    QCOMPARE(brandBook.kind, OutlookItemKind::Task);
    QCOMPARE(brandBook.task.subject, QStringLiteral("Send brand book"));
    QCOMPARE(brandBook.task.body, QStringLiteral("Final PDF to Northwind"));
    QCOMPARE(brandBook.task.status, 1);
    QCOMPARE(brandBook.task.percentComplete, 0.5);
    QCOMPARE(brandBook.task.start, QDate(2026, 10, 1));
    QCOMPARE(brandBook.task.due, QDate(2026, 10, 15));
    QVERIFY(!brandBook.task.complete);
    QCOMPARE(brandBook.task.importance, 2);
    const OutlookItem invoices = f.item(Invoices);
    QCOMPARE(invoices.task.status, 2);
    QVERIFY(invoices.task.complete);
    QCOMPARE(invoices.task.completed, QDate(2026, 9, 20));
    QCOMPARE(invoices.task.importance, 0);
}

void TestMailPst::otherItems()
{
    Fixture f(true);
    const OutlookItem sticky = f.item(Sticky);
    QCOMPARE(sticky.kind, OutlookItemKind::Other);
    QCOMPARE(sticky.messageClass, QStringLiteral("IPM.StickyNote"));
    QCOMPARE(outlookItemKind(QStringLiteral("IPM.Schedule.Meeting.Request")), OutlookItemKind::Mail);
    QCOMPARE(outlookItemKind(QStringLiteral("IPM.TaskRequest")), OutlookItemKind::Mail);
    QCOMPARE(outlookItemKind(QStringLiteral("ipm.contact.custom")), OutlookItemKind::Contact);
    QCOMPARE(outlookItemKind(QStringLiteral("IPM.Appointment")), OutlookItemKind::Appointment);
}

void TestMailPst::msgContactGroup()
{
    // An IPM.DistList and an IPM.Contact saved as .msg read through the same
    // mapping (the named-property streams of MS-OXMSG 2.2.3).
    QByteArray entries;
    quint16 index = 0;
    for (const quint32 lid : {0x8053u, 0x8054u, 0x8055u, 0x8083u, 0x8082u})
        entries += PstBuild::le32(lid) + PstBuild::le16(quint16(3 << 1)) + PstBuild::le16(index++);
    MsgBuilder group;
    group.nameGuids = Mapi::guidBytes(Mapi::kPsetidAddress);
    group.nameEntries = entries;
    group.unicode(0x001A, QStringLiteral("IPM.DistList")).unicode(0x0037, QStringLiteral("Book club"))
        .unicode(0x8000, QStringLiteral("Book club"))
        .mvBinary(0x8001, {pstOneOff(QStringLiteral("Ann Lee"), QStringLiteral("ann@example.org")),
                           pstOneOff(QStringLiteral("Ben Ito"), QStringLiteral("ben@example.org"))})
        .mvBinary(0x8002, {pstOneOff(QStringLiteral("Ann Lee"), QStringLiteral("ann@example.org")),
                           pstOneOff(QStringLiteral("Ben Ito"), QStringLiteral("ben@example.org"))});
    OutlookItem item;
    QString error;
    QVERIFY2(readOutlookMsg(group.build(), &item, &error), qPrintable(error));
    QCOMPARE(item.kind, OutlookItemKind::DistList);
    QCOMPARE(item.contact.displayName, QStringLiteral("Book club"));
    QCOMPARE(item.members.size(), qsizetype(2));
    QCOMPARE(item.members.at(1).name, QStringLiteral("Ben Ito"));
    QCOMPARE(item.members.at(1).address, QStringLiteral("ben@example.org"));

    MsgBuilder person;
    person.nameGuids = group.nameGuids;
    person.nameEntries = entries;
    person.unicode(0x001A, QStringLiteral("IPM.Contact")).unicode(0x3001, QStringLiteral("Ann Lee"))
        .unicode(0x3A06, QStringLiteral("Ann")).unicode(0x3A11, QStringLiteral("Lee"))
        .unicode(0x8003, QStringLiteral("ann@example.org")).unicode(0x8004, QStringLiteral("SMTP"));
    QVERIFY2(readOutlookMsg(person.build(), &item, &error), qPrintable(error));
    QCOMPARE(item.kind, OutlookItemKind::Contact);
    QCOMPARE(item.contact.primaryEmail(), QStringLiteral("ann@example.org"));
    QCOMPARE(item.contact.familyName, QStringLiteral("Lee"));
}
