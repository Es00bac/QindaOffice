// SPDX-License-Identifier: GPL-3.0-or-later
// The HTML mail sanitizer, its tokenizer and the plain-text converter (M64):
// a hostile corpus, Outlook- and Gmail-shaped mail, the cid/inline image
// contract with QTextDocument, and timed pathological inputs. Headless:
// QT_QPA_PLATFORM=offscreen; Qt Gui only to lay the output out.

#include "plain_text_html.h"
#include "tst_mail_html_samples.h"
#include "tst_mail_html_support.h"

#include <QElapsedTimer>
#include <QTest>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextTable>

using namespace QindaOffice::Mail;
using namespace Qt::StringLiterals;

using namespace MailHtmlTest;

class TestMailHtml : public QObject
{
    Q_OBJECT

private slots:
    void tokenizerTagsAndAttributes()
    {
        QCOMPARE(tokenDump(u"<IMG/src=\"x\"/onerror=alert(1) Alt = 'a b' checked data-x=>"_s),
                 u"S(img src=[x] onerror=[alert(1)] alt=[a b] checked=[] data-x=[])"_s);
        QCOMPARE(tokenDump(u"<img/src=x/onerror=alert(1)><br/><p a=1 a=2></P >"_s),
                 u"S(img src=[x/onerror=alert(1)])S(br/)S(p a=[1] a=[2])E(p)"_s);
        QCOMPARE(tokenDump(u"<a href='x' =y \"q\"=z>t</a title=\">\">"_s), u"S(a href=[x] =y=[] \"q\"=[z])T(t)E(a)"_s);
    }

    void tokenizerCharacterReferences()
    {
        QCOMPARE(decodeHtml(u"&amp;&lt;&gt;&quot;&apos;&nbsp;&copy;&copy&COPY;&Eacute;&hellip;&euro;"_s),
                 u"&<>\"'\u00a0©©©É…€"_s);
        QCOMPARE(decodeHtml(u"&#65;&#x41;&#X41;&#0065&#106avascript"_s), u"AAAAjavascript"_s);
        QCOMPARE(decodeHtml(u"&#0;&#xD800;&#x110000;&#99999999999;&#129;"_s), QString(5, QChar(0xFFFD)));
        QCOMPARE(decodeHtml(u"&#128;&#150;&#159;&#x1F600;"_s), u"€–Ÿ\U0001F600"_s);
        QCOMPARE(decodeHtml(u"&notit; &copy2020 &ampx &bogus; & &#; &#x; &"_s), u"¬it; ©2020 &x &bogus; & &#; &#x; &"_s);
        QCOMPARE(decodeHtml(u"?a=1&copy=2&amp;b=3&lt"_s, true), u"?a=1&copy=2&b=3<"_s);
        QCOMPARE(decodeHtml(u"a\u0000b"_s), u"a\ufffdb"_s);
    }

    void tokenizerCommentsAndLiterals()
    {
        QCOMPARE(tokenDump(u"a<!-- c -- d -->b<!-->c<!--->d<!-- e --!>f<!-- open"_s),
                 u"T(a)C( c -- d )T(b)C()T(c)C()T(d)C( e )T(f)C( open)"_s);
        QCOMPARE(tokenDump(u"a < b <3 <!x> </ y> </> <?pi?> c</"_s),
                 u"T(a < b <3 )C(x)T( )C( y)T( )T( )P(?pi?)T( c</)"_s);
        QCOMPARE(tokenDump(u"<!DOCTYPE html><![CDATA[<b>]]>x"_s), u"D(DOCTYPE html)X(<b>)T(x)"_s);
    }

    void tokenizerRawText()
    {
        QCOMPARE(tokenDump(u"<script>var s = \"</script\"; x</SCRIPT ><title>a &amp; b</title><style>p{}</style x>"_s),
                 u"S(script)R(var s = \"</script\"; x)E(script)S(title)R(a & b)E(title)S(style)R(p{})E(style)"_s);
        QCOMPARE(tokenDump(u"<textarea>a<b></textareax>"_s), u"S(textarea)R(a<b></textareax>)"_s);
        QCOMPARE(tokenDump(u"<plaintext><b>x</plaintext>"_s), u"S(plaintext)R(<b>x</plaintext>)"_s);
        QCOMPARE(tokenDump(u"<script></\u017fcript>"_s), u"S(script)R(</\u017fcript>)"_s);
    }

