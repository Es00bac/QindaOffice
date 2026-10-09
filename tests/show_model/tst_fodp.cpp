// SPDX-License-Identifier: GPL-3.0-or-later
// Flat-ODP gates (M62 step 1): the writer's output reads back as the SAME
// model (geometry, rotation, line direction, fills, outlines, run and
// paragraph formatting, bullets, white space, notes, masters, builds); the
// output is structurally valid ODF (every style reference resolves, no
// formatting attributes on text:span / text:p, every length has a unit,
// rotation as rotate+translate); files the pre-M62 writer produced
// (unit-less centimetres, unlinked styles) and LibreOffice's own styled
// output read correctly.

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "odf_dom.h"
#include "presentation.h"

#include <QDir>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSet>
#include <QtTest>

#include <functional>

using namespace QindaOffice::Show;
namespace Odf = QindaOffice::Show::Odf;

class FodpTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripIsLossless();
    void writesValidOdf();
    void readsPreM62Files();
    void readsLibreOfficeStyles();
    void shippedTemplatesLoadAtSize();
};

namespace {

TextRun run(const QString &text, bool bold = false, const QColor &color = Qt::black, int size = 18,
            const QString &family = QString())
{
    TextRun r;
    r.text = text;
    r.bold = bold;
    r.color = color;
    r.pointSize = size;
    r.fontFamily = family;
    return r;
}

TextParagraph paragraph(const QList<TextRun> &runs, int alignment = 0, bool bullet = false, int level = 0)
{
    TextParagraph p;
    p.runs = runs;
    p.alignment = alignment;
    p.bullet = bullet;
    p.listLevel = level;
    return p;
}

Shape shape(const QString &id, ShapeKind kind, const QRectF &rect)
{
    Shape s;
    s.id = id;
    s.kind = kind;
    s.rect = rect;
    return s;
}

Presentation richDeck()
{
    Presentation deck;
    deck.title = QStringLiteral("Q3 <review> & \"plans\"");
    deck.slideSize = QSizeF(960, 540);
    SlideMaster master;
    master.id = QStringLiteral("4c1e-master"); // starts with a digit: not an NCName
    master.name = QStringLiteral("Night");
    master.background = QColor(10, 20, 40);
    master.titleColor = QColor(250, 250, 250);
    master.bodyColor = QColor(200, 200, 210, 128);
    master.fontFamily = QStringLiteral("DejaVu Sans");
    deck.masters.append(master);

    Slide &one = deck.addSlide(QStringLiteral("Rotations"));
    one.master = master.id;
    one.transition = QStringLiteral("zoom");
    one.notes = QStringLiteral("First line\n  indented second <b>\nthird");
    Shape tilted = shape(QStringLiteral("tilted"), ShapeKind::Rectangle, QRectF(100.25, 80.5, 200, 90));
    tilted.rotation = 30;
    tilted.fill = QColor(255, 0, 0, 100);
    tilted.stroke = QColor(0, 0, 255);
    tilted.strokeWidth = 2.25;
    tilted.paragraphs = {paragraph({run(QStringLiteral("  two leading, two  inner, trailing  "))})};
    one.shapes.append(tilted);
    Shape oval = shape(QStringLiteral("oval"), ShapeKind::Ellipse, QRectF(400, 300, 120.125, 60));
    oval.rotation = 330;
    oval.fill = QColor(0, 160, 80);
    oval.strokeWidth = 0;
    oval.buildStep = 2;
    oval.animation = ShapeAnimation::FlyInLeft;
    oval.animationMs = 900;
    one.shapes.append(oval);
    // Lines in all four directions, an arrow pointing back up-left.
    one.shapes.append(shape(QStringLiteral("se"), ShapeKind::Line, QRectF(10, 10, 100, 50)));
    one.shapes.append(shape(QStringLiteral("nw"), ShapeKind::Arrow, QRectF(300, 200, -120, -80)));
    one.shapes.append(shape(QStringLiteral("ne"), ShapeKind::Line, QRectF(500, 400, 90, -70)));
    Shape sw = shape(QStringLiteral("sw"), ShapeKind::Arrow, QRectF(700, 100, -60, 40));
    sw.rotation = 15;
    sw.stroke = QColor(Qt::darkGreen);
    sw.strokeWidth = 3;
    sw.groupId = QStringLiteral("g1");
    one.shapes.append(sw);

    Slide &two = deck.addSlide(QStringLiteral("Text"));
    two.transition = QStringLiteral("slide");
    Shape box = shape(QStringLiteral("box"), ShapeKind::TextBox, QRectF(40, 40, 600, 400));
    box.strokeWidth = 0;
    box.groupId = QStringLiteral("g1");
    box.extra.insert(QStringLiteral("note"), QStringLiteral("kept"));
    box.paragraphs = {
        paragraph({run(QStringLiteral("Title"), true, QColor(Qt::darkBlue), 40, QStringLiteral("Liberation Serif"))},
                  int(Qt::AlignHCenter)),
        paragraph({run(QStringLiteral("Left")), run(QStringLiteral(" and "), false, QColor(1, 2, 3, 4)),
                   run(QStringLiteral("tab\there"), false, QColor())},
                  int(Qt::AlignLeft)),
        paragraph({run(QStringLiteral("Right with a break") + QChar(QChar::LineSeparator) + QStringLiteral("then more"))},
                  int(Qt::AlignRight)),
        paragraph({run(QStringLiteral("Justified"))}, int(Qt::AlignJustify)),
        paragraph({run(QStringLiteral("Point one"))}, 0, true, 0),
        paragraph({run(QStringLiteral("Point one.one"))}, 0, true, 1),
        paragraph({run(QStringLiteral("Point one.one.one"))}, 0, true, 2),
        paragraph({run(QStringLiteral("Point one.two"))}, 0, true, 1),
        paragraph({run(QStringLiteral("Point two"))}, 0, true, 0),
        paragraph({run(QStringLiteral("level without a bullet"))}, 0, false, 2),
        paragraph({}),
        paragraph({run(QStringLiteral("after an empty paragraph")), run(QString())}),
    };
    TextRun italic = run(QStringLiteral("italic"));
    italic.italic = true;
    box.paragraphs[3].runs.append(italic);
    two.shapes.append(box);
    Shape picture = shape(QStringLiteral("pic"), ShapeKind::Image, QRectF(650, 50, 100, 80));
    picture.imagePng = QByteArray("\x89PNG\r\n\x1a\nnot really", 18);
    two.shapes.append(picture);
    return deck;
}

QString attrKey(const char16_t *ns, const char *local)
{
    return QLatin1Char('{') + QString::fromUtf16(ns) + QLatin1Char('}') + QLatin1String(local);
}

} // namespace

