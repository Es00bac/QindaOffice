// SPDX-License-Identifier: GPL-3.0-or-later
// tst_mail_msg, RTF: MS-OXRTFCP decompression against the specification's
// worked examples (3.1.1, 3.1.2) and corrupted blobs, the test LZFu
// compressor, and MS-OXRTFEX HTML / plain-text extraction including
// hostile input.

#include "tst_mail_msg.h"

#include "cfb_writer_msg.h"
#include "rtf_compressed.h"
#include "rtf_text.h"

#include <QRandomGenerator>
#include <QTest>

using namespace QindaOffice::Mail;
using namespace QindaOffice::Mail::Test;

namespace {

// MS-OXRTFCP 3.1.1 and 3.1.2, byte for byte.
QByteArray specCompressed1()
{
    return QByteArray::fromHex("2d0000002b0000004c5a4675f1c5c7a703000a007263706731323542320af32068656c09002062"
                               "7705b06c647d0a800fa0");
}
QByteArray specRaw1() { return QByteArrayLiteral("{\\rtf1\\ansi\\ansicpg1252\\pard hello world}\r\n"); }
QByteArray specCompressed2() { return QByteArray::fromHex("1a0000001c0000004c5a4675e2d44b51410004205758595a0d6e7d010eb0"); }
QByteArray specRaw2() { return QByteArrayLiteral("{\\rtf1 WXYZWXYZWXYZWXYZWXYZ}"); }

QString decompressError(const QByteArray &blob)
{
    QByteArray rtf = QByteArrayLiteral("stale");
    QString error;
    if (decompressRtf(blob, &rtf, &error)) return QStringLiteral("(succeeded)");
    if (!rtf.isEmpty()) return QStringLiteral("(output left behind)");
    return error.isEmpty() ? QStringLiteral("(failed silently)") : error;
}

QByteArray withLzfuPayload(const QByteArray &payload, qsizetype rawSize)
{
    return rtfBlobHeader(payload.size(), rawSize, 0x75465A4C, rtfCompressedCrc(payload)) + payload;
}

QString html(const QByteArray &rtf)
{
    QString out;
    QString error;
    if (!rtfToHtml(rtf, &out, &error)) return QStringLiteral("(failed: %1)").arg(error);
    return out;
}

// Modeled on the MS-OXRTFEX section 4 example (reconstructed; see the
// report of M64): style block, comments, <p>/<font>/<b>, <br> as \line,
// character references with RTF stand-ins, RTF specials and a list.
QByteArray rtfexExample()
{
    return QByteArrayLiteral(
        "{\\rtf1\\ANSI\\ansicpg1252\\fromhtml1 \\deff0{\\fonttbl\n"
        "{\\f0\\fmodern Courier New;}{\\f1\\fswiss Arial;}{\\f2\\fswiss\\fcharset0 Arial;}}\n"
        "{\\colortbl\\red0\\green0\\blue0;\\red0\\green0\\blue255;}\n"
        "{\\*\\htmltag64}\n"
        "\\uc1\\pard\\plain\\deftab360 \\f0\\fs24 \n"
        "{\\*\\htmltag148 <html>\\par }\n"
        "{\\*\\htmltag50 \\par }\n"
        "{\\*\\htmltag18 <head>\\par }\n"
        "{\\*\\htmltag50 \\par }\n"
        "{\\*\\htmltag177 <style>\\par\n"
        "<!--\\par\n"
        " /* Style Definitions */\\par\n"
        " p.MsoNormal, li.MsoNormal\\par\n"
        "\\tab \\{margin:0in;\\par\n"
        "\\tab margin-bottom:.0001pt;\\par\n"
        "\\tab font-size:12.0pt;\\par\n"
        "\\tab font-family:\"Times New Roman\";\\}\\par\n"
        "-->\\par\n"
        "</style>}\n"
        "{\\*\\htmltag241 \\par }\n"
        "{\\*\\htmltag58 </head>}\n"
        "{\\*\\htmltag50 \\par }\n"
        "{\\*\\htmltag17 <body>}\n"
        "{\\*\\htmltag50 \\par }\n"
        "{\\*\\htmltag8 <!-- Converted from text/plain format -->}\n"
        "{\\*\\htmltag50 \\par }\n"
        "{\\*\\htmltag64 <p>}\\htmlrtf {\\htmlrtf0 \n"
        "{\\*\\htmltag148 <font size=2>}\\htmlrtf {\\fs20\\htmlrtf0 Note the line break inside a P tag. "
        "{\\*\\htmltag116 <br>}\\htmlrtf \\line \\htmlrtf0 This is a new line.\n"
        "{\\*\\htmltag84 <b>}\\htmlrtf {\\b\\htmlrtf0 This is a bold text\n"
        "{\\*\\htmltag92 </b>}\\htmlrtf }\\htmlrtf0  \n"
        "{\\*\\htmltag156 </font>}\\htmlrtf }\\htmlrtf0  \n"
        "{\\*\\htmltag72 </p>}\\htmlrtf \\par }\\htmlrtf0 \n"
        "{\\*\\htmltag50 \\par }\n"
        "{\\*\\htmltag64 <p>}\\htmlrtf {\\htmlrtf0  \n"
        "{\\*\\htmltag148 <font size=2>}\\htmlrtf {\\fs20\\htmlrtf0 This is a normal text with a character references:\n"
        "{\\*\\htmltag84 &nbsp;}\\htmlrtf \\'a0\\htmlrtf0  {\\*\\htmltag84 &lt;}\\htmlrtf <\\htmlrtf0  "
        "{\\*\\htmltag84 &uml;}\\htmlrtf \\'a8\\htmlrtf0 \n"
        "{\\*\\htmltag116 <br>}\\htmlrtf \\line \\htmlrtf0 characters which have special meaning in RTF: \\{\\}\\\\\n"
        "{\\*\\htmltag116 <br>}\\htmlrtf \\line \\htmlrtf0 \n"
        "{\\*\\htmltag156 </font>}\\htmlrtf }\\htmlrtf0  \n"
        "{\\*\\htmltag72 </p>}\\htmlrtf \\par }\\htmlrtf0 \n"
        "{\\*\\htmltag50 \\par }\n"
        "{\\*\\htmltag96 <ol>}\\htmlrtf {{\\*\\pn\\pnlvlbody\\pndec\\pnstart1\\pnindent360{\\pntxta.}}\\li720\\fi-360\\htmlrtf0 \n"
        "{\\*\\htmltag68 <li>}{\\htmlrtf {\\pntext 1.\\tab}\\htmlrtf0 This is a list item}\n"
        "{\\*\\htmltag76 </li>}\\htmlrtf \\par }\\htmlrtf0 \n"
        "{\\*\\htmltag104 </ol>}\\htmlrtf \\par\\htmlrtf0 \n"
        "{\\*\\htmltag50 \\par }\n"
        "{\\*\\htmltag58 </body>}\n"
        "{\\*\\htmltag50 \\par }\n"
        "{\\*\\htmltag27 </html>}\n"
        "{\\*\\htmltag50 \\par }}\n");
}

QString rtfexExpectedHtml()
{
    return QStringLiteral(
        "<html>\r\n"
        "\r\n"
        "<head>\r\n"
        "\r\n"
        "<style>\r\n"
        "<!--\r\n"
        " /* Style Definitions */\r\n"
        " p.MsoNormal, li.MsoNormal\r\n"
        "\t{margin:0in;\r\n"
        "\tmargin-bottom:.0001pt;\r\n"
        "\tfont-size:12.0pt;\r\n"
        "\tfont-family:\"Times New Roman\";}\r\n"
        "-->\r\n"
        "</style>\r\n"
        "</head>\r\n"
        "<body>\r\n"
        "<!-- Converted from text/plain format -->\r\n"
        "<p><font size=2>Note the line break inside a P tag. <br>This is a new line."
        "<b>This is a bold text</b> </font> </p>\r\n"
        "<p> <font size=2>This is a normal text with a character references:&nbsp; &lt; &uml;"
        "<br>characters which have special meaning in RTF: {}\\<br></font> </p>\r\n"
        "<ol><li>This is a list item</li></ol>\r\n"
        "</body>\r\n"
        "</html>\r\n");
}

QByteArray randomBytes(QRandomGenerator &rng, int size)
{
    QByteArray bytes(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i) bytes[i] = char(rng.bounded(256));
    return bytes;
}

} // namespace

