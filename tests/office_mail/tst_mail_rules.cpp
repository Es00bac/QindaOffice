// SPDX-License-Identifier: GPL-3.0-or-later
// M81 (D-090): the rule engine of office_mail — every condition (as a rule
// condition and as an exception), every action's effect on the plan, the
// order of rules and "stop processing more rules", validation, the
// conditions' sentences, and the rules file's JSON round trip and import.
// Pure: no store, no network.

#include "mail_rules.h"

#include <QJsonDocument>
#include <QTimeZone>
#include <QJsonObject>
#include <QtTest>

using namespace QindaOffice::Mail;

namespace {

MessageFacts sample()
{
    MessageFacts f;
    f.id = QStringLiteral("m1");
    f.accountId = QStringLiteral("work");
    f.folderKey = QStringLiteral("acct:work:INBOX");
    f.senderName = QStringLiteral("Grace Hopper");
    f.senderAddress = QStringLiteral("grace@navy.example");
    f.to = QStringLiteral("Ada Lovelace <ada@example.org>");
    f.cc = QString();
    f.subject = QStringLiteral("Quarterly budget review");
    f.body = QStringLiteral("Please read the attached numbers before Friday.");
    f.headers = QStringLiteral("From: Grace Hopper <grace@navy.example>\r\nX-Mailer: Fleet 2.0\r\nImportance: High\r\n");
    f.headersKnown = true;
    f.hasAttachments = true;
    f.importance = QStringLiteral("high");
    f.sizeBytes = 250 * 1024;
    f.received = QDateTime(QDate(2026, 10, 7), QTime(9, 30), QTimeZone::UTC);
    f.categories = {QStringLiteral("Blue category")};
    f.flagged = false;
    f.unread = true;
    return f;
}

CriteriaContext context()
{
    CriteriaContext c;
    c.myAddresses = {QStringLiteral("ADA@example.org"), QStringLiteral("ada@work.example")};
    c.now = QDateTime(QDate(2026, 10, 9), QTime(12, 0), QTimeZone::UTC);
    return c;
}

Condition condition(const QString &type, const QStringList &values = {}, const QString &value = {})
{
    Condition c;
    c.type = type;
    c.values = values;
    c.value = value;
    return c;
}

RuleAction action(const QString &type, const QString &value = {}, const QString &folder = {},
                  const QStringList &values = {})
{
    RuleAction a;
    a.type = type;
    a.value = value;
    a.folderKey = folder;
    a.values = values;
    return a;
}

MailRule rule(const QString &id, const QList<Condition> &conditions, const QList<RuleAction> &actions,
              const QList<Condition> &exceptions = {})
{
    MailRule r;
    r.id = id;
    r.name = QStringLiteral("Rule ") + id;
    r.conditions = conditions;
    r.actions = actions;
    r.exceptions = exceptions;
    return r;
}

} // namespace