    void tokenizerDropsTagsCutByTheEnd()
    {
        QCOMPARE(tokenDump(u"ok<img src=\"a onerror=x"_s), u"T(ok)"_s);
        QCOMPARE(tokenDump(u"ok<a href=x"_s), u"T(ok)"_s);
        QCOMPARE(tokenDump(u"ok</a"_s), u"T(ok)"_s);
    }

    void keepsSafeMarkup()
    {
        const SanitizedHtml r = clean(u"<P CLASS=x>Hello <B>bold</B> <a href='https://example.org/?a=1&amp;b=2'>link</a></P>"_s);
        QCOMPARE(r.html, u"<html><head></head><body><p class=\"x\">Hello <b>bold</b> "
                         "<a href=\"https://example.org/?a=1&amp;b=2\">link</a></p></body></html>"_s);
        QCOMPARE(r.removedElements, 0);
        QCOMPARE(r.removedAttributes, 0);
    }

    void reescapesTextAndAttributes()
    {
        const SanitizedHtml r = clean(u"<p title='a\"b<c>&amp;d'>&lt;script&gt; &amp; &#169; &#150; &bogus; &amp</p>"_s);
        QCOMPARE(bodyOf(r), u"<p title=\"a&quot;b&lt;c&gt;&amp;d\">&lt;script&gt; &amp; © – &amp;bogus; &amp;</p>"_s);
        QCOMPARE(plainOf(r.html), u"<script> & © – &bogus; &"_s);
    }