void TestMailMsg::rtfCrcSpecVectors()
{
    QCOMPARE(rtfCompressedCrc(specCompressed1().mid(16)), 0xA7C7C5F1u);
    QCOMPARE(rtfCompressedCrc(specCompressed2().mid(16)), 0x514BD4E2u);
    QCOMPARE(rtfCompressedCrc(QByteArray()), 0u);
    QCOMPARE(rtfCompressionPrefix().size(), qsizetype(207));
    QVERIFY(rtfCompressionPrefix().startsWith("{\\rtf1\\ansi\\mac\\deff0\\deftab720"));
    QVERIFY(rtfCompressionPrefix().endsWith("\\b\\i\\u\\tab\\tx"));
}

void TestMailMsg::rtfDecompressSpecVectors()
{
    QByteArray rtf;
    QString error;
    QVERIFY2(decompressRtf(specCompressed1(), &rtf, &error), qPrintable(error));
    QCOMPARE(rtf, specRaw1());
    QVERIFY2(decompressRtf(specCompressed2(), &rtf, &error), qPrintable(error));
    QCOMPARE(rtf, specRaw2());
    // The test compressor (cfb_writer_msg.h) makes the same choices as the
    // specification's encoder, so it reproduces both examples exactly.
    QCOMPARE(compressRtfLzfu(specRaw1()), specCompressed1());
    QCOMPARE(compressRtfLzfu(specRaw2()), specCompressed2());
    // Trailing bytes after COMPSIZE are not part of the stream.
    QVERIFY2(decompressRtf(specCompressed1() + QByteArray(8, '\0'), &rtf, &error), qPrintable(error));
    QCOMPARE(rtf, specRaw1());
}