class TestMailRules : public QObject {
    Q_OBJECT
private slots:
    void everyCondition_data()
    {
        QTest::addColumn<QVariantMap>("map");
        QTest::addColumn<bool>("onSample");
        QTest::addColumn<bool>("onOther");
        const auto row = [](const char *name, const QVariantMap &map, bool sample, bool other) {
            QTest::newRow(name) << map << sample << other;
        };
        // `other` is the sample changed so the condition flips.
        row("from-address", {{"type", "from"}, {"values", QStringList{"grace@navy"}}}, true, false);
        row("from-name", {{"type", "from"}, {"values", QStringList{"nobody", "HOPPER"}}}, true, false);
        row("sentTo", {{"type", "sentTo"}, {"values", QStringList{"lovelace"}}}, true, false);
        row("sentOnlyToMe", {{"type", "sentOnlyToMe"}}, true, false);
        row("toMe", {{"type", "toMe"}}, true, false);
        row("toOrCcMe", {{"type", "toOrCcMe"}}, true, false);
        row("subject", {{"type", "subject"}, {"values", QStringList{"budget"}}}, true, false);
        row("body", {{"type", "body"}, {"values", QStringList{"attached numbers"}}}, true, false);
        row("subjectOrBody", {{"type", "subjectOrBody"}, {"values", QStringList{"friday"}}}, true, false);
        row("header", {{"type", "header"}, {"values", QStringList{"x-mailer: fleet"}}}, true, false);
        row("hasAttachment", {{"type", "hasAttachment"}}, true, false);
        row("importance", {{"type", "importance"}, {"value", "high"}}, true, false);
        row("size-min", {{"type", "size"}, {"min", 100}, {"max", 0}}, true, false);
        row("size-range", {{"type", "size"}, {"min", 200}, {"max", 300}}, true, false);
        row("received", {{"type", "received"}, {"after", "2026-10-01"}, {"before", "2026-10-08"}}, true, false);
        row("account", {{"type", "account"}, {"values", QStringList{"home", "work"}}}, true, false);
        row("category-named", {{"type", "category"}, {"values", QStringList{"blue CATEGORY"}}}, true, false);
        row("category-any", {{"type", "category"}}, true, false);
        row("flagged", {{"type", "flagged"}}, false, true);
        row("unread", {{"type", "unread"}}, true, false);
        row("olderThan", {{"type", "olderThan"}, {"days", 1}}, true, false);
    }

    void everyCondition()
    {
        QFETCH(QVariantMap, map);
        QFETCH(bool, onSample);
        QFETCH(bool, onOther);
        const Condition c = Criteria::fromVariantMap(map);
        QVERIFY2(Criteria::validate(c, QStringLiteral("c")).isEmpty(), qPrintable(Criteria::validate(c, QStringLiteral("c")).join('\n')));
        QVERIFY(Criteria::types().contains(c.type));
        QCOMPARE(Criteria::matches(c, sample(), context()), onSample);

        // The flipped message: every field different.
        MessageFacts other = sample();
        other.accountId = QStringLiteral("personal");
        other.senderName = QStringLiteral("Linus");
        other.senderAddress = QStringLiteral("linus@kernel.example");
        other.to = QStringLiteral("ada@example.org, bob@example.org");
        other.cc = QStringLiteral("carol@example.org");
        other.subject = QStringLiteral("Hello");
        other.body = QStringLiteral("Nothing to see.");
        other.headers = QStringLiteral("From: linus@kernel.example\r\n");
        other.hasAttachments = false;
        other.importance = QStringLiteral("normal");
        other.sizeBytes = 2 * 1024;
        other.received = QDateTime(QDate(2026, 10, 9), QTime(11, 0), QTimeZone::UTC);
        other.categories = {};
        other.flagged = true;
        other.unread = false;
        if (c.type == QLatin1String("toMe") || c.type == QLatin1String("toOrCcMe")) other.to = other.cc = QStringLiteral("x@y.example");
        QCOMPARE(Criteria::matches(c, other, context()), onOther);
        // The same condition as an exception turns the rule off.
        MailRule r = rule(QStringLiteral("x"), {}, {action(QStringLiteral("markRead"))}, {c});
        QCOMPARE(Rules::applies(r, sample(), context()), !onSample);
        QCOMPARE(Rules::applies(r, other, context()), !onOther);
        // Round trip through the stored map.
        QCOMPARE(Criteria::toVariantMap(Criteria::fromVariantMap(Criteria::toVariantMap(c))), Criteria::toVariantMap(c));
        QVERIFY(!Criteria::describe(c).isEmpty());
    }

