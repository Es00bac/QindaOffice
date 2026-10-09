// SPDX-License-Identifier: GPL-3.0-or-later
// M81 (D-090): office_mail's organizing values — color categories (Outlook's
// presets, the six defaults, IMAP keywords both ways), follow-up flags
// (Outlook's preset dates on every weekday, custom, complete, wording) and
// search folders (every preset, "any" folders, the account scope).

#include "follow_up.h"
#include "mail_categories.h"
#include "search_folders.h"

#include <QTimeZone>
#include <QtTest>

using namespace QindaOffice::Mail;

namespace {

MessageFacts message()
{
    MessageFacts f;
    f.accountId = QStringLiteral("work");
    f.senderName = QStringLiteral("Grace Hopper");
    f.senderAddress = QStringLiteral("grace@navy.example");
    f.to = QStringLiteral("ada@example.org");
    f.subject = QStringLiteral("Budget");
    f.body = QStringLiteral("Numbers inside");
    f.hasAttachments = true;
    f.importance = QStringLiteral("high");
    f.sizeBytes = 300 * 1024;
    f.received = QDateTime(QDate(2026, 9, 1), QTime(8, 0), QTimeZone::UTC);
    f.categories = {QStringLiteral("Finance")};
    f.unread = true;
    return f;
}

// RFC 3501 flag-keyword: one or more ATOM-CHARs (printable ASCII without
// the atom-specials). (QindaTK's ImapFolders::validFlag is a narrower
// allow-list for its own APPEND flags, not the protocol's rule.)
bool rfc3501Atom(const QString &keyword)
{
    if (keyword.isEmpty()) return false;
    for (const QChar c : keyword) {
        if (c.unicode() < 0x21 || c.unicode() > 0x7e || QStringView(u"(){%*\"\\]").contains(c)) return false;
    }
    return true;
}

CriteriaContext context()
{
    CriteriaContext c;
    c.myAddresses = {QStringLiteral("ada@example.org")};
    c.now = QDateTime(QDate(2026, 10, 9), QTime(12, 0), QTimeZone::UTC);
    return c;
}

} // namespace

