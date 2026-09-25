// SPDX-License-Identifier: GPL-3.0-or-later
// Draft construction (reply / reply-all / forward, subject rules, quoting,
// address lists), RFC 6068 mailto: parsing and the message list's display
// rules (dateText, dayGroup, search and quick filters) — pure functions.

#include "mail_test_support.h"

#include "qindamail/src/draft_builder.h"
#include "qindamail/src/message_list_model.h"

using namespace QindaOffice::Apps::Mail;

class TestMailDrafts : public QObject {
    Q_OBJECT

private slots:
    void addressLists()
    {
        const auto list = Drafts::parseAddressList(
            QStringLiteral("\"Doe, Jane\" <jane@x.org>, Bob <bob@y.org>; carol@z.org"));
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).name, QStringLiteral("Doe, Jane"));
        QCOMPARE(list.at(0).address, QStringLiteral("jane@x.org"));
        QCOMPARE(list.at(2).address, QStringLiteral("carol@z.org"));
        QCOMPARE(Drafts::formatAddressList(list),
                 QStringLiteral("\"Doe, Jane\" <jane@x.org>, Bob <bob@y.org>, carol@z.org"));
    }

    void subjects_data()
    {
        QTest::addColumn<QString>("subject");
        QTest::addColumn<QString>("reply");
        QTest::addColumn<QString>("forward");
        QTest::newRow("plain") << "Lunch" << "Re: Lunch" << "Fwd: Lunch";
        QTest::newRow("re") << "Re: Lunch" << "Re: Lunch" << "Fwd: Re: Lunch";
        QTest::newRow("RE upper") << "RE: Lunch" << "RE: Lunch" << "Fwd: RE: Lunch";
        QTest::newRow("counter") << "Re[2]: Lunch" << "Re[2]: Lunch" << "Fwd: Re[2]: Lunch";
        QTest::newRow("german") << "AW: Mittag" << "AW: Mittag" << "Fwd: AW: Mittag";
        QTest::newRow("fwd") << "Fwd: Lunch" << "Re: Fwd: Lunch" << "Fwd: Lunch";
        QTest::newRow("fw") << "FW: Lunch" << "Re: FW: Lunch" << "FW: Lunch";
        QTest::newRow("empty") << "" << "Re: " << "Fwd: ";
    }

    void subjects()
    {
        QFETCH(QString, subject);
        QFETCH(QString, reply);
        QFETCH(QString, forward);
        QCOMPARE(Drafts::replySubject(subject), reply);
        QCOMPARE(Drafts::forwardSubject(subject), forward);
    }

    void quoting()
    {
        QCOMPARE(Drafts::quoteBody(QStringLiteral("one\r\ntwo\n> old\n")), QStringLiteral("> one\n> two\n>> old"));
    }

    void replyAndReplyAll()
    {
        const QVariantMap source = MailTest::message(
            QStringLiteral("m1"), QStringLiteral("Plans"), QDateTime::currentDateTimeUtc(),
            {{QStringLiteral("to"), QStringLiteral("Ada <ada@example.org>, Bob <bob@example.org>")},
             {QStringLiteral("cc"), QStringLiteral("carol@example.org, ADA@example.org")},
             {QStringLiteral("threadId"), QStringLiteral("root@example.org")},
             {QStringLiteral("bodyPlain"), QStringLiteral("See you.\nG.")}});
        const QStringList own{QStringLiteral("ada@example.org")};
        const QVariantMap reply = Drafts::reply(source, false, own, QStringLiteral("Ada"), QLocale::c());
        QCOMPARE(reply.value(QStringLiteral("to")).toString(), QStringLiteral("Grace Hopper <grace@example.org>"));
        QCOMPARE(reply.value(QStringLiteral("cc")).toString(), QString());
        QCOMPARE(reply.value(QStringLiteral("subject")).toString(), QStringLiteral("Re: Plans"));
        QCOMPARE(reply.value(QStringLiteral("inReplyTo")).toString(), QStringLiteral("m1@example.org"));
        QCOMPARE(reply.value(QStringLiteral("references")).toString(),
                 QStringLiteral("<root@example.org> <m1@example.org>"));
        const QString body = reply.value(QStringLiteral("body")).toString();
        QVERIFY(body.contains(QStringLiteral("\n-- \nAda")));
        QVERIFY(body.contains(QStringLiteral("Grace Hopper wrote:\n> See you.\n> G.")));
        QVERIFY(body.indexOf(QStringLiteral("-- \nAda")) < body.indexOf(QStringLiteral("> See you.")));

        const QVariantMap all = Drafts::reply(source, true, own, {}, QLocale::c());
        QCOMPARE(all.value(QStringLiteral("to")).toString(),
                 QStringLiteral("Grace Hopper <grace@example.org>, Bob <bob@example.org>"));
        // Own address (in any case) and duplicates never come back.
        QCOMPARE(all.value(QStringLiteral("cc")).toString(), QStringLiteral("carol@example.org"));

        // A reply to a message we sent continues with its recipients.
        QVariantMap mine = source;
        mine.insert(QStringLiteral("sender"), QStringLiteral("Ada"));
        mine.insert(QStringLiteral("senderAddress"), QStringLiteral("ada@example.org"));
        const QVariantMap followUp = Drafts::reply(mine, false, own, {}, QLocale::c());
        QCOMPARE(followUp.value(QStringLiteral("to")).toString(), QStringLiteral("Bob <bob@example.org>"));
    }

    void forwardDraft()
    {
        const QVariantMap source = MailTest::message(QStringLiteral("m2"), QStringLiteral("Report"),
                                                     QDateTime::currentDateTimeUtc());
        const QVariantMap forward = Drafts::forward(source, {}, QLocale::c());
        QCOMPARE(forward.value(QStringLiteral("subject")).toString(), QStringLiteral("Fwd: Report"));
        QCOMPARE(forward.value(QStringLiteral("to")).toString(), QString());
        const QString body = forward.value(QStringLiteral("body")).toString();
        QVERIFY(body.contains(QStringLiteral("---------- Forwarded message ----------")));
        QVERIFY(body.contains(QStringLiteral("From: Grace Hopper <grace@example.org>")));
        QVERIFY(body.contains(QStringLiteral("Subject: Report")));
        QVERIFY(body.endsWith(QStringLiteral("Body of Report")));
        QVERIFY(forward.value(QStringLiteral("inReplyTo")).toString().isEmpty());
    }

    // The examples of RFC 6068 §6.1 and §6.2.
    void mailto_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("to");
        QTest::addColumn<QString>("cc");
        QTest::addColumn<QString>("subject");
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("inReplyTo");
        QTest::newRow("simple") << "mailto:chris@example.com" << "chris@example.com" << "" << "" << "" << "";
        QTest::newRow("subject") << "mailto:infobot@example.com?subject=current-issue" << "infobot@example.com" << ""
                                 << "current-issue" << "" << "";
        QTest::newRow("body") << "mailto:infobot@example.com?body=send%20current-issue" << "infobot@example.com" << ""
                              << "" << "send current-issue" << "";
        QTest::newRow("crlf") << "mailto:infobot@example.com?body=send%20current-issue%0D%0Asend%20index"
                              << "infobot@example.com" << "" << "" << "send current-issue\nsend index" << "";
        QTest::newRow("in-reply-to") << "mailto:list@example.org?In-Reply-To=%3C3469A91.D10AF4C@example.com%3E"
                                     << "list@example.org" << "" << "" << "" << "3469A91.D10AF4C@example.com";
        QTest::newRow("cc") << "mailto:joe@example.com?cc=bob@example.com&body=hello" << "joe@example.com"
                            << "bob@example.com" << "" << "hello" << "";
        QTest::newRow("percent") << "mailto:gorby%25kremvax@example.com" << "gorby%kremvax@example.com" << "" << ""
                                 << "" << "";
        QTest::newRow("question") << "mailto:unlikely%3Faddress@example.com?blat=foop" << "unlikely?address@example.com"
                                  << "" << "" << "" << "";
        QTest::newRow("ampersand") << "mailto:Mike%26family@example.org" << "Mike&family@example.org" << "" << "" << ""
                                   << "";
        QTest::newRow("quoted") << "mailto:%22not%40me%22@example.org" << "\"not@me\"@example.org" << "" << "" << ""
                                << "";
        QTest::newRow("to header") << "mailto:?to=joe@example.com&cc=bob@example.com&body=hello" << "joe@example.com"
                                   << "bob@example.com" << "" << "hello" << "";
        QTest::newRow("two to") << "mailto:?to=addr1@an.example,addr2@an.example" << "addr1@an.example, addr2@an.example"
                                << "" << "" << "" << "";
        QTest::newRow("path and to") << "mailto:addr1@an.example?to=addr2@an.example"
                                     << "addr1@an.example, addr2@an.example" << "" << "" << "" << "";
        QTest::newRow("utf-8") << "mailto:user@example.org?subject=caf%C3%A9" << "user@example.org" << "" << "café" << ""
                               << "";
        QTest::newRow("idn") << "mailto:user@%E7%B4%8D%E8%B1%86.example.org?subject=Test&body=NATTO"
                             << "user@xn--99zt52a.example.org" << "" << "Test" << "NATTO" << "";
        QTest::newRow("plus is data") << "mailto:a@b.org?subject=1+1" << "a@b.org" << "" << "1+1" << "" << "";
    }

    void mailto()
    {
        QFETCH(QString, url);
        const QVariantMap parsed = Drafts::parseMailto(url);
        QVERIFY(parsed.value(QStringLiteral("ok")).toBool());
        QTEST(parsed.value(QStringLiteral("to")).toString(), "to");
        QTEST(parsed.value(QStringLiteral("cc")).toString(), "cc");
        QTEST(parsed.value(QStringLiteral("subject")).toString(), "subject");
        QTEST(parsed.value(QStringLiteral("body")).toString(), "body");
        QTEST(parsed.value(QStringLiteral("inReplyTo")).toString(), "inReplyTo");
    }

    void mailtoRejectsOtherSchemes()
    {
        QVERIFY(!Drafts::parseMailto(QStringLiteral("https://example.org")).value(QStringLiteral("ok")).toBool());
    }

    void dateTextAndDayGroup()
    {
        const QLocale locale(QLocale::English, QLocale::UnitedKingdom); // weeks start on Monday
        // Thursday 24 September 2026, 15:00 local.
        const QDateTime now(QDate(2026, 9, 24), QTime(15, 0));
        const auto at = [](int day, int hour) { return QDateTime(QDate(2026, 9, day), QTime(hour, 5)); };
        QCOMPARE(MessageListModel::dateText(at(24, 9), now, locale), locale.toString(QTime(9, 5), QLocale::ShortFormat));
        QCOMPARE(MessageListModel::dayGroup(at(24, 9), now, locale), QStringLiteral("Today"));
        QCOMPARE(MessageListModel::dayGroup(at(23, 9), now, locale), QStringLiteral("Yesterday"));
        QCOMPARE(MessageListModel::dateText(at(22, 9), now, locale), locale.dayName(2, QLocale::ShortFormat));
        QCOMPARE(MessageListModel::dayGroup(at(21, 9), now, locale), QStringLiteral("This week"));
        QCOMPARE(MessageListModel::dayGroup(at(20, 9), now, locale), QStringLiteral("Earlier"));
        QCOMPARE(MessageListModel::dateText(at(20, 9), now, locale),
                 locale.toString(QDate(2026, 9, 20), QLocale::ShortFormat));
    }

    void searchAndFilters()
    {
        const QVariantMap record = MailTest::message(
            QStringLiteral("m3"), QStringLiteral("Quarterly numbers"), QDateTime::currentDateTimeUtc(),
            {{QStringLiteral("unread"), false},
             {QStringLiteral("attachments"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("a")}}}}});
        QVERIFY(MessageListModel::matches(record, {QStringLiteral("QUARTERLY"), QStringLiteral("grace")},
                                          QStringLiteral("all")));
        QVERIFY(!MessageListModel::matches(record, {QStringLiteral("quarterly"), QStringLiteral("nobody")},
                                           QStringLiteral("all")));
        QVERIFY(MessageListModel::matches(record, {}, QStringLiteral("attachments")));
        QVERIFY(!MessageListModel::matches(record, {}, QStringLiteral("unread")));
        QVERIFY(!MessageListModel::matches(record, {}, QStringLiteral("flagged")));
    }
};

MAIL_TEST_MAIN(TestMailDrafts)

#include "tst_qindamail_drafts.moc"
