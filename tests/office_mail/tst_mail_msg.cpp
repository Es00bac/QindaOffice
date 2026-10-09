// SPDX-License-Identifier: GPL-3.0-or-later
// tst_mail_msg, Outlook messages (envelope): readMsg() over .msg files built
// by cfb_writer_msg.h — From/Sender/recipients, PT_STRING8 code pages,
// transport-header and display-list fallbacks, date fallbacks and files that
// are not messages. Also the test main for the executable; bodies,
// attachments and embedded messages are in tst_mail_msg_parts.cpp.

#include "tst_mail_msg.h"

#include "tst_mail_msg_support.h"

#include <QRandomGenerator>
#include <QTest>

using namespace QindaOffice::Mail;
using namespace QindaOffice::Mail::Test;

void TestMailMsg::msgEnvelope()
{
    MsgBuilder msg;
    msg.unicode(0x001A, QStringLiteral("IPM.Note"))
        .unicode(0x0037, QStringLiteral("Quarterly report: draft"))
        .unicode(0x0042, QStringLiteral("Ada Lovelace"))
        .unicode(0x0064, QStringLiteral("EX"))
        .unicode(0x0065, QStringLiteral("/O=EXAMPLE/OU=EXCHANGE ADMINISTRATIVE GROUP/CN=RECIPIENTS/CN=ADA"))
        .unicode(0x5D02, QStringLiteral("ada@example.org"))
        .unicode(0x0C1A, QStringLiteral("Assistant Bot"))
        .unicode(0x0C1E, QStringLiteral("SMTP"))
        .unicode(0x0C1F, QStringLiteral("bot@example.org"))
        .time(0x0039, utc(2024, 3, 5, 14, 30, 15))
        .time(0x0E06, utc(2024, 3, 5, 14, 31, 2))
        .time(0x3007, utc(2024, 3, 5, 14, 29, 0))
        .unicode(0x1035, QStringLiteral("<id-123@example.org>"))
        .unicode(0x1042, QStringLiteral("<parent@example.org>"))
        .unicode(0x1039, QStringLiteral("<root@example.org> <parent@example.org>"))
        .unicode(0x1000, QStringLiteral("Body text"))
        .unicode(0x0E04, QStringLiteral("ignored when recipient objects exist"));
    msg.recipients = {
        recipient(1, QStringLiteral("Bob Builder"), QStringLiteral("SMTP"), QStringLiteral("bob@example.org")),
        recipient(1, QStringLiteral("Doe, John"), QStringLiteral("EX"), QStringLiteral("/O=EXAMPLE/OU=X/CN=JOHN"),
                  QStringLiteral("john.doe@example.org")),
        recipient(2, QStringLiteral("Carol"), QStringLiteral("EX"), QStringLiteral("/O=EXAMPLE/OU=X/CN=CAROL")),
        recipient(3, {}, QStringLiteral("SMTP"), QStringLiteral("dave@example.org")),
        recipient(0x10000001, QStringLiteral("Eve"), QStringLiteral("SMTP"), QStringLiteral("eve@example.org")),
        recipient(2, QStringLiteral("Frank \"The Tank\""), QStringLiteral("SMTP"), QStringLiteral("frank@example.org")),
        recipient(1, QStringLiteral("Mallory"), QStringLiteral("SMTP"), QStringLiteral("not-an-address")),
    };
    MessageContent m;
    QVERIFY(readOk(msg.build(), &m));
    QCOMPARE(m.subject, QStringLiteral("Quarterly report: draft"));
    QCOMPARE(m.fromName, QStringLiteral("Ada Lovelace"));
    QCOMPARE(m.fromAddress, QStringLiteral("ada@example.org"));
    QCOMPARE(m.senderName, QStringLiteral("Assistant Bot"));
    QCOMPARE(m.senderAddress, QStringLiteral("bot@example.org"));
    QCOMPARE(m.to, QStringLiteral("Bob Builder <bob@example.org>, \"Doe, John\" <john.doe@example.org>, "
                                  "Eve <eve@example.org>, Mallory"));
    QCOMPARE(m.cc, QStringLiteral("Carol, \"Frank \\\"The Tank\\\"\" <frank@example.org>"));
    QCOMPARE(m.bcc, QStringLiteral("dave@example.org"));
    QCOMPARE(m.date, utc(2024, 3, 5, 14, 30, 15));
    QCOMPARE(m.received, utc(2024, 3, 5, 14, 31, 2));
    QCOMPARE(m.messageId, QStringLiteral("id-123@example.org"));
    QCOMPARE(m.inReplyTo, QStringLiteral("parent@example.org"));
    QCOMPARE(m.references, QStringLiteral("<root@example.org> <parent@example.org>"));
    QCOMPARE(m.plain, QStringLiteral("Body text"));
    QVERIFY(m.html.isEmpty());
    QVERIFY(m.replyTo.isEmpty());
    QVERIFY(m.calendar.isEmpty() && m.calendarMethod.isEmpty());
    QVERIFY(m.attachments.isEmpty());
    QVERIFY(m.extraHeaders.isEmpty());

    // A Sender equal to From is not repeated; a split subject is joined and
    // control characters never reach a single-line field.
    MsgBuilder same;
    same.unicode(0x003D, QStringLiteral("RE: "))
        .unicode(0x0E1D, QStringLiteral("Lunch\r\nplans\u0001"))
        .unicode(0x0042, QStringLiteral("Ada"))
        .unicode(0x5D02, QStringLiteral("ada@example.org"))
        .unicode(0x0C1A, QStringLiteral("Ada"))
        .unicode(0x5D01, QStringLiteral("ADA@example.org"));
    QVERIFY(readOk(same.build(), &m));
    QCOMPARE(m.subject, QStringLiteral("RE: Lunch plans"));
    QCOMPARE(m.fromAddress, QStringLiteral("ada@example.org"));
    QVERIFY(m.senderName.isEmpty() && m.senderAddress.isEmpty());
}

