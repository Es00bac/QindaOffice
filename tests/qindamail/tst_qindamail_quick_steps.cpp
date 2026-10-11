// SPDX-License-Identifier: GPL-3.0-or-later
#include "mail_test_support.h"
#include "fake_imap_server.h"
#include "fake_mail_server.h"
#include "qindamail/src/account_runtime.h"
#include "qindamail/src/mail_quick_steps.h"
#include "qindamail/src/mail_organizer.h"
#include <qindatk/local_productivity_store.h>
#include <QFile>
#include <QJsonDocument>
using namespace QindaOffice::Apps::Mail;
using namespace Qt::StringLiterals;
class QuickStepsTest : public QObject {
    Q_OBJECT
private slots:
    void persistenceOrderAndValidation()
    {
        MailTest::Env env;
        auto *steps=env.service->quickSteps();
        auto first=steps->newStep(u"local"_s); first.insert(u"name"_s,u"Archive review"_s);
        QVERIFY(steps->saveStep(first).value(u"ok"_s).toBool());
        auto second=steps->newStep(u"local"_s); second.insert(u"name"_s,u"Later"_s);
        QVERIFY(steps->saveStep(second).value(u"ok"_s).toBool());
        QVERIFY(steps->moveStep(second.value(u"id"_s).toString(),0));
        auto reopened=env.reopen();
        QCOMPARE(reopened->quickSteps()->forMailbox(u"local"_s).first().toMap().value(u"name"_s).toString(),u"Later"_s);
        second.insert(u"actions"_s,QVariantList{QVariantMap{{u"kind"_s,u"move"_s},{u"folderKey"_s,u"acct:local:Trash"_s}},
                                               QVariantMap{{u"kind"_s,u"markRead"_s}}});
        QVERIFY(!steps->saveStep(second).value(u"ok"_s).toBool());
        QVERIFY(steps->removeStep(first.value(u"id"_s).toString()));
        QCOMPARE(steps->forMailbox(u"local"_s).size(),1);
    }
    void orderedActionsCreateUnqueuedDraftAndMoveLast()
    {
        MailTest::Env env;
        const QString id=u"local:test"_s;
        QVERIFY(env.service->store()->upsert(u"messages"_s,MailTest::message(id,u"Review"_s,QDateTime::currentDateTimeUtc())).value(u"ok"_s).toBool());
        QTest::qWait(10);
        auto *steps=env.service->quickSteps(); auto step=steps->newStep(u"local"_s);
        const auto folders=env.service->folderTargets(u"local"_s);
        QVERIFY(!folders.isEmpty());
        QString destination;
        for (const auto &folder:folders) if (folder.toMap().value(u"key"_s).toString().endsWith(u":Archive"_s)) destination=folder.toMap().value(u"key"_s).toString();
        QVERIFY2(!destination.isEmpty(),qPrintable(QJsonDocument::fromVariant(folders).toJson()));
        step.insert(u"actions"_s,QVariantList{
            QVariantMap{{u"kind"_s,u"markRead"_s},{u"read"_s,true}},
            QVariantMap{{u"kind"_s,u"categories"_s},{u"names"_s,QStringList{u"Review"_s}}},
            QVariantMap{{u"kind"_s,u"flag"_s},{u"preset"_s,u"today"_s}},
            QVariantMap{{u"kind"_s,u"replyDraft"_s},{u"body"_s,u"Thanks"_s}},
            QVariantMap{{u"kind"_s,u"copy"_s},{u"folderKey"_s,destination}},
            QVariantMap{{u"kind"_s,u"move"_s},{u"folderKey"_s,destination}}});
        QVERIFY2(steps->saveStep(step).value(u"ok"_s).toBool(),qPrintable(steps->validate(step).join(u'\n')));
        QSignalSpy done(steps,&MailQuickSteps::finished);
        const auto result=MailTest::waitResult(done,steps->execute(step.value(u"id"_s).toString(),{id}));
        QCOMPARE(result.value(u"state"_s).toString(),u"accepted"_s);
        QCOMPARE(result.value(u"completed"_s).toInt(),6);
        QCOMPARE(done.size(),1);
        QCOMPARE(env.service->store()->drafts().size(),1);
        const auto draft=env.service->store()->drafts().first().toMap();
        QVERIFY(!draft.value(u"queued"_s).toBool());
        QVERIFY(draft.value(u"body"_s).toString().startsWith(u"Thanks"_s));
        QCOMPARE(env.service->organizer()->categoriesOf(u"message"_s,id),QStringList{u"Review"_s});
        QVERIFY(!env.service->messageById(id).value(u"unread"_s).toBool());
    }
    void cancellationAndMissingMessageAreVisible()
    {
        MailTest::Env env; auto *steps=env.service->quickSteps(); auto step=steps->newStep(u"local"_s);
        QVERIFY(steps->saveStep(step).value(u"ok"_s).toBool());
        const QString id=u"local:test"_s;
        QVERIFY(env.service->store()->upsert(u"messages"_s,MailTest::message(id,u"Review"_s,QDateTime::currentDateTimeUtc())).value(u"ok"_s).toBool());
        QTest::qWait(10);
        QSignalSpy done(steps,&MailQuickSteps::finished);
        const QString request=steps->execute(step.value(u"id"_s).toString(),{id});
        steps->cancel();
        auto result=MailTest::waitResult(done,request);
        QCOMPARE(result.value(u"state"_s).toString(),u"cancelled"_s);
        QCOMPARE(result.value(u"completed"_s).toInt(),0);
        QVERIFY(env.service->messageById(id).value(u"unread"_s).toBool());
        const QString rejected=steps->execute(step.value(u"id"_s).toString(),{u"missing"_s});
        result=MailTest::waitResult(done,rejected);
        QCOMPARE(result.value(u"state"_s).toString(),u"rejected"_s);
        QVERIFY(!result.value(u"error"_s).toString().isEmpty());
    }

