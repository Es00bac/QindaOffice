// SPDX-License-Identifier: GPL-3.0-or-later
// tst_mail_msg, Outlook messages (parts): HTML, plain and RTF-only bodies
// (MELA and LZFu, broken RTF), attachments, embedded messages and their
// depth limit, and a truncation / byte-flip fuzz of a rich message.

#include "tst_mail_msg.h"

#include "tst_mail_msg_support.h"

#include <QRandomGenerator>
#include <QTest>

using namespace QindaOffice::Mail;
using namespace QindaOffice::Mail::Test;

void TestMailMsg::msgHtmlBody()
{
    const QString expected = QStringLiteral("<p>Grüße €</p>");
    const QByteArray utf8 = expected.toUtf8();
    const QByteArray cp1252 = QByteArray("<p>Gr\xfc\xdf" "e \x80</p>");
    MessageContent m;
    MsgBuilder utf8Message;
    utf8Message.binary(0x1013, utf8 + QByteArray(2, '\0'));
    QVERIFY(readOk(utf8Message.build(), &m));
    QCOMPARE(m.html, expected);
    MsgBuilder declared;
    declared.longValue(0x3FDE, 1252).binary(0x1013, cp1252);
    QVERIFY(readOk(declared.build(), &m));
    QCOMPARE(m.html, expected);
    MsgBuilder guessed;   // no code page and not UTF-8: windows-1252
    guessed.binary(0x1013, cp1252);
    QVERIFY(readOk(guessed.build(), &m));
    QCOMPARE(m.html, expected);
    MsgBuilder declaredUtf8;
    declaredUtf8.longValue(0x3FDE, 65001).binary(0x1013, utf8);
    QVERIFY(readOk(declaredUtf8.build(), &m));
    QCOMPARE(m.html, expected);
    MsgBuilder unicode;
    unicode.unicode(0x1013, expected);
    QVERIFY(readOk(unicode.build(), &m));
    QCOMPARE(m.html, expected);
    MsgBuilder both;   // PidTagHtml wins over the RTF body
    both.binary(0x1013, utf8).binary(0x1009, storeRtfMela("{\\rtf1\\ansi\\fromhtml1 {\\*\\htmltag64 <p>}other}"));
    QVERIFY(readOk(both.build(), &m));
    QCOMPARE(m.html, expected);
    QVERIFY(m.plain.isEmpty());
}

void TestMailMsg::msgPlainBody()
{
    MsgBuilder msg;
    msg.unicode(0x0037, QStringLiteral("plain")).unicode(0x1000, QStringLiteral("Line 1\r\nLine 2\r\n\tIndented ✓"));
    MessageContent m;
    QVERIFY(readOk(msg.build(), &m));
    QCOMPARE(m.plain, QStringLiteral("Line 1\r\nLine 2\r\n\tIndented ✓"));
    QVERIFY(m.html.isEmpty());
    // MAPI strings end at the first NUL.
    MsgBuilder nul;
    nul.variable(0x1000, kMsgPtUnicode, QByteArray("a\0b\0\0\0c\0", 8));
    QVERIFY(readOk(nul.build(), &m));
    QCOMPARE(m.plain, QStringLiteral("ab"));
}

void TestMailMsg::msgRtfEncapsulatedHtml_data()
{
    QTest::addColumn<bool>("compressed");
    QTest::newRow("MELA (stored)") << false;
    QTest::newRow("LZFu (compressed)") << true;
}

void TestMailMsg::msgRtfEncapsulatedHtml()
{
    QFETCH(bool, compressed);
    const QByteArray rtf = QByteArrayLiteral(
        "{\\rtf1\\ansi\\ansicpg1252\\fromhtml1 \\deff0{\\fonttbl{\\f0\\fswiss Arial;}}\r\n"
        "{\\*\\htmltag19 <html>}{\\*\\htmltag34 <head>}{\\*\\htmltag161 <title>}Hi{\\*\\htmltag169 </title>}"
        "{\\*\\htmltag41 </head>}\r\n"
        "{\\*\\htmltag50 <body>}{\\*\\htmltag64 <p>}\\htmlrtf {\\htmlrtf0 Gr\\'fc\\'dfe {\\*\\htmltag84 <b>}"
        "\\htmlrtf {\\b\\htmlrtf0 world\\htmlrtf }\\htmlrtf0 {\\*\\htmltag92 </b>}\r\n"
        "{\\*\\htmltag84 <img src=\"cid:logo@x\">}\\htmlrtf }\\htmlrtf0 {\\*\\htmltag72 </p>}\\htmlrtf \\par\\htmlrtf0 "
        "{\\*\\htmltag58 </body></html>}}");
    MsgBuilder msg;
    msg.unicode(0x0037, QStringLiteral("RTF only")).binary(0x1009, compressed ? compressRtfLzfu(rtf) : storeRtfMela(rtf));
    MsgAttachmentBuilder logo;
    logo.longValue(0x3705, 1).unicode(0x3704, QStringLiteral("logo.png")).unicode(0x3712, QStringLiteral("logo@x"));
    logo.binary(0x3701, "PNG");
    msg.attachments.append(logo);
    MessageContent m;
    QVERIFY(readOk(msg.build(), &m));
    QCOMPARE(m.html, QStringLiteral("<html><head><title>Hi</title></head><body><p>Grüße <b>world</b>"
                                    "<img src=\"cid:logo@x\"></p></body></html>"));
    QVERIFY(m.plain.isEmpty());
    QCOMPARE(m.attachments.size(), qsizetype(1));
    QVERIFY(m.attachments.at(0).inlinePart);   // cid: referenced by the de-encapsulated HTML
}