    void conditionsNeedTheirFacts()
    {
        MessageFacts f = sample();
        f.headersKnown = false;
        QVERIFY(!Criteria::matches(condition(QStringLiteral("header"), {QStringLiteral("x-mailer")}), f, context()));
        f.sizeBytes = -1;
        Condition size = condition(QStringLiteral("size"));
        size.minimum = 1;
        QVERIFY(!Criteria::matches(size, f, context()));
        // "Sent only to me" fails with a Cc or a second To.
        f = sample();
        f.cc = QStringLiteral("bob@example.org");
        QVERIFY(!Criteria::matches(condition(QStringLiteral("sentOnlyToMe")), f, context()));
        QVERIFY(Criteria::matches(condition(QStringLiteral("toOrCcMe")), f, context()));
        // Several conditions must all match; several values are alternatives.
        const QList<Condition> both{condition(QStringLiteral("from"), {QStringLiteral("grace")}),
                                    condition(QStringLiteral("subject"), {QStringLiteral("nope"), QStringLiteral("review")})};
        QVERIFY(Criteria::matchesAll(both, sample(), context()));
        QVERIFY(!Criteria::matchesAll(both + QList<Condition>{condition(QStringLiteral("flagged"))}, sample(), context()));
        QVERIFY(Criteria::matchesAll({}, sample(), context()));
        QVERIFY(!Criteria::matchesAny({}, sample(), context()));
        // An unknown type (a newer file) never matches and is reported.
        const Condition unknown = condition(QStringLiteral("mood"));
        QVERIFY(!Criteria::matches(unknown, sample(), context()));
        QVERIFY(!Criteria::validate(unknown, QStringLiteral("conditions[0]")).isEmpty());
        QVERIFY(Criteria::needsHeaders({condition(QStringLiteral("importance"), {}, QStringLiteral("low"))}));
        QVERIFY(!Criteria::needsHeaders({condition(QStringLiteral("subject"), {QStringLiteral("a")})}));
    }

    void importanceFromHeaders()
    {
        QCOMPARE(Criteria::importanceFromHeaders(QStringLiteral("Subject: x\r\nImportance: high\r\n")), QStringLiteral("high"));
        QCOMPARE(Criteria::importanceFromHeaders(QStringLiteral("X-Priority: 1 (Highest)\r\n")), QStringLiteral("high"));
        QCOMPARE(Criteria::importanceFromHeaders(QStringLiteral("X-Priority: 5\r\n")), QStringLiteral("low"));
        QCOMPARE(Criteria::importanceFromHeaders(QStringLiteral("Importance:\r\n low\r\n")), QStringLiteral("low"));
        QCOMPARE(Criteria::importanceFromHeaders(QStringLiteral("Priority: urgent\r\n")), QStringLiteral("high"));
        QCOMPARE(Criteria::importanceFromHeaders(QStringLiteral("Subject: Importance: high\r\n")), QStringLiteral("normal"));
    }

