// SPDX-License-Identifier: GPL-3.0-or-later
// M81 (D-090) through the real window, at 1280 and 520 px: Rules and Alerts
// and the rule editor (a rule made by clicking, saved, listed, turned off),
// Create Rule from a message, Categorize and the Color Categories dialog,
// Follow Up (the reading pane's bar, the row's due label, Mark Complete),
// a search folder made in its dialog and shown in the folder tree, the
// task board's flagged mail, and Automatic Replies (over a fake ManageSieve
// server, and plainly unavailable for POP3). QQO_GRAB_DIR saves PNGs.

#include "mail_test_support.h"

#include "fake_imap_server.h"
#include "fake_mail_server.h"
#include "office_mail/fake_managesieve_server.h"

#include "qindamail/src/mail_host.h"
#include "qindamail/src/mail_organizer.h"
#include "qindamail/src/mail_quick_steps.h"

#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <qindatk/local_productivity_store.h>

#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

using namespace QindaOffice::Apps::Mail;
using QindaOffice::Test::findNamed;

namespace {

QByteArray mail(const QByteArray &from, const QByteArray &subject, const QByteArray &id)
{
    return "From: " + from + "\r\nTo: Ada <ada@work.example>\r\nSubject: " + subject
        + "\r\nDate: Thu, 8 Oct 2026 09:00:00 +0000\r\nMessage-ID: <" + id + ">\r\n"
        + "MIME-Version: 1.0\r\nContent-Type: text/plain\r\n\r\nThe budget numbers are attached.\r\n";
}

struct Scene {
    MailTest::Env env;
    QQmlApplicationEngine engine;
    std::unique_ptr<MailHost> host;
    QQuickWindow *window = nullptr;
    QString error;
    QString first, second;

    explicit Scene(int width)
    {
        first = import(QStringLiteral("a.eml"), mail("Grace Hopper <grace@navy.example>", "Quarterly budget", "budget@x"));
        second = import(QStringLiteral("b.eml"), mail("Linus <linus@kernel.example>", "Patch review", "patch@x"));
        env.service->organizer()->setToday(QDate(2026, 10, 7));
        host = std::make_unique<MailHost>(env.service.get());
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")));
        QObject *object = component.createWithInitialProperties({{QStringLiteral("host"), QVariant::fromValue(host.get())}});
        if (object == nullptr) {
            error = component.errorString();
            return;
        }
        window = qobject_cast<QQuickWindow *>(object);
        host->attachWindow(window);
        window->resize(width, 800);
        window->show();
        env.service->setCurrentFolderKey(QStringLiteral("acct:local:Inbox"));
    }
    ~Scene() { delete window; }

    QString import(const QString &name, const QByteArray &bytes)
    {
        const QString path = env.dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write(bytes);
        file.close();
        return env.service->importMessageFile(path);
    }
    [[nodiscard]] QQuickItem *item(const QString &name) const { return findNamed(window->contentItem(), name); }
    // A popup's content (dialogs live in the window's overlay).
    [[nodiscard]] QQuickItem *in(const char *dialog, const QString &name) const
    {
        QObject *popup = window->findChild<QObject *>(QString::fromLatin1(dialog));
        auto *body = popup != nullptr ? popup->property("contentItem").value<QQuickItem *>() : nullptr;
        return body != nullptr ? findNamed(body, name) : nullptr;
    }
    // Scrolled into view by every Tk.Scroll around it (a dialog's body, a
    // list), after the layout settled (Tk.Flex places its children in polish).
    bool click(QQuickItem *target)
    {
        if (target == nullptr || !target->isVisible()) return false;
        QTest::qWait(120);
        for (QQuickItem *p = target->parentItem(); p != nullptr; p = p->parentItem()) {
            if (p->property("_qindaScrollViewport").toBool())
                QMetaObject::invokeMethod(p, "ensureVisible", Q_ARG(QVariant, QVariant::fromValue(target)));
        }
        QTest::qWait(60);
        QTest::mouseClick(window, Qt::LeftButton, {}, target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
        return true;
    }
    // Why a dialog's Save is off: its problems and what it would save.
    static QString why(QObject *editor)
    {
        QVariant rule;
        QMetaObject::invokeMethod(editor, "collect", Q_RETURN_ARG(QVariant, rule));
        return QString::fromUtf8(QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("problems"), editor->property("problems")},
                                                                        {QStringLiteral("rule"), rule}})
                                     .toJson(QJsonDocument::Compact));
    }
    void grab(const QString &name) { QTest::qWait(150); QindaOffice::Test::saveGrab(window, name); }
    // Typed into the focused field (QTest::keyClicks wants a QWidget).
    void type(const QString &text)
    {
        for (const QChar c : text) QTest::keyClick(window, c.toLatin1());
    }
};

} // namespace