void TestMailMsg::msgString8CodePages_data()
{
    QTest::addColumn<QByteArray>("text8");
    QTest::addColumn<QString>("expected");
    QTest::addColumn<int>("codePageProperty");
    QTest::addColumn<int>("codePage");
    QTest::newRow("cp1252, PidTagMessageCodepage")
        << QByteArray("Caf\xe9 \x80 \x93quoted\x94") << QStringLiteral("Café € “quoted”") << 0x3FFD << 1252;
    QTest::newRow("cp1251, PidTagMessageCodepage")
        << QByteArray("\xcf\xf0\xe8\xe2\xe5\xf2 \xec\xe8\xf0") << QStringLiteral("Привет мир") << 0x3FFD << 1251;
    QTest::newRow("cp1251, PidTagInternetCodepage only")
        << QByteArray("\xcf\xf0\xe8\xe2\xe5\xf2 \xec\xe8\xf0") << QStringLiteral("Привет мир") << 0x3FDE << 1251;
    QTest::newRow("no code page: windows-1252") << QByteArray("Caf\xe9") << QStringLiteral("Café") << 0 << 0;
}

void TestMailMsg::msgString8CodePages()
{
    QFETCH(QByteArray, text8);
    QFETCH(QString, expected);
    QFETCH(int, codePageProperty);
    QFETCH(int, codePage);
    MsgBuilder msg;
    if (codePageProperty) msg.longValue(quint16(codePageProperty), quint32(codePage));
    msg.string8(0x0037, text8)
        .string8(0x0042, text8)
        .string8(0x0064, "SMTP")
        .string8(0x0065, "s@example.org")
        .string8(0x1000, text8 + "\r\nline 2");
    msg.recipients.append(MsgProps().string8(0x3001, text8).string8(0x3002, "SMTP").string8(0x3003, "r@example.org"));
    MessageContent m;
    QVERIFY(readOk(msg.build(), &m));
    QCOMPARE(m.subject, expected);
    QCOMPARE(m.fromName, expected);
    QCOMPARE(m.fromAddress, QStringLiteral("s@example.org"));
    QCOMPARE(m.plain, expected + QStringLiteral("\r\nline 2"));
    QVERIFY2(m.to.contains(expected) && m.to.endsWith(QStringLiteral("<r@example.org>")), qPrintable(m.to));
}

