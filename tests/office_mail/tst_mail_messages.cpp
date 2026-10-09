// SPDX-License-Identifier: GPL-3.0-or-later
// M64 (D-072): QindaMail's message files — the RFC 5322 reader (structure,
// Content-ID, section numbers equal to QindaTK::Mime::parse's), the writer
// (deterministic, inline parts, attached messages, read back by both
// readers), mbox (separators, >From unescaping, bounds) and the renderer
// (cid pictures, data: pictures, remote pictures blocked / allowed,
// attached pictures below the body, plain text). Fixtures are written here.

#include "mbox.h"
#include "message_render.h"
#include "rfc822_reader.h"
#include "rfc822_writer.h"

#include <qindatk/mail_mime.h>

#include <QBuffer>
#include <QImage>
#include <QTest>

using namespace QindaOffice::Mail;

namespace {

QByteArray crlf(QByteArray text)
{
    text.replace("\r\n", "\n");
    text.replace("\n", "\r\n");
    return text;
}

QByteArray png(int width, int height, QRgb color)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// An Outlook-style message: mixed(related(alternative(plain, html), image cid), pdf).
QByteArray newsletter()
{
    const QByteArray image = png(4, 3, 0xff3366cc).toBase64();
    return crlf("From: =?UTF-8?B?w4lsb2RpZQ==?= <elodie@example.org>\n"
                "Sender: list@example.org\n"
                "To: \"Lovelace, Ada\" <ada@example.org>, bob@example.org\n"
                "Cc: carol@example.org\n"
                "Bcc: dave@example.org\n"
                "Reply-To: replies@example.org\n"
                "Subject: =?UTF-8?Q?Caf=C3=A9_news?=\n"
                "Date: Tue, 6 Oct 2026 09:30:00 +0200 (CEST)\n"
                "Message-ID: <news-1@example.org>\n"
                "In-Reply-To: <older@example.org>\n"
                "References: <root@example.org> <older@example.org>\n"
                "X-Mailer: Microsoft Outlook 16.0\n"
                "MIME-Version: 1.0\n"
                "Content-Type: multipart/mixed; boundary=\"outer\"\n"
                "\n"
                "This is a multi-part message in MIME format.\n"
                "--outer\n"
                "Content-Type: multipart/related; boundary=\"rel\"; type=\"multipart/alternative\"\n"
                "\n"
                "--rel\n"
                "Content-Type: multipart/alternative; boundary=\"alt\"\n"
                "\n"
                "--alt\n"
                "Content-Type: text/plain; charset=utf-8\n"
                "Content-Transfer-Encoding: quoted-printable\n"
                "\n"
                "Caf=C3=A9 opens on Monday.\n"
                "--alt\n"
                "Content-Type: text/html; charset=utf-8\n"
                "\n"
                "<p>Caf\xc3\xa9 <b>opens</b> on Monday.<img src=\"cid:logo@example.org\"></p>\n"
                "--alt--\n"
                "--rel\n"
                "Content-Type: image/png\n"
                "Content-ID: <logo@example.org>\n"
                "Content-Transfer-Encoding: base64\n"
                "\n" + image + "\n"
                "--rel--\n"
                "--outer\n"
                "Content-Type: application/pdf; name=\"menu.pdf\"\n"
                "Content-Disposition: attachment; filename*=utf-8''men%C3%BC.pdf\n"
                "Content-Transfer-Encoding: base64\n"
                "\n"
                "JVBERi0xLjQK\n"
                "--outer--\n");
}

} // namespace

class TestMailMessages : public QObject {
    Q_OBJECT

private slots:
    void readerHeadersAndBodies()
    {
        const MessageContent message = readRfc822(newsletter());
        QCOMPARE(message.subject, QStringLiteral("Café news"));
        QCOMPARE(message.fromName, QStringLiteral("Élodie"));
        QCOMPARE(message.fromAddress, QStringLiteral("elodie@example.org"));
        QCOMPARE(message.senderAddress, QStringLiteral("list@example.org"));
        QCOMPARE(message.to, QStringLiteral("\"Lovelace, Ada\" <ada@example.org>, bob@example.org"));
        QCOMPARE(message.cc, QStringLiteral("carol@example.org"));
        QCOMPARE(message.bcc, QStringLiteral("dave@example.org"));
        QCOMPARE(message.replyTo, QStringLiteral("replies@example.org"));
        QCOMPARE(message.messageId, QStringLiteral("news-1@example.org"));
        QCOMPARE(message.inReplyTo, QStringLiteral("older@example.org"));
        QCOMPARE(message.references, QStringLiteral("<root@example.org> <older@example.org>"));
        QCOMPARE(message.date.toUTC(), QDateTime(QDate(2026, 10, 6), QTime(7, 30), QTimeZone::UTC));
        QCOMPARE(message.plain, QStringLiteral("Café opens on Monday."));   // the CRLF before a boundary belongs to it
        QVERIFY(message.html.contains(QStringLiteral("<b>opens</b>")));
        QVERIFY(message.html.contains(QStringLiteral("cid:logo@example.org")));
        bool mailer = false;
        for (const auto &header : message.extraHeaders) mailer = mailer || header.first == "x-mailer";
        QVERIFY(mailer);
    }

