// SPDX-License-Identifier: GPL-3.0-or-later
// QindaMail's message files and reading (M64, D-072), through the service:
// an .eml opens into the local Inbox with its source kept (formatted body,
// inline pictures, attachments readable without a download), an mbox
// imports into a folder of its own, a message saves as .eml (its source
// byte for byte) and its attachments all at once, remote pictures load only
// after "Show pictures" (through an injected fetcher — no network), links
// open only for http(s), and the selected message prints. Headless.

#include "fake_imap_server.h"
#include "mail_test_support.h"
#include "office_mail/cfb_writer_msg.h"

#include "qindamail/src/mail_host.h"
#include "qindamail/src/message_print.h"

#include "message_content.h"
#include "message_render.h"
#include "rfc822_reader.h"

#include <qindatk/local_productivity_store.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QPdfWriter>
#include <QPrinter>
#include <QTextDocument>

using namespace QindaOffice::Apps::Mail;

namespace {

QByteArray crlf(QByteArray text)
{
    text.replace("\r\n", "\n");
    text.replace("\n", "\r\n");
    return text;
}

QByteArray pngBytes(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(0xff2266aa);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QByteArray htmlMessage(const QByteArray &subject = "Spring menu")
{
    return crlf("From: Maya Okafor <maya@example.org>\n"
                "To: ada@example.org\n"
                "Subject: " + subject + "\n"
                "Date: Tue, 6 Oct 2026 09:30:00 +0000\n"
                "Message-ID: <" + subject.toHex() + "@example.org>\n"
                "MIME-Version: 1.0\n"
                "Content-Type: multipart/mixed; boundary=\"m\"\n\n"
                "--m\n"
                "Content-Type: multipart/related; boundary=\"r\"\n\n"
                "--r\n"
                "Content-Type: text/html; charset=utf-8\n\n"
                "<h1>Menu</h1><p><b>Soup</b> of the day <img src=\"cid:soup@x\"> "
                "<img src=\"https://pixel.example/open.gif\" width=40 height=20 alt=\"logo\"> "
                "<a href=\"https://example.org/menu\">full menu</a></p>\n"
                "--r\n"
                "Content-Type: image/png\nContent-ID: <soup@x>\nContent-Transfer-Encoding: base64\n\n"
                + pngBytes(6, 4).toBase64() + "\n"
                "--r--\n"
                "--m\n"
                "Content-Type: text/plain; name=\"notes.txt\"\nContent-Disposition: attachment; filename=\"notes.txt\"\n\n"
                "bring bowls\n"
                "--m\n"
                "Content-Type: text/plain; name=\"notes.txt\"\nContent-Disposition: attachment; filename=\"notes.txt\"\n\n"
                "second copy\n"
                "--m--\n");
}

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(bytes);
    return path;
}

} // namespace