    void everyActionShapesThePlan()
    {
        const QList<MailRule> rules{rule(QStringLiteral("1"), {condition(QStringLiteral("from"), {QStringLiteral("grace")})},
                                         {action(QStringLiteral("moveTo"), {}, QStringLiteral("acct:work:Budget")),
                                          action(QStringLiteral("copyTo"), {}, QStringLiteral("acct:work:Archive/2026")),
                                          action(QStringLiteral("markRead")),
                                          action(QStringLiteral("flag"), QStringLiteral("thisWeek")),
                                          action(QStringLiteral("assignCategory"), {}, {}, {QStringLiteral("Finance"), QStringLiteral("finance")}),
                                          action(QStringLiteral("setImportance"), QStringLiteral("low")),
                                          action(QStringLiteral("forward"), QStringLiteral("Bob <bob@example.org>")),
                                          action(QStringLiteral("redirect"), QStringLiteral("carol@example.org")),
                                          action(QStringLiteral("notify"))})};
        for (const MailRule &r : rules) QVERIFY2(Rules::validate(r).isEmpty(), qPrintable(Rules::validate(r).join('\n')));
        const RulePlan plan = Rules::plan(rules, sample(), context());
        QCOMPARE(plan.matchedRules, QStringList{QStringLiteral("1")});
        QCOMPARE(plan.moveTo, QStringLiteral("acct:work:Budget"));
        QCOMPARE(plan.copyTo, QStringList{QStringLiteral("acct:work:Archive/2026")});
        QVERIFY(plan.markRead);
        QCOMPARE(plan.flag, QStringLiteral("thisWeek"));
        QCOMPARE(plan.addCategories, QStringList{QStringLiteral("Finance")});
        QCOMPARE(plan.importance, QStringLiteral("low"));
        QCOMPARE(plan.forwardTo, QStringList{QStringLiteral("Bob <bob@example.org>")});
        QCOMPARE(plan.redirectTo, QStringList{QStringLiteral("carol@example.org")});
        QCOMPARE(plan.alerts, QStringList{QStringLiteral("Rule 1")});           // empty text: the rule's name
        QVERIFY(plan.disposes());
        QVERIFY(!plan.deleteMessage && !plan.deletePermanently && !plan.stopped);

        // Delete, permanent delete, clear flag and clear categories.
        const RulePlan cleared = Rules::plan(
            {rule(QStringLiteral("a"), {}, {action(QStringLiteral("assignCategory"), {}, {}, {QStringLiteral("X")}),
                                           action(QStringLiteral("clearCategories")), action(QStringLiteral("clearFlag")),
                                           action(QStringLiteral("delete"))}),
             rule(QStringLiteral("b"), {}, {action(QStringLiteral("deletePermanently"))})},
            sample(), context());
        QVERIFY(cleared.clearCategories && cleared.addCategories.isEmpty());
        QCOMPARE(cleared.flag, QStringLiteral("clear"));
        QVERIFY(cleared.deleteMessage && cleared.deletePermanently);
        QCOMPARE(cleared.matchedRules, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    }

    void orderStopAndMoves()
    {
        const Condition grace = condition(QStringLiteral("from"), {QStringLiteral("grace")});
        QList<MailRule> rules{
            rule(QStringLiteral("first"), {grace}, {action(QStringLiteral("moveTo"), {}, QStringLiteral("acct:work:A"))}),
            rule(QStringLiteral("off"), {grace}, {action(QStringLiteral("markRead"))}),
            rule(QStringLiteral("second"), {grace}, {action(QStringLiteral("moveTo"), {}, QStringLiteral("acct:work:B")),
                                                     action(QStringLiteral("stop"))}),
            rule(QStringLiteral("never"), {grace}, {action(QStringLiteral("flag"), QStringLiteral("today"))}),
        };
        rules[1].enabled = false;
        RulePlan plan = Rules::plan(rules, sample(), context());
        // The first move is the move; the later one files a copy; stop ends the list.
        QCOMPARE(plan.matchedRules, (QStringList{QStringLiteral("first"), QStringLiteral("second")}));
        QCOMPARE(plan.moveTo, QStringLiteral("acct:work:A"));
        QCOMPARE(plan.copyTo, QStringList{QStringLiteral("acct:work:B")});
        QVERIFY(plan.stopped);
        QVERIFY(!plan.markRead);
        QVERIFY(plan.flag.isEmpty());
        // Reordered: the stopping rule first keeps the others out.
        std::swap(rules[0], rules[2]);
        plan = Rules::plan(rules, sample(), context());
        QCOMPARE(plan.matchedRules, QStringList{QStringLiteral("second")});
        QCOMPARE(plan.moveTo, QStringLiteral("acct:work:B"));
        // Run Rules Now with a chosen subset, disabled ones included on request.
        plan = Rules::plan(rules, sample(), context(), {QStringLiteral("off"), QStringLiteral("never")}, true);
        QCOMPARE(plan.matchedRules, (QStringList{QStringLiteral("off"), QStringLiteral("never")}));
        QVERIFY(plan.markRead);
        QCOMPARE(plan.flag, QStringLiteral("today"));
        // A message no rule matches gets an empty plan.
        MessageFacts other = sample();
        other.senderName = other.senderAddress = QStringLiteral("someone");
        QVERIFY(Rules::plan(rules, other, context()).isEmpty());
    }

    void validation()
    {
        MailRule r = rule(QStringLiteral("v"), {condition(QStringLiteral("subject"))},
                          {action(QStringLiteral("moveTo")), action(QStringLiteral("flag"), QStringLiteral("someday")),
                           action(QStringLiteral("forward"), QStringLiteral("not an address")),
                           action(QStringLiteral("assignCategory")), action(QStringLiteral("setImportance"), QStringLiteral("urgent")),
                           action(QStringLiteral("teleport"))});
        r.name.clear();
        const QString problems = Rules::validate(r).join(QLatin1Char('\n'));
        for (const char *path : {"name:", "conditions[0].values:", "actions[0].folderKey:", "actions[1].value:",
                                 "actions[2].value:", "actions[3].values:", "actions[4].value:", "actions[5].type:"})
            QVERIFY2(problems.contains(QLatin1String(path)), qPrintable(QLatin1String(path) + QLatin1Char('\n') + problems));
        MailRule stopOnly = rule(QStringLiteral("s"), {}, {action(QStringLiteral("stop"))});
        QVERIFY(Rules::validate(stopOnly).join(QLatin1Char(' ')).contains(QStringLiteral("at least one action")));
        Condition size = condition(QStringLiteral("size"));
        size.minimum = 50;
        size.maximum = 10;
        QVERIFY(!Criteria::validate(size, QStringLiteral("c")).isEmpty());
        Condition range = condition(QStringLiteral("received"));
        range.after = QDate(2026, 2, 1);
        range.before = QDate(2026, 1, 1);
        QVERIFY(!Criteria::validate(range, QStringLiteral("c")).isEmpty());
    }

    void jsonRoundTripAndImport()
    {
        MailRule r = rule(QStringLiteral("keep"), {condition(QStringLiteral("from"), {QStringLiteral("grace")})},
                          {action(QStringLiteral("moveTo"), {}, QStringLiteral("acct:work:Budget"))},
                          {condition(QStringLiteral("importance"), {}, QStringLiteral("low"))});
        r.extra.insert(QStringLiteral("futureField"), 42);           // unknown keys survive
        r.actions[0].extra.insert(QStringLiteral("note"), QStringLiteral("kept"));
        const QVariantMap map = Rules::toVariantMap(r);
        QCOMPARE(Rules::toVariantMap(Rules::fromVariantMap(map)), map);

        MailRule broken = rule(QStringLiteral("broken"), {condition(QStringLiteral("mood"))}, {action(QStringLiteral("markRead"))});
        const QByteArray json = Rules::exportJson({r, broken});
        QCOMPARE(QJsonDocument::fromJson(json).object().value(QStringLiteral("schema")).toString(),
                 QString::fromLatin1(Rules::kExportSchema));
        const Rules::ImportResult imported = Rules::importJson(json);
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QCOMPARE(imported.rules.size(), 2);
        QVERIFY(imported.rules.at(0).id != r.id);                     // fresh ids
        QCOMPARE(imported.rules.at(0).name, r.name);
        QVERIFY(imported.rules.at(0).enabled);
        QCOMPARE(imported.rules.at(0).extra.value(QStringLiteral("futureField")).toInt(), 42);
        QVERIFY(!imported.rules.at(1).enabled);                       // a problem: comes in off
        QCOMPARE(imported.problems.size(), 1);
        QVERIFY(imported.problems.first().startsWith(QStringLiteral("Rule broken: conditions[0].type")));

        QVERIFY(!Rules::importJson("{\"schema\":\"something/1\",\"rules\":[]}").ok);
        QVERIFY(!Rules::importJson("not json").ok);
        QVERIFY(Rules::summary(r).contains(QStringLiteral("grace")));
        QVERIFY(Rules::summary(r).contains(QStringLiteral("Budget")));
        QVERIFY(Rules::needsHeaders(r));                              // the importance exception reads headers
    }
};

QTEST_GUILESS_MAIN(TestMailRules)

#include "tst_mail_rules.moc"