void FodpTest::roundTripIsLossless()
{
    const Presentation deck = richDeck();
    QString error;
    const QByteArray fodp = writeFodp(deck, &error);
    QVERIFY2(!fodp.isEmpty(), qPrintable(error));
    const Presentation back = readFodp(fodp, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slides.size(), deck.slides.size());
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        for (qsizetype s = 0; s < deck.slides.at(i).shapes.size(); ++s) {
            const Shape &was = deck.slides.at(i).shapes.at(s);
            const Shape &is = back.slides.at(i).shapes.at(s);
            QCOMPARE(is.id, was.id);
            QCOMPARE(is.rect, was.rect);
            QCOMPARE(is.rotation, was.rotation);
            QCOMPARE(is.paragraphs, was.paragraphs);
            QCOMPARE(is, was);
        }
        QCOMPARE(back.slides.at(i), deck.slides.at(i));
    }
    QCOMPARE(back.masters, deck.masters);
    QCOMPARE(back, deck);
    // Writing the read-back deck changes nothing.
    QCOMPARE(writeFodp(back), fodp);
}

void FodpTest::writesValidOdf()
{
    const QByteArray fodp = writeFodp(richDeck());
    QString error;
    const Odf::OdfNodePtr root = Odf::parseOdf(fodp, &error);
    QVERIFY2(root, qPrintable(error));
    QCOMPARE(root->attr(Odf::kOffice, u"version"), QStringLiteral("1.3"));

    QSet<QString> styles; // family/name
    QSet<QString> lists;
    QSet<QString> markers;
    QSet<QString> masterPages;
    const std::function<void(const Odf::OdfNode &)> collect = [&](const Odf::OdfNode &n) {
        if (n.is(Odf::kStyle, u"style")) {
            styles.insert(n.attr(Odf::kStyle, u"family") + QLatin1Char('/') + n.attr(Odf::kStyle, u"name"));
        } else if (n.is(Odf::kText, u"list-style")) {
            lists.insert(n.attr(Odf::kStyle, u"name"));
        } else if (n.is(Odf::kDraw, u"marker")) {
            markers.insert(n.attr(Odf::kDraw, u"name"));
        } else if (n.is(Odf::kStyle, u"master-page")) {
            masterPages.insert(n.attr(Odf::kStyle, u"name"));
            // A master-page name is an NCName.
            QVERIFY(!n.attr(Odf::kStyle, u"name").front().isDigit());
        }
        for (const Odf::OdfNode *c : n.elements()) {
            collect(*c);
        }
    };
    collect(*root);
    QVERIFY(markers.contains(QStringLiteral("Arrow")));

    static const QRegularExpression kUnit(QStringLiteral("^-?[0-9.]+(pt|cm|mm|in|pc|px)$"));
    int shapes = 0;
    const std::function<void(const Odf::OdfNode &)> check = [&](const Odf::OdfNode &n) {
        const bool isShape = n.is(Odf::kDraw, u"frame") || n.is(Odf::kDraw, u"custom-shape")
            || n.is(Odf::kDraw, u"line");
        if (isShape && n.attr(Odf::kPresentation, u"class") != u"notes") {
            ++shapes;
            const QString style = n.attr(Odf::kDraw, u"style-name");
            QVERIFY2(styles.contains(QStringLiteral("graphic/") + style), qPrintable(style));
            if (n.hasAttr(Odf::kDraw, u"transform")) {
                QVERIFY(n.attr(Odf::kDraw, u"transform").contains(QLatin1String("translate")));
                QVERIFY(!n.hasAttr(Odf::kSvg, u"x"));
            }
            if (n.is(Odf::kDraw, u"line")) {
                const QString marker = n.attr(Odf::kDraw, u"marker-end");
                QVERIFY(marker.isEmpty() || markers.contains(marker));
            }
        }
        for (const char *length : {"x", "y", "width", "height", "x1", "y1", "x2", "y2"}) {
            const QString value = n.attributes.value(attrKey(Odf::kSvg, length));
            if (!value.isEmpty()) {
                QVERIFY2(kUnit.match(value).hasMatch(), qPrintable(value));
            }
        }
        if (n.is(Odf::kText, u"p") || n.is(Odf::kText, u"span")) {
            for (auto it = n.attributes.cbegin(); it != n.attributes.cend(); ++it) {
                QVERIFY2(!it.key().startsWith(QLatin1Char('{') + QString::fromUtf16(Odf::kFo)), qPrintable(it.key()));
            }
            const QString style = n.attr(Odf::kText, u"style-name");
            if (!style.isEmpty()) {
                const QString family = n.is(Odf::kText, u"p") ? QStringLiteral("paragraph/") : QStringLiteral("text/");
                QVERIFY2(styles.contains(family + style), qPrintable(style));
            }
        }
        if (n.is(Odf::kText, u"list") && n.hasAttr(Odf::kText, u"style-name")) {
            QVERIFY(lists.contains(n.attr(Odf::kText, u"style-name")));
        }
        if (n.is(Odf::kDraw, u"page")) {
            QVERIFY(styles.contains(QStringLiteral("drawing-page/") + n.attr(Odf::kDraw, u"style-name")));
            QVERIFY(masterPages.contains(n.attr(Odf::kDraw, u"master-page-name")));
        }
        for (const Odf::OdfNode *c : n.elements()) {
            check(*c);
        }
    };
    check(*root);
    QCOMPARE(shapes, 8);
}