    void imapActionsRunInOrderThroughTheProvider()
    {
        MailTest::Env env;
        MailTest::FakeImapServer imap("pw");
        MailTest::FakeMailServer smtp(MailTest::FakeMailServer::Kind::Smtp, "pw");
        imap.inbox.append({7, "From: Grace <grace@example.org>\r\nTo: Ada <ada@work.example>\r\n"
                               "Subject: Review\r\nMessage-ID: <review@example.org>\r\n"
                               "MIME-Version: 1.0\r\nContent-Type: text/plain\r\n\r\nPlease review.\r\n", false});
        const auto server = [](quint16 port, bool incoming) {
            QVariantMap value{{u"host"_s, u"127.0.0.1"_s}, {u"port"_s, port},
                              {u"security"_s, u"none"_s}, {u"userName"_s, u"ada"_s}};
            if (incoming) value.insert(u"protocol"_s, u"imap"_s);
            return value;
        };
        const QVariantMap added = env.service->addAccount({
            {u"kind"_s, u"imap"_s}, {u"displayName"_s, u"Work"_s},
            {u"emailAddress"_s, u"ada@work.example"_s},
            {u"incoming"_s, server(imap.port(), true)},
            {u"outgoing"_s, server(smtp.port(), false)}}, u"pw"_s);
        QVERIFY2(added.value(u"ok"_s).toBool(), qPrintable(added.value(u"error"_s).toString()));
        const QString account = added.value(u"id"_s).toString();
        auto *runtime = env.service->runtime(account);
        QVERIFY(runtime != nullptr);
        runtime->setMailboxes({QVariantMap{{u"name"_s, u"INBOX"_s}, {u"specialUse"_s, u"inbox"_s}},
                               QVariantMap{{u"name"_s, u"Projects"_s}, {u"delimiter"_s, u"/"_s}}});
        QSignalSpy synced(env.service.get(), &MailService::operationFinished);
        QCOMPARE(MailTest::waitState(synced, env.service->syncFolder(u"acct:"_s + account + u":INBOX"_s)).first,
                 u"accepted"_s);
        const QString message = u"imap:%1:INBOX:4242:7"_s.arg(account);
        QVERIFY(!env.service->messageById(message).isEmpty());
        const QString folder = u"acct:"_s + account + u":Projects"_s;
        auto *steps = env.service->quickSteps();
        auto step = steps->newStep(account);
        step.insert(u"actions"_s, QVariantList{
            QVariantMap{{u"kind"_s, u"markRead"_s}, {u"read"_s, true}},
            QVariantMap{{u"kind"_s, u"categories"_s}, {u"mode"_s, u"add"_s}, {u"names"_s, QStringList{u"Review"_s}}},
            QVariantMap{{u"kind"_s, u"flag"_s}, {u"preset"_s, u"today"_s}},
            QVariantMap{{u"kind"_s, u"copy"_s}, {u"folderKey"_s, folder}},
            QVariantMap{{u"kind"_s, u"move"_s}, {u"folderKey"_s, folder}}});
        QVERIFY2(steps->saveStep(step).value(u"ok"_s).toBool(), qPrintable(steps->validate(step).join(u'\n')));
        QSignalSpy finished(steps, &MailQuickSteps::finished);
        const QVariantMap result = MailTest::waitResult(finished, steps->execute(step.value(u"id"_s).toString(), {message}));
        QCOMPARE(result.value(u"state"_s).toString(), u"accepted"_s);
        QCOMPARE(result.value(u"completed"_s).toInt(), 5);
        QCOMPARE(imap.inbox.size(), 0);
        QCOMPARE(imap.folders.value(u"Projects"_s).size(), 2);
        for (const auto &copied : imap.folders.value(u"Projects"_s)) {
            QVERIFY(copied.seen && copied.flagged);
            QVERIFY(copied.keywords.contains(u"Review"_s));
        }
        const int copyAt = imap.commands.indexOf(QByteArrayLiteral("UID COPY 7 \"Projects\""));
        const int moveAt = imap.commands.indexOf(QByteArrayLiteral("UID MOVE 7 \"Projects\""));
        QVERIFY(copyAt >= 0 && moveAt > copyAt);
    }
};
int main(int argc,char **argv) {
    qputenv("QT_QPA_PLATFORM","offscreen"); qputenv("QT_QUICK_BACKEND","software");
    QGuiApplication app(argc,argv); QuickStepsTest test; return QTest::qExec(&test,argc,argv);
}
#include "tst_qindamail_quick_steps.moc"