    void readerPartsMatchTheToolkitSections()
    {
        const QByteArray raw = newsletter();
        const MessageContent message = readRfc822(raw);
        QCOMPARE(message.attachments.size(), 2);
        const MessageAttachment &logo = message.attachments.at(0);
        QCOMPARE(logo.mimeType, QStringLiteral("image/png"));
        QCOMPARE(logo.contentId, QStringLiteral("logo@example.org"));
        QVERIFY(logo.inlinePart);
        QCOMPARE(QImage::fromData(logo.data).size(), QSize(4, 3));
        const MessageAttachment &pdf = message.attachments.at(1);
        QCOMPARE(pdf.name, QStringLiteral("menü.pdf"));
        QVERIFY(!pdf.inlinePart);
        QCOMPARE(pdf.data, QByteArray("%PDF-1.4\n"));
        // The toolkit's record ids are "<record>/<section>": same parts, same numbers.
        const QindaTK::Mime::Parsed toolkit = QindaTK::Mime::parse(raw);
        QCOMPARE(toolkit.attachments.size(), message.attachments.size());
        for (qsizetype i = 0; i < toolkit.attachments.size(); ++i)
            QCOMPARE(message.attachments.at(i).section, toolkit.attachments.at(i).section);
        QCOMPARE(logo.section, QStringLiteral("1.2"));
        QCOMPARE(pdf.section, QStringLiteral("2"));
    }

    void readerRelatedRootFromStartAndCalendar()
    {
        const QByteArray raw = crlf("Subject: invite\n"
                                    "Content-Type: multipart/related; boundary=b; start=\"<root@x>\"\n"
                                    "\n"
                                    "--b\n"
                                    "Content-Type: image/gif\nContent-ID: <pic@x>\nContent-Transfer-Encoding: base64\n\nR0lGODlhAQABAAAAACw=\n"
                                    "--b\n"
                                    "Content-Type: multipart/alternative; boundary=a\nContent-ID: <root@x>\n\n"
                                    "--a\nContent-Type: text/plain\n\nplain\n"
                                    "--a\nContent-Type: text/calendar; method=REQUEST; charset=utf-8\n\nBEGIN:VCALENDAR\nMETHOD:REQUEST\nEND:VCALENDAR\n"
                                    "--a--\n"
                                    "--b--\n");
        const MessageContent message = readRfc822(raw);
        QCOMPARE(message.plain, QStringLiteral("plain"));
        QCOMPARE(message.calendarMethod, QStringLiteral("REQUEST"));
        QVERIFY(message.calendar.contains(QStringLiteral("BEGIN:VCALENDAR")));
        QCOMPARE(message.attachments.size(), 2);
        QCOMPARE(message.attachments.at(0).contentId, QStringLiteral("pic@x"));
        QVERIFY(message.attachments.at(0).inlinePart);       // a resource of the root
        QCOMPARE(message.attachments.at(1).mimeType, QStringLiteral("text/calendar"));
        QCOMPARE(message.attachments.at(1).section, QStringLiteral("2.2"));
    }

    void readerAttachedMessageAndOddInput()
    {
        const QByteArray inner = crlf("From: x@example.org\nSubject: Inner one\n\nhello\n");
        const QByteArray raw = crlf("Subject: outer\nContent-Type: multipart/mixed; boundary=m\n\n--m\n"
                                    "Content-Type: text/plain\n\nsee attached\n--m\n"
                                    "Content-Type: message/rfc822\n\n") + inner + crlf("--m--\n");
        const MessageContent message = readRfc822(raw);
        QCOMPARE(message.attachments.size(), 1);
        QCOMPARE(message.attachments.at(0).name, QStringLiteral("Inner one.eml"));
        QCOMPARE(message.attachments.at(0).mimeType, QStringLiteral("message/rfc822"));
        QVERIFY(message.attachments.at(0).data.contains("hello"));
        // Garbage, headers only, an unterminated multipart: never a crash.
        for (const QByteArray &odd : {QByteArray(), QByteArray("no header body"), QByteArray("Subject: x\r\n"),
                                      crlf("Content-Type: multipart/mixed; boundary=z\n\n--z\nContent-Type: text/plain\n\nunterminated"),
                                      QByteArray(100000, '-')}) {
            const MessageContent parsed = readRfc822(odd);
            Q_UNUSED(parsed)
        }
        QCOMPARE(readRfc822(crlf("Content-Type: multipart/mixed; boundary=z\n\n--z\nContent-Type: text/plain\n\nunterminated")).plain,
                 QStringLiteral("unterminated"));
    }