class TestOrganizerUi : public QObject {
    Q_OBJECT
private slots:
    void quickStepsDialog_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }

    void quickStepsDialog()
    {
        QFETCH(int, width);
        Scene scene(width);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.env.service->setMarkReadDelayMs(-1);
        scene.env.service->setSelectedMessageId(scene.first);
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailQuickStepsAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("quickStepsDialog"));
        QTRY_VERIFY(dialog != nullptr && dialog->property("opened").toBool());
        QQuickItem *name = scene.in("quickStepsDialog", QStringLiteral("quickStepName"));
        QVERIFY(name != nullptr && name->isVisible());
        QCOMPARE(name->property("text").toString(), QStringLiteral("New Quick Step"));
        QVERIFY(scene.click(scene.in("quickStepsDialog", QStringLiteral("quickStepSave"))));
        auto *steps = scene.env.service->quickSteps();
        QTRY_COMPARE(steps->forMailbox(QStringLiteral("local")).size(), 1);
        scene.grab(QStringLiteral("qindamail_quick_steps_%1").arg(width));
        QVERIFY(scene.click(scene.in("quickStepsDialog", QStringLiteral("quickStepRun"))));
        QTRY_COMPARE(steps->outcome().value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
        QVERIFY(!scene.env.service->messageById(scene.first).value(QStringLiteral("unread")).toBool());
    }

    void rulesDialogAndEditor_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }

    void rulesDialogAndEditor()
    {
        QFETCH(int, width);
        Scene scene(width);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailRulesAction")));
        QObject *rules = scene.window->findChild<QObject *>(QStringLiteral("rulesDialog"));
        QVERIFY(rules != nullptr);
        QTRY_VERIFY(rules->property("opened").toBool());
        scene.grab(QStringLiteral("qindamail_rules_empty_%1").arg(width));
        QVERIFY(scene.click(scene.in("rulesDialog", QStringLiteral("rulesNew"))));
        QObject *editor = scene.window->findChild<QObject *>(QStringLiteral("ruleEditor"));
        QTRY_VERIFY(editor->property("opened").toBool());
        // Name it, check "From" with an address, then "Move it to" a folder.
        QQuickItem *name = scene.in("ruleEditor", QStringLiteral("ruleName"));
        QVERIFY(name != nullptr);
        name->setProperty("text", QStringLiteral("Grace's mail"));
        QQuickItem *from = scene.in("ruleEditor", QStringLiteral("conditionText_from"));
        QVERIFY(from != nullptr);
        from->forceActiveFocus();
        scene.type(QStringLiteral("grace@navy"));
        QQuickItem *pages = scene.in("ruleEditor", QStringLiteral("rulePages"));
        QVERIFY(pages != nullptr);
        QMetaObject::invokeMethod(pages, "activate", Q_ARG(QVariant, 1));
        QTRY_VERIFY(scene.in("ruleEditor", QStringLiteral("actionCheck_moveTo"))->isVisible());
        QVERIFY(scene.click(scene.in("ruleEditor", QStringLiteral("actionCheck_moveTo"))));
        QTRY_VERIFY2(editor->property("primaryEnabled").toBool(), qPrintable(Scene::why(editor)));
        QQuickItem *summary = scene.in("ruleEditor", QStringLiteral("ruleSummary"));
        QTRY_VERIFY(summary->property("text").toString().contains(QStringLiteral("grace@navy")));
        scene.grab(QStringLiteral("qindamail_rule_editor_%1").arg(width));
        QMetaObject::invokeMethod(editor, "accept");
        QTRY_COMPARE(scene.env.service->organizer()->rules().size(), 1);
        const QVariantMap saved = scene.env.service->organizer()->rules().first().toMap();
        QCOMPARE(saved.value(QStringLiteral("name")).toString(), QStringLiteral("Grace's mail"));
        QCOMPARE(saved.value(QStringLiteral("actions")).toList().first().toMap().value(QStringLiteral("type")).toString(), QStringLiteral("moveTo"));
        // Listed with its box: turning it off is one click.
        const QString on = QStringLiteral("ruleOn_") + saved.value(QStringLiteral("id")).toString();
        QTRY_VERIFY(scene.in("rulesDialog", on) != nullptr);
        QVERIFY(scene.click(scene.in("rulesDialog", on)));
        QTRY_VERIFY(!scene.env.service->organizer()->rules().first().toMap().value(QStringLiteral("enabled")).toBool());
        scene.grab(QStringLiteral("qindamail_rules_%1").arg(width));
    }

    void createRuleCategorizeAndFollowUp()
    {
        Scene scene(1280);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.env.service->setMarkReadDelayMs(-1);
        scene.env.service->setSelectedMessageId(scene.first);
        QTRY_VERIFY(scene.host->actions()->entry(QStringLiteral("mailCreateRuleAction"))->isEnabled());
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailCreateRuleAction")));
        QObject *editor = scene.window->findChild<QObject *>(QStringLiteral("ruleEditor"));
        QTRY_VERIFY(editor->property("opened").toBool());
        QCOMPARE(scene.in("ruleEditor", QStringLiteral("ruleName"))->property("text").toString(), QStringLiteral("grace@navy.example"));
        QVERIFY(scene.in("ruleEditor", QStringLiteral("conditionCheck_from"))->property("checked").toBool());
        QCOMPARE(scene.in("ruleEditor", QStringLiteral("conditionText_subject"))->property("text").toString(), QStringLiteral("Quarterly budget"));
        QVERIFY(!scene.in("ruleEditor", QStringLiteral("conditionCheck_subject"))->property("checked").toBool());
        scene.grab(QStringLiteral("qindamail_create_rule_1280"));
        QMetaObject::invokeMethod(editor, "close");
        // Categorize: the menu lists the six, a pick tags the message.
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailCategorizeAction")));
        QObject *menu = scene.window->findChild<QObject *>(QStringLiteral("categorizeMenu"));
        QTRY_VERIFY(menu->property("opened").toBool());
        scene.grab(QStringLiteral("qindamail_categorize_menu_1280"));
        MailOrganizer *organizer = scene.env.service->organizer();
        static_cast<void>(organizer->toggleCategory(QStringLiteral("message"), {scene.first}, QStringLiteral("Green category"), true));
        QMetaObject::invokeMethod(menu, "close");
        QTRY_VERIFY(scene.item(QStringLiteral("readingCategory_Green category")) != nullptr);
        // Follow Up ▸ Tomorrow: the reading pane's bar and the row's label.
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailFlagTomorrowAction")));
        QQuickItem *followUp = nullptr;
        QTRY_VERIFY((followUp = scene.item(QStringLiteral("readingFollowUpText"))) != nullptr && followUp->isVisible());
        QVERIFY(followUp->property("text").toString().startsWith(QStringLiteral("Follow up.")));
        QTRY_VERIFY(scene.item(QStringLiteral("messageRowDue")) != nullptr);
        scene.grab(QStringLiteral("qindamail_follow_up_1280"));
        // The Color Categories dialog: assign by its box, add one.
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailCategoriesAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("categoriesDialog"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(scene.in("categoriesDialog", QStringLiteral("categoryCheck_Green category"))->property("checked").toBool());
        QVERIFY(scene.click(scene.in("categoriesDialog", QStringLiteral("categoryNew"))));
        QQuickItem *field = scene.in("categoriesDialog", QStringLiteral("categoryName"));
        QTRY_VERIFY(field != nullptr && field->isVisible());
        field->forceActiveFocus();
        scene.type(QStringLiteral("Travel"));
        QTest::keyClick(scene.window, Qt::Key_Return);
        QTRY_COMPARE(organizer->categoriesFor(QStringLiteral("local")).size(), 7);
        QTRY_VERIFY(scene.in("categoriesDialog", QStringLiteral("categoryCheck_Travel")) != nullptr);
        // Enter added it (checked for the message) and left the dialog up.
        QVERIFY(dialog->property("opened").toBool());
        QVERIFY(scene.in("categoriesDialog", QStringLiteral("categoryCheck_Travel"))->property("checked").toBool());
        scene.grab(QStringLiteral("qindamail_categories_dialog_1280"));
        QMetaObject::invokeMethod(dialog, "accept");
        QTRY_COMPARE(organizer->categoriesOf(QStringLiteral("message"), scene.first),
                     (QStringList{QStringLiteral("Green category"), QStringLiteral("Travel")}));
        // Mark Complete from the bar.
        QVERIFY(scene.click(scene.item(QStringLiteral("readingFlagComplete"))));
        QTRY_COMPARE(organizer->followUpOf(scene.first).value(QStringLiteral("status")).toString(), QStringLiteral("complete"));
        // The flagged mail is on the task board.
        scene.host->setView(QStringLiteral("tasks"));
        QTRY_VERIFY(scene.item(QStringLiteral("taskCard_mailflag:") + scene.first) != nullptr);
        scene.grab(QStringLiteral("qindamail_tasks_flagged_1280"));
    }

    void searchFolderDialog_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }

    void searchFolderDialog()
    {
        QFETCH(int, width);
        Scene scene(width);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailNewSearchFolderAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("searchFolderDialog"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(scene.click(scene.in("searchFolderDialog", QStringLiteral("searchPreset_words"))));
        QTRY_COMPARE(scene.in("searchFolderDialog", QStringLiteral("searchFolderName"))->property("text").toString(),
                     QStringLiteral("Mail with Specific Words"));
        // Only the first message's subject has it (both bodies mention the budget).
        scene.in("searchFolderDialog", QStringLiteral("searchFolderArgument"))->setProperty("text", QStringLiteral("quarterly"));
        scene.grab(QStringLiteral("qindamail_search_folder_dialog_%1").arg(width));
        QMetaObject::invokeMethod(dialog, "accept");
        QTRY_VERIFY(scene.env.service->currentFolderKey().startsWith(QStringLiteral("search:")));
        const QString key = scene.env.service->currentFolderKey();
        QTRY_COMPARE(scene.env.service->messageModel()->rowCount(), 1);
        QTRY_VERIFY(scene.item(QStringLiteral("folder_") + key) != nullptr);
        QVERIFY(scene.host->actions()->entry(QStringLiteral("mailEditSearchFolderAction"))->isEnabled());
        scene.grab(QStringLiteral("qindamail_search_folder_%1").arg(width));
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailDeleteSearchFolderAction")));
        QTRY_VERIFY(scene.item(QStringLiteral("folder_") + key) == nullptr);
    }

    void automaticRepliesDialog_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }

    void automaticRepliesDialog()
    {
        QFETCH(int, width);
        Scene scene(width);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        MailTest::FakeImapServer imap("pw");
        MailTest::FakeManageSieveServer sieve("pw");
        const auto server = [](quint16 port, const QString &protocol) {
            QVariantMap map{{QStringLiteral("host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), port},
                            {QStringLiteral("security"), QStringLiteral("none")}, {QStringLiteral("userName"), QStringLiteral("ada")}};
            if (!protocol.isEmpty()) map.insert(QStringLiteral("protocol"), protocol);
            return map;
        };
        const QString account = scene.env.service->addAccount(
            {{QStringLiteral("kind"), QStringLiteral("imap")}, {QStringLiteral("displayName"), QStringLiteral("Work")},
             {QStringLiteral("emailAddress"), QStringLiteral("ada@work.example")},
             {QStringLiteral("incoming"), server(imap.port(), QStringLiteral("imap"))}, {QStringLiteral("outgoing"), server(imap.port(), {})},
             {QStringLiteral("sieve"), QVariantMap{{QStringLiteral("host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), sieve.port()},
                                                    {QStringLiteral("security"), QStringLiteral("none")}}}},
            QStringLiteral("pw")).value(QStringLiteral("id")).toString();
        MailTest::FakeMailServer pop(MailTest::FakeMailServer::Kind::Pop3, "pw");
        scene.env.service->addAccount({{QStringLiteral("kind"), QStringLiteral("pop3")}, {QStringLiteral("displayName"), QStringLiteral("Home")},
                                       {QStringLiteral("emailAddress"), QStringLiteral("ada@home.example")},
                                       {QStringLiteral("incoming"), server(pop.port(), QStringLiteral("pop3"))},
                                       {QStringLiteral("outgoing"), server(pop.port(), {})}}, QStringLiteral("pw"));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.env.service->setCurrentFolderKey(QStringLiteral("acct:") + account + QStringLiteral(":INBOX"));
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailAutoRepliesAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("autoRepliesDialog"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_VERIFY(dialog->property("available").toBool());
        QCOMPARE(dialog->property("method").toString(), QStringLiteral("sieve"));
        QVERIFY(scene.click(scene.in("autoRepliesDialog", QStringLiteral("autoRepliesOn"))));
        scene.in("autoRepliesDialog", QStringLiteral("autoRepliesInside"))->setProperty("text", QStringLiteral("Back on Monday."));
        scene.grab(QStringLiteral("qindamail_auto_replies_%1").arg(width));
        // The dialog stays up (reopened) until the server answers, then closes.
        QMetaObject::invokeMethod(dialog, "accept");
        QTRY_VERIFY_WITH_TIMEOUT(!sieve.scripts.isEmpty(), 15000);
        QTRY_VERIFY(!dialog->property("loading").toBool());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(sieve.scripts.first().contains("Back on Monday."));
        // POP3: plainly not available.
        QObject *accounts = scene.in("autoRepliesDialog", QStringLiteral("autoRepliesAccount"));
        Q_UNUSED(accounts);
        QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QString())));
        const QVariantList ways = scene.env.service->organizer()->autoReplyAccounts();
        QString popId;
        for (const QVariant &way : ways)
            if (way.toMap().value(QStringLiteral("method")).toString() == QLatin1String("none")) popId = way.toMap().value(QStringLiteral("id")).toString();
        QVERIFY(!popId.isEmpty());
        QMetaObject::invokeMethod(dialog, "load", Q_ARG(QVariant, popId));
        QTRY_VERIFY(scene.in("autoRepliesDialog", QStringLiteral("autoRepliesProblem"))->isVisible());
        QVERIFY(!dialog->property("primaryEnabled").toBool());
        scene.grab(QStringLiteral("qindamail_auto_replies_pop3_%1").arg(width));
    }
};

MAIL_TEST_MAIN(TestOrganizerUi)

#include "tst_qindamail_organizer_ui.moc"