    void hostileCorpus_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("forbidden");
        QTest::addColumn<QString>("expected");
        for (const MailHtmlSamples::HostileCase &c : MailHtmlSamples::kHostile)
            QTest::newRow(c.name) << QString::fromUtf8(c.html) << QString::fromUtf8(c.forbidden) << QString::fromUtf8(c.expected);
    }

    void hostileCorpus()
    {
        QFETCH(QString, html);
        QFETCH(QString, forbidden);
        QFETCH(QString, expected);
        const SanitizedHtml r = clean(html);
        const QString lower = r.html.toLower();
        for (const QString &bad : forbidden.split(u'|', Qt::SkipEmptyParts))
            QVERIFY2(!lower.contains(bad), qPrintable(bad + u" in "_s + r.html));
        QVERIFY2(structureProblem(r.html, false).isEmpty(), qPrintable(structureProblem(r.html, false)));
        QVERIFY2(nonLocalRequests(r.html).isEmpty(), qPrintable(nonLocalRequests(r.html).join(u' ')));
        if (!expected.isEmpty())
            QVERIFY2(plainOf(r.html).contains(expected), qPrintable(r.html));
        const SanitizedHtml allowed = clean(html, true);
        QVERIFY2(structureProblem(allowed.html, true).isEmpty(), qPrintable(structureProblem(allowed.html, true)));
    }

    void removedCounts()
    {
        const SanitizedHtml r = clean(u"<p onclick=\"x()\" title=t>a</p><script>x</script><iframe></iframe>"
                                      "<a href=\"javascript:x\" target=_blank>l</a><meta name=x><o:p>k</o:p>"_s);
        QCOMPARE(r.removedElements, 2);   // script, iframe — not the plain <meta>, not <o:p>
        QCOMPARE(r.removedAttributes, 3); // onclick, the javascript: href, target
        QVERIFY(bodyOf(r).contains(u"<p title=\"t\">a</p>"_s));
        QVERIFY(bodyOf(r).contains(u"<a>l</a>k"_s));
    }

    void allowedLinks()
    {
        const SanitizedHtml r = clean(u"<a href=\"HTTPS://e.example/x\">1</a><a href=\"mailto:a@e.example\">2</a>"
                                      "<a href=\"#top\">3</a><a href=\"//cdn.example/p\">4</a><a href=\"rel/x\">5</a>"_s);
        QCOMPARE(bodyOf(r), u"<a href=\"https://e.example/x\">1</a><a href=\"mailto:a@e.example\">2</a>"
                            "<a href=\"#top\">3</a><a href=\"https://cdn.example/p\">4</a><a>5</a>"_s);
    }

    void styleSheetsFiltered()
    {
        const SanitizedHtml r = clean(u"<style><!-- @import url(http://evil.example/a.css); @media (max-width:600px){.m{color:red}}"
                                      "@font-face{font-family:x;src:url(http://evil.example/f.woff)} p.a > b, .c{color: green; "
                                      "position: absolute; font-size: 99999px} *{margin:0} :not(.x){color:red} --></style><p>t</p>"_s);
        QVERIFY(r.remoteBlocked);
        QVERIFY2(r.html.contains(u"<style>p.a &gt; b, .c { color: green }\n* { margin: 0 }\n</style>"_s), qPrintable(r.html));
        const SanitizedHtml allowed = clean(u"<p style=\"background:url(http://t.example/x.gif)\">t</p>"_s, true);
        QVERIFY(!allowed.remoteBlocked && !allowed.html.contains(u"url"_s));
    }

    void cssValueRules()
    {
        const SanitizedHtml r = clean(u"<p style=\"POSITION:Relative; position: fixed; color :RED !important;"
                                      "width:1e9px; font-family:'a;b', serif; behavior:x; top: -20px; z-index: 99999\">t</p>"_s);
        QCOMPARE(bodyOf(r), u"<p style=\"position: Relative; color: RED !important; font-family: 'a;b', serif; top: -20px\">t</p>"_s);
    }

    void remoteCssOnlySetsBlocked()
    {
        const SanitizedHtml r = clean(u"<p style=\"background-image:url(https://t.example/bg.png)\">t</p>"
                                      "<style>@import \"https://t.example/a.css\";</style>"_s);
        QVERIFY(r.remoteBlocked && r.remoteImages.isEmpty() && !r.html.contains(u"t.example"_s));
        QVERIFY(!clean(u"<p style=\"background:url(cid:x); -qt-fg-texture-cachekey: 7\">t</p>"_s).remoteBlocked);
        QVERIFY(!clean(u"<p style=\"-qt-fg-texture-cachekey: 7\">t</p>"_s).html.contains(u"qt-"_s));
    }

    void styleMediaAndHiddenClasses()
    {
        const SanitizedHtml r = clean(u"<style media=\"print\">p{color:red}</style><style media=\"only screen and "
                                      "(max-width:600px)\">.m{display:none}</style><style media=\"screen\">b{color:blue}</style>"
                                      "<div class=m>x</div>"_s);
        QVERIFY2(r.html.contains(u"<style>b { color: blue }\n</style>"_s) && plainOf(r.html).contains(u'x'), qPrintable(r.html));
    }

    void trackingPixelBlocked()
    {
        QVERIFY(!nonLocalRequests(u"<img src=\"https://t.example/p.gif\">"_s).isEmpty()); // the recorder works
        const SanitizedHtml r = clean(u"<p>Hi</p><img src=\"https://t.example/p.gif\" width=1 height=1>"_s);
        QVERIFY(r.remoteBlocked);
        QCOMPARE(r.remoteImages, QStringList{u"https://t.example/p.gif"_s});
        QVERIFY(!r.html.contains(u"http"_s));
        QCOMPARE(r.removedElements, 0);
    }

    void remoteImagesWhenAllowed()
    {
        const QString html = u"<img src=\"https://t.example/p.gif\" width=1 height=1>"
                             "<img src=\"https://t.example/q.gif\" style=\"width:1px;height:1px\">"
                             "<img src=\"https://e.example/photo.jpg\" width=\"600\" height=\"400\" alt=\"Photo\">"_s;
        const SanitizedHtml r = clean(html, true);
        QVERIFY(!r.remoteBlocked);
        QCOMPARE(r.remoteImages, QStringList{u"https://e.example/photo.jpg"_s}); // the pixels are gone
        QCOMPARE(bodyOf(r), u"<img src=\"https://e.example/photo.jpg\" width=\"600\" height=\"400\" alt=\"Photo\">"_s);
        QCOMPARE(r.removedElements, 2);
    }

    void blockedImageShowsAlt()
    {
        const SanitizedHtml r = clean(u"<a href=\"https://e.example/\"><img src=\"http://e.example/logo.png\" alt=\" ACME  <Corp> \"></a>"_s);
        QCOMPARE(bodyOf(r), u"<a href=\"https://e.example/\"><span>ACME &lt;Corp&gt;</span></a>"_s);
    }

    void remoteImagesDeduplicated()
    {
        const SanitizedHtml r = clean(u"<img src=\"//cdn.example/a.png\"><img src=\" https://cdn.example/a.png\">"
                                      "<img src=\"HTTP://cdn.example/b.png\"><td background=\"https://cdn.example/bg.png\">"_s);
        QCOMPARE(r.remoteImages, (QStringList{u"https://cdn.example/a.png"_s, u"http://cdn.example/b.png"_s}));
        QVERIFY(r.remoteBlocked);
    }

    void cidImages()
    {
        const SanitizedHtml r = clean(u"<img src=\"cid:part1%40example.org\"><img src=\"CID:<part2@example.org>\">"
                                      "<img src=\"cid:part1@example.org\" alt=x><img src=\"cid:\">"_s);
        QCOMPARE(r.cidReferences, (QStringList{u"part1@example.org"_s, u"part2@example.org"_s}));
        QCOMPARE(bodyOf(r), u"<img src=\"cid:part1@example.org\"><img src=\"cid:part2@example.org\">"
                            "<img src=\"cid:part1@example.org\" alt=\"x\">"_s);
        QCOMPARE(r.removedElements, 1);
        QCOMPARE(cidUrl(u"a b\"c&d<e>@x/y"_s), u"cid:a%20b%22c%26d%3Ce%3E@x/y"_s);
        QCOMPARE(inlineImageUrl(7), u"qindamail-inline:7"_s);
    }

    // AGENT-CONTRACT check: what the renderer registers is what the HTML uses.
    void cidAndInlineUrlsResolveInTextDocument()
    {
        const QByteArray png = pngBytes(Qt::red, 40, 30);
        const SanitizedHtml r = clean(u"<p><img src=\"cid:logo%40acme.example\"> <img src=\""_s + dataUrl(png) + u"\"></p>"_s);
        QCOMPARE(r.inlineImages, QList<QByteArray>{png});
        RecordingDocument document;
        document.addResource(QTextDocument::ImageResource, QUrl(cidUrl(u"logo@acme.example"_s)), QImage(40, 30, QImage::Format_ARGB32));
        document.addResource(QTextDocument::ImageResource, QUrl(inlineImageUrl(0)), QImage::fromData(r.inlineImages.at(0)));
        render(document, r.html);
        QVERIFY2(document.requested.isEmpty(), qPrintable(document.requested.join(u' ')));
        int images = 0;
        for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat format = it.fragment().charFormat();
                if (!format.isImageFormat())
                    continue;
                ++images;
                const QVariant image = document.resource(QTextDocument::ImageResource, QUrl(format.toImageFormat().name()));
                QCOMPARE(image.value<QImage>().size(), QSize(40, 30));
            }
        }
        QCOMPARE(images, 2);
    }

    void inlineImages()
    {
        const QByteArray png = pngBytes(Qt::blue);
        QString spaced = dataUrl(png);
        spaced.insert(40, u"\n  "_s);
        const SanitizedHtml r = clean(u"<img src=\"DATA:Image/PNG;Base64,"_s + spaced.mid(22) + u"\"><img src=\""_s + dataUrl(png) + u"\">"_s);
        QCOMPARE(r.inlineImages, (QList<QByteArray>{png, png}));
        QCOMPARE(bodyOf(r), u"<img src=\"qindamail-inline:0\"><img src=\"qindamail-inline:1\">"_s);
    }

    void dataUrlsRejected()
    {
        const QByteArray png = pngBytes(Qt::green);
        const QStringList sources = {dataUrl("<svg onload=alert(1)/>", "image/svg+xml"), dataUrl("<script>x</script>", "text/html"),
                                     u"data:image/png,"_s + QString::fromLatin1(png.toPercentEncoding()), dataUrl("hello world"),
                                     u"data:image/png;base64,!!!!"_s, dataUrl(png).replace(u"image/png"_s, u"image/gif;x=1"_s) + u"%"_s};
        for (const QString &source : sources) {
            const SanitizedHtml r = clean(u"<img src=\""_s + source + u"\">t"_s);
            QVERIFY2(r.inlineImages.isEmpty() && bodyOf(r) == u"t"_s && r.removedElements == 1, qPrintable(source.left(60)));
        }
        HtmlSanitizeOptions small;
        small.maxInlineImageBytes = 16;
        QVERIFY(sanitizeHtml(u"<img src=\""_s + dataUrl(png) + u"\">"_s, small).inlineImages.isEmpty());
        QString many;
        for (int i = 0; i < 300; ++i) // distinct strings, same payload
            many += u"<img src=\"data:image/png;base64,"_s + QString(i, u' ') + QString::fromLatin1(png.toBase64()) + u"\">"_s;
        const SanitizedHtml capped = clean(many);
        QCOMPARE(capped.inlineImages.size(), kMaxInlineImages);
        QCOMPARE(capped.removedElements, 300 - kMaxInlineImages);
    }

    void hiddenContent_data()
    {
        QTest::addColumn<QString>("html");
        QTest::newRow("display none") << u"<div style=\"display:none\">pre</div><p>x</p>"_s;
        QTest::newRow("spacing case important") << u"<div style=\"DISPLAY : NONE !important\">pre</div>x"_s;
        QTest::newRow("mso-hide") << u"<span style='mso-hide:all'>pre <b>bold</b></span>x"_s;
        QTest::newRow("hidden attribute") << u"<div hidden><div>pre</div></div>x"_s;
        QTest::newRow("class from style") << u"<style>.preheader{display:none!important}</style><div class=\"a preheader\">pre</div>x"_s;
        QTest::newRow("id from style") << u"<style>td#ph{display:none}</style><table><tr><td id=ph>pre<td>x</table>"_s;
        QTest::newRow("hidden li") << u"<ul><li style=\"display:none\">pre<li>x</ul>"_s;
        QTest::newRow("hidden p ends at block") << u"<div><p style=\"display:none\">pre<div>x</div></div>"_s;
        QTest::newRow("ancestor end tag") << u"<table><tr><td><div style=\"display:none\">pre</td><td>x</td></tr></table>"_s;
        QTest::newRow("hidden image") << u"<img src=\"cid:a@b\" style=\"display:none\" alt=pre>x"_s;
    }

    void hiddenContent()
    {
        QFETCH(QString, html);
        const SanitizedHtml r = clean(html);
        QVERIFY2(!bodyOf(r).contains(u"pre"_s) && plainOf(r.html).contains(u'x'), qPrintable(r.html));
        QVERIFY(r.removedElements >= 1);
        QVERIFY(structureProblem(r.html, false).isEmpty());
        QVERIFY(clean(u"<div style=\"display:none;display:block\">shown</div>"_s).html.contains(u"shown"_s));
    }

    void unknownSectioningAndMappedElements()
    {
        const SanitizedHtml r = clean(u"<o:p>x</o:p><custom-el data-x=1>y</custom-el><section><header>H</header><article>"
                                      "<strike>s</strike><ins>i</ins><mark>m</mark><del>d</del></article></section>"
                                      "<noscript><p>visible</p></noscript>a<wbr>b</br><button>Go</button>"_s);
        QCOMPARE(bodyOf(r), u"xy<div><div>H</div><div><s>s</s><u>i</u><span>m</span><del>d</del></div></div>"
                            "<p>visible</p>a\u200bb<br>Go"_s);
    }

    void balancesAndRepairsStructure()
    {
        QCOMPARE(bodyOf(clean(u"</div></b>text</p><b>bold"_s)), u"text<b>bold</b>"_s);
        QCOMPARE(bodyOf(clean(u"<ul><li>one<li>two<p>para<p>next</ul>"_s)), u"<ul><li>one</li><li>two<p>para</p><p>next</p></li></ul>"_s);
        QCOMPARE(bodyOf(clean(u"<a href=\"https://a.example/\">one<a href=\"https://b.example/\">two</a></a>"_s)),
                 u"<a href=\"https://a.example/\">one</a><a href=\"https://b.example/\">two</a>"_s);
        QCOMPARE(bodyOf(clean(u"<h1>a<h2>b</h1>c<p>d<table><tr><td>e"_s)),
                 u"<h1>a</h1><h2>b</h2>c<p>d</p><table><tr><td>e</td></tr></table>"_s);
        QCOMPARE(bodyOf(clean(u"<table><tr><td>a<td>b<tr><td>c</table>"_s)),
                 u"<table><tr><td>a</td><td>b</td></tr><tr><td>c</td></tr></table>"_s);
        const SanitizedHtml stray = clean(u"<table><tr>STRAY<td>cell</td></tr></table><td>loose</td>"_s);
        QCOMPARE(bodyOf(stray), u"<table><tr><td>STRAY</td><td>cell</td></tr></table>loose"_s);
        QVERIFY(plainOf(stray.html).contains(u"STRAY"_s));
    }

    void maxTableDepthOption()
    {
        HtmlSanitizeOptions options;
        options.maxTableDepth = 1;
        const QString html = u"<table><tr><td>a<table><tr><td>b</td><td>c</td></tr></table></td></tr></table>"_s;
        QCOMPARE(bodyOf(sanitizeHtml(html, options)), u"<table><tr><td>a<br> b c</td></tr></table>"_s);
    }

    void attributeLimit()
    {
        QString html = u"<p"_s;
        for (int i = 0; i < 300; ++i)
            html += u" a"_s + QString::number(i);
        html += u">x"_s;
        HtmlTokenizer tokenizer(html);
        HtmlToken token;
        QVERIFY(tokenizer.next(token));
        QCOMPARE(token.attributes.size(), HtmlTokenizer::kMaxAttributes);
        QCOMPARE(token.droppedAttributes, 300 - HtmlTokenizer::kMaxAttributes);
        QCOMPARE(clean(html).removedAttributes, 300);
    }

    void titleAndSkeleton()
    {
        const SanitizedHtml r = clean(u"<!DOCTYPE html><html><head><title> My \n Title </title></head>\n<body bgcolor=\"#ffeeee\" onload=x>\n<p>x</p>\n</body></html>"_s);
        QCOMPARE(r.title, u"My Title"_s);
        QCOMPARE(r.html, u"<html><head></head><body bgcolor=\"#ffeeee\"><p>x</p>\n</body></html>"_s);
    }

    void attributeValuesBounded()
    {
        const SanitizedHtml r = clean(u"<table width=99999999 border=5000 cellpadding=-1><tr><td colspan=100000 rowspan=0 "
                                      "height=\"150%\" valign=\"top;x\">a</td></tr></table><font size=+2 color=\"#123456\" face=\"Arial\">f</font>"_s);
        QCOMPARE(bodyOf(r), u"<table width=\"8192\" border=\"1000\"><tr><td rowspan=\"1\" height=\"100%\" colspan=\"64\">a</td></tr></table>"
                            "<font size=\"+2\" color=\"#123456\" face=\"Arial\">f</font>"_s);
    }

    void outlookSample()
    {
        const SanitizedHtml r = clean(QString::fromUtf8(MailHtmlSamples::kOutlook));
        QVERIFY2(structureProblem(r.html, false).isEmpty(), qPrintable(structureProblem(r.html, false)));
        QVERIFY(r.html.contains(u"p.MsoNormal, li.MsoNormal, div.MsoNormal { margin: 0in; font-size: 11.0pt; font-family: &quot;Calibri&quot;,sans-serif }"_s));
        for (const QString &gone : {u"font-face"_s, u"@page"_s, u"behavior"_s, u"Cambria"_s, u"shapedefaults"_s, u"vlink"_s, u"Picture"_s})
            QVERIFY2(!r.html.contains(gone), qPrintable(gone));
        QCOMPARE(r.cidReferences, QStringList{u"image001.png@01DA1234.56789AB0"_s});
        QVERIFY(r.html.contains(u"<body lang=\"EN-US\" style=\"word-wrap: break-word\">"_s));
        const QString plain = plainOf(r.html);
        for (const QString &line : {u"Hi Ada,"_s, u"The quarterly numbers are attached – see the chart below."_s,
                                    u"Revenue"_s, u"€1.2M"_s, u"Regards,\nBob"_s})
            QVERIFY2(plain.contains(line), qPrintable(plain));
        QVERIFY(nonLocalRequests(r.html).isEmpty());
    }

    void gmailSample()
    {
        const SanitizedHtml r = clean(QString::fromUtf8(MailHtmlSamples::kGmail));
        QVERIFY(r.html.contains(u"<div dir=\"ltr\">Thanks, that works for me."_s));
        QVERIFY(r.html.contains(u"<blockquote class=\"gmail_quote\" style=\"margin: 0px 0px 0px 0.8ex; "
                                "border-left: 1px solid rgb(204,204,204); padding-left: 1ex\">"_s));
        QVERIFY(r.html.contains(u"<a href=\"mailto:bob@example.org\">bob@example.org</a>"_s));
        QVERIFY(r.remoteBlocked);
        QCOMPARE(r.remoteImages, QStringList{u"https://ci3.example.com/meips/track.gif"_s});
        const QString plain = plainOf(r.html);
        QVERIFY(plain.contains(u"On Mon, 5 Oct 2026 at 09:12, Bob Example <bob@example.org> wrote:"_s));
        QVERIFY(plain.contains(u"Does Tuesday work?"_s));
        QVERIFY(structureProblem(r.html, false).isEmpty() && nonLocalRequests(r.html).isEmpty());
    }

    void deepNestingIsFastAndBounded()
    {
        const QString html = u"<div>"_s.repeated(100000) + u"x"_s + u"</div>"_s.repeated(100000) + u"<p>after</p>"_s;
        QElapsedTimer timer;
        timer.start();
        const SanitizedHtml r = clean(html);
        QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(r.html.count(u"<div>"_s), kMaxOpenElements);
        QVERIFY(bodyOf(r).endsWith(u"</div><p>after</p>"_s));
        QVERIFY(structureProblem(r.html, false).isEmpty());
    }

    void pathologicalInputsAreFast_data()
    {
        QTest::addColumn<QString>("html");
        const QString mb(1 << 20, u'x');
        QTest::newRow("1 MB of <") << QString(1 << 20, u'<');
        QTest::newRow("unclosed quote") << u"<a title=\""_s + mb;
        QTest::newRow("unclosed comment") << u"<p>x</p><!--"_s + mb;
        QTest::newRow("unclosed script") << u"<script>"_s + mb;
        QTest::newRow("many attributes") << u"<p "_s + u"a "_s.repeated(500000) + u">x"_s;
        QTest::newRow("quote soup") << u"<a x='"_s.repeated(200000);
        QTest::newRow("references") << u"&#"_s.repeated(300000) + u"&amp"_s.repeated(200000);
        QTest::newRow("tag name soup") << u"<p"_s.repeated(400000);
        QTest::newRow("end tag soup") << u"</"_s.repeated(400000) + u"</b>"_s.repeated(200000);
        QTest::newRow("nested tables") << u"<table><tr><td>"_s.repeated(50000);
        QTest::newRow("list item walks") << u"<ul><li><ul>"_s + u"<span>"_s.repeated(250) + u"<li></li>"_s.repeated(20000);
        QTest::newRow("hidden elements") << u"<b hidden>x</b>"_s.repeated(40000);
        QTest::newRow("style rules") << u"<style>"_s + u"a{color:red}"_s.repeated(40000) + u"/*"_s + mb;
        QTest::newRow("css declarations") << u"<p style=\""_s + u"color:red;"_s.repeated(40000) + u"\">x</p>"_s;
        QTest::newRow("cells") << u"<table>"_s + u"<tr><td>a"_s.repeated(40000);
    }

    void pathologicalInputsAreFast()
    {
        QFETCH(QString, html);
        QElapsedTimer timer;
        timer.start();
        const SanitizedHtml r = clean(html);
        QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed()) + u" ms"_s));
        QVERIFY2(structureProblem(r.html, false).isEmpty(), qPrintable(structureProblem(r.html, false)));
    }

    void plainTextPathologicalInputsAreFast()
    {
        for (const QString &text : {QString(1 << 19, u'@'), u"a@"_s.repeated(250000), u"http://"_s.repeated(50000),
                                    QString(1 << 19, u'(') + u"https://x.example"_s + QString(1 << 19, u')'), u"www.a"_s.repeated(100000)}) {
            QElapsedTimer timer;
            timer.start();
            const QString html = plainTextToHtml(text);
            QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));
            QVERIFY(!html.isEmpty());
        }
    }

    void truncatesHugeInput()
    {
        const QString html = u"<p>"_s + QString(kMaxHtmlLength, u'a') + u"<script>alert(1)</script>"_s;
        const SanitizedHtml r = clean(html);
        QVERIFY(r.truncated);
        QVERIFY(!r.html.contains(u"script"_s));
        QVERIFY(r.html.endsWith(u"</p></body></html>"_s));
        QVERIFY(!clean(u"<p>x</p>"_s).truncated);
    }

    void nestedTablesAreFlattened()
    {
        const QString html = u"<table><tr><td>cell "_s.repeated(100) + u"deep"_s + u"</td></tr></table>"_s.repeated(100);
        const SanitizedHtml r = clean(html);
        QCOMPARE(r.html.count(u"<table>"_s), HtmlSanitizeOptions().maxTableDepth);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(plainOf(r.html).contains(u"deep"_s));
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
    }

    void tableCellBudget()
    {
        // 40 KB that Qt pads to a 2 001 × 200 grid (17 s, 270 MB) unbounded.
        const QString html = u"<table><tr>"_s + u"<td>x</td>"_s.repeated(200) + u"</tr>"_s
                             + u"<tr><td>y</td></tr>"_s.repeated(2000) + u"</table><p>after</p>"_s;
        const SanitizedHtml r = clean(html);
        QElapsedTimer timer;
        timer.start();
        QTextDocument document;
        document.setHtml(r.html);
        document.setTextWidth(600);
        (void)document.documentLayout()->documentSize();
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
        const QList<QTextFrame *> frames = document.rootFrame()->childFrames();
        QVERIFY(!frames.isEmpty());
        const auto *table = qobject_cast<QTextTable *>(frames.first());
        QVERIFY(table);
        QCOMPARE(table->columns(), kMaxTableColumns);
        QVERIFY(table->rows() * table->columns() <= kMaxTableCells + kMaxTableColumns);
        QCOMPARE(document.toPlainText().count(u'y'), 2000);
        QVERIFY(document.toPlainText().contains(u"after"_s));
    }

    void plainTextEscapes()
    {
        const QString html = plainTextToHtml(u"<script>alert('x') & \"q\"</script>\u0000"_s);
        QCOMPARE(html, u"<div style=\"white-space: pre-wrap;\">\n&lt;script&gt;alert('x') &amp; &quot;q&quot;&lt;/script&gt;\ufffd</div>"_s);
    }

    void plainTextLinks_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("links"); // "href|text" pairs separated by '\n'
        QTest::newRow("balanced parenthesis") << u"see https://example.org/a_(b). next"_s << u"https://example.org/a_(b)|https://example.org/a_(b)"_s;
        QTest::newRow("unbalanced parenthesis") << u"(see https://example.org/x)."_s << u"https://example.org/x|https://example.org/x"_s;
        QTest::newRow("www") << u"go to www.example.org, then"_s << u"https://www.example.org|www.example.org"_s;
        QTest::newRow("query and case") << u"HTTP://Example.org/Q?a=1&b=2!"_s << u"HTTP://Example.org/Q?a=1&b=2|HTTP://Example.org/Q?a=1&b=2"_s;
        QTest::newRow("mail") << u"mail me at a@b.example.org."_s << u"mailto:a@b.example.org|a@b.example.org"_s;
        QTest::newRow("angle brackets") << u"<https://example.org/>, <bob.smith+x@mail.example.com>"_s
                                        << u"https://example.org/|https://example.org/\nmailto:bob.smith+x@mail.example.com|bob.smith+x@mail.example.com"_s;
        QTest::newRow("url with user") << u"https://user@host.example/p"_s << u"https://user@host.example/p|https://user@host.example/p"_s;
        QTest::newRow("no links") << u"http:// or www. or a@b or x@y.c or javascript:alert(1) or xhttp://a.b"_s << QString();
    }

    void plainTextLinks()
    {
        QFETCH(QString, text);
        QFETCH(QString, links);
        const QString html = plainTextToHtml(text);
        QStringList expected;
        for (const QString &pair : links.split(u'\n', Qt::SkipEmptyParts)) {
            const QStringList parts = pair.split(u'|');
            expected << u"<a href=\""_s + parts.at(0).toHtmlEscaped() + u"\">"_s + parts.at(1).toHtmlEscaped() + u"</a>"_s;
        }
        QCOMPARE(html.count(u"<a "_s), expected.size());
        for (const QString &link : std::as_const(expected))
            QVERIFY2(html.contains(link), qPrintable(html));
        QCOMPARE(plainOf(html), text);
    }

    void plainTextQuotes()
    {
        const QString html = plainTextToHtml(u"> quoted\n>> deeper\nplain"_s);
        QCOMPARE(html, u"<div style=\"white-space: pre-wrap;\">\n<span style=\"color: #5f6368;\">&gt; quoted</span>\n"
                       "<span style=\"color: #5f6368;\">&gt;&gt; deeper</span>\nplain</div>"_s);
    }

    void plainTextRoundTrip()
    {
        for (const QString &text : {u"a  b\n\n  c\td"_s, u"\n\nfirst"_s, u"last\n\n"_s, u"only\n"_s, u"\n"_s, QString(),
                                    u"> quote\nhttps://example.org/ and a@b.example.org"_s}) {
            QCOMPARE(plainOf(plainTextToHtml(text)), text);
        }
        QCOMPARE(plainOf(plainTextToHtml(u"x\r\ny\rz"_s)), u"x\ny\nz"_s);
    }
};

QTEST_MAIN(TestMailHtml)
#include "tst_mail_html.moc"