    void readerDeepNestingStaysBounded()
    {
        QByteArray raw = "Content-Type: multipart/mixed; boundary=b0\r\n\r\n";
        for (int depth = 0; depth < 40; ++depth) {
            raw += "--b" + QByteArray::number(depth) + "\r\nContent-Type: multipart/mixed; boundary=b"
                + QByteArray::number(depth + 1) + "\r\n\r\n";
        }
        raw += "--b40\r\nContent-Type: text/plain\r\n\r\ndeep\r\n";
        const MessageContent message = readRfc822(raw);
        QVERIFY(message.plain.isEmpty() || message.plain == QStringLiteral("deep"));
        // The toolkit stops at the same depth: identical attachment sections.
        const QindaTK::Mime::Parsed toolkit = QindaTK::Mime::parse(raw);
        QCOMPARE(message.attachments.size(), toolkit.attachments.size());
    }

    void writerRoundTripsThroughBothReaders()
    {
        MessageContent message = readRfc822(newsletter());
        const QByteArray written = writeRfc822(message);
        QVERIFY(!written.contains("\n\n"));                 // CRLF only
        QCOMPARE(writeRfc822(message), written);             // deterministic
        const MessageContent again = readRfc822(written);
        QCOMPARE(again.subject, message.subject);
        QCOMPARE(again.fromAddress, message.fromAddress);
        QCOMPARE(again.fromName, message.fromName);
        QCOMPARE(again.bcc, message.bcc);
        QCOMPARE(again.messageId, message.messageId);
        QCOMPARE(again.inReplyTo, message.inReplyTo);
        QCOMPARE(again.date, message.date);
        QCOMPARE(again.html.trimmed(), message.html.trimmed());
        QCOMPARE(again.attachments.size(), 2);
        QCOMPARE(again.attachments.at(0).contentId, QStringLiteral("logo@example.org"));
        QVERIFY(again.attachments.at(0).inlinePart);
        QCOMPARE(again.attachments.at(0).data, message.attachments.at(0).data);
        QCOMPARE(again.attachments.at(1).name, QStringLiteral("menü.pdf"));
        const QindaTK::Mime::Parsed toolkit = QindaTK::Mime::parse(written);
        QCOMPARE(toolkit.subject, message.subject);
        QCOMPARE(toolkit.attachments.size(), 2);
    }

    void writerHandlesEmbeddedMessagesAndBareNames()
    {
        auto inner = std::make_shared<MessageContent>();
        inner->subject = QStringLiteral("Quarterly report");
        inner->fromAddress = QStringLiteral("boss@example.org");
        inner->plain = QStringLiteral("numbers");
        MessageContent outer;
        outer.subject = QStringLiteral("Fwd: report");
        outer.fromName = QStringLiteral("Ada");
        outer.fromAddress = QStringLiteral("ada@example.org");
        outer.to = QStringLiteral("Exchange Only Person, bob@example.org, \"Doe, John\"");
        outer.plain = QStringLiteral("See below");
        MessageAttachment attached;
        attached.name = QStringLiteral("Quarterly report.eml");
        attached.mimeType = QStringLiteral("message/rfc822");
        attached.embedded = inner;
        outer.attachments.append(attached);
        const QByteArray raw = writeRfc822(outer);
        QVERIFY(raw.contains("Message-ID: <"));             // derived when missing
        QVERIFY(raw.contains("@qindamail.invalid>"));
        QVERIFY(raw.contains("Exchange Only Person:;"));    // kept visible as an empty group
        QVERIFY(raw.contains("\"Doe, John\":;"));            // a phrase with specials is quoted
        const MessageContent read = readRfc822(raw);
        QCOMPARE(read.attachments.size(), 1);
        QCOMPARE(read.attachments.at(0).mimeType, QStringLiteral("message/rfc822"));
        const MessageContent nested = readRfc822(read.attachments.at(0).data);
        QCOMPARE(nested.subject, QStringLiteral("Quarterly report"));
        QCOMPARE(nested.plain.trimmed(), QStringLiteral("numbers"));
        QCOMPARE(derivedMessageId(outer), derivedMessageId(outer));
    }