class TestMailOrganize : public QObject {
    Q_OBJECT
private slots:
    void presetsAndDefaults()
    {
        QCOMPARE(Categories::presets().size(), 25);
        QCOMPARE(QString::fromLatin1(Categories::presets().first().name), QStringLiteral("Red"));
        QCOMPARE(QString::fromLatin1(Categories::presets().last().name), QStringLiteral("DarkCranberry"));
        for (int i = 0; i < 25; ++i) QCOMPARE(Categories::presetForColor(Categories::colorForPreset(i)), i);
        QCOMPARE(Categories::presetForColor(QStringLiteral("#0000ff")), 7);          // nearest: Blue
        QCOMPARE(Categories::presetForColor(QStringLiteral("blue")), -1);
        const QList<Category> defaults = Categories::defaults();
        QCOMPARE(defaults.size(), 6);
        QCOMPARE(defaults.at(0).name, QStringLiteral("Red category"));
        QCOMPARE(defaults.at(4).preset, 7);
        QCOMPARE(defaults.at(4).color, Categories::colorForPreset(7));
        QCOMPARE(Categories::indexOf(defaults, QStringLiteral("  green CATEGORY ")), 3);

        Category c;
        c.name = QStringLiteral("Red category");
        QVERIFY(!Categories::validate(c, defaults).isEmpty());                         // duplicate, any case
        c.name = QStringLiteral("A, B");
        QVERIFY(!Categories::validate(c).isEmpty());
        c.name = QStringLiteral("Projects");
        c.color = QStringLiteral("#12ab34");
        QVERIFY(Categories::validate(c, defaults).isEmpty());
        c.extra.insert(QStringLiteral("serverId"), QStringLiteral("abc"));
        QCOMPARE(Categories::toVariantMap(Categories::fromVariantMap(Categories::toVariantMap(c))), Categories::toVariantMap(c));
        QCOMPARE(Categories::normalized({QStringLiteral(" A "), QStringLiteral("a"), QString(), QStringLiteral("B")}),
                 (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
        // A list read from disk drops empty and duplicate names.
        const QVariantList stored{QVariantMap{{QStringLiteral("name"), QStringLiteral("X")}},
                                  QVariantMap{{QStringLiteral("name"), QStringLiteral("x")}},
                                  QVariantMap{{QStringLiteral("name"), QString()}}};
        QCOMPARE(Categories::listFromVariant(stored).size(), 1);
    }

    void keywordsRoundTrip_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("keyword");
        QTest::newRow("plain") << QStringLiteral("Finance") << QStringLiteral("Finance");
        QTest::newRow("space") << QStringLiteral("Red category") << QStringLiteral("Red_category");
        QTest::newRow("underscore") << QStringLiteral("a_b") << QStringLiteral("a&AF8-b");
        QTest::newRow("ampersand") << QStringLiteral("R&D") << QStringLiteral("R&-D");
        QTest::newRow("specials") << QStringLiteral("To do (now)") << QStringLiteral("To_do_&ACg-now&ACk-");
        QTest::newRow("dollar") << QStringLiteral("$money") << QStringLiteral("&ACQ-money");
        QTest::newRow("umlaut") << QStringLiteral("Büro") << QStringLiteral("B&APw-ro");
        QTest::newRow("emoji") << QStringLiteral("Fun 🎉") << QString();
    }

    void keywordsRoundTrip()
    {
        QFETCH(QString, name);
        QFETCH(QString, keyword);
        const QString wire = Categories::keywordFor(name);
        if (!keyword.isEmpty()) QCOMPARE(wire, keyword);
        // A keyword atom any RFC 3501 server accepts for STORE …
        QVERIFY2(rfc3501Atom(wire), qPrintable(wire));
        QVERIFY(Categories::isCategoryKeyword(wire));
        // … that reads back as the same name, with or without the list.
        QCOMPARE(Categories::nameForKeyword(wire), name);
        Category known;
        known.name = name;
        QCOMPARE(Categories::nameForKeyword(wire.toUpper() == wire ? wire : wire, {known}), name);
    }

    void systemKeywordsAreNotCategories()
    {
        for (const char *keyword : {"$Forwarded", "$MDNSent", "$Junk", "\\Flagged", "\\Seen", "$label1"}) {
            QVERIFY2(!Categories::isCategoryKeyword(QString::fromLatin1(keyword)), keyword);
            QCOMPARE(Categories::nameForKeyword(QString::fromLatin1(keyword)), QString());
        }
        // A known category wins over decoding (case-insensitive keywords).
        Category c;
        c.name = QStringLiteral("Red Category");
        QCOMPARE(Categories::nameForKeyword(QStringLiteral("red_category"), {c}), QStringLiteral("Red Category"));
        // A keyword another client set becomes a category of its own.
        QCOMPARE(Categories::nameForKeyword(QStringLiteral("NonJunk_two")), QStringLiteral("NonJunk two"));
    }

    void followUpPresets_data()
    {
        QTest::addColumn<QDate>("today");
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QDate>("start");
        QTest::addColumn<QDate>("due");
        const QDate mon(2026, 10, 5), wed(2026, 10, 7), thu(2026, 10, 8), fri(2026, 10, 9), sat(2026, 10, 10);
        QTest::newRow("today") << wed << "today" << wed << wed;
        QTest::newRow("tomorrow-fri") << fri << "tomorrow" << sat << sat;    // calendar days, as Outlook
        QTest::newRow("thisWeek-mon") << mon << "thisWeek" << mon.addDays(2) << fri;
        QTest::newRow("thisWeek-thu") << thu << "thisWeek" << fri << fri;   // +2 is past Friday
        QTest::newRow("thisWeek-fri") << fri << "thisWeek" << fri << fri;
        QTest::newRow("thisWeek-sat") << sat << "thisWeek" << sat << sat;   // the week's work is over
        QTest::newRow("nextWeek-wed") << wed << "nextWeek" << QDate(2026, 10, 12) << QDate(2026, 10, 16);
        QTest::newRow("noDate") << wed << "noDate" << QDate() << QDate();
    }

    void followUpPresets()
    {
        QFETCH(QDate, today);
        QFETCH(QString, preset);
        QFETCH(QDate, start);
        QFETCH(QDate, due);
        const FollowUp flag = FollowUps::forPreset(preset, today);
        QVERIFY(flag.isFlagged());
        QCOMPARE(flag.preset, preset);
        QCOMPARE(flag.start, start);
        QCOMPARE(flag.due, due);
        QCOMPARE(FollowUps::fromVariantMap(FollowUps::toVariantMap(flag)).due, due);
        QCOMPARE(FollowUps::toVariantMap(FollowUps::fromVariantMap(FollowUps::toVariantMap(flag))), FollowUps::toVariantMap(flag));
    }

    void followUpWeeksAndWords()
    {
        // A week that starts on Sunday (US Outlook) and a Sunday "today".
        const QDate sun(2026, 10, 11);
        FollowUp next = FollowUps::forPreset(QStringLiteral("nextWeek"), sun, Qt::Sunday);
        QCOMPARE(next.start, QDate(2026, 10, 19));
        QCOMPARE(next.due, QDate(2026, 10, 23));
        FollowUp week = FollowUps::forPreset(QStringLiteral("thisWeek"), sun, Qt::Sunday);
        QCOMPARE(week.due, QDate(2026, 10, 16));
        QCOMPARE(week.start, QDate(2026, 10, 13));
        // A four-day work week (Mon–Thu).
        FollowUp short_ = FollowUps::forPreset(QStringLiteral("thisWeek"), QDate(2026, 10, 5), Qt::Monday, {1, 2, 3, 4});
        QCOMPARE(short_.due, QDate(2026, 10, 8));
        QVERIFY(!FollowUps::forPreset(QStringLiteral("someday"), sun).isFlagged());
        QVERIFY(!FollowUps::forPreset(QStringLiteral("custom"), sun).isFlagged());

        FollowUp custom = FollowUps::custom(QDate(2026, 11, 3), QDate(2026, 11, 1));
        QCOMPARE(custom.start, QDate(2026, 11, 1));                       // never after the due date
        QCOMPARE(FollowUps::custom(QDate(), QDate(2026, 11, 1)).start, QDate(2026, 11, 1));
        const QDateTime now(QDate(2026, 10, 9), QTime(15, 0), QTimeZone::UTC);
        const FollowUp done = FollowUps::completed(custom, now);
        QVERIFY(done.isComplete() && !done.isFlagged());
        QCOMPARE(done.completed, now);
        QCOMPARE(done.due, custom.due);
        QVERIFY(FollowUps::toVariantMap(FollowUp()).isEmpty());

        const QLocale en(QLocale::English, QLocale::UnitedStates);
        const FollowUp today = FollowUps::forPreset(QStringLiteral("today"), QDate(2026, 10, 9));
        QCOMPARE(FollowUps::describe(today, en), QStringLiteral("Follow up. Start by Friday, October 9, 2026. Due by Friday, October 9, 2026."));
        QCOMPARE(FollowUps::describe(FollowUps::forPreset(QStringLiteral("noDate"), QDate(2026, 10, 9)), en), QStringLiteral("Follow up."));
        QVERIFY(FollowUps::describe(done, en).startsWith(QStringLiteral("Completed on")));
        QCOMPARE(FollowUps::dueLabel(today, QDate(2026, 10, 9), en), QStringLiteral("Today"));
        QCOMPARE(FollowUps::dueLabel(today, QDate(2026, 10, 8), en), QStringLiteral("Tomorrow"));
        QCOMPARE(FollowUps::dueLabel(today, QDate(2026, 10, 12), en), QStringLiteral("Overdue"));
        QCOMPARE(FollowUps::dueLabel(done, QDate(2026, 10, 9), en), QStringLiteral("Done"));
    }

    void searchFolderPresets_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QVariant>("argument");
        QTest::addColumn<bool>("matches");
        QTest::newRow("unread") << "unread" << QVariant() << true;
        QTest::newRow("flagged") << "flagged" << QVariant() << false;
        QTest::newRow("unreadOrFlagged") << "unreadOrFlagged" << QVariant() << true;
        QTest::newRow("important") << "important" << QVariant() << true;
        QTest::newRow("fromPeople") << "fromPeople" << QVariant(QStringLiteral("linus; grace@navy")) << true;
        QTest::newRow("fromPeople-no") << "fromPeople" << QVariant(QStringList{QStringLiteral("linus")}) << false;
        QTest::newRow("toMe") << "toMe" << QVariant() << true;
        QTest::newRow("attachments") << "attachments" << QVariant() << true;
        QTest::newRow("categorized-any") << "categorized" << QVariant() << true;
        QTest::newRow("categorized-named") << "categorized" << QVariant(QStringList{QStringLiteral("Travel")}) << false;
        QTest::newRow("large") << "large" << QVariant() << true;             // default 100 KB
        QTest::newRow("large-500") << "large" << QVariant(500) << false;
        QTest::newRow("old") << "old" << QVariant(30) << true;
        QTest::newRow("words") << "words" << QVariant(QStringLiteral("numbers")) << true;
    }