void TestMailMsg::msgRtfOnlyText()
{
    const QByteArray plainRtf = QByteArrayLiteral(
        "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Arial;}}\\pard Hello\\par World \\'e9t\\'e9\\par}");
    MessageContent m;
    MsgBuilder msg;
    msg.unicode(0x0037, QStringLiteral("rtf")).binary(0x1009, compressRtfLzfu(plainRtf));
    QVERIFY(readOk(msg.build(), &m));
    QCOMPARE(m.plain, QStringLiteral("Hello\nWorld été\n"));
    QVERIFY(m.html.isEmpty());
    MsgBuilder withBody;   // an existing PidTagBody wins
    withBody.unicode(0x1000, QStringLiteral("Body")).binary(0x1009, compressRtfLzfu("{\\rtf1\\ansi\\fromtext from rtf}"));
    QVERIFY(readOk(withBody.build(), &m));
    QCOMPARE(m.plain, QStringLiteral("Body"));
    MsgBuilder fromText;
    fromText.unicode(0x0037, QStringLiteral("t")).binary(0x1009, compressRtfLzfu("{\\rtf1\\ansi\\fromtext from rtf\\par}"));
    QVERIFY(readOk(fromText.build(), &m));
    QCOMPARE(m.plain, QStringLiteral("from rtf\n"));
    // A broken RTF body never fails the message.
    QByteArray broken = compressRtfLzfu(plainRtf);
    broken[20] = char(broken[20] ^ 0x55);
    MsgBuilder badCrc;
    badCrc.unicode(0x0037, QStringLiteral("broken")).unicode(0x1000, QStringLiteral("kept")).binary(0x1009, broken);
    QVERIFY(readOk(badCrc.build(), &m));
    QCOMPARE(m.plain, QStringLiteral("kept"));
    QVERIFY(m.html.isEmpty());
    // Neither does an RTF stream that is unreadable at the compound-file level.
    MsgBuilder unreadable;
    unreadable.unicode(0x0037, QStringLiteral("cfb")).binary(0x1009, storeRtfMela(plainRtf));
    CfbLayout layout;
    QByteArray file = unreadable.build({}, &layout);
    const int id = layout.idOf(QStringLiteral("__substg1.0_10090102"));
    QVERIFY(id > 0 && layout.items.at(id).mini && layout.items.at(id).size > 64);
    cfbPut32(file, layout.miniFatEntryOffset(layout.items.at(id).start), 5000);
    QVERIFY(readOk(file, &m));
    QCOMPARE(m.subject, QStringLiteral("cfb"));
    QVERIFY(m.plain.isEmpty() && m.html.isEmpty());
}