void TestMailMsg::rtfDecompressStored()
{
    const QByteArray raw = QByteArrayLiteral("{\\rtf1\\ansi stored}");
    QByteArray rtf;
    QString error;
    QVERIFY2(decompressRtf(storeRtfMela(raw), &rtf, &error), qPrintable(error));
    QCOMPARE(rtf, raw);
    QByteArray blob = storeRtfMela(raw + "XYZ");   // RAWSIZE shorter than the payload
    cfbPut32(blob, 4, quint32(raw.size()));
    QVERIFY2(decompressRtf(blob, &rtf, &error), qPrintable(error));
    QCOMPARE(rtf, raw);
    blob = storeRtfMela(raw);
    cfbPut32(blob, 4, quint32(raw.size() + 5));
    QVERIFY(decompressError(blob).contains(QLatin1String("truncated")));
    QVERIFY2(decompressRtf(storeRtfMela({}), &rtf, &error), qPrintable(error));
    QVERIFY(rtf.isEmpty());
}

void TestMailMsg::rtfDecompressRejectsCorruption()
{
    QByteArray blob = specCompressed1();
    blob[20] = char(blob[20] ^ 0x01);
    QVERIFY(decompressError(blob).contains(QLatin1String("CRC")));
    const QByteArray payload = specCompressed1().mid(16);
    QVERIFY(decompressError(withLzfuPayload(payload.chopped(4), 43)).contains(QLatin1String("end marker")));
    QVERIFY(decompressError(withLzfuPayload(payload.chopped(1), 43)).contains(QLatin1String("dictionary reference")));
    QVERIFY(decompressError(specCompressed1().chopped(3)).contains(QLatin1String("truncated")));
    blob = specCompressed1();
    cfbPut32(blob, 4, quint32(64 * 1024 * 1024 + 1));
    QVERIFY(decompressError(blob).contains(QLatin1String("64 MiB")));
    blob = specCompressed1();
    cfbPut32(blob, 8, 0x12345678);
    QVERIFY(decompressError(blob).contains(QLatin1String("Unknown")));
    QVERIFY(decompressError(specCompressed1().left(10)).contains(QLatin1String("header")));
    QVERIFY(decompressError(QByteArray()).contains(QLatin1String("header")));
    blob = specCompressed1();
    cfbPut32(blob, 0, 4);
    QVERIFY(decompressError(blob).contains(QLatin1String("impossible")));
    blob = specCompressed1();
    cfbPut32(blob, 4, 10);   // RAWSIZE smaller than the output
    QVERIFY(decompressError(blob).contains(QLatin1String("beyond")));

    // Random payloads with a valid header and CRC exercise the decoder's
    // bounds: never a crash, always either RTF of the declared size or an error.
    QRandomGenerator rng(0xc0ffee);
    for (int i = 0; i < 300; ++i) {
        const QByteArray random = randomBytes(rng, 1 + rng.bounded(400));
        const qsizetype rawSize = rng.bounded(2000);
        QByteArray rtf;
        QString error;
        if (decompressRtf(withLzfuPayload(random, rawSize), &rtf, &error)) QVERIFY(rtf.size() <= rawSize);
        else QVERIFY(!error.isEmpty() && rtf.isEmpty());
    }
}