void FodpTest::readsPreM62Files()
{
    // What the pre-M62 writer produced: unit-less centimetres, a
    // shape-<name> style nobody referenced, fo: attributes on spans and
    // paragraphs, qq-list-N bullets, a clockwise rotate() beside svg:x/y,
    // a master page named by its id, a non-SMIL transition name.
    const QByteArray legacy = R"FODP(<?xml version="1.0" encoding="UTF-8"?>
<office:document office:mimetype="application/vnd.oasis.opendocument.presentation" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:presentation="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0" xmlns:smil="urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0" xmlns:qq="https://qindaqt.example.org/qqo">
    <office:automatic-styles>
        <style:page-layout style:name="Default">
            <style:page-layout-properties fo:page-width="25.40000cm" fo:page-height="14.28750cm"/>
        </style:page-layout>
        <style:style style:name="trans-s1" style:family="drawing-page">
            <style:drawing-page-properties smil:type="zoom"/>
        </style:style>
        <style:style style:name="shape-r1" style:family="graphic">
            <style:graphic-properties draw:fill="solid" draw:fill-color="#c8dcff" draw:stroke="solid" svg:stroke-color="#000000" svg:stroke-width="1.5pt"/>
        </style:style>
    </office:automatic-styles>
    <office:master-styles>
        <style:master-page style:name="Default" style:page-layout-name="Default"/>
        <style:master-page style:name="m1" style:page-layout-name="Default" qq:master-name="Dark" qq:background="#102030" qq:title-color="#ffffff" qq:body-color="#eeeeee" qq:font="Sans"/>
    </office:master-styles>
    <office:body>
        <office:presentation>
            <draw:page draw:name="s1" draw:master-page-name="m1" draw:style-name="trans-s1" qq:title="Old" qq:layout="title+content">
                <draw:frame draw:name="t1" svg:x="2.54000" svg:y="1.27000" svg:width="20.32000" svg:height="2.54000">
                    <draw:text-box>
                        <text:p fo:text-align="center">
                            <text:span fo:font-weight="bold" fo:color="#003366" fo:font-size="36pt">The Problem</text:span>
                            <text:span fo:font-size="36pt">!</text:span>
                        </text:p>
                        <text:p text:style-name="qq-list-2">
                            <text:span fo:font-size="22pt" style:font-name="Liberation Sans">nested point</text:span>
                        </text:p>
                    </draw:text-box>
                </draw:frame>
                <draw:custom-shape draw:name="r1" svg:x="5.08000" svg:y="5.08000" svg:width="5.08000" svg:height="2.54000" draw:transform="rotate (0.5235987755982988)" qq:build-step="1">
                    <draw:enhanced-geometry draw:type="rectangle" svg:viewBox="0 0 5080 2540"/>
                </draw:custom-shape>
                <presentation:notes>
                    <draw:frame presentation:class="notes" svg:x="0cm" svg:y="0cm" svg:width="1cm" svg:height="1cm">
                        <draw:text-box>
                            <text:p>line one
line two</text:p>
                        </draw:text-box>
                    </draw:frame>
                </presentation:notes>
            </draw:page>
        </office:presentation>
    </office:body>
</office:document>
)FODP";
    QString error;
    const Presentation deck = readFodp(legacy, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(qRound(deck.slideSize.width()), 720);
    QCOMPARE(qRound(deck.slideSize.height()), 405);
    QCOMPARE(deck.masters.size(), 1);
    QCOMPARE(deck.masters.first().id, QStringLiteral("m1"));
    QCOMPARE(deck.masters.first().background, QColor(0x10, 0x20, 0x30));
    QCOMPARE(deck.slides.size(), 1);
    const Slide &slide = deck.slides.first();
    QCOMPARE(slide.master, QStringLiteral("m1"));
    QCOMPARE(slide.transition, QStringLiteral("zoom"));
    QCOMPARE(slide.notes, QStringLiteral("line one\nline two"));
    QCOMPARE(slide.shapes.size(), 2);
    // 2.54 unit-less "cm" is 72 pt — not 2.54 pt.
    const Shape &title = slide.shapes.at(0);
    QCOMPARE(title.kind, ShapeKind::TextBox);
    QVERIFY(qAbs(title.rect.x() - 72) < 0.01 && qAbs(title.rect.width() - 576) < 0.01);
    QCOMPARE(title.paragraphs.size(), 2);
    QCOMPARE(title.paragraphs.at(0).alignment, int(Qt::AlignHCenter));
    // Indentation between the spans is not a space.
    QCOMPARE(title.paragraphs.at(0).runs.size(), 2);
    QCOMPARE(title.paragraphs.at(0).runs.at(0).text, QStringLiteral("The Problem"));
    QVERIFY(title.paragraphs.at(0).runs.at(0).bold);
    QCOMPARE(title.paragraphs.at(0).runs.at(0).color, QColor(0x00, 0x33, 0x66));
    QCOMPARE(title.paragraphs.at(0).runs.at(0).pointSize, 36.0);
    QCOMPARE(title.paragraphs.at(0).runs.at(1).text, QStringLiteral("!"));
    QVERIFY(title.paragraphs.at(1).bullet);
    QCOMPARE(title.paragraphs.at(1).listLevel, 1);
    QCOMPARE(title.paragraphs.at(1).runs.first().fontFamily, QStringLiteral("Liberation Sans"));
    // The unlinked style still colours the rectangle; the old clockwise
    // rotation stays clockwise and the box stays where svg:x/y put it.
    const Shape &box = slide.shapes.at(1);
    QCOMPARE(box.kind, ShapeKind::Rectangle);
    QCOMPARE(box.fill, QColor(0xc8, 0xdc, 0xff));
    QCOMPARE(box.strokeWidth, 1.5);
    QVERIFY(qAbs(box.rotation - 30) < 1e-9);
    QVERIFY(qAbs(box.rect.x() - 144) < 0.01 && qAbs(box.rect.height() - 72) < 0.01);
    QCOMPARE(box.buildStep, 1);
}