void TestMailMsg::msgAttachments()
{
    MsgBuilder msg;
    msg.unicode(0x0037, QStringLiteral("With attachments")).binary(0x1013, "<p>See <img src=\"CID:image001.png@01D0\"></p>");
    const QByteArray pdf = "%PDF-1.4 fake";
    const QByteArray big(10000, 'z');
    MsgAttachmentBuilder a0, a1, a2, a3, a4, a5, a6;
    a0.longValue(0x3705, 1).unicode(0x3707, QStringLiteral("report.pdf")).unicode(0x3704, QStringLiteral("REPORT~1.PDF"));
    a0.unicode(0x370E, QStringLiteral("Application/PDF; name=report.pdf")).binary(0x3701, pdf);
    a1.longValue(0x3705, 1).unicode(0x3704, QStringLiteral("image001.png")).unicode(0x3712, QStringLiteral("<image001.png@01D0>"));
    a1.binary(0x3701, "\x89PNG");
    a2.longValue(0x3705, 1).unicode(0x3001, QStringLiteral("notes.txt")).boolValue(0x7FFE, true).binary(0x3701, "hidden");
    a3.longValue(0x3705, 1).unicode(0x3707, QStringLiteral("style.css")).longValue(0x3714, 4);
    a3.unicode(0x3713, QStringLiteral("http://example.org/style.css")).binary(0x3701, "p{}");
    a4.longValue(0x3705, 6).unicode(0x3707, QStringLiteral("chart.xls")).binary(0x3701, "OLE");
    a5.binary(0x3701, "?");
    a6.longValue(0x3705, 1).unicode(0x3707, QStringLiteral("unknown.qqx")).unicode(0x370E, QStringLiteral("not a mime"));
    a6.binary(0x3701, big);
    msg.attachments = {a0, a1, a2, a3, a4, a5, a6};
    MessageContent m;
    QVERIFY(readOk(msg.build(), &m));
    const QList<MessageAttachment> &at = m.attachments;
    QCOMPARE(at.size(), qsizetype(7));
    QCOMPARE(at[0].name, QStringLiteral("report.pdf"));
    QCOMPARE(at[0].mimeType, QStringLiteral("application/pdf"));
    QCOMPARE(at[0].data, pdf);
    QVERIFY(!at[0].inlinePart && at[0].contentId.isEmpty() && !at[0].embedded && at[0].section.isEmpty());
    QCOMPARE(at[1].name, QStringLiteral("image001.png"));
    QCOMPARE(at[1].mimeType, QStringLiteral("image/png"));
    QCOMPARE(at[1].contentId, QStringLiteral("image001.png@01D0"));
    QVERIFY(at[1].inlinePart);
    QCOMPARE(at[2].name, QStringLiteral("notes.txt"));
    QCOMPARE(at[2].mimeType, QStringLiteral("text/plain"));
    QCOMPARE(at[2].data, QByteArray("hidden"));
    QVERIFY(at[2].inlinePart);
    QCOMPARE(at[3].mimeType, QStringLiteral("application/octet-stream"));
    QCOMPARE(at[3].contentLocation, QStringLiteral("http://example.org/style.css"));
    QVERIFY(at[3].inlinePart);
    QCOMPARE(at[4].name, QStringLiteral("chart.xls"));
    QCOMPARE(at[4].mimeType, QStringLiteral("application/octet-stream"));
    QCOMPARE(at[4].data, QByteArray("OLE"));
    QVERIFY(!at[4].inlinePart);
    QVERIFY(at[5].name.isEmpty());
    QCOMPARE(at[5].mimeType, QStringLiteral("application/octet-stream"));
    QCOMPARE(at[5].data, QByteArray("?"));
    QCOMPARE(at[6].mimeType, QStringLiteral("application/octet-stream"));
    QVERIFY(at[6].data == big);

    MsgBuilder many;
    many.unicode(0x0037, QStringLiteral("many"));
    for (int i = 0; i <= kMaxMsgAttachments; ++i) {
        MsgAttachmentBuilder attachment;
        attachment.longValue(0x3705, 1);
        many.attachments.append(attachment);
    }
    const QString failure = readFailure(many.build());
    QVERIFY2(failure.contains(QLatin1String("more than 1000 attachments")), qPrintable(failure));
}

void TestMailMsg::msgEmbeddedMessages()
{
    auto inner2 = std::make_shared<MsgBuilder>();
    inner2->unicode(0x0037, QStringLiteral("Inner two"))
        .unicode(0x1000, QStringLiteral("deepest body"))
        .unicode(0x0042, QStringLiteral("Zed"))
        .unicode(0x5D02, QStringLiteral("zed@example.org"));
    MsgAttachmentBuilder file;
    file.longValue(0x3705, 1).unicode(0x3707, QStringLiteral("deep.txt")).binary(0x3701, "deep data");
    inner2->attachments.append(file);
    inner2->recipients.append(recipient(1, QStringLiteral("Ann"), QStringLiteral("SMTP"), QStringLiteral("ann@example.org")));
    auto inner1 = std::make_shared<MsgBuilder>();
    inner1->unicode(0x0037, QStringLiteral("Inner one")).unicode(0x1000, QStringLiteral("middle body"));
    MsgAttachmentBuilder second;
    second.longValue(0x3705, 5).unicode(0x3001, QStringLiteral("Display only"));
    second.embedded = inner2;
    inner1->attachments.append(second);
    MsgBuilder top;
    top.unicode(0x0037, QStringLiteral("Top"));
    MsgAttachmentBuilder first;   // no names at all: subject + ".eml"
    first.longValue(0x3705, 5);
    first.embedded = inner1;
    MsgAttachmentBuilder named;
    named.longValue(0x3705, 5).unicode(0x3707, QStringLiteral("forwarded.msg"));
    named.embedded = inner2;
    top.attachments = {first, named};

    MessageContent m;
    QVERIFY(readOk(top.build(), &m));
    QCOMPARE(m.attachments.size(), qsizetype(2));
    const MessageAttachment &a = m.attachments.at(0);
    QCOMPARE(a.name, QStringLiteral("Inner one.eml"));
    QCOMPARE(a.mimeType, QStringLiteral("message/rfc822"));
    QVERIFY(a.data.isEmpty());
    QVERIFY(a.embedded);
    QCOMPARE(a.embedded->subject, QStringLiteral("Inner one"));
    QCOMPARE(a.embedded->plain, QStringLiteral("middle body"));
    QCOMPARE(a.embedded->attachments.size(), qsizetype(1));
    const MessageAttachment &b = a.embedded->attachments.at(0);
    QCOMPARE(b.name, QStringLiteral("Inner two.eml"));
    QVERIFY(b.embedded);
    QCOMPARE(b.embedded->subject, QStringLiteral("Inner two"));
    QCOMPARE(b.embedded->plain, QStringLiteral("deepest body"));
    QCOMPARE(b.embedded->fromAddress, QStringLiteral("zed@example.org"));
    QCOMPARE(b.embedded->to, QStringLiteral("Ann <ann@example.org>"));
    QCOMPARE(b.embedded->attachments.size(), qsizetype(1));
    QCOMPARE(b.embedded->attachments.at(0).name, QStringLiteral("deep.txt"));
    QCOMPARE(b.embedded->attachments.at(0).data, QByteArray("deep data"));
    QCOMPARE(m.attachments.at(1).name, QStringLiteral("forwarded.msg"));
    QCOMPARE(m.attachments.at(1).mimeType, QStringLiteral("message/rfc822"));
    QCOMPARE(m.attachments.at(1).embedded->subject, QStringLiteral("Inner two"));
}