    void searchFolderPresets()
    {
        QFETCH(QString, preset);
        QFETCH(QVariant, argument);
        QFETCH(bool, matches);
        const SearchFolder folder = SearchFolders::forPreset(preset, argument);
        QVERIFY(!folder.id.isEmpty());
        QCOMPARE(folder.name, SearchFolders::presetName(preset));
        QVERIFY2(SearchFolders::validate(folder).isEmpty(), qPrintable(SearchFolders::validate(folder).join('\n')));
        QCOMPARE(SearchFolders::matches(folder, message(), context()), matches);
        QCOMPARE(SearchFolders::toVariantMap(SearchFolders::fromVariantMap(SearchFolders::toVariantMap(folder))),
                 SearchFolders::toVariantMap(folder));
    }

    void searchFolderScopeAndCustom()
    {
        SearchFolder unread = SearchFolders::forPreset(QStringLiteral("unread"));
        unread.accounts = {QStringLiteral("home")};
        QVERIFY(!SearchFolders::matches(unread, message(), context()));     // another account
        unread.accounts = {QStringLiteral("home"), QStringLiteral("work")};
        QVERIFY(SearchFolders::matches(unread, message(), context()));
        SearchFolder custom = SearchFolders::forPreset(QStringLiteral("custom"));
        QVERIFY(custom.conditions.isEmpty());
        QVERIFY(!SearchFolders::validate(custom).isEmpty());                 // nothing chosen yet
        QVERIFY(!SearchFolders::matches(custom, message(), context()));
        Condition from;
        from.type = QStringLiteral("from");
        from.values = {QStringLiteral("grace")};
        Condition read;
        read.type = QStringLiteral("unread");
        custom.conditions = {from, read};
        custom.name = QStringLiteral("Grace, unread");
        QVERIFY(SearchFolders::validate(custom).isEmpty());
        QVERIFY(SearchFolders::matches(custom, message(), context()));
        MessageFacts done = message();
        done.unread = false;
        QVERIFY(!SearchFolders::matches(custom, done, context()));
        custom.match = QStringLiteral("any");
        QVERIFY(SearchFolders::matches(custom, done, context()));
    }
};

QTEST_GUILESS_MAIN(TestMailOrganize)

#include "tst_mail_organize.moc"
