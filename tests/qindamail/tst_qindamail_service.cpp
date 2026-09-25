// SPDX-License-Identifier: GPL-3.0-or-later
// MailService over a temp tree: the local account, the folder tree and its
// counts, the message list (search, filters, conversations, day groups),
// selection marking read after the delay, local message actions, drafts
// (reply / forward / mailto / save / discard) and persistence.

#include "mail_test_support.h"

#include "qindamail/src/folder_model.h"
#include "qindamail/src/message_list_model.h"

#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>

using namespace QindaOffice::Apps::Mail;

class TestMailService : public QObject {
    Q_OBJECT

    static QStringList keys(const FolderModel *model)
    {
        QStringList list;
        for (const FolderRow &row : model->rows()) list.append(row.key);
        return list;
    }

    static FolderRow row(const FolderModel *model, const QString &key)
    {
        const int at = model->indexOfKey(key);
        return at >= 0 ? model->rows().at(at) : FolderRow();
    }

    // Three local inbox messages (two in one conversation), one archived.
    static void seed(MailService *service)
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        auto *store = service->store();
        store->upsert(QStringLiteral("messages"),
                      MailTest::message(QStringLiteral("local:1"), QStringLiteral("Budget draft"), now.addSecs(-60),
                                        {{QStringLiteral("threadId"), QStringLiteral("t-budget")},
                                         {QStringLiteral("localMailbox"), QStringLiteral("Inbox")}}));
        store->upsert(QStringLiteral("messages"),
                      MailTest::message(QStringLiteral("local:2"), QStringLiteral("Re: Budget draft"), now.addSecs(-30),
                                        {{QStringLiteral("threadId"), QStringLiteral("t-budget")},
                                         {QStringLiteral("unread"), false},
                                         {QStringLiteral("localMailbox"), QStringLiteral("Inbox")}}));
        store->upsert(QStringLiteral("messages"),
                      MailTest::message(QStringLiteral("local:3"), QStringLiteral("Holiday photos"), now.addDays(-20),
                                        {{QStringLiteral("flagged"), true},
                                         {QStringLiteral("sender"), QStringLiteral("Linus")},
                                         {QStringLiteral("senderAddress"), QStringLiteral("linus@example.org")},
                                         {QStringLiteral("localMailbox"), QStringLiteral("Inbox")}}));
        store->upsert(QStringLiteral("messages"),
                      MailTest::message(QStringLiteral("local:4"), QStringLiteral("Old receipt"), now.addDays(-40),
                                        {{QStringLiteral("localMailbox"), QStringLiteral("Archive")}}));
        QVERIFY(store->save());
    }