void TestMailMsg::msgEmbeddingDepthLimit()
{
    MessageContent m;
    QVERIFY(readOk(nestedMessages(kMaxMsgEmbeddingDepth), &m));
    const MessageContent *deepest = &m;
    for (int level = 1; level <= kMaxMsgEmbeddingDepth; ++level) {
        QCOMPARE(deepest->attachments.size(), qsizetype(1));
        QVERIFY(deepest->attachments.at(0).embedded);
        deepest = deepest->attachments.at(0).embedded.get();
        QCOMPARE(deepest->subject, QStringLiteral("level %1").arg(level));
    }
    const QString failure = readFailure(nestedMessages(kMaxMsgEmbeddingDepth + 1));
    QVERIFY2(failure.contains(QLatin1String("nested more than 8 deep")), qPrintable(failure));
}

void TestMailMsg::msgCorruptionFuzz()
{
    auto embedded = std::make_shared<MsgBuilder>();
    embedded->unicode(0x0037, QStringLiteral("embedded")).unicode(0x1000, QString(300, u'e'));
    MsgBuilder msg;
    msg.unicode(0x0037, QStringLiteral("Fuzz me"))
        .unicode(0x0042, QStringLiteral("Ada"))
        .unicode(0x5D02, QStringLiteral("ada@example.org"))
        .unicode(0x1000, QString(2000, u'p'))
        .binary(0x1013, QByteArray(6000, 'h'))
        .binary(0x1009, compressRtfLzfu(QByteArray(3000, 'r')))
        .time(0x0039, utc(2024, 1, 1, 0, 0, 0));
    msg.recipients = {recipient(1, QStringLiteral("Bob"), QStringLiteral("SMTP"), QStringLiteral("bob@example.org")),
                      recipient(2, QStringLiteral("Cy"), QStringLiteral("SMTP"), QStringLiteral("cy@example.org"))};
    MsgAttachmentBuilder data;
    data.longValue(0x3705, 1).unicode(0x3707, QStringLiteral("data.bin")).binary(0x3701, QByteArray(9000, 'd'));
    MsgAttachmentBuilder message;
    message.longValue(0x3705, 5);
    message.embedded = embedded;
    msg.attachments = {data, message};
    const QByteArray file = msg.build();
    MessageContent m;
    QVERIFY(readOk(file, &m));
    QCOMPARE(m.attachments.size(), qsizetype(2));

    QRandomGenerator rng(0x0b0e);
    int failures = 0;
    // Either a clean read or a failure with a message — never a crash, a
    // silent failure or stale content.
    auto check = [&failures](const QByteArray &bytes) {
        const QString result = readFailure(bytes);
        if (result == QLatin1String("(succeeded)")) return true;
        ++failures;
        return !result.startsWith(u'(');
    };
    for (int i = 0; i < 100; ++i) QVERIFY(check(file.left(rng.bounded(int(file.size())))));
    for (int i = 0; i < 300; ++i) {
        QByteArray copy = file;
        const int flips = 1 + rng.bounded(6);
        for (int k = 0; k < flips; ++k) copy[rng.bounded(int(copy.size()))] = char(rng.bounded(256));
        QVERIFY(check(copy));
    }
    QVERIFY(failures > 50);
}