void TestMailMsg::rtfCompressorRoundTrip()
{
    QByteArray document;
    for (int i = 0; i < 300; ++i)
        document += "{\\pard\\plain Line " + QByteArray::number(i) + " of the \\b test\\b0  document\\par}\r\n";
    QVERIFY(document.size() > 3 * 4096);   // wraps the dictionary several times
    QRandomGenerator rng(42);
    const QList<QByteArray> samples = {document, QByteArray(), QByteArrayLiteral("x"), QByteArray(5000, 'a'),
                                       randomBytes(rng, 3000)};
    for (const QByteArray &raw : samples) {
        const QByteArray blob = compressRtfLzfu(raw);
        QByteArray rtf;
        QString error;
        QVERIFY2(decompressRtf(blob, &rtf, &error), qPrintable(error));
        QVERIFY(rtf == raw);
    }
    QVERIFY(compressRtfLzfu(document).size() < document.size() / 4);
}

void TestMailMsg::rtfEncapsulationKinds()
{
    QVERIFY(rtfEncapsulation(rtfexExample()) == RtfBodyKind::Html);
    QVERIFY(rtfEncapsulation("{\\rtf1\\ansi\\ansicpg1252\\fromhtml1 \\fbidis \\deff0{\\fonttbl}}") == RtfBodyKind::Html);
    QVERIFY(rtfEncapsulation("\r\n {\\rtf1\\ansi\\fromtext \\deff0 text}") == RtfBodyKind::Text);
    QVERIFY(rtfEncapsulation("{\\rtf1\\ansi hello}") == RtfBodyKind::Rtf);
    QVERIFY(rtfEncapsulation("{\\rtf1\\ansi\\fromhtml0 x}") == RtfBodyKind::Rtf);
    QVERIFY(rtfEncapsulation("{\\rtf1{\\fonttbl}\\fromhtml1 late}") == RtfBodyKind::Rtf);
    QVERIFY(rtfEncapsulation(specRaw1()) == RtfBodyKind::Rtf);
    QVERIFY(rtfEncapsulation(QByteArray()) == RtfBodyKind::None);
    QVERIFY(rtfEncapsulation("plain text") == RtfBodyKind::None);
    QVERIFY(rtfEncapsulation("{\\rtf") == RtfBodyKind::Rtf);
    QVERIFY(rtfEncapsulation("{\\rtf1\\") == RtfBodyKind::Rtf);
}

void TestMailMsg::rtfHtmlSpecExample()
{
    QCOMPARE(html(rtfexExample()), rtfexExpectedHtml());
    // The same RTF as plain text is what a non-HTML reader shows: the
    // \htmlrtf fragments, no tags.
    const QString plain = rtfToPlainText(rtfexExample());
    QVERIFY2(plain.contains(QStringLiteral("Note the line break inside a P tag. \nThis is a new line.")), qPrintable(plain));
    QVERIFY(plain.contains(QStringLiteral("references:  < ¨\ncharacters which have special meaning in RTF: {}\\\n")));
    QVERIFY(plain.contains(QStringLiteral("1.\tThis is a list item\n")));
    QVERIFY(!plain.contains(QStringLiteral("<p>")));
    QVERIFY(!plain.contains(QStringLiteral("MsoNormal")));
}

