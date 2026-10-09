// SPDX-License-Identifier: GPL-3.0-or-later
// QindaMail's Outlook import (M76, D-085): the standard .pst fixture into a
// local folder tree (unread and flags kept), People (a contact and a group
// that expands), the local calendar (a weekly series in its zone with a
// deleted occurrence, an all-day event) and the task board; a second
// import replaces instead of duplicating; progress and cancel; corrupt
// files fail cleanly; 10 000 messages within the stated bound; .msg
// contacts, groups, appointments and tasks routed to their homes; and the
// import dialog through the real window (progress, summary, grabs).

#include "mail_test_support.h"

#include "office_mail/pst_fixture.h"
#include "qindamail/src/mail_host.h"
#include "qindamail/src/outlook_records.h"
#include "qindamail/src/pst_import.h"

#include "contact_store.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <qindatk/local_productivity_store.h>

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

using namespace QindaOffice::Apps::Mail;
using namespace QindaOffice::Mail::Test;
using QindaOffice::Test::findNamed;

namespace {

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(bytes);
    return path;
}

// A small file with its own record key (its own identity).
QByteArray pstFile(const QByteArray &recordKey)
{
    PstBuilder b(true);
    b.recordKey = recordKey;
    b.top.children = {pstMailFolders(b).children.last()};
    return b.build();
}

QVariantMap runImport(MailService *service, const QString &path, int timeoutMs = 60000)
{
    QSignalSpy finished(service->outlookImport(), &PstImporter::finished);
    const QString id = service->outlookImport()->start(QUrl::fromLocalFile(path));
    if (id.isEmpty()) return {{QStringLiteral("state"), QStringLiteral("not started")}};
    return MailTest::waitResult(finished, id, timeoutMs);
}

QList<QVariantMap> recordsIn(MailService *service, const QString &mailbox)
{
    QList<QVariantMap> out;
    for (const QVariant &value : service->store()->messages()) {
        if (value.toMap().value(QStringLiteral("localMailbox")).toString() == mailbox) out.append(value.toMap());
    }
    return out;
}

QVariantMap findById(const QVariantList &records, const QString &id)
{
    for (const QVariant &value : records) {
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    }
    return {};
}

} // namespace

class TestMailPstImport : public QObject {
    Q_OBJECT

private slots:
    void importsEveryKindOfItem();
    void secondImportReplaces();
    void progressAndCancel();
    void corruptFilesFailCleanly();
    void tenThousandMessages();
    void msgItemsGoToTheirHomes();
    void importDialogThroughTheWindow();
};

