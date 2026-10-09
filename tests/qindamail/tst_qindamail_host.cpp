// SPDX-License-Identifier: GPL-3.0-or-later
// MailHost: every command id exists with an icon QindaTK ships, the radio
// groups hold one checked member, shortcuts are unique, the enabled state
// follows the view and the selection, the panel export passes the desktop's
// own validator with every exported id resolving, paths handed to the app
// (mailto:, .ics) land in the right place, `qindamail --attach <file>
// mailto:…` opens a composer with the files attached (M48), and the window
// instantiates from the app's QML module.

#include "mail_test_support.h"

#include "qindamail/src/launch_arguments.h"
#include "qindamail/src/mail_host.h"

#include "menu_source.h"

#include <qindaqt/shell/global_menu/protocol/menu_validation.h>

#include <qindatk/local_productivity_store.h>

#include <algorithm>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QUuid>

using namespace QindaOffice::Apps::Mail;
using QindaOffice::Quick::ActionEntry;
using QindaOffice::Quick::ActionModelMenuSource;
namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;

class TestMailHost : public QObject {
    Q_OBJECT

    static void collect(const Protocol::MenuItem &item, QStringList &ids)
    {
        if (item.kind == Protocol::MenuItemKind::Action) ids.append(item.id);
        for (const Protocol::MenuItem &child : item.children) collect(child, ids);
    }