void TestMailMsg::msgTransportHeaderFallback()
{
    const QString headers = QStringLiteral(
        "Microsoft Mail Internet Headers Version 2.0\r\n"
        "Received: from mx.example.org\r\n"
        "\tby host.example.org; Tue, 1 Jul 2003 10:52:40 +0200\r\n"
        "From: =?utf-8?Q?Jos=C3=A9?= =?utf-8?Q?_P=C3=A9rez?= <jose@example.org>\r\n"
        "To: \"Doe, Jane\" <jane@example.org>,\r\n"
        " bob@example.org (Bob B)\r\n"
        "Cc: =?iso-8859-1?B?SvxyZ2Vu?= <juergen@example.org>, undisclosed-recipients:;\r\n"
        "Reply-To: list@example.org\r\n"
        "Subject: not used\r\n"
        "Date: Tue, 1 Jul 2003 10:52:37 +0200\r\n"
        "Message-ID: <hdr-id@example.org>\r\n"
        "In-Reply-To: <prev@example.org> (comment)\r\n"
        "References: <a@example.org>\r\n"
        " <prev@example.org>\r\n"
        "\r\n"
        "X-After-Headers: not a header\r\n");
    MsgBuilder msg;
    msg.unicode(0x0037, QStringLiteral("Props subject")).unicode(0x007D, headers);
    MessageContent m;
    QVERIFY(readOk(msg.build(), &m));
    QCOMPARE(m.subject, QStringLiteral("Props subject"));
    QCOMPARE(m.fromName, QStringLiteral("José Pérez"));
    QCOMPARE(m.fromAddress, QStringLiteral("jose@example.org"));
    QCOMPARE(m.to, QStringLiteral("\"Doe, Jane\" <jane@example.org>, Bob B <bob@example.org>"));
    QCOMPARE(m.cc, QStringLiteral("Jürgen <juergen@example.org>"));
    QVERIFY(m.bcc.isEmpty());
    QCOMPARE(m.replyTo, QStringLiteral("list@example.org"));
    QCOMPARE(m.date, utc(2003, 7, 1, 8, 52, 37));
    QCOMPARE(m.messageId, QStringLiteral("hdr-id@example.org"));
    QCOMPARE(m.inReplyTo, QStringLiteral("prev@example.org"));
    QCOMPARE(m.references, QStringLiteral("<a@example.org> <prev@example.org>"));
    QVERIFY(m.extraHeaders.isEmpty());
}

void TestMailMsg::msgDisplayListFallback()
{
    MsgBuilder msg;
    msg.unicode(0x0037, QStringLiteral("No recipient objects"))
        .unicode(0x0E04, QStringLiteral("Alice Smith; Doe, John;  "))
        .unicode(0x0E03, QStringLiteral("Carol"))
        .unicode(0x0C1A, QStringLiteral("Bob Sender"))
        .unicode(0x5D01, QStringLiteral("bob@example.org"));
    MessageContent m;
    QVERIFY(readOk(msg.build(), &m));
    QCOMPARE(m.to, QStringLiteral("Alice Smith, \"Doe, John\""));
    QCOMPARE(m.cc, QStringLiteral("Carol"));
    QVERIFY(m.bcc.isEmpty());
    QCOMPARE(m.fromName, QStringLiteral("Bob Sender"));   // no From: the Sender is the author
    QCOMPARE(m.fromAddress, QStringLiteral("bob@example.org"));
    QVERIFY(m.senderName.isEmpty() && m.senderAddress.isEmpty());

    MsgBuilder exchange;   // Exchange-only From, the same person as an SMTP Sender
    exchange.unicode(0x0042, QStringLiteral("Exchange User"))
        .unicode(0x0064, QStringLiteral("EX"))
        .unicode(0x0065, QStringLiteral("/O=ORG/OU=EXCHANGE/CN=RECIPIENTS/CN=EXUSER"))
        .unicode(0x0C1A, QStringLiteral("Exchange User"))
        .unicode(0x0C1E, QStringLiteral("EX"))
        .unicode(0x0C1F, QStringLiteral("/O=ORG/OU=EXCHANGE/CN=RECIPIENTS/CN=EXUSER"))
        .unicode(0x5D01, QStringLiteral("ex@example.org"));
    QVERIFY(readOk(exchange.build(), &m));
    QCOMPARE(m.fromName, QStringLiteral("Exchange User"));
    QCOMPARE(m.fromAddress, QStringLiteral("ex@example.org"));
    QVERIFY(m.senderName.isEmpty() && m.senderAddress.isEmpty());

    MsgBuilder delegated;   // Exchange-only From, a different Sender: From keeps only the name
    delegated.unicode(0x0042, QStringLiteral("Boss"))
        .unicode(0x0064, QStringLiteral("EX"))
        .unicode(0x0065, QStringLiteral("/O=ORG/CN=BOSS"))
        .unicode(0x0C1A, QStringLiteral("Assistant"))
        .unicode(0x5D01, QStringLiteral("assistant@example.org"));
    QVERIFY(readOk(delegated.build(), &m));
    QCOMPARE(m.fromName, QStringLiteral("Boss"));
    QVERIFY(m.fromAddress.isEmpty());
    QCOMPARE(m.senderName, QStringLiteral("Assistant"));
    QCOMPARE(m.senderAddress, QStringLiteral("assistant@example.org"));

    MsgBuilder nameless;   // a nameless Sender is not assumed to be the From person
    nameless.unicode(0x0042, QStringLiteral("Boss"))
        .unicode(0x0064, QStringLiteral("EX"))
        .unicode(0x0065, QStringLiteral("/O=ORG/CN=BOSS"))
        .unicode(0x5D01, QStringLiteral("delegate@example.org"));
    QVERIFY(readOk(nameless.build(), &m));
    QCOMPARE(m.fromName, QStringLiteral("Boss"));
    QVERIFY(m.fromAddress.isEmpty());
    QVERIFY(m.senderName.isEmpty());
    QCOMPARE(m.senderAddress, QStringLiteral("delegate@example.org"));
}