void TestMailMsg::rtfHtmlEscapesAndCodePages()
{
    // \uN with \ucN fallbacks, negative \uN, cp1252 \'hh.
    QCOMPARE(html("{\\rtf1\\ansi\\ansicpg1252\\fromhtml1 {\\*\\htmltag64 <p>}\\uc1\\u8364?\\u-3913?"
                  "\\uc2\\u12354\\'82\\'a0 caf\\'e9{\\*\\htmltag72 </p>}}"),
             QStringLiteral("<p>€あ café</p>"));
    // A double-byte code page: consecutive \'hh decode as one run.
    QCOMPARE(html("{\\rtf1\\ansi\\ansicpg932\\fromhtml1 {\\*\\htmltag64 <p>}\\'82\\'a0\\'82\\'a2{\\*\\htmltag72 </p>}}"),
             QStringLiteral("<p>あい</p>"));
    QCOMPARE(html("{\\rtf1\\ansi\\ansicpg1251\\fromhtml1 \\'cf\\'f0\\'e8\\'e2\\'e5\\'f2}"),
             QStringLiteral("Привет"));
    // A font's \fcharset overrides \ansicpg for its text.
    QCOMPARE(html("{\\rtf1\\ansi\\ansicpg1252\\fromhtml1 \\deff0{\\fonttbl{\\f0\\fswiss Arial;}"
                  "{\\f1\\fswiss\\fcharset204 Arial Cyr;}}{\\f1 \\'cf\\'f0\\'e8}{\\'e9}}"),
             QStringLiteral("Приé"));
    // \htmlrtf is group-scoped and suppresses \par/\line/\tab and text.
    QCOMPARE(html("{\\rtf1\\ansi\\fromhtml1 A\\htmlrtf B{C\\htmlrtf0 D}E\\htmlrtf0 F\\par\\htmlrtf G\\par"
                  "\\htmlrtf0\\tab H\\line I}"),
             QStringLiteral("ADF\r\n\tH\r\nI"));
    // \* destinations other than \htmltag are dropped, \htmltag content kept verbatim.
    QCOMPARE(html("{\\rtf1\\ansi\\fromhtml1 {\\*\\generator Riched20;}{\\*\\mhtmltag84 http://x/a.png}"
                  "{\\*\\htmltag84 <img src=\"cid:a\">}{\\info{\\title t}}x}"),
             QStringLiteral("<img src=\"cid:a\">x"));
    // Control symbols.
    QCOMPARE(html("{\\rtf1\\fromhtml1 a\\~b\\-c\\_d\\{\\}\\\\}"), QStringLiteral("a bc‑d{}\\"));
    // \binN skips raw bytes, braces included.
    QCOMPARE(html("{\\rtf1\\fromhtml1 a{\\bin4 {}{}}b}"), QStringLiteral("ab"));
}

void TestMailMsg::rtfHtmlHostileInput()
{
    QString out;
    QString error;
    const QByteArray deep = "{\\rtf1\\fromhtml1 " + QByteArray(600, '{') + "x" + QByteArray(600, '}') + "}";
    QVERIFY(!rtfToHtml(deep, &out, &error));
    QVERIFY2(error.contains(QLatin1String("deep")), qPrintable(error));
    QVERIFY(out.isEmpty());
    const QByteArray deepButAllowed = "{\\rtf1\\fromhtml1 " + QByteArray(kMaxRtfGroupDepth - 1, '{') + "x"
        + QByteArray(kMaxRtfGroupDepth - 1, '}') + "}";
    QCOMPARE(html(deepButAllowed), QStringLiteral("x"));
    QVERIFY(!rtfToHtml("{\\rtf1\\ansi no html}", &out, &error));
    QVERIFY(error.contains(QLatin1String("fromhtml1")));
    QVERIFY(!rtfToHtml("not rtf", &out, &error));

    QCOMPARE(html("{\\rtf1\\fromhtml1 a}}}b"), QStringLiteral("a"));         // stops at the document's end
    QCOMPARE(html("{\\rtf1\\fromhtml1 a{b"), QStringLiteral("ab"));          // unclosed groups
    QCOMPARE(html("{\\rtf1\\fromhtml1 a\\u99999999999999999999?\\u-99999?\\u?}"), QStringLiteral("a��"));
    QCOMPARE(html("{\\rtf1\\fromhtml1 a\\u0?b\\u7?c}"), QStringLiteral("abc"));   // control characters dropped
    QCOMPARE(html("{\\rtf1\\fromhtml1 a\\'4"), QStringLiteral("a"));
    QCOMPARE(html("{\\rtf1\\fromhtml1 a\\'zzb}"), QStringLiteral("azzb"));
    QCOMPARE(html("{\\rtf1\\fromhtml1 a\\"), QStringLiteral("a"));
    QCOMPARE(html("{\\rtf1\\fromhtml1 a\\bin999999999999 xyz}"), QStringLiteral("a"));
    QCOMPARE(html("{\\rtf1\\fromhtml1 a\\uc999999\\u65?bcdef}"), QStringLiteral("aA"));
    QCOMPARE(html("{\\rtf1\\fromhtml1 \\ansicpg-5 \\ansicpg99999999 \\f-3 \\'e9}"), QStringLiteral("é"));

    QRandomGenerator rng(0xbad);
    const QByteArray alphabet = QByteArrayLiteral("{}\\'*uchtmlrtfag0123456789 -;\r\nxyz");
    for (int i = 0; i < 400; ++i) {
        QByteArray rtf = QByteArrayLiteral("{\\rtf1\\fromhtml1 ");
        const int size = rng.bounded(300);
        for (int k = 0; k < size; ++k)
            rtf += rng.bounded(4) == 0 ? char(rng.bounded(256)) : alphabet.at(rng.bounded(int(alphabet.size())));
        QString result;
        if (!rtfToHtml(rtf, &result, &error)) QVERIFY(!error.isEmpty());
        QVERIFY(result.size() <= rtf.size());
        QVERIFY(rtfToPlainText(rtf).size() <= rtf.size());
    }
}