    // The Lucide names the toolkit ships (qtk-preview --list-icons) that
    // this host uses; a rename in QindaTK fails here, not as a blank button.
    static QStringList shippedIcons()
    {
        return {QStringLiteral("mail-plus"), QStringLiteral("calendar-plus"), QStringLiteral("list-plus"),
                QStringLiteral("reply"), QStringLiteral("reply-all"), QStringLiteral("arrow-right"),
                QStringLiteral("trash-2"), QStringLiteral("archive"), QStringLiteral("mail-open"),
                QStringLiteral("flag"), QStringLiteral("folder"), QStringLiteral("refresh-cw"),
                QStringLiteral("search"), QStringLiteral("user-plus"), QStringLiteral("settings"),
                QStringLiteral("mail"), QStringLiteral("calendar"), QStringLiteral("list-todo"),
                QStringLiteral("panel-right"), QStringLiteral("mails"), QStringLiteral("calendar-check"),
                QStringLiteral("chevron-left"), QStringLiteral("chevron-right"), QStringLiteral("calendar-clock"),
                QStringLiteral("calendar-range"), QStringLiteral("columns-3"), QStringLiteral("calendar-days"),
                QStringLiteral("import"), QStringLiteral("file-output"), QStringLiteral("link-2"),
                QStringLiteral("panels-top-left"),
                // M64 (D-072)
                QStringLiteral("users"), QStringLiteral("book-user"), QStringLiteral("image"), QStringLiteral("save"),
                QStringLiteral("paperclip"), QStringLiteral("mailbox"), QStringLiteral("file-down"),
                // M76 (D-085)
                QStringLiteral("archive-restore"), QStringLiteral("users-round"), QStringLiteral("cloud"),
                // M81 (D-090)
                QStringLiteral("wand"), QStringLiteral("wand-sparkles"), QStringLiteral("play"), QStringLiteral("tag"),
                QStringLiteral("sliders-horizontal"), QStringLiteral("eraser"), QStringLiteral("calendar-plus"),
                QStringLiteral("check-check"), QStringLiteral("circle-x"), QStringLiteral("folder-plus"),
                QStringLiteral("settings-2"), QStringLiteral("trash"), QStringLiteral("message-square"),
                QStringLiteral("sparkles"), QStringLiteral("mail-x")};
    }

private slots:
    void everyCommandExists()
    {
        MailTest::Env env;
        MailHost host(env.service.get());
        const QStringList ids = MailHost::commandIds();
        QCOMPARE(ids.size(), 64);                                   // M81 added 20
        for (const QString &id : ids) {
            ActionEntry *entry = host.actions()->entry(id);
            QVERIFY2(entry != nullptr, qPrintable(id));
            QVERIFY2(!entry->plainText().isEmpty(), qPrintable(id));
            QVERIFY2(shippedIcons().contains(entry->iconName()), qPrintable(id + QLatin1Char(' ') + entry->iconName()));
        }
        ActionEntry *newWindow = host.actions()->entry(QStringLiteral("fileNewAction"));
        QCOMPARE(newWindow->plainText(), QStringLiteral("New Window"));
        QCOMPARE(newWindow->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+N")));
        QCOMPARE(host.actions()->entry(QStringLiteral("mailNewMessageAction"))->shortcut(),
                 QKeySequence(QStringLiteral("Ctrl+N")));
        // No document: Save stays disabled (D-011); Print prints the open
        // message (M64), so with nothing selected it is disabled too.
        QVERIFY(!host.actions()->entry(QStringLiteral("fileSaveAction"))->isEnabled());
        QVERIFY(!host.actions()->entry(QStringLiteral("filePrintAction"))->isEnabled());
        QCOMPARE(host.actions()->entry(QStringLiteral("contactNewAction"))->shortcut(),
                 QKeySequence(QStringLiteral("Ctrl+Shift+C")));
        QCOMPARE(host.actions()->entry(QStringLiteral("mailSaveMessageAction"))->shortcut(), QKeySequence(QStringLiteral("F12")));
        QCOMPARE(host.actions()->entry(QStringLiteral("mailViewPeopleAction"))->radioGroup(), QStringLiteral("mailView"));
        // M81: Outlook's Custom flag and Ignore keys.
        QCOMPARE(host.actions()->entry(QStringLiteral("mailFlagCustomAction"))->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+G")));
        QCOMPARE(host.actions()->entry(QStringLiteral("mailIgnoreAction"))->shortcut(), QKeySequence(QStringLiteral("Ctrl+Delete")));
        QCOMPARE(host.actions()->entry(QStringLiteral("mailRulesAction"))->plainText(), QStringLiteral("Manage Rules & Alerts…"));
    }

    void shortcutsAreUnique()
    {
        MailTest::Env env;
        MailHost host(env.service.get());
        QHash<QString, QString> seen;
        for (ActionEntry *entry : host.actions()->entries()) {
            const QString key = entry->shortcut().toString(QKeySequence::PortableText);
            if (key.isEmpty()) continue;
            QVERIFY2(!seen.contains(key), qPrintable(key + QStringLiteral(": ") + seen.value(key)
                                                     + QStringLiteral(" and ") + entry->id()));
            seen.insert(key, entry->id());
        }
        QVERIFY(seen.size() > 25);
    }

    void radioGroupsAndView()
    {
        MailTest::Env env;
        MailHost host(env.service.get());
        const auto checked = [&host](const QStringList &ids) {
            QStringList on;
            for (const QString &id : ids) {
                if (host.actions()->entry(id)->isChecked()) on.append(id);
            }
            return on;
        };
        const QStringList views{QStringLiteral("mailViewMailAction"), QStringLiteral("mailViewCalendarAction"),
                                QStringLiteral("mailViewTasksAction")};
        const QStringList modes{QStringLiteral("calDayAction"), QStringLiteral("calWorkWeekAction"),
                                QStringLiteral("calWeekAction"), QStringLiteral("calMonthAction")};
        for (const QString &id : views) QCOMPARE(host.actions()->entry(id)->radioGroup(), QStringLiteral("mailView"));
        for (const QString &id : modes) QCOMPARE(host.actions()->entry(id)->radioGroup(), QStringLiteral("calendarMode"));
        QCOMPARE(checked(views), QStringList{QStringLiteral("mailViewMailAction")});
        QCOMPARE(checked(modes), QStringList{QStringLiteral("calWeekAction")});
        QCOMPARE(host.view(), QStringLiteral("mail"));
        QCOMPARE(host.title(), QStringLiteral("Inbox"));

        QSignalSpy viewChanged(&host, &MailHost::viewChanged);
        QVERIFY(host.actions()->trigger(QStringLiteral("mailViewCalendarAction")));
        QCOMPARE(host.view(), QStringLiteral("calendar"));
        QCOMPARE(checked(views), QStringList{QStringLiteral("mailViewCalendarAction")});
        QCOMPARE(host.title(), QStringLiteral("Calendar"));
        QVERIFY(host.actions()->entry(QStringLiteral("calNextAction"))->isEnabled());
        // A calendar mode picked from the mail view switches to the calendar.
        host.setView(QStringLiteral("tasks"));
        QVERIFY(!host.actions()->entry(QStringLiteral("calNextAction"))->isEnabled());
        QVERIFY(host.actions()->trigger(QStringLiteral("calMonthAction")));
        QCOMPARE(host.calendarMode(), QStringLiteral("month"));
        QCOMPARE(host.view(), QStringLiteral("calendar"));
        QCOMPARE(checked(modes), QStringList{QStringLiteral("calMonthAction")});
        QVERIFY(viewChanged.count() >= 3);

        // Conversations follow the service both ways (a mail-view command).
        QVERIFY(!host.actions()->entry(QStringLiteral("mailConversationsAction"))->isEnabled());
        host.setView(QStringLiteral("mail"));
        QVERIFY(host.actions()->trigger(QStringLiteral("mailConversationsAction")));
        QVERIFY(!env.service->threaded());
        env.service->setThreaded(true);
        QVERIFY(host.actions()->entry(QStringLiteral("mailConversationsAction"))->isChecked());
        QSignalSpy pane(&host, &MailHost::readingPaneVisibleChanged);
        QVERIFY(host.actions()->trigger(QStringLiteral("mailReadingPaneAction")));
        QVERIFY(!host.readingPaneVisible());
        QCOMPARE(pane.count(), 1);
    }

    void enabledStateFollowsSelection()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        service->store()->upsert(QStringLiteral("messages"),
                                 MailTest::message(QStringLiteral("local:1"), QStringLiteral("Hello"),
                                                   QDateTime::currentDateTimeUtc()));
        QVERIFY(service->store()->save());
        MailHost host(service);
        const QStringList respond{QStringLiteral("mailReplyAction"), QStringLiteral("mailReplyAllAction"),
                                  QStringLiteral("mailForwardAction"), QStringLiteral("mailDeleteAction"),
                                  QStringLiteral("mailArchiveAction"), QStringLiteral("mailToggleReadAction"),
                                  QStringLiteral("mailToggleFlagAction"), QStringLiteral("mailMoveAction")};
        for (const QString &id : respond) QVERIFY2(!host.actions()->entry(id)->isEnabled(), qPrintable(id));
        service->setMarkReadDelayMs(-1);
        service->setSelectedMessageId(QStringLiteral("local:1"));
        for (const QString &id : respond) QVERIFY2(host.actions()->entry(id)->isEnabled(), qPrintable(id));
        // M64: the open message prints, saves and gives its sender to People.
        for (const char *id : {"filePrintAction", "filePrintPreviewAction", "mailExportPdfAction", "mailSaveMessageAction",
                               "mailAddSenderAction", "mailShowPicturesAction"})
            QVERIFY2(host.actions()->entry(QString::fromLatin1(id))->isEnabled(), id);
        QVERIFY(!host.actions()->entry(QStringLiteral("mailSaveAttachmentsAction"))->isEnabled()); // none here
        QVERIFY(!host.actions()->entry(QStringLiteral("contactDeleteAction"))->isEnabled());
        QCOMPARE(host.actions()->entry(QStringLiteral("mailToggleReadAction"))->plainText(), QStringLiteral("Mark as Read"));

        QSignalSpy compose(&host, &MailHost::composeRequested);
        QVERIFY(host.actions()->trigger(QStringLiteral("mailReplyAllAction")));
        QCOMPARE(compose.count(), 1);
        QCOMPARE(compose.first().first().toMap().value(QStringLiteral("subject")).toString(), QStringLiteral("Re: Hello"));
        QVERIFY(host.actions()->trigger(QStringLiteral("mailToggleReadAction")));
        QTRY_COMPARE(host.actions()->entry(QStringLiteral("mailToggleReadAction"))->plainText(),
                     QStringLiteral("Mark as Unread"));
        // Outside the mail view the message commands let their keys through.
        host.setView(QStringLiteral("calendar"));
        for (const QString &id : respond) QVERIFY2(!host.actions()->entry(id)->isEnabled(), qPrintable(id));

        QSignalSpy dialog(&host, &MailHost::accountDialogRequested);
        QVERIFY(host.actions()->trigger(QStringLiteral("mailAddAccountAction")));
        QCOMPARE(dialog.first().first().toString(), QString());
        QSignalSpy event(&host, &MailHost::eventDialogRequested);
        QVERIFY(host.actions()->trigger(QStringLiteral("mailNewEventAction")));
        QCOMPARE(event.count(), 1);
        QSignalSpy task(&host, &MailHost::taskNewRequested);
        QVERIFY(host.actions()->trigger(QStringLiteral("taskNewAction")));
        QCOMPARE(task.count(), 1);
        QCOMPARE(host.view(), QStringLiteral("tasks"));
    }

    void panelExportValidates()
    {
        MailTest::Env env;
        MailHost host(env.service.get());
        ActionModelMenuSource source(host.menuTree(), host.actions(), QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QVERIFY2(snapshot.complete, qPrintable(QStringLiteral("incomplete: ") + snapshot.defectCode));
        const Protocol::ValidationResult result = Protocol::validateMenuTree(snapshot.tree);
        QVERIFY2(result.accepted, qPrintable(QStringLiteral("rejected: %1 at %2").arg(result.reasonCode, result.path)));
        QStringList titles;
        for (const Protocol::MenuItem &item : snapshot.tree.items) titles.append(item.text);
        QCOMPARE(titles, (QStringList{QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View"),
                                      QStringLiteral("Message"), QStringLiteral("Folder"), QStringLiteral("People"),
                                      QStringLiteral("Calendar"), QStringLiteral("Accounts"), QStringLiteral("AI"),
                                      QStringLiteral("Settings"), QStringLiteral("Help")}));
        QStringList ids;
        for (const Protocol::MenuItem &item : snapshot.tree.items) collect(item, ids);
        for (const QString &id : ids) QVERIFY2(host.actions()->entry(id) != nullptr, qPrintable(id));
        for (const QString &id : MailHost::commandIds()) QVERIFY2(ids.contains(id), qPrintable(id + " not in a menu"));
        // File starts with the new-item commands, then New Window; M76:
        // File ▸ Import holds the Outlook data file and the mailbox.
        QStringList file;
        collect(snapshot.tree.items.first(), file);
        QCOMPARE(file.mid(0, 6), (QStringList{QStringLiteral("mailNewMessageAction"), QStringLiteral("mailNewEventAction"),
                                              QStringLiteral("taskNewAction"), QStringLiteral("contactNewAction"),
                                              QStringLiteral("contactNewGroupAction"), QStringLiteral("fileNewAction")}));
        const auto import = std::find_if(snapshot.tree.items.first().children.cbegin(), snapshot.tree.items.first().children.cend(),
                                         [](const Protocol::MenuItem &item) { return item.text == QLatin1String("Import"); });
        QVERIFY(import != snapshot.tree.items.first().children.cend());
        QStringList imports;
        collect(*import, imports);
        QCOMPARE(imports, (QStringList{QStringLiteral("mailImportPstAction"), QStringLiteral("mailImportMboxAction")}));
        // M81: Message ▸ Categorize / Follow Up / Rules, the Folder menu, Accounts ▸ Automatic Replies.
        const auto menuIds = [&snapshot](const QString &title) {
            QStringList out;
            for (const Protocol::MenuItem &item : snapshot.tree.items)
                if (item.text == title) collect(item, out);
            return out;
        };
        const QStringList message = menuIds(QStringLiteral("Message"));
        for (const char *id : {"mailCategorizeAction", "mailCategoriesAction", "mailClearCategoriesAction", "mailFlagTodayAction",
                               "mailFlagCustomAction", "mailFlagCompleteAction", "mailFlagClearAction", "mailCreateRuleAction",
                               "mailRulesAction", "mailRunRulesAction", "mailCleanUpAction", "mailIgnoreAction"})
            QVERIFY2(message.contains(QLatin1String(id)), id);
        QCOMPARE(menuIds(QStringLiteral("Folder")), (QStringList{QStringLiteral("mailNewSearchFolderAction"),
                                                                 QStringLiteral("mailEditSearchFolderAction"),
                                                                 QStringLiteral("mailDeleteSearchFolderAction")}));
        QVERIFY(menuIds(QStringLiteral("Accounts")).contains(QStringLiteral("mailAutoRepliesAction")));
    }

    void pathsHandedToTheApp()
    {
        MailTest::Env env;
        MailHost host(env.service.get());
        QSignalSpy compose(&host, &MailHost::composeRequested);
        // A second launch hands paths to an existing window, never a new one.
        QVERIFY(host.isPristineUntitled());
        QVERIFY(host.loadPath(QStringLiteral("mailto:joe@example.com?subject=Hi")));
        QTRY_COMPARE(compose.count(), 1);
        QCOMPARE(compose.first().first().toMap().value(QStringLiteral("to")).toString(), QStringLiteral("joe@example.com"));

        const QString ics = env.dir.filePath(QStringLiteral("party.ics"));
        QFile file(ics);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//test//EN\r\nBEGIN:VEVENT\r\nUID:party-1\r\n"
                   "DTSTAMP:20260901T000000Z\r\nDTSTART:20261003T180000Z\r\nDTEND:20261003T220000Z\r\n"
                   "SUMMARY:Party\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
        file.close();
        QVERIFY(host.loadPath(QUrl::fromLocalFile(ics).toString()));
        QCOMPARE(host.view(), QStringLiteral("calendar"));
        QTRY_COMPARE(env.service->store()->events().size(), 1);
        QVERIFY(!host.loadPath(env.dir.filePath(QStringLiteral("missing.eml"))));
    }

    void attachHandOff()
    {
        namespace Launch = QindaOffice::Apps::Mail::Launch;
        MailTest::Env env;
        const QString pdf = env.dir.filePath(QStringLiteral("INV-1001.pdf"));
        const QString notes = env.dir.filePath(QStringLiteral("notes.txt"));
        for (const QString &path : {pdf, notes}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(path.endsWith(QStringLiteral(".pdf")) ? QByteArray("%PDF-1.4\n%fake\n") : QByteArray("hello\n"));
        }
        // No --attach: the arguments pass through untouched.
        const QStringList plain{QStringLiteral("mailto:joe@example.com"), QStringLiteral("--new-instance")};
        QCOMPARE(Launch::foldAttachArguments(plain, env.dir.path()), plain);
        // Repeatable, both spellings, relative paths made absolute.
        const QStringList folded = Launch::foldAttachArguments(
            {QStringLiteral("--attach"), QStringLiteral("INV-1001.pdf"), QStringLiteral("--attach=") + notes,
             QStringLiteral("mailto:ap@acme.test?subject=Invoice%20INV-1001&body=Hello"), QStringLiteral("--new-instance")},
            env.dir.path());
        QCOMPARE(folded.size(), 2);
        QCOMPARE(folded.first(), QStringLiteral("--new-instance"));
        const Launch::ComposeRequest request = Launch::parseComposeToken(folded.last());
        QVERIFY(request.ok);
        QCOMPARE(request.attachments, (QStringList{pdf, notes}));
        QCOMPARE(request.mailto, QStringLiteral("mailto:ap@acme.test?subject=Invoice%20INV-1001&body=Hello"));
        QVERIFY(!Launch::parseComposeToken(QStringLiteral("mailto:x@y")).ok);

        MailHost host(env.service.get());
        QSignalSpy compose(&host, &MailHost::composeRequested);
        QVERIFY(host.loadPath(folded.last()));
        QTRY_COMPARE(compose.count(), 1);
        const QVariantMap draft = compose.first().first().toMap();
        QCOMPARE(draft.value(QStringLiteral("to")).toString(), QStringLiteral("ap@acme.test"));
        QCOMPARE(draft.value(QStringLiteral("subject")).toString(), QStringLiteral("Invoice INV-1001"));
        const QVariantList attached = draft.value(QStringLiteral("attachments")).toList();
        QCOMPARE(attached.size(), 2);
        QCOMPARE(attached.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("INV-1001.pdf"));
        QCOMPARE(attached.first().toMap().value(QStringLiteral("mimeType")).toString(), QStringLiteral("application/pdf"));
        // The draft owns a copy in the attachment store.
        const QVariantMap stored =
            env.service->attachmentStore()->read(attached.first().toMap().value(QStringLiteral("id")).toString());
        QVERIFY(stored.value(QStringLiteral("ok")).toBool());
        QVERIFY(stored.value(QStringLiteral("bytes")).toByteArray().startsWith("%PDF"));

        // A file that is gone is named, not silently dropped.
        const QStringList missing = Launch::foldAttachArguments(
            {QStringLiteral("--attach"), env.dir.filePath(QStringLiteral("gone.pdf"))}, env.dir.path());
        QVERIFY(host.loadPath(missing.last()));
        QTRY_COMPARE(compose.count(), 2);
        const QVariantMap blank = compose.last().first().toMap();
        QVERIFY(blank.value(QStringLiteral("attachments")).toList().isEmpty());
        QCOMPARE(blank.value(QStringLiteral("missingAttachments")).toStringList(), QStringList{QStringLiteral("gone.pdf")});

        // AGENT-GUARD check: a mailto: link can never attach a local file.
        const QVariantMap hostile = env.service->mailtoDraft(QStringLiteral("mailto:x@example.com?attach=") + pdf);
        QVERIFY(hostile.value(QStringLiteral("attachments")).toList().isEmpty());
    }

    void windowInstantiates()
    {
        MailTest::Env env;
        auto *host = new MailHost(env.service.get());
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QObject *object = component.createWithInitialProperties({{QStringLiteral("host"), QVariant::fromValue(host)}});
        auto *window = qobject_cast<QQuickWindow *>(object);
        QVERIFY2(window != nullptr, qPrintable(component.errorString()));
        host->attachWindow(window);
        window->show();
        QCOMPARE(window->title(), QStringLiteral("Inbox — ") + host->appDisplayName());
        delete window; // first: bindings must not see the host go away under them
        delete host;
    }
};

MAIL_TEST_MAIN(TestMailHost)

#include "tst_qindamail_host.moc"