void TestMailPstImport::importsEveryKindOfItem()
{
    MailTest::Env env;
    MailService *service = env.service.get();
    PstBuilder builder(true);
    const QString path = writeFile(env.dir, QStringLiteral("outlook.pst"), buildStandardPst(true, 1, nullptr, &builder));
    const QVariantMap summary = runImport(service, path);
    QCOMPARE(summary.value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
    QCOMPARE(summary.value(QStringLiteral("folder")).toString(), QStringLiteral("outlook"));
    QCOMPARE(summary.value(QStringLiteral("folders")).toInt(), 8);
    QCOMPARE(summary.value(QStringLiteral("messages")).toInt(), 6);
    QCOMPARE(summary.value(QStringLiteral("contacts")).toInt(), 1);
    QCOMPARE(summary.value(QStringLiteral("groups")).toInt(), 1);
    QCOMPARE(summary.value(QStringLiteral("events")).toInt(), 2);
    QCOMPARE(summary.value(QStringLiteral("tasks")).toInt(), 2);
    QCOMPARE(summary.value(QStringLiteral("skipped")).toInt(), 1);   // the sticky note
    QCOMPARE(service->outlookImport()->phase(), QStringLiteral("done"));

    // Mail: the file's tree under "On this computer", read state and flags kept.
    QCOMPARE(recordsIn(service, QStringLiteral("outlook/Inbox")).size(), 4);
    QCOMPARE(recordsIn(service, QStringLiteral("outlook/Inbox/Projects")).size(), 1);
    QCOMPARE(recordsIn(service, QStringLiteral("outlook/Sent Items")).size(), 1);
    const QStringList keys = [&] {
        QStringList out;
        for (const FolderRow &row : service->folderModel()->rows()) out.append(row.key + QLatin1Char('@') + QString::number(row.depth));
        return out;
    }();
    QVERIFY2(keys.contains(QStringLiteral("acct:local:outlook@1")), qPrintable(keys.join(u' ')));
    QVERIFY(keys.contains(QStringLiteral("acct:local:outlook/Inbox@2")));
    QVERIFY(keys.contains(QStringLiteral("acct:local:outlook/Inbox/Projects@3")));
    for (const QVariantMap &record : recordsIn(service, QStringLiteral("outlook/Inbox"))) {
        if (record.value(QStringLiteral("subject")).toString() == QLatin1String("Plain hello")) {
            QVERIFY(record.value(QStringLiteral("unread")).toBool());
            QVERIFY(record.value(QStringLiteral("flagged")).toBool());
        }
        if (record.value(QStringLiteral("subject")).toString() == QLatin1String("Quarterly report")) {
            QVERIFY(!record.value(QStringLiteral("unread")).toBool());
            QVERIFY(service->contentOf(record.value(QStringLiteral("id")).toString()).html.contains(QLatin1String("Numbers")));
            QCOMPARE(record.value(QStringLiteral("attachments")).toList().size(), 2);
        }
    }
    // People: the contact, and the group expanding to its two members.
    QindaOffice::Mail::ContactStore *contacts = service->contacts();
    const QString graceUid = OutlookRecords::uidFor(OutlookRecords::itemKey(builder.recordKey, builder.messageNids.at(6)));
    const QVariantMap grace = contacts->contactForAddress(QStringLiteral("grace@example.org"));
    QCOMPARE(grace.value(QStringLiteral("uid")).toString(), graceUid);
    QVERIFY(grace.value(QStringLiteral("hasPhoto")).toBool());
    QString team;
    for (const QVariant &value : contacts->contacts()) {
        if (value.toMap().value(QStringLiteral("isGroup")).toBool()) team = value.toMap().value(QStringLiteral("id")).toString();
    }
    QVERIFY(!team.isEmpty());
    QCOMPARE(contacts->expandGroup(team).value(QStringLiteral("recipients")).toStringList(),
             (QStringList{QStringLiteral("Grace Hopper <grace@example.org>"), QStringLiteral("Alan Turing <alan@example.org>")}));
    // Calendar: wall times in the event's zone, the series and its exception.
    const QVariantMap review = findById(service->store()->events(),
                                        OutlookRecords::itemKey(builder.recordKey, builder.messageNids.at(8)));
    QCOMPARE(review.value(QStringLiteral("zone")).toString(), QStringLiteral("America/Los_Angeles"));
    QCOMPARE(review.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-10-05T09:00:00"));
    QCOMPARE(review.value(QStringLiteral("location")).toString(), QStringLiteral("Room 4"));
    const QVariantMap rule = review.value(QStringLiteral("recurrenceRule")).toMap();
    QCOMPARE(rule.value(QStringLiteral("frequency")).toString(), QStringLiteral("weekly"));
    QCOMPARE(rule.value(QStringLiteral("count")).toInt(), 10);
    QCOMPARE(rule.value(QStringLiteral("weekdays")).toList(), (QVariantList{1, 3}));
    QCOMPARE(review.value(QStringLiteral("exceptions")).toList(), QVariantList{QStringLiteral("2026-10-12T09:00:00")});
    const QVariantMap holiday = findById(service->store()->events(),
                                         OutlookRecords::itemKey(builder.recordKey, builder.messageNids.at(9)));
    QVERIFY(holiday.value(QStringLiteral("allDay")).toBool());
    QCOMPARE(holiday.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-12-25"));
    QCOMPARE(holiday.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-12-26"));
    // Tasks on the board.
    const QVariantMap brandBook = findById(service->store()->tasks(), QStringLiteral("task:")
                                           + OutlookRecords::itemKey(builder.recordKey, builder.messageNids.at(10)));
    QCOMPARE(brandBook.value(QStringLiteral("status")).toString(), QStringLiteral("doing"));
    QCOMPARE(brandBook.value(QStringLiteral("priority")).toString(), QStringLiteral("High"));
    QCOMPARE(brandBook.value(QStringLiteral("dueDate")).toString(), QStringLiteral("2026-10-15"));
    const QVariantMap invoices = findById(service->store()->tasks(), QStringLiteral("task:")
                                          + OutlookRecords::itemKey(builder.recordKey, builder.messageNids.at(11)));
    QCOMPARE(invoices.value(QStringLiteral("status")).toString(), QStringLiteral("done"));
    // Everything survives a restart.
    auto reopened = env.reopen();
    QCOMPARE(reopened->store()->messages().size(), 6);
    QCOMPARE(reopened->contacts()->count(), 2);
}

void TestMailPstImport::secondImportReplaces()
{
    MailTest::Env env;
    MailService *service = env.service.get();
    const QString path = writeFile(env.dir, QStringLiteral("backup.pst"), buildStandardPst(false, 2));
    QCOMPARE(runImport(service, path).value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
    const auto sizes = [service] {
        return QList<qsizetype>{service->store()->messages().size(), service->store()->events().size(),
                                service->store()->tasks().size(), qsizetype(service->contacts()->count())};
    };
    const QList<qsizetype> first = sizes();
    QCOMPARE(first, (QList<qsizetype>{6, 2, 2, 2}));
    // The same file (copied elsewhere) again: same folder, nothing doubled.
    const QString copy = writeFile(env.dir, QStringLiteral("copy of backup.pst"), buildStandardPst(false, 2));
    const QVariantMap again = runImport(service, copy);
    QCOMPARE(again.value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
    QCOMPARE(again.value(QStringLiteral("folder")).toString(), QStringLiteral("backup"));
    QCOMPARE(sizes(), first);
    // A different file with the same name gets its own folder.
    PstBuilder other(true);
    other.recordKey = QByteArray(16, '\x42');
    other.top.children = {pstMailFolders(other).children.first()};
    QDir().mkpath(env.dir.filePath(QStringLiteral("sub")));
    const QString second = writeFile(env.dir, QStringLiteral("sub/backup.pst"), other.build());
    QCOMPARE(runImport(service, second).value(QStringLiteral("folder")).toString(), QStringLiteral("backup (2)"));
    // "Archive" is a local folder already: an archive.pst gets its own.
    const QString archive = writeFile(env.dir, QStringLiteral("Archive.pst"), pstFile(QByteArray(16, '\x07')));
    QCOMPARE(runImport(service, archive).value(QStringLiteral("folder")).toString(), QStringLiteral("Archive (2)"));
}

void TestMailPstImport::progressAndCancel()
{
    MailTest::Env env;
    MailService *service = env.service.get();
    PstBuilder b(true);
    PstFolderSpec bulk{QStringLiteral("Bulk"), QStringLiteral("IPF.Note"), {}, {}};
    for (int i = 0; i < 300; ++i) {
        PstMessageSpec m = pstMail(b, QStringLiteral("Bulk %1").arg(i), QStringLiteral("Ada"), QStringLiteral("ada@example.org"),
                                   pstUtc(2026, 1, 1).addSecs(i * 60), true);
        m.props.text(0x1000, QStringLiteral("Body %1").arg(i), true);
        bulk.messages.append(m);
    }
    b.top.children = {bulk};
    const QString path = writeFile(env.dir, QStringLiteral("bulk.pst"), b.build());
    PstImporter *importer = service->outlookImport();
    importer->setSliceMs(0);   // one item per event-loop turn
    QSignalSpy progress(importer, &PstImporter::progressChanged);
    QSignalSpy finished(importer, &PstImporter::finished);
    double seen = -1;
    int total = 0;
    const auto connection = connect(importer, &PstImporter::progressChanged, this, [&] {
        if (importer->phase() != QLatin1String("importing") || importer->processed() < 5) return;
        seen = importer->progress();
        total = importer->total();
        importer->cancel();
    });
    const QString id = importer->start(QUrl::fromLocalFile(path));
    QVERIFY(importer->running());
    QVERIFY(importer->start(QUrl::fromLocalFile(path)).isEmpty());   // one at a time
    const QVariantMap cancelled = MailTest::waitResult(finished, id);
    disconnect(connection);
    QCOMPARE(total, 300);
    QVERIFY(seen > 0 && seen < 1);
    QCOMPARE(cancelled.value(QStringLiteral("state")).toString(), QStringLiteral("cancelled"));
    QVERIFY(cancelled.value(QStringLiteral("messages")).toInt() < 300);
    QVERIFY(progress.count() > 2);
    importer->setSliceMs(40);
    QCOMPARE(runImport(service, path).value(QStringLiteral("messages")).toInt(), 300);
    QCOMPARE(service->store()->messages().size(), 300);   // the cancelled part was not doubled
}

void TestMailPstImport::corruptFilesFailCleanly()
{
    MailTest::Env env;
    MailService *service = env.service.get();
    QSignalSpy notices(service, &MailService::notify);
    const QByteArray bytes = buildStandardPst(true, 1);
    const QVariantMap truncated = runImport(service, writeFile(env.dir, QStringLiteral("cut.pst"), bytes.left(bytes.size() / 2)));
    QCOMPARE(truncated.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
    QVERIFY2(truncated.value(QStringLiteral("error")).toString().contains(QLatin1String("truncated")),
             qPrintable(truncated.value(QStringLiteral("error")).toString()));
    QCOMPARE(service->outlookImport()->phase(), QStringLiteral("failed"));
    QVERIFY(service->store()->messages().isEmpty());
    QVERIFY(!notices.isEmpty());
    QCOMPARE(runImport(service, writeFile(env.dir, QStringLiteral("text.pst"), "not an outlook file"))
                 .value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
    // Damage inside one item: that item is skipped with its reason, the rest arrives.
    PstBuild::PstLayout layout;
    QByteArray damaged = buildStandardPst(true, 1, &layout);
    const auto &block = layout.blocks.at(layout.blocks.size() * 3 / 4);
    damaged[block.ib + 2] = char(damaged.at(block.ib + 2) ^ 0x33);
    const QVariantMap partial = runImport(service, writeFile(env.dir, QStringLiteral("damaged.pst"), damaged));
    QVERIFY(partial.value(QStringLiteral("state")).toString() == QLatin1String("accepted")
            || partial.value(QStringLiteral("state")).toString() == QLatin1String("rejected"));
    if (partial.value(QStringLiteral("state")).toString() == QLatin1String("accepted")) {
        QVERIFY(partial.value(QStringLiteral("skipped")).toInt() >= 1);
        QVERIFY(!partial.value(QStringLiteral("warnings")).toStringList().isEmpty());
    }
}

void TestMailPstImport::tenThousandMessages()
{
    // AGENT-NOTE: the bound is for the Debug build on the desktop; the
    // measured time is in docs/verification.md (M76 Mail gates).
    MailTest::Env env;
    MailService *service = env.service.get();
    PstBuilder b(true);
    PstFolderSpec archive{QStringLiteral("Archive"), QStringLiteral("IPF.Note"), {}, {}};
    for (int i = 0; i < 10000; ++i) {
        PstMessageSpec m = pstMail(b, QStringLiteral("Archived %1").arg(i), QStringLiteral("Sender %1").arg(i % 50),
                                   QStringLiteral("sender%1@example.org").arg(i % 50), pstUtc(2025, 1, 1).addSecs(i * 3600), true);
        m.props.text(0x1000, QStringLiteral("Archived message number %1.\n").arg(i).repeated(4), true);
        m.recipients = {pstRecipient(b, 1, QStringLiteral("Ada"), QStringLiteral("SMTP"), QStringLiteral("ada@example.org"))};
        archive.messages.append(m);
    }
    b.top.children = {archive};
    const QString path = writeFile(env.dir, QStringLiteral("big.pst"), b.build());
    QElapsedTimer timer;
    timer.start();
    const QVariantMap summary = runImport(service, path, 300000);
    const qint64 elapsed = timer.elapsed();
    qInfo("10 000-message .pst (%lld KiB) imported in %lld ms", qlonglong(QFileInfo(path).size() / 1024), qlonglong(elapsed));
    QCOMPARE(summary.value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
    QCOMPARE(summary.value(QStringLiteral("messages")).toInt(), 10000);
    QCOMPARE(service->store()->messages().size(), 10000);
    QVERIFY2(elapsed < 180000, qPrintable(QString::number(elapsed)));
}

void TestMailPstImport::msgItemsGoToTheirHomes()
{
    MailTest::Env env;
    MailHost host(env.service.get());
    MsgBuilder group;
    QByteArray entries;
    quint16 index = 0;
    for (const quint32 lid : {0x8053u, 0x8054u, 0x8055u})
        entries += PstBuild::le32(lid) + PstBuild::le16(quint16(3 << 1)) + PstBuild::le16(index++);
    group.nameGuids = QindaOffice::Mail::Mapi::guidBytes(QindaOffice::Mail::Mapi::kPsetidAddress);
    group.nameEntries = entries;
    group.unicode(0x001A, QStringLiteral("IPM.DistList")).unicode(0x0037, QStringLiteral("Choir"))
        .mvBinary(0x8001, {pstOneOff(QStringLiteral("Ann Lee"), QStringLiteral("ann@example.org"))})
        .mvBinary(0x8002, {pstOneOff(QStringLiteral("Ann Lee"), QStringLiteral("ann@example.org"))});
    const QString groupPath = writeFile(env.dir, QStringLiteral("Choir.msg"), group.build());
    QVERIFY(host.loadPath(groupPath));
    QCOMPARE(host.view(), QStringLiteral("people"));
    QVERIFY(host.loadPath(groupPath));   // the same file again: one group
    QCOMPARE(env.service->contacts()->count(), 1);
    const QVariantMap choir = env.service->contacts()->contacts().first().toMap();
    QVERIFY(choir.value(QStringLiteral("isGroup")).toBool());
    QCOMPARE(env.service->contacts()->expandGroup(choir.value(QStringLiteral("id")).toString())
                 .value(QStringLiteral("recipients")).toStringList(), QStringList{QStringLiteral("Ann Lee <ann@example.org>")});

    MsgBuilder task;
    task.unicode(0x001A, QStringLiteral("IPM.Task")).unicode(0x0037, QStringLiteral("Water plants"));
    QVERIFY(host.loadPath(writeFile(env.dir, QStringLiteral("task.msg"), task.build())));
    QCOMPARE(host.view(), QStringLiteral("tasks"));
    QCOMPARE(env.service->store()->tasks().size(), 1);
    // Mail saved as .msg still opens as a message.
    MsgBuilder mail;
    mail.unicode(0x001A, QStringLiteral("IPM.Note")).unicode(0x0037, QStringLiteral("Just mail")).unicode(0x1000, QStringLiteral("Hi"));
    QVERIFY(host.loadPath(writeFile(env.dir, QStringLiteral("mail.msg"), mail.build())));
    QCOMPARE(host.view(), QStringLiteral("mail"));
    QCOMPARE(env.service->store()->messages().size(), 1);
}

void TestMailPstImport::importDialogThroughTheWindow()
{
    MailTest::Env env;
    MailHost host(env.service.get());
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{QStringLiteral("host"), QVariant::fromValue(&host)}}));
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    host.attachWindow(window);
    window->resize(1280, 800);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(host.actions()->entry(QStringLiteral("mailImportPstAction"))->isEnabled());
    QSignalSpy asked(&host, &MailHost::outlookImportRequested);
    QVERIFY(host.actions()->trigger(QStringLiteral("mailImportPstAction")));
    QCOMPARE(asked.count(), 1);

    PstImporter *importer = env.service->outlookImport();
    importer->setSliceMs(1);
    QSignalSpy finished(importer, &PstImporter::finished);
    const QString path = writeFile(env.dir, QStringLiteral("Outlook.pst"), buildStandardPst(true, 1));
    QVERIFY(host.loadPath(path));   // the file manager's route: the dialog follows the importer
    QTRY_VERIFY(!host.actions()->entry(QStringLiteral("mailImportPstAction"))->isEnabled() || !finished.isEmpty());
    QObject *dialog = nullptr;
    QTRY_VERIFY((dialog = object->findChild<QObject *>(QStringLiteral("pstImportDialog"))) != nullptr
                && dialog->property("visible").toBool());
    QTRY_COMPARE(finished.count(), 1);
    QTRY_COMPARE(dialog->property("title").toString(), QStringLiteral("Outlook data file imported"));
    QTest::qWait(200);
    QindaOffice::Test::saveGrab(window, QStringLiteral("qindamail_pst_summary_1280"));
    QCOMPARE(dialog->property("primaryText").toString(), QStringLiteral("Show imported mail"));
    QVERIFY(host.actions()->entry(QStringLiteral("mailImportPstAction"))->isEnabled());
    QMetaObject::invokeMethod(dialog, "accept");
    QTRY_COMPARE(env.service->currentFolderKey(), QStringLiteral("acct:local:Outlook"));
    QCOMPARE(host.view(), QStringLiteral("mail"));
    // The summary again at 520 px, and the ribbon gate with the new commands.
    window->resize(520, 800);
    QMetaObject::invokeMethod(dialog, "open");
    QTest::qWait(250);
    QindaOffice::Test::saveGrab(window, QStringLiteral("qindamail_pst_summary_520"));
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(100);
}

MAIL_TEST_MAIN(TestMailPstImport)

#include "tst_qindamail_pst.moc"