    void mboxSplitsAndUnescapes()
    {
        const QByteArray data = crlf("From alice@example.org Sat Jan  3 01:05:34 1996\n"
                                     "Subject: one\n\n"
                                     "line\n"
                                     ">From the start\n"
                                     ">>From quoted twice\n"
                                     "\n"
                                     "From the tree, not a separator (no time)\n"
                                     "\n"
                                     "From - Sat Jan 03 01:05:34 1996\n"
                                     "Subject: two\n\n"
                                     "second\n");
        MboxReader reader;
        QVERIFY(reader.openData(data));
        QByteArray message;
        QVERIFY(reader.next(&message));
        const MessageContent first = readRfc822(message);
        QCOMPARE(first.subject, QStringLiteral("one"));
        QVERIFY(first.plain.contains(QStringLiteral("\nFrom the start\n")));
        QVERIFY(first.plain.contains(QStringLiteral("\n>From quoted twice\n")));
        QVERIFY(first.plain.contains(QStringLiteral("From the tree")));
        QVERIFY(reader.next(&message));
        QCOMPARE(readRfc822(message).subject, QStringLiteral("two"));
        QVERIFY(!reader.next(&message));
        QCOMPARE(reader.messagesRead(), 2);
    }

    void mboxBoundsAndRefusals()
    {
        MboxReader notMbox;
        QVERIFY(!notMbox.openData("Subject: plain eml\r\n\r\nbody"));
        QVERIFY(!notMbox.error().isEmpty());
        QByteArray big = "From a@b Sat Jan  3 01:05:34 1996\nSubject: big\n\n" + QByteArray(5000, 'x') + "\n";
        big += "\nFrom a@b Sat Jan  3 01:05:35 1996\nSubject: small\n\nok\n";
        MboxReader::Limits limits;
        limits.maxMessageBytes = 1000;
        MboxReader bounded(limits);
        QVERIFY(bounded.openData(big));
        QByteArray message;
        QVERIFY(bounded.next(&message));                     // the oversized one is skipped
        QCOMPARE(readRfc822(message).subject, QStringLiteral("small"));
        QCOMPARE(bounded.oversized(), 1);
        limits = {};
        limits.maxMessages = 1;
        MboxReader counted(limits);
        QVERIFY(counted.openData(big));
        QVERIFY(counted.next(&message));
        QVERIFY(!counted.next(&message));
        QVERIFY(counted.limitReached());
    }

    void renderResolvesCidAndBlocksRemote()
    {
        MessageContent message = readRfc822(newsletter());
        message.html += QStringLiteral("<img src=\"https://tracker.example/p.gif\" width=200 height=100>");
        RenderPolicy policy;
        RenderedBody body = renderBody(message, policy);
        QVERIFY(body.fromHtml);
        QVERIFY(body.remoteBlocked);
        QCOMPARE(body.remoteImages, QStringList{QStringLiteral("https://tracker.example/p.gif")});
        QVERIFY(!body.html.contains(QStringLiteral("tracker.example")));
        QCOMPARE(body.cidAttachments, QList<int>{0});
        bool cid = false;
        for (const auto &[url, bytes] : body.images) cid = cid || (url.startsWith(QStringLiteral("cid:")) && !bytes.isEmpty());
        QVERIFY(cid);
        policy.allowRemote = true;
        policy.remoteImages.insert(QStringLiteral("https://tracker.example/p.gif"), png(1, 1, 0xff000000));
        body = renderBody(message, policy);
        QVERIFY(!body.remoteBlocked);
        QVERIFY(body.html.contains(QStringLiteral("https://tracker.example/p.gif")));
        bool remote = false;
        for (const auto &[url, bytes] : body.images) remote = remote || url == QStringLiteral("https://tracker.example/p.gif");
        QVERIFY(remote);
    }

    void renderShowsAttachedPicturesAndPlainText()
    {
        MessageContent message;
        message.plain = QStringLiteral("See https://example.org/page and <script>x</script>");
        MessageAttachment photo;
        photo.name = QStringLiteral("photo.png");
        photo.mimeType = QStringLiteral("image/png");
        photo.data = png(8, 8, 0xff00ff00);
        message.attachments.append(photo);
        const RenderedBody body = renderBody(message, {});
        QVERIFY(!body.fromHtml);
        QVERIFY(body.html.contains(QStringLiteral("href=\"https://example.org/page\"")));
        QVERIFY(!body.html.contains(QStringLiteral("<script>")));
        QVERIFY(body.html.contains(attachedImageUrl(0)));
        QCOMPARE(body.inlineAttachments, QList<int>{0});
        QVERIFY(body.cidAttachments.isEmpty());              // still listed as an attachment
    }
};

QTEST_MAIN(TestMailMessages)

#include "tst_mail_messages.moc"