class TestMailFiles : public QObject {
    Q_OBJECT

private slots:
    void emlKeepsItsSourceAndRendersFormatted()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QByteArray raw = htmlMessage();
        const QString id = service->importMessageFile(writeFile(env.dir, QStringLiteral("menu.eml"), raw));
        QVERIFY(id.startsWith(QStringLiteral("local:eml:")));
        QCOMPARE(service->selectedMessageId(), id);
        QCOMPARE(service->storedSource(id), raw);
        const QindaOffice::Mail::MessageContent content = service->contentOf(id);
        QVERIFY(content.html.contains(QStringLiteral("<b>Soup</b>")));
        // Visible attachments: the two text files; the cid picture is in the body.
        const QVariantList attachments = service->messageById(id).value(QStringLiteral("attachments")).toList();
        QCOMPARE(attachments.size(), 3);
        const QindaOffice::Mail::RenderedBody body = QindaOffice::Mail::renderBody(content, service->renderPolicy(id));
        QVERIFY(body.remoteBlocked);
        QCOMPARE(body.cidAttachments.size(), 1);
        QCOMPARE(content.attachments.at(body.cidAttachments.first()).section, QStringLiteral("1.2"));
        // The same bytes opened again are the same message.
        QCOMPARE(service->importMessageFile(writeFile(env.dir, QStringLiteral("again.eml"), raw)), id);
        // A restart finds the source again.
        auto reopened = env.reopen();
        QCOMPARE(reopened->storedSource(id), raw);
        reopened.reset();
        // A record that went behind the app's back (an expunge applied by a
        // sync) loses its kept source at the next start.
        const QString file = env.options().dataDir + QStringLiteral("/sources/")
            + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) + QStringLiteral(".eml");
        QVERIFY(QFile::exists(file));
        QVERIFY(service->store()->remove(QStringLiteral("messages"), id).value(QStringLiteral("ok")).toBool());
        QVERIFY(service->store()->save());
        auto third = env.reopen();
        QTRY_VERIFY(!QFile::exists(file));
    }

    void outlookMsgOpensAsAnOrdinaryMessage()
    {
        using namespace QindaOffice::Mail::Test;
        MailTest::Env env;
        MailService *service = env.service.get();
        const QByteArray png = pngBytes(5, 5);
        MsgBuilder inner;
        inner.unicode(0x0037, QStringLiteral("Original quote")).unicode(0x0C1A, QStringLiteral("Vendor"))
            .unicode(0x5D01, QStringLiteral("sales@vendor.example")).unicode(0x1000, QStringLiteral("Price list attached."));
        MsgBuilder msg;
        msg.unicode(0x0037, QStringLiteral("Quarterly numbers")).unicode(0x0042, QStringLiteral("Grace Hopper"))
            .unicode(0x5D02, QStringLiteral("grace@example.org")).unicode(0x1035, QStringLiteral("<q3-numbers@example.org>"))
            .time(0x0039, QDateTime(QDate(2026, 10, 1), QTime(9, 0), QTimeZone::UTC))
            .binary(0x1013, "<p>Numbers are <b>up</b>.<img src=\"cid:chart@x\"></p>");
        MsgProps to;
        to.longValue(0x0C15, 1).unicode(0x3001, QStringLiteral("Ada Lovelace")).unicode(0x39FE, QStringLiteral("ada@example.org"));
        MsgProps cc;
        cc.longValue(0x0C15, 2).unicode(0x3001, QStringLiteral("Alan Turing")).unicode(0x39FE, QStringLiteral("alan@example.org"));
        msg.recipients = {to, cc};
        MsgAttachmentBuilder chart;
        chart.longValue(0x3705, 1).unicode(0x3707, QStringLiteral("chart.png")).unicode(0x370E, QStringLiteral("image/png"))
            .unicode(0x3712, QStringLiteral("chart@x")).binary(0x3701, png);
        MsgAttachmentBuilder pdf;
        pdf.longValue(0x3705, 1).unicode(0x3707, QStringLiteral("report.pdf")).binary(0x3701, "%PDF-1.4 fake");
        MsgAttachmentBuilder embedded;
        embedded.longValue(0x3705, 5).unicode(0x3001, QStringLiteral("Original quote"));
        embedded.embedded = std::make_shared<MsgBuilder>(inner);
        msg.attachments = {chart, pdf, embedded};
        const QString path = writeFile(env.dir, QStringLiteral("numbers.msg"), msg.build());

        const QString id = service->importMessageFile(path);
        QVERIFY(id.startsWith(QStringLiteral("local:eml:")));
        const QVariantMap record = service->messageById(id);
        QCOMPARE(record.value(QStringLiteral("subject")).toString(), QStringLiteral("Quarterly numbers"));
        QCOMPARE(record.value(QStringLiteral("senderAddress")).toString(), QStringLiteral("grace@example.org"));
        const QindaOffice::Mail::MessageContent content = service->contentOf(id);
        QVERIFY(content.html.contains(QStringLiteral("<b>up</b>")));
        QCOMPARE(content.messageId, QStringLiteral("q3-numbers@example.org"));
        QVERIFY(content.to.contains(QStringLiteral("ada@example.org")));
        QVERIFY(content.cc.contains(QStringLiteral("alan@example.org")));
        QCOMPARE(content.attachments.size(), 3);
        const QindaOffice::Mail::RenderedBody body = QindaOffice::Mail::renderBody(content, service->renderPolicy(id));
        QCOMPARE(body.cidAttachments.size(), 1);                 // the chart sits in the body
        bool attachedMessage = false;
        for (const QindaOffice::Mail::MessageAttachment &part : content.attachments) {
            if (part.mimeType != QStringLiteral("message/rfc822")) continue;
            attachedMessage = QindaOffice::Mail::readRfc822(part.data).subject == QStringLiteral("Original quote");
        }
        QVERIFY(attachedMessage);
        // The same Outlook file opened again is the same message.
        QCOMPARE(service->importMessageFile(writeFile(env.dir, QStringLiteral("again.msg"), msg.build())), id);
        // Saved as .eml, it reads back as the same message.
        QSignalSpy done(service, &MailService::operationFinished);
        const QString eml = env.dir.filePath(QStringLiteral("numbers.eml"));
        QCOMPARE(MailTest::waitState(done, service->saveMessageAs(id, QUrl::fromLocalFile(eml))).first, QStringLiteral("accepted"));
        QFile saved(eml);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QindaOffice::Mail::MessageContent reread = QindaOffice::Mail::readRfc822(saved.readAll());
        QCOMPARE(reread.subject, QStringLiteral("Quarterly numbers"));
        QCOMPARE(reread.attachments.size(), 3);
        // A broken .msg is refused with a notice, nothing stored.
        QSignalSpy notices(service, &MailService::notify);
        QByteArray broken = msg.build();
        broken.truncate(700);
        QVERIFY(service->importMessageFile(writeFile(env.dir, QStringLiteral("broken.msg"), broken)).isEmpty());
        QCOMPARE(notices.count(), 1);
        // A ".msg" of another program (not a compound file) is refused too.
        QVERIFY(service->importMessageFile(writeFile(env.dir, QStringLiteral("strings.msg"), "msgid \"x\"\nmsgstr \"y\"\n")).isEmpty());
        QCOMPARE(notices.count(), 2);
    }

    void attachmentsComeFromTheSource()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QString id = service->importMessageFile(writeFile(env.dir, QStringLiteral("menu.eml"), htmlMessage()));
        QSignalSpy done(service, &MailService::operationFinished);
        QTemporaryDir out;
        const QString saveId = service->saveAttachmentAs(id, id + QStringLiteral("/2"), QUrl::fromLocalFile(out.filePath(QStringLiteral("one.txt"))));
        QCOMPARE(MailTest::waitState(done, saveId).first, QStringLiteral("accepted"));
        QFile one(out.filePath(QStringLiteral("one.txt")));
        QVERIFY(one.open(QIODevice::ReadOnly));
        QCOMPARE(one.readAll().trimmed(), QByteArray("bring bowls"));
        // Save all: unique names, nothing overwritten.
        const QString all = service->saveAllAttachments(id, QUrl::fromLocalFile(out.path()));
        QCOMPARE(MailTest::waitState(done, all).first, QStringLiteral("accepted"));
        QStringList names = QDir(out.path()).entryList(QDir::Files);
        names.sort();
        // The picture inside the body is not a file to save.
        QCOMPARE(names, (QStringList{QStringLiteral("notes (2).txt"), QStringLiteral("notes.txt"), QStringLiteral("one.txt")}));
    }

    void saveAsWritesTheOriginalSource()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QByteArray raw = htmlMessage();
        const QString id = service->importMessageFile(writeFile(env.dir, QStringLiteral("menu.eml"), raw));
        QSignalSpy done(service, &MailService::operationFinished);
        const QString target = env.dir.filePath(QStringLiteral("saved.eml"));
        QCOMPARE(MailTest::waitState(done, service->saveMessageAs(id, QUrl::fromLocalFile(target))).first,
                 QStringLiteral("accepted"));
        QFile saved(target);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), raw);
        // A record without source (as POP3 keeps them) still saves: from the record.
        env.service->store()->upsert(QStringLiteral("messages"),
                                     MailTest::message(QStringLiteral("pop3:x:1"), QStringLiteral("Plain one"), QDateTime::currentDateTimeUtc()));
        const QString plainTarget = env.dir.filePath(QStringLiteral("plain.eml"));
        QCOMPARE(MailTest::waitState(done, service->saveMessageAs(QStringLiteral("pop3:x:1"), QUrl::fromLocalFile(plainTarget))).first,
                 QStringLiteral("accepted"));
        QFile plain(plainTarget);
        QVERIFY(plain.open(QIODevice::ReadOnly));
        const QindaOffice::Mail::MessageContent read = QindaOffice::Mail::readRfc822(plain.readAll());
        QCOMPARE(read.subject, QStringLiteral("Plain one"));
        QCOMPARE(read.plain.trimmed(), QStringLiteral("Body of Plain one"));
    }

    void mboxImportsIntoItsOwnFolder()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QByteArray mbox;
        for (int i = 0; i < 30; ++i) {
            mbox += "From sender@example.org Sat Jan  3 01:05:" + QByteArray::number(10 + i) + " 2026\n";
            mbox += "From: sender@example.org\nSubject: archived " + QByteArray::number(i) + "\n\n>From the archive\n\n";
        }
        QSignalSpy done(service, &MailService::operationFinished);
        const QString request = service->importMailbox(QUrl::fromLocalFile(writeFile(env.dir, QStringLiteral("Old Mail.mbox"), mbox)));
        QCOMPARE(MailTest::waitState(done, request).first, QStringLiteral("accepted"));
        QCOMPARE(service->currentFolderKey(), QStringLiteral("acct:local:Old Mail"));
        QCOMPARE(service->messageModel()->rowCount(), 30);
        bool listed = false;
        for (const QVariant &value : service->folderTargets(QStringLiteral("local")))
            listed = listed || value.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("acct:local:Old Mail");
        QVERIFY(listed);
        const QString any = service->messageModel()->get(0).value(QStringLiteral("messageId")).toString();
        QVERIFY(service->contentOf(any).plain.startsWith(QStringLiteral("From the archive")));
        // A second import of another file with the same name gets its own folder.
        const QString again = service->importMailbox(QUrl::fromLocalFile(writeFile(env.dir, QStringLiteral("Old Mail.mbox"),
            "From x@example.org Sat Jan  3 01:05:00 2026\nSubject: new one\n\nbody\n")));
        QCOMPARE(MailTest::waitState(done, again).first, QStringLiteral("accepted"));
        QCOMPARE(service->currentFolderKey(), QStringLiteral("acct:local:Old Mail (2)"));
        // Not an mbox: rejected, nothing created.
        const QString bad = service->importMailbox(QUrl::fromLocalFile(writeFile(env.dir, QStringLiteral("x.mbox"), "Subject: no\n\nx")));
        QCOMPARE(MailTest::waitState(done, bad).first, QStringLiteral("rejected"));
    }

    void picturesLoadOnlyWhenAsked()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QStringList fetched;
        service->setRemoteFetcher([&fetched](const QUrl &url, MailService::FetchDone done) {
            fetched.append(url.toString());
            QTimer::singleShot(0, [done] { done(pngBytes(40, 20), QString()); });
        });
        const QString id = service->importMessageFile(writeFile(env.dir, QStringLiteral("menu.eml"), htmlMessage()));
        QVERIFY(!service->renderPolicy(id).allowRemote);
        QTest::qWait(50);
        QVERIFY(fetched.isEmpty());
        QSignalSpy pictures(service, &MailService::picturesChanged);
        service->showPictures(id);
        QTRY_COMPARE(fetched, QStringList{QStringLiteral("https://pixel.example/open.gif")});
        QTRY_VERIFY(service->renderPolicy(id).remoteImages.contains(QStringLiteral("https://pixel.example/open.gif")));
        const QindaOffice::Mail::RenderedBody body = QindaOffice::Mail::renderBody(service->contentOf(id), service->renderPolicy(id));
        QVERIFY(!body.remoteBlocked);
        // Always for this sender: kept across a restart.
        service->setSenderPicturesAllowed(QStringLiteral("Maya@Example.org"), true);
        auto reopened = env.reopen();
        QVERIFY(reopened->senderPicturesAllowed(QStringLiteral("maya@example.org")));
        reopened->setSenderPicturesAllowed(QStringLiteral("maya@example.org"), false);
        QVERIFY(!reopened->senderPicturesAllowed(QStringLiteral("maya@example.org")));
    }

    void linksOpenOnlyOnTheWeb()
    {
        MailTest::Env env;
        QList<QUrl> opened;
        env.service->setUrlOpener([&opened](const QUrl &url) { opened.append(url); return true; });
        QVERIFY(env.service->openLink(QStringLiteral("https://example.org/menu")));
        for (const char *bad : {"javascript:alert(1)", "file:///etc/passwd", "data:text/html,x", "ftp://example.org/x", "https://"})
            QVERIFY2(!env.service->openLink(QString::fromLatin1(bad)), bad);
        QCOMPARE(opened, QList<QUrl>{QUrl(QStringLiteral("https://example.org/menu"))});
    }

    void imapSourceIsFetchedOnceAndKept()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeImapServer imap("imap-pw");
        imap.inbox.append({7, htmlMessage("IMAP menu"), false});
        const auto server = [&imap](bool incoming) {
            QVariantMap map{{QStringLiteral("host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), imap.port()},
                            {QStringLiteral("security"), QStringLiteral("none")}, {QStringLiteral("userName"), QStringLiteral("ada")}};
            if (incoming) map.insert(QStringLiteral("protocol"), QStringLiteral("imap"));
            return map;
        };
        const QVariantMap added = service->addAccount({{QStringLiteral("kind"), QStringLiteral("imap")},
                                                       {QStringLiteral("displayName"), QStringLiteral("Work")},
                                                       {QStringLiteral("emailAddress"), QStringLiteral("ada@work.example")},
                                                       {QStringLiteral("incoming"), server(true)},
                                                       {QStringLiteral("outgoing"), server(false)}},
                                                      QStringLiteral("imap-pw"));
        QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(added.value(QStringLiteral("error")).toString()));
        const QString account = added.value(QStringLiteral("id")).toString();
        QSignalSpy finished(service, &MailService::operationFinished);
        const auto sync = MailTest::waitState(finished, service->syncFolder(QStringLiteral("acct:") + account + QStringLiteral(":INBOX")));
        QVERIFY2(sync.first == QLatin1String("accepted"), qPrintable(sync.second));
        const QString id = QStringLiteral("imap:%1:INBOX:4242:7").arg(account);
        QVERIFY(!service->messageById(id).isEmpty());
        QVERIFY(service->contentOf(id).html.isEmpty());          // the toolkit kept text only
        imap.commands.clear();
        QByteArray source;
        bool answered = false;
        service->loadSource(id, [&](const QByteArray &bytes, const QString &) { source = bytes; answered = true; });
        QTRY_VERIFY(answered);
        QCOMPARE(source, htmlMessage("IMAP menu"));
        QVERIFY(service->contentOf(id).html.contains(QStringLiteral("<b>Soup</b>")));
        // The size first, then the whole message once (BODY.PEEK[]: not \Seen).
        QVERIFY(imap.commands.contains("UID FETCH 7 (UID RFC822.SIZE)"));
        QCOMPARE(imap.commands.count("UID FETCH 7 (UID BODY.PEEK[])"), 1);
        // Kept: no second download, the attachments are read from it.
        imap.commands.clear();
        answered = false;
        service->loadSource(id, [&](const QByteArray &, const QString &) { answered = true; });
        QTRY_VERIFY(answered);
        QVERIFY(imap.commands.isEmpty());
        QCOMPARE(service->messageById(id).value(QStringLiteral("attachments")).toList().size(), 3);
        QTemporaryDir out;
        QSignalSpy done(service, &MailService::operationFinished);
        const QString save = service->saveAttachmentAs(id, id + QStringLiteral("/2"), QUrl::fromLocalFile(out.filePath(QStringLiteral("n.txt"))));
        QCOMPARE(MailTest::waitState(done, save).first, QStringLiteral("accepted"));
        QVERIFY(imap.commands.isEmpty());                      // from the kept source, not the server
    }

    void selectedMessagePrints()
    {
        MailTest::Env env;
        MailHost host(env.service.get());
        QVERIFY(!host.actions()->entry(QStringLiteral("filePrintAction"))->isEnabled());
        const QString id = env.service->importMessageFile(writeFile(env.dir, QStringLiteral("menu.eml"), htmlMessage()));
        QTRY_VERIFY(host.actions()->entry(QStringLiteral("filePrintAction"))->isEnabled());
        const std::unique_ptr<QTextDocument> document = MessagePrinter::document(env.service.get(), id);
        QVERIFY(document != nullptr);
        const QString text = document->toPlainText();
        QVERIFY(text.contains(QStringLiteral("Spring menu")));
        QVERIFY(text.contains(QStringLiteral("Maya Okafor <maya@example.org>")));
        QVERIFY(text.contains(QStringLiteral("notes.txt")));
        QVERIFY(text.contains(QStringLiteral("Soup of the day")));
        const QString pdf = env.dir.filePath(QStringLiteral("menu.pdf"));
        QVERIFY(host.exportMessagePdf(QUrl::fromLocalFile(pdf)));
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.read(5) == "%PDF-");
        QVERIFY(file.size() > 1000);
    }
};

MAIL_TEST_MAIN(TestMailFiles)

#include "tst_qindamail_files.moc"