void FodpTest::readsLibreOfficeStyles()
{
    const QByteArray lo = R"FODP(<?xml version="1.0" encoding="UTF-8"?>
<office:document office:version="1.3" office:mimetype="application/vnd.oasis.opendocument.presentation" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:presentation="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0" xmlns:smil="urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0">
 <office:styles>
  <style:default-style style:family="graphic"><style:graphic-properties svg:stroke-color="#3465a4" draw:fill-color="#729fcf"/></style:default-style>
  <style:style style:name="standard" style:family="graphic"><style:graphic-properties draw:stroke="solid" svg:stroke-width="0cm" draw:fill="solid"/><style:text-properties fo:font-size="18pt"/></style:style>
  <draw:marker draw:name="Arrow" svg:viewBox="0 0 20 30" svg:d="M10 0l-10 30h20z"/>
 </office:styles>
 <office:automatic-styles>
  <style:page-layout style:name="PM0"><style:page-layout-properties fo:page-width="21cm" fo:page-height="29.7cm"/></style:page-layout>
  <style:page-layout style:name="PM1"><style:page-layout-properties fo:page-width="28cm" fo:page-height="15.75cm"/></style:page-layout>
  <style:style style:name="dp1" style:family="drawing-page"><style:drawing-page-properties presentation:transition-type="automatic" smil:type="pushWipe" smil:subtype="fromRight" presentation:visibility="hidden"/></style:style>
  <style:style style:name="gr1" style:family="graphic" style:parent-style-name="standard"><style:graphic-properties draw:fill-color="#ff0000" svg:stroke-color="#00ff00" svg:stroke-width="0.1cm"/></style:style>
  <style:style style:name="gr2" style:family="graphic" style:parent-style-name="standard"><style:graphic-properties draw:marker-start="Arrow" draw:fill="none"/></style:style>
  <style:style style:name="P1" style:family="paragraph"><style:paragraph-properties fo:text-align="center"/><style:text-properties fo:font-size="24pt"/></style:style>
  <style:style style:name="T1" style:family="text"><style:text-properties fo:font-weight="bold" fo:color="#0000ff" style:font-name="Liberation Sans"/></style:style>
  <style:style style:name="T2" style:family="text"><style:text-properties fo:font-style="italic"/></style:style>
  <text:list-style style:name="L1"><text:list-level-style-bullet text:level="1" text:bullet-char="&#x25CF;"/><text:list-level-style-bullet text:level="2" text:bullet-char=""/></text:list-style>
 </office:automatic-styles>
 <office:master-styles><style:master-page style:name="Default" style:page-layout-name="PM1"/></office:master-styles>
 <office:body><office:presentation>
  <draw:page draw:name="page1" draw:style-name="dp1" draw:master-page-name="Default">
   <draw:custom-shape draw:style-name="gr1" svg:width="4cm" svg:height="2cm" draw:transform="rotate (0.523598775598299) translate (5cm 5cm)">
    <text:p text:style-name="P1"><text:span text:style-name="T1">Bold</text:span> <text:span text:style-name="T2">italic</text:span></text:p>
    <draw:enhanced-geometry svg:viewBox="0 0 21600 21600" draw:type="ooxml-ellipse"/>
   </draw:custom-shape>
   <draw:line draw:style-name="gr2" svg:x1="1cm" svg:y1="1cm" svg:x2="3cm" svg:y2="2cm"/>
   <draw:frame draw:style-name="gr1" draw:text-style-name="P1" svg:x="1in" svg:y="72pt" svg:width="10mm" svg:height="2cm" presentation:class="outline">
    <draw:text-box><text:list text:style-name="L1"><text:list-item><text:p>Top</text:p><text:list><text:list-item><text:p>Sub without label</text:p></text:list-item></text:list></text:list-item></text:list></draw:text-box>
   </draw:frame>
   <draw:g>
    <draw:rect draw:style-name="gr1" svg:x="1cm" svg:y="10cm" svg:width="1cm" svg:height="1cm"/>
    <draw:ellipse draw:style-name="gr1" svg:x="3cm" svg:y="10cm" svg:width="1cm" svg:height="1cm"/>
   </draw:g>
   <draw:custom-shape draw:style-name="gr1" svg:x="5cm" svg:y="10cm" svg:width="2cm" svg:height="1cm"><draw:enhanced-geometry svg:viewBox="0 0 21600 21600" draw:type="right-arrow"/></draw:custom-shape>
   <draw:custom-shape draw:style-name="gr1" svg:x="8cm" svg:y="10cm" svg:width="2cm" svg:height="1cm"><draw:enhanced-geometry svg:viewBox="0 0 21600 21600" draw:type="ooxml-chevron"/></draw:custom-shape>
   <presentation:notes><draw:frame presentation:class="notes"><draw:text-box><text:p>One<text:s/>two</text:p><text:p>Three</text:p></draw:text-box></draw:frame></presentation:notes>
  </draw:page>
 </office:presentation></office:body>
</office:document>
)FODP";
    QString error;
    const Presentation deck = readFodp(lo, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // The slide page layout, not the notes one.
    QVERIFY(qAbs(deck.slideSize.width() - 28 * 72 / 2.54) < 0.01);
    QCOMPARE(deck.slides.size(), 1);
    const Slide &slide = deck.slides.first();
    QCOMPARE(slide.transition, QStringLiteral("slide"));
    QCOMPARE(slide.notes, QStringLiteral("One two\nThree"));
    QVERIFY(slide.hidden);
    QCOMPARE(slide.shapes.size(), 7);

    // rotate(π/6) is 30° COUNTER-clockwise: the model's clockwise 330°,
    // centred where LibreOffice centres it (5.232 cm + 2 cm, 3.866 cm + 1 cm).
    const Shape &oval = slide.shapes.at(0);
    QCOMPARE(oval.kind, ShapeKind::Ellipse);
    QVERIFY2(qAbs(oval.rotation - 330) < 1e-6, qPrintable(QString::number(oval.rotation)));
    const qreal cm = 72.0 / 2.54;
    QVERIFY(qAbs(oval.rect.center().x() - 7.232 * cm) < 0.1 && qAbs(oval.rect.center().y() - 4.866 * cm) < 0.1);
    QVERIFY(qAbs(oval.rect.width() - 4 * cm) < 1e-6 && qAbs(oval.rect.height() - 2 * cm) < 1e-6);
    // Styles through the parent chain: fill colour from gr1, "solid" from
    // standard; outline from gr1.
    QCOMPARE(oval.fill, QColor(Qt::red));
    QCOMPARE(oval.stroke, QColor(Qt::green));
    QVERIFY(qAbs(oval.strokeWidth - 0.1 * cm) < 1e-6);
    QCOMPARE(oval.paragraphs.size(), 1);
    const TextParagraph &p = oval.paragraphs.first();
    QCOMPARE(p.alignment, int(Qt::AlignHCenter));
    QCOMPARE(p.runs.size(), 3);
    QCOMPARE(p.runs.at(0).text, QStringLiteral("Bold"));
    QVERIFY(p.runs.at(0).bold);
    QCOMPARE(p.runs.at(0).color, QColor(Qt::blue));
    QCOMPARE(p.runs.at(0).pointSize, 24.0);
    QCOMPARE(p.runs.at(0).fontFamily, QStringLiteral("Liberation Sans"));
    QCOMPARE(p.runs.at(1).text, QStringLiteral(" "));
    QVERIFY(!p.runs.at(1).bold);
    QVERIFY(p.runs.at(2).italic);

    // A start marker only: the arrow points back to (1 cm, 1 cm).
    const Shape &arrow = slide.shapes.at(1);
    QCOMPARE(arrow.kind, ShapeKind::Arrow);
    QVERIFY(qAbs(arrow.rect.bottomRight().x() - 1 * cm) < 1e-6 && qAbs(arrow.rect.topLeft().x() - 3 * cm) < 1e-6);
    QCOMPARE(arrow.stroke, QColor(0x34, 0x65, 0xa4)); // the family default
    QCOMPARE(arrow.strokeWidth, 0.75);                 // a 0 width is a hairline

    const Shape &outline = slide.shapes.at(2);
    QCOMPARE(outline.kind, ShapeKind::TextBox);
    QVERIFY(qAbs(outline.rect.x() - 72) < 1e-9 && qAbs(outline.rect.width() - 10 * 72 / 25.4) < 1e-9);
    QCOMPARE(outline.paragraphs.size(), 2);
    QVERIFY(outline.paragraphs.at(0).bullet);
    QCOMPARE(outline.paragraphs.at(0).runs.first().pointSize, 24.0);
    QCOMPARE(outline.paragraphs.at(0).alignment, int(Qt::AlignHCenter));
    QCOMPARE(outline.paragraphs.at(1).listLevel, 1);
    QVERIFY(!outline.paragraphs.at(1).bullet); // an empty bullet character

    QCOMPARE(slide.shapes.at(3).kind, ShapeKind::Rectangle);
    QCOMPARE(slide.shapes.at(4).kind, ShapeKind::Ellipse);
    QVERIFY(!slide.shapes.at(3).groupId.isEmpty());
    QCOMPARE(slide.shapes.at(3).groupId, slide.shapes.at(4).groupId);
    // LibreOffice's arrow and its spelling of an Office chevron keep their outlines.
    QCOMPARE(slide.shapes.at(5).kind, ShapeKind::AutoShape);
    QCOMPARE(slide.shapes.at(5).geometry, QStringLiteral("rightArrow"));
    QCOMPARE(slide.shapes.at(6).geometry, QStringLiteral("chevron"));
}