void TestMailMsg::msgDateFallbacks()
{
    const QDateTime delivery = utc(2023, 1, 2, 3, 4, 5);
    const QDateTime creation = utc(2022, 12, 31, 23, 59, 59);
    MessageContent m;
    MsgBuilder delivered;
    delivered.unicode(0x0037, QStringLiteral("d")).time(0x0E06, delivery).time(0x3007, creation);
    QVERIFY(readOk(delivered.build(), &m));
    QCOMPARE(m.date, delivery);
    QCOMPARE(m.received, delivery);
    MsgBuilder created;
    created.unicode(0x0037, QStringLiteral("c")).time(0x3007, creation);
    QVERIFY(readOk(created.build(), &m));
    QCOMPARE(m.date, creation);
    QVERIFY(!m.received.isValid());
    MsgBuilder mapiNull;   // 4501-01-01 is MAPI's "no date"
    mapiNull.time(0x0039, utc(4501, 1, 1, 0, 0, 0)).time(0x3007, creation);
    QVERIFY(readOk(mapiNull.build(), &m));
    QCOMPARE(m.date, creation);
    MsgBuilder bare;   // no dates and no bodies at all
    bare.unicode(0x0037, QStringLiteral("none"));
    bare.fixed.append({(quint32(0x0039) << 16) | kMsgPtSysTime, 0});
    QVERIFY(readOk(bare.build(), &m));
    QVERIFY(!m.date.isValid() && !m.received.isValid());
    QVERIFY(m.plain.isEmpty() && m.html.isEmpty() && m.attachments.isEmpty());
    QVERIFY(m.to.isEmpty() && m.fromName.isEmpty() && m.fromAddress.isEmpty());
}

void TestMailMsg::msgNotAMessage()
{
    const QByteArray document = buildCfb(CfbNode::folder(QStringLiteral("Root Entry"),
        {CfbNode::stream(QStringLiteral("WordDocument"), QByteArray(5000, 'w')),
         CfbNode::stream(QStringLiteral("\u0005SummaryInformation"), QByteArray(200, 's'))}));
    QVERIFY(readFailure(document).contains(QLatin1String("not an Outlook message")));
    QVERIFY(readFailure("From: someone\r\n\r\nbody").contains(QLatin1String("Cannot read the Outlook message")));
    QVERIFY(readFailure(QByteArray()).contains(QLatin1String("Cannot read the Outlook message")));

    MessageContent m;
    const QByteArray subjectOnly = buildCfb(CfbNode::folder(QStringLiteral("Root Entry"),
        {CfbNode::stream(QStringLiteral("__substg1.0_0037001F"), msgUtf16Le(QStringLiteral("Just a subject")))}));
    QVERIFY(readOk(subjectOnly, &m));
    QCOMPARE(m.subject, QStringLiteral("Just a subject"));
    QVERIFY(m.plain.isEmpty() && m.to.isEmpty() && m.attachments.isEmpty() && !m.date.isValid());

    const QByteArray shortProperties = buildCfb(CfbNode::folder(QStringLiteral("Root Entry"),
        {CfbNode::stream(QStringLiteral("__properties_version1.0"), QByteArray(10, '\0')),
         CfbNode::stream(QStringLiteral("__substg1.0_0037001F"), msgUtf16Le(QStringLiteral("short props")))}));
    QVERIFY(readOk(shortProperties, &m));
    QCOMPARE(m.subject, QStringLiteral("short props"));

    MsgBuilder odd;   // entries of the wrong kind are ignored
    odd.unicode(0x0037, QStringLiteral("odd"));
    odd.extraNodes = {CfbNode::folder(QStringLiteral("__substg1.0_1000001F")),
                      CfbNode::stream(QStringLiteral("__recip_version1.0_#00000005"), "not a storage")};
    QVERIFY(readOk(odd.build(), &m));
    QCOMPARE(m.subject, QStringLiteral("odd"));
    QVERIFY(m.plain.isEmpty() && m.to.isEmpty());

    MsgBuilder huge;   // a property stream over 1 MiB is hostile
    huge.unicode(0x0037, QStringLiteral("huge"));
    for (int i = 0; i < 70000; ++i) huge.longValue(quint16(0x8000 + i % 0x7000), quint32(i));
    QVERIFY(readFailure(huge.build()).contains(QLatin1String("1 MiB")));
}

QTEST_GUILESS_MAIN(TestMailMsg)