private slots:
    void localAccountAlwaysExists()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QVariantList accounts = service->accounts();
        QCOMPARE(accounts.size(), 1);
        const QVariantMap local = accounts.first().toMap();
        QCOMPARE(local.value(QStringLiteral("id")).toString(), QStringLiteral("local"));
        QCOMPARE(local.value(QStringLiteral("displayName")).toString(), QStringLiteral("On this computer"));
        QCOMPARE(local.value(QStringLiteral("status")).toString(), QStringLiteral("idle"));
        QCOMPARE(service->defaultAccountId(), QStringLiteral("local"));
        QVERIFY(service->sendableAccounts().isEmpty());
        QCOMPARE(keys(service->folderModel()),
                 (QStringList{QStringLiteral("all:inbox"), QStringLiteral("acct:local"),
                              QStringLiteral("acct:local:Inbox"), QStringLiteral("acct:local:Drafts"),
                              QStringLiteral("acct:local:Outbox"), QStringLiteral("acct:local:Sent"),
                              QStringLiteral("acct:local:Archive"), QStringLiteral("acct:local:Trash")}));
        QCOMPARE(service->currentFolderKey(), QStringLiteral("acct:local:Inbox"));
        QCOMPARE(service->currentFolderName(), QStringLiteral("Inbox"));
        QVERIFY(row(service->folderModel(), QStringLiteral("acct:local")).isAccount);
        QVERIFY(!row(service->folderModel(), QStringLiteral("acct:local")).selectable);
        const QVariantList calendars = service->calendars();
        QCOMPARE(calendars.size(), 1);
        QCOMPARE(calendars.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("local"));
        QCOMPARE(service->writableCalendars().size(), 1);
        QCOMPARE(service->taskColumns().size(), 3);
        // A folder cannot be collapsed away from under the current key.
        service->toggleFolderExpanded(QStringLiteral("acct:local"));
        QCOMPARE(service->folderModel()->indexOfKey(QStringLiteral("acct:local:Inbox")), -1);
        QVERIFY(!row(service->folderModel(), QStringLiteral("acct:local")).expanded);
        service->toggleFolderExpanded(QStringLiteral("acct:local"));
        QVERIFY(service->folderModel()->indexOfKey(QStringLiteral("acct:local:Inbox")) > 0);
    }

    void foldersCountAndList()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        seed(service);
        QTRY_COMPARE(row(service->folderModel(), QStringLiteral("acct:local:Inbox")).total, 3);
        QCOMPARE(row(service->folderModel(), QStringLiteral("acct:local:Inbox")).unread, 2);
        QCOMPARE(row(service->folderModel(), QStringLiteral("acct:local:Archive")).total, 1);
        QCOMPARE(row(service->folderModel(), QStringLiteral("all:inbox")).unread, 2);
        QCOMPARE(service->unreadTotal(), 2);

        MessageListModel *list = service->messageModel();
        // Conversations: the budget thread is one row of two, newest first.
        QCOMPARE(list->rowCount(), 2);
        QVariantMap first = list->get(0);
        QCOMPARE(first.value(QStringLiteral("messageId")).toString(), QStringLiteral("local:2"));
        QCOMPARE(first.value(QStringLiteral("threadCount")).toInt(), 2);
        QVERIFY(first.value(QStringLiteral("unread")).toBool());
        QCOMPARE(first.value(QStringLiteral("dayGroup")).toString(), QStringLiteral("Today"));
        QCOMPARE(first.value(QStringLiteral("accountId")).toString(), QStringLiteral("local"));
        QCOMPARE(first.value(QStringLiteral("threadMessageIds")).toStringList(),
                 (QStringList{QStringLiteral("local:1"), QStringLiteral("local:2")}));
        QCOMPARE(list->get(1).value(QStringLiteral("dayGroup")).toString(), QStringLiteral("Earlier"));
        QCOMPARE(service->conversation(QStringLiteral("t-budget")).size(), 2);

        service->setThreaded(false);
        QCOMPARE(list->rowCount(), 3);
        service->setFilter(QStringLiteral("unread"));
        QCOMPARE(list->rowCount(), 2);
        service->setFilter(QStringLiteral("flagged"));
        QCOMPARE(list->rowCount(), 1);
        QCOMPARE(list->get(0).value(QStringLiteral("messageId")).toString(), QStringLiteral("local:3"));
        service->setFilter(QStringLiteral("all"));
        service->setSearchText(QStringLiteral("linus photos"));
        QCOMPARE(list->rowCount(), 1);
        service->setSearchText(QStringLiteral("body of budget"));
        QCOMPARE(list->rowCount(), 2);
        service->setSearchText(QString());

        // The unified inbox shows the same three; Archive shows its one.
        service->setCurrentFolderKey(QStringLiteral("all:inbox"));
        QCOMPARE(list->rowCount(), 3);
        service->setCurrentFolderKey(QStringLiteral("acct:local:Archive"));
        QCOMPARE(service->currentFolderName(), QStringLiteral("Archive"));
        QCOMPARE(list->rowCount(), 1);
        // Account headers are not selectable.
        service->setCurrentFolderKey(QStringLiteral("acct:local"));
        QCOMPARE(service->currentFolderKey(), QStringLiteral("acct:local:Archive"));
        // The threaded setting survives a restart.
        auto reopened = env.reopen();
        QVERIFY(!reopened->threaded());
    }

    void selectionMarksReadAfterDelay()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        seed(service);
        service->setMarkReadDelayMs(80);
        QSignalSpy selected(service, &MailService::selectedMessageIdChanged);
        service->setSelectedMessageId(QStringLiteral("local:1"));
        QCOMPARE(selected.count(), 1);
        const QVariantMap message = service->selectedMessage();
        QCOMPARE(message.value(QStringLiteral("subject")).toString(), QStringLiteral("Budget draft"));
        QCOMPARE(message.value(QStringLiteral("accountId")).toString(), QStringLiteral("local"));
        QCOMPARE(message.value(QStringLiteral("to")).toString(), QStringLiteral("Ada <ada@example.org>"));
        QCOMPARE(message.value(QStringLiteral("bodyTruncated")).toBool(), false);
        QVERIFY(message.contains(QStringLiteral("attachments")));
        QVERIFY(message.value(QStringLiteral("unread")).toBool());
        QTest::qWait(20);
        QVERIFY(service->selectedMessage().value(QStringLiteral("unread")).toBool()); // not yet
        QTRY_VERIFY(!service->selectedMessage().value(QStringLiteral("unread")).toBool());
        QTRY_COMPARE(service->unreadTotal(), 1);

        // Moving on before the delay leaves the message unread.
        service->setSelectedMessageId(QStringLiteral("local:3"));
        service->markRead({QStringLiteral("local:3")}, false);
        service->setMarkReadDelayMs(200);
        service->setSelectedMessageId(QStringLiteral("local:1"));
        service->setSelectedMessageId(QStringLiteral("local:3"));
        service->setSelectedMessageId(QStringLiteral("local:2"));
        QTest::qWait(300);
        QVERIFY(service->messageById(QStringLiteral("local:3")).value(QStringLiteral("unread")).toBool());
    }

    void localActions()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        seed(service);
        QSignalSpy finished(service, &MailService::operationFinished);
        QString id = service->setFlagged({QStringLiteral("local:1")}, true);
        QCOMPARE(MailTest::waitState(finished, id).first, QStringLiteral("accepted"));
        QVERIFY(service->messageById(QStringLiteral("local:1")).value(QStringLiteral("flagged")).toBool());

        id = service->archive({QStringLiteral("local:1")});
        QCOMPARE(MailTest::waitState(finished, id).first, QStringLiteral("accepted"));
        QCOMPARE(service->messageById(QStringLiteral("local:1")).value(QStringLiteral("folderKey")).toString(),
                 QStringLiteral("acct:local:Archive"));

        id = service->moveTo({QStringLiteral("local:1")}, QStringLiteral("acct:local:Inbox"));
        QCOMPARE(MailTest::waitState(finished, id).first, QStringLiteral("accepted"));
        QCOMPARE(service->messageById(QStringLiteral("local:1")).value(QStringLiteral("folderKey")).toString(),
                 QStringLiteral("acct:local:Inbox"));

        // Delete goes to Trash; deleting from Trash removes the record.
        id = service->deleteMessages({QStringLiteral("local:1")});
        QCOMPARE(MailTest::waitState(finished, id).first, QStringLiteral("accepted"));
        QTRY_COMPARE(row(service->folderModel(), QStringLiteral("acct:local:Trash")).total, 1);
        id = service->deleteMessages({QStringLiteral("local:1")});
        QCOMPARE(MailTest::waitState(finished, id).first, QStringLiteral("accepted"));
        QVERIFY(service->messageById(QStringLiteral("local:1")).isEmpty());

        // Unknown ids and cross-account moves are refused, nothing changes.
        id = service->moveTo({QStringLiteral("local:2")}, QStringLiteral("acct:other:INBOX"));
        QCOMPARE(MailTest::waitState(finished, id).first, QStringLiteral("rejected"));
        id = service->archive({QStringLiteral("nope")});
        QCOMPARE(MailTest::waitState(finished, id).first, QStringLiteral("rejected"));
        // Nothing to do still answers (on a later turn, after the id is known).
        id = service->markRead({}, true);
        QCOMPARE(MailTest::waitState(finished, id).first, QStringLiteral("accepted"));

        const QVariantList targets = service->folderTargets(QStringLiteral("local"));
        QStringList targetKeys;
        for (const QVariant &value : targets) targetKeys.append(value.toMap().value(QStringLiteral("key")).toString());
        QVERIFY(targetKeys.contains(QStringLiteral("acct:local:Archive")));
        QVERIFY(!targetKeys.contains(QStringLiteral("acct:local:Drafts")));
    }

    void draftsReplyForwardAndMailto()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        seed(service);
        auto *attachments = service->attachmentStore();
        QVERIFY(attachments->importBytes(QStringLiteral("local:3/2"), QStringLiteral("beach.jpg"),
                                         QStringLiteral("image/jpeg"), QByteArray("JPEGDATA"))
                    .value(QStringLiteral("ok")).toBool());
        QVariantMap photo = service->messageById(QStringLiteral("local:3"));
        photo.insert(QStringLiteral("attachments"),
                     QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("local:3/2")},
                                              {QStringLiteral("name"), QStringLiteral("beach.jpg")},
                                              {QStringLiteral("mimeType"), QStringLiteral("image/jpeg")},
                                              {QStringLiteral("size"), 8},
                                              {QStringLiteral("state"), QStringLiteral("local")}}});
        photo.remove(QStringLiteral("folderKey"));
        photo.remove(QStringLiteral("isDraft"));
        service->store()->upsert(QStringLiteral("messages"), photo);

        const QVariantMap reply = service->replyDraft(QStringLiteral("local:3"), false);
        QCOMPARE(reply.value(QStringLiteral("subject")).toString(), QStringLiteral("Re: Holiday photos"));
        QCOMPARE(reply.value(QStringLiteral("to")).toString(), QStringLiteral("Linus <linus@example.org>"));
        QCOMPARE(reply.value(QStringLiteral("accountId")).toString(), QStringLiteral("local"));
        QVERIFY(reply.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("draft:")));

        const QVariantMap forward = service->forwardDraft(QStringLiteral("local:3"));
        const QVariantList copied = forward.value(QStringLiteral("attachments")).toList();
        QCOMPARE(copied.size(), 1);
        const QString copyId = copied.first().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(copyId != QStringLiteral("local:3/2"));
        QCOMPARE(attachments->read(copyId).value(QStringLiteral("bytes")).toByteArray(), QByteArray("JPEGDATA"));

        const QVariantMap saved = service->saveDraft(forward);
        QVERIFY2(saved.value(QStringLiteral("ok")).toBool(), qPrintable(saved.value(QStringLiteral("error")).toString()));
        QCOMPARE(saved.value(QStringLiteral("id")).toString(), forward.value(QStringLiteral("id")).toString());
        QTRY_COMPARE(row(service->folderModel(), QStringLiteral("acct:local:Drafts")).total, 1);
        service->setCurrentFolderKey(QStringLiteral("acct:local:Drafts"));
        QCOMPARE(service->messageModel()->rowCount(), 1);
        QVERIFY(service->messageModel()->get(0).value(QStringLiteral("isDraft")).toBool());
        // A draft survives a restart.
        QCOMPARE(env.reopen()->store()->drafts().size(), 1);
        // Discarding removes its own copy, never the message's attachment.
        QVERIFY(service->discardDraft(saved.value(QStringLiteral("id")).toString()));
        QVERIFY(!attachments->read(copyId).value(QStringLiteral("ok")).toBool());
        QVERIFY(attachments->read(QStringLiteral("local:3/2")).value(QStringLiteral("ok")).toBool());
        QTRY_COMPARE(row(service->folderModel(), QStringLiteral("acct:local:Drafts")).total, 0);

        const QVariantMap mailto = service->mailtoDraft(
            QStringLiteral("mailto:joe@example.com?cc=bob@example.com&subject=Hi&body=hello%0D%0Athere"));
        QCOMPARE(mailto.value(QStringLiteral("to")).toString(), QStringLiteral("joe@example.com"));
        QCOMPARE(mailto.value(QStringLiteral("cc")).toString(), QStringLiteral("bob@example.com"));
        QCOMPARE(mailto.value(QStringLiteral("subject")).toString(), QStringLiteral("Hi"));
        QCOMPARE(mailto.value(QStringLiteral("body")).toString(), QStringLiteral("hello\nthere"));
        QVERIFY(!mailto.value(QStringLiteral("id")).toString().isEmpty());

        // Sending from the local account (no SMTP) keeps the draft and says why.
        QSignalSpy sent(service, &MailService::sendFinished);
        const QString requestId = service->send(mailto);
        const QVariantMap result = MailTest::waitResult(sent, requestId);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QCOMPARE(service->store()->drafts().size(), 1);
    }

    void openedMessageFile()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QString path = env.dir.filePath(QStringLiteral("note.eml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("From: Ada <ada@example.org>\r\nTo: bob@example.org\r\nSubject: Opened\r\n"
                   "Message-ID: <opened@example.org>\r\nDate: Fri, 25 Sep 2026 10:00:00 +0000\r\n"
                   "MIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=b\r\n\r\n"
                   "--b\r\nContent-Type: text/plain\r\n\r\nHello there\r\n"
                   "--b\r\nContent-Type: text/plain; name=a.txt\r\nContent-Disposition: attachment; filename=a.txt\r\n\r\n"
                   "attached\r\n--b--\r\n");
        file.close();
        const QString id = service->importMessageFile(path);
        QVERIFY(!id.isEmpty());
        QCOMPARE(service->importMessageFile(path), id); // same bytes, same message
        QCOMPARE(service->selectedMessageId(), id);
        const QVariantMap message = service->selectedMessage();
        QCOMPARE(message.value(QStringLiteral("subject")).toString(), QStringLiteral("Opened"));
        QCOMPARE(message.value(QStringLiteral("folderKey")).toString(), QStringLiteral("acct:local:Inbox"));
        const QVariantList attachments = message.value(QStringLiteral("attachments")).toList();
        QCOMPARE(attachments.size(), 1);
        QCOMPARE(attachments.first().toMap().value(QStringLiteral("state")).toString(), QStringLiteral("local"));

        // Opening writes the stored bytes to a private file and hands it on.
        QUrl opened;
        service->setUrlOpener([&opened](const QUrl &url) {
            opened = url;
            return true;
        });
        QSignalSpy finished(service, &MailService::operationFinished);
        const QString attachmentId = attachments.first().toMap().value(QStringLiteral("id")).toString();
        const QString request = service->openAttachment(id, attachmentId);
        QCOMPARE(MailTest::waitState(finished, request).first, QStringLiteral("accepted"));
        QFile written(opened.toLocalFile());
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll().trimmed(), QByteArray("attached"));
        const QString target = env.dir.filePath(QStringLiteral("saved.txt"));
        const QString save = service->saveAttachmentAs(id, attachmentId, QUrl::fromLocalFile(target));
        QCOMPARE(MailTest::waitState(finished, save).first, QStringLiteral("accepted"));
        QVERIFY(QFileInfo::exists(target));
    }
};

MAIL_TEST_MAIN(TestMailService)

#include "tst_qindamail_service.moc"