void TestMailMsg::rtfPlainText()
{
    QCOMPARE(rtfToPlainText("{\\rtf1\\ansi\\ansicpg1252\\fromtext \\fbidis \\deff0{\\fonttbl{\\f0\\fswiss Arial;}}"
                            "{\\colortbl\\red0\\green0\\blue0;}{\\*\\generator Riched20;}\\uc1\\pard\\plain\\f0\\fs20 "
                            "Line one\\par Line two\\tab tabbed\\par caf\\'e9 \\u8364?}"),
             QStringLiteral("Line one\nLine two\ttabbed\ncafé €"));
    QCOMPARE(rtfToPlainText("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times;}}{\\stylesheet{\\s0 Normal;}}"
                            "{\\info{\\title Secret title}{\\author Someone}}{\\header Header text\\par}"
                            "{\\footer Footer text}{\\*\\shppict{\\pict\\pngblip 89504e47}}{\\pict\\wmetafile8 0102}"
                            "{\\object\\objemb{\\*\\objdata 0105}{\\result Shown result}}\\pard Visible {\\b bold} text\\par "
                            "{\\field{\\*\\fldinst HYPERLINK \"http://x\"}{\\fldrslt link text}}\\line end\\emdash ok"
                            "\\bin3 {}x after}"),
             QStringLiteral("Shown resultVisible bold text\nlink text\nend—ok after"));
    // \fromtext honours \htmlrtf; ordinary RTF does not have it.
    QCOMPARE(rtfToPlainText("{\\rtf1\\fromtext a\\htmlrtf b\\htmlrtf0 c}"), QStringLiteral("ac"));
    QCOMPARE(rtfToPlainText("{\\rtf1 \\lquote q\\rquote  \\ldblquote d\\rdblquote \\bullet\\endash}"),
             QStringLiteral("‘q’ “d”•–"));
    QCOMPARE(rtfToPlainText(specRaw1()), QStringLiteral("hello world"));
}

void TestMailMsg::rtfPlainTextHostileInput()
{
    QCOMPARE(rtfToPlainText("not rtf"), QString());
    QCOMPARE(rtfToPlainText(QByteArray()), QString());
    // Too deep: the text before the limit is kept.
    const QByteArray deep = "{\\rtf1 ok" + QByteArray(1000, '{') + "x" + QByteArray(1000, '}') + "}";
    QCOMPARE(rtfToPlainText(deep), QStringLiteral("ok"));
    QCOMPARE(rtfToPlainText("{\\rtf1 a\\'"), QStringLiteral("a"));
    QCOMPARE(rtfToPlainText("{\\rtf1 a}}}}{b"), QStringLiteral("a"));
}