void FodpTest::shippedTemplatesLoadAtSize()
{
    // The templates were written by the pre-M62 writer (unit-less cm) and
    // loaded at 3.5 % size; they are regenerated and must stay right.
    const QDir dir(QStringLiteral(QQO_SHOW_TEMPLATE_DIR));
    const QStringList files = dir.entryList({QStringLiteral("*.fodp")}, QDir::Files);
    QVERIFY(files.size() >= 2);
    for (const QString &name : files) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QString error;
        const Presentation deck = readFodp(bytes, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(deck.slideSize, QSizeF(720, 405));
        QVERIFY(!deck.slides.isEmpty());
        for (const Slide &slide : deck.slides) {
            for (const Shape &shape : slide.shapes) {
                QVERIFY2(shape.rect.width() > 100 && shape.rect.height() > 20, qPrintable(name));
                QVERIFY(QRectF(QPointF(0, 0), deck.slideSize).contains(shape.rect));
            }
        }
        // Every length in the file carries its unit.
        static const QRegularExpression kBare(QStringLiteral("svg:(x|y|width|height)=\"-?[0-9.]+\""));
        QVERIFY2(!kBare.match(QString::fromUtf8(bytes)).hasMatch(), qPrintable(name));
    }
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    FodpTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_fodp.moc"
