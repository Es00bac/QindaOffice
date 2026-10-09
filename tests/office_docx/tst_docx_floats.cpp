// SPDX-License-Identifier: GPL-3.0-or-later
// M77 gates for floating pictures in office_docx (D-086): Word's wp:anchor
// read as a picture's wrap and position — Square, Tight, Top and Bottom as
// Qt floating frames the text flows around, Behind Text drawn where its
// position puts it on the page; an untouched anchor written back as Word
// wrote it; each wrap written as Word writes it, schema-valid and read back
// equal; a moved picture changing only its position; In Line ↔ Square;
// LibreOffice rendering anchored pictures.

#include "docx_fixture.h"
#include "docx_float.h"
#include "docx_properties.h"
#include "docx_roundtrip.h"
#include "docx_text.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFrame>

using namespace QindaOffice;
using Test::DocxFixture;

namespace {

QString anchor(const QString &attributes, const QString &positions, const QString &wrap, const QString &name)
{
    return QStringLiteral(
               "<w:r><w:drawing><wp:anchor %1 simplePos=\"0\" relativeHeight=\"251659264\" locked=\"0\" layoutInCell=\"1\""
               " allowOverlap=\"1\"><wp:simplePos x=\"0\" y=\"0\"/>%2<wp:extent cx=\"1905000\" cy=\"952500\"/>"
               "<wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>%3<wp:docPr id=\"7\" name=\"%4\" descr=\"A red box\"/>"
               "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr><a:graphic>"
               "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr>"
               "<pic:cNvPr id=\"0\" name=\"red.png\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"rIdImg\"/>"
               "<a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
               "<a:ext cx=\"1905000\" cy=\"952500\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
               "</pic:pic></a:graphicData></a:graphic></wp:anchor></w:drawing></w:r>")
        .arg(attributes, positions, wrap, name);
}

const QString kSquare = anchor(QStringLiteral("distT=\"0\" distB=\"0\" distL=\"114300\" distR=\"114300\" behindDoc=\"0\""),
                               QStringLiteral("<wp:positionH relativeFrom=\"column\"><wp:posOffset>952500</wp:posOffset></wp:positionH>"
                                              "<wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>95250</wp:posOffset></wp:positionV>"),
                               QStringLiteral("<wp:wrapSquare wrapText=\"bothSides\"/>"), QStringLiteral("Square"));
const QString kBehind = anchor(QStringLiteral("distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\" behindDoc=\"1\""),
                               QStringLiteral("<wp:positionH relativeFrom=\"margin\"><wp:align>center</wp:align></wp:positionH>"
                                              "<wp:positionV relativeFrom=\"page\"><wp:posOffset>914400</wp:posOffset></wp:positionV>"),
                               QStringLiteral("<wp:wrapNone/>"), QStringLiteral("Behind"));
const QString kTopBottom = anchor(QStringLiteral("distT=\"0\" distB=\"0\" distL=\"114300\" distR=\"114300\" behindDoc=\"0\""),
                                  QStringLiteral("<wp:positionH relativeFrom=\"column\"><wp:align>right</wp:align></wp:positionH>"
                                                 "<wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>0</wp:posOffset></wp:positionV>"),
                                  QStringLiteral("<wp:wrapTopAndBottom/>"), QStringLiteral("TopBottom"));
const QString kTight = anchor(QStringLiteral("distT=\"0\" distB=\"0\" distL=\"114300\" distR=\"114300\" behindDoc=\"0\""),
                              QStringLiteral("<wp:positionH relativeFrom=\"column\"><wp:posOffset>0</wp:posOffset></wp:positionH>"
                                             "<wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>0</wp:posOffset></wp:positionV>"),
                              QStringLiteral("<wp:wrapTight wrapText=\"bothSides\"><wp:wrapPolygon edited=\"1\"><wp:start x=\"0\" y=\"0\"/>"
                                             "<wp:lineTo x=\"0\" y=\"21000\"/><wp:lineTo x=\"21000\" y=\"21600\"/><wp:lineTo x=\"0\" y=\"0\"/>"
                                             "</wp:wrapPolygon></wp:wrapTight>"),
                              QStringLiteral("Tight"));

QString paragraphWith(const QString &drawing, const QString &text)
{
    return QStringLiteral("<w:p>%1<w:r><w:t xml:space=\"preserve\">%2</w:t></w:r></w:p>").arg(drawing, text);
}

QByteArray sourcePackage()
{
    DocxFixture f;
    const QString words = QStringLiteral("Text that flows around the picture. ").repeated(12);
    f.body = paragraphWith(kSquare, words) + paragraphWith(kBehind, words) + paragraphWith(kTopBottom, words)
        + paragraphWith(kTight, words);
    f.media.insert(QStringLiteral("rIdImg"), Test::pngBytes(40, 20));
    return f.build();
}

// Lays the body out as QindaWrite does: Letter, one-inch margins.
void layOut(QTextDocument *body)
{
    body->setPageSize(QSizeF(816, 1056));
    body->setDocumentMargin(96);
    body->documentLayout()->documentSize();
}

QList<int> pictures(const QTextDocument &doc)
{
    QList<int> out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat()) {
                out.append(it.fragment().position());
            }
        }
    }
    return out;
}

QTextCharFormat formatAt(const QTextDocument &doc, int position)
{
    return Docx::characterFormatAt(doc, position);
}

} // namespace

class DocxFloatsTest : public QObject {
    Q_OBJECT
private slots:
    void anchorsReadAsFloatingPictures();
    void untouchedAnchorsKept();
    void eachWrapRoundTrips();
    void positionChangeRewritesTheAnchor();
    void inlineToSquareAndBack();
    void libreOfficeRendersAnchoredPictures();
};

void DocxFloatsTest::anchorsReadAsFloatingPictures()
{
    auto doc = Test::readWord(sourcePackage());
    QVERIFY(doc);
    layOut(&doc->body);
    const QList<int> at = pictures(doc->body);
    QCOMPARE(at.size(), 4);
    const Docx::PictureFloat square = Docx::pictureFloat(formatAt(doc->body, at.at(0)));
    QCOMPARE(square.wrap, QStringLiteral("square"));
    QCOMPARE(square.hRelative, QStringLiteral("column"));
    QCOMPARE(square.hOffset, 100.0);
    QCOMPARE(square.vOffset, 10.0);
    QCOMPARE(square.distRight, 12.0);
    const auto *frame = qobject_cast<const QTextFrame *>(doc->body.objectForFormat(formatAt(doc->body, at.at(0))));
    QVERIFY(frame != nullptr);
    QCOMPARE(frame->frameFormat().position(), QTextFrameFormat::FloatLeft);
    // The picture sits 100 px into the column, 10 px below its line; the
    // text flows to its right.
    const QRectF squareRect = Docx::floatingPictureRect(doc->body, at.at(0));
    QCOMPARE(squareRect.left(), 96.0 + 100.0);
    QCOMPARE(squareRect.size(), QSizeF(200, 100));
    const Docx::PictureFloat behind = Docx::pictureFloat(formatAt(doc->body, at.at(1)));
    QCOMPARE(behind.wrap, QStringLiteral("behind"));
    QCOMPARE(behind.hAlign, QStringLiteral("center"));
    QCOMPARE(behind.vRelative, QStringLiteral("page"));
    QVERIFY(doc->body.objectForFormat(formatAt(doc->body, at.at(1))) == nullptr);
    QCOMPARE(Docx::floatingPictureRect(doc->body, at.at(1)), QRectF(96 + (624 - 200) / 2.0, 96, 200, 100));
    const Docx::PictureFloat topBottom = Docx::pictureFloat(formatAt(doc->body, at.at(2)));
    QCOMPARE(topBottom.wrap, QStringLiteral("topAndBottom"));
    const auto *right = qobject_cast<const QTextFrame *>(doc->body.objectForFormat(formatAt(doc->body, at.at(2))));
    QVERIFY(right != nullptr);
    QCOMPARE(right->frameFormat().position(), QTextFrameFormat::FloatRight);
    QCOMPARE(Docx::pictureFloat(formatAt(doc->body, at.at(3))).wrap, QStringLiteral("tight"));
    QVERIFY(Docx::hasFloatingPictures(doc->body));
    QVERIFY(doc->parts.warnings.isEmpty()); // floating pictures are no longer shown approximately
}

void DocxFloatsTest::untouchedAnchorsKept()
{
    auto doc = Test::readWord(sourcePackage());
    QVERIFY(doc);
    QTextCursor c(&doc->body);
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral(" Edited."));
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
    // Every anchor as Word wrote it — position, extent, wrap, polygon —
    // with only the docPr id and the picture's relationship renumbered.
    for (const QString &source : {kSquare, kBehind, kTopBottom, kTight}) {
        const QString from = source.section(QStringLiteral("<wp:simplePos"), 1).section(QStringLiteral("<wp:docPr"), 0, 0);
        QVERIFY2(xml.contains(QStringLiteral("<wp:simplePos").toUtf8() + from.toUtf8()), qPrintable(from));
    }
    QVERIFY(xml.contains("<wp:anchor distT=\"0\" distB=\"0\" distL=\"114300\" distR=\"114300\" simplePos=\"0\""
                         " relativeHeight=\"251659264\" behindDoc=\"0\" locked=\"0\" layoutInCell=\"1\" allowOverlap=\"1\">")
            || xml.contains("<wp:anchor distT=\"0\" distB=\"0\" distL=\"114300\" distR=\"114300\" behindDoc=\"0\""));
    QCOMPARE(xml.count("descr=\"A red box\""), 4);
}

void DocxFloatsTest::eachWrapRoundTrips()
{
    for (const char *wrap : {"inline", "square", "tight", "through", "topAndBottom", "behind", "inFront"}) {
        QTextDocument body;
        Docx::WordParts parts;
        layOut(&body);
        QTextCursor c(&body);
        c.insertText(QStringLiteral("Before. "));
        QTextImageFormat image;
        image.setName(Docx::addPicture(&body, &parts.media, Test::pngBytes(60, 30, Qt::blue)));
        image.setWidth(120);
        image.setHeight(60);
        const int position = c.position();
        c.insertImage(image);
        c.insertText(QStringLiteral(" After the picture, more words to wrap around it."));
        Docx::PictureFloat layout;
        layout.wrap = QString::fromLatin1(wrap);
        layout.hRelative = QStringLiteral("margin");
        layout.hOffset = 48.25;
        layout.vRelative = QStringLiteral("paragraph");
        layout.vOffset = 12.5;
        layout.distLeft = layout.distRight = 9;
        if (layout.wrap == u"behind" || layout.wrap == u"inFront") {
            layout.hRelative = QStringLiteral("page");
            layout.hAlign = QStringLiteral("right");
            layout.vRelative = QStringLiteral("margin");
            layout.vAlign = QStringLiteral("bottom");
        }
        QCOMPARE(Docx::setPictureFloat(&body, position, layout), position);
        const QByteArray bytes = Docx::docxBytes(body, parts);
        QString problems;
        QVERIFY2(Test::validates(bytes, &problems), qPrintable(QString::fromLatin1(wrap) + QLatin1Char(' ') + problems));
        const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
        if (layout.wrap == u"inline") {
            QVERIFY(xml.contains("<wp:inline") && !xml.contains("<wp:anchor"));
            continue;
        }
        QVERIFY2(xml.contains("<wp:anchor"), wrap);
        auto back = Test::readWord(bytes);
        QVERIFY(back);
        const Docx::PictureFloat read = Docx::pictureFloat(formatAt(back->body, pictures(back->body).value(0)));
        QCOMPARE(read.wrap, layout.wrap);
        QCOMPARE(read.hRelative, layout.hRelative);
        QCOMPARE(read.hAlign, layout.hAlign);
        QCOMPARE(read.vRelative, layout.vRelative);
        QCOMPARE(read.vAlign, layout.vAlign);
        if (layout.hAlign.isEmpty()) {
            QCOMPARE(read.hOffset, layout.hOffset);
            QCOMPARE(read.vOffset, layout.vOffset);
        }
        QCOMPARE(read.distLeft, layout.distLeft);
    }
}

void DocxFloatsTest::positionChangeRewritesTheAnchor()
{
    auto doc = Test::readWord(sourcePackage());
    QVERIFY(doc);
    layOut(&doc->body);
    const int square = pictures(doc->body).at(0);
    const Docx::PictureFloat moved = Docx::movedPictureFloat(doc->body, square, QPointF(48, 24));
    QCOMPARE(moved.hOffset, 148.0);
    QCOMPARE(moved.vOffset, 34.0);
    QCOMPARE(Docx::setPictureFloat(&doc->body, square, moved), square);
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<wp:positionH relativeFrom=\"column\"><wp:posOffset>1409700</wp:posOffset></wp:positionH>"
                          "<wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>323850</wp:posOffset></wp:positionV>"),
             xml.constData());
    QVERIFY(xml.contains("name=\"Square\" descr=\"A red box\""));
    QCOMPARE(xml.count("<wp:wrapSquare wrapText=\"bothSides\"/>"), 1);
}

void DocxFloatsTest::inlineToSquareAndBack()
{
    DocxFixture f;
    f.body = QStringLiteral(
        "<w:p><w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"1905000\" cy=\"952500\"/>"
        "<wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/><wp:docPr id=\"3\" name=\"Logo\" descr=\"Company logo\"/>"
        "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr><a:graphic>"
        "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr>"
        "<pic:cNvPr id=\"0\" name=\"logo.png\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"rIdImg\"/>"
        "<a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
        "<a:ext cx=\"1905000\" cy=\"952500\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
        "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r><w:r><w:t>Caption</w:t></w:r></w:p>");
    f.media.insert(QStringLiteral("rIdImg"), Test::pngBytes(40, 20));
    auto doc = Test::readWord(f.build());
    QVERIFY(doc);
    layOut(&doc->body);
    const int at = pictures(doc->body).at(0);
    const Docx::PictureFloat square = Docx::wrappedPictureFloat(doc->body, at, QStringLiteral("square"));
    QCOMPARE(square.wrap, QStringLiteral("square"));
    QCOMPARE(square.hRelative, QStringLiteral("column"));
    QCOMPARE(Docx::setPictureFloat(&doc->body, at, square), at);
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<wp:wrapSquare wrapText=\"bothSides\"/><wp:docPr id=\"1\" name=\"Logo\" descr=\"Company logo\"/>"),
             xml.constData());
    QVERIFY(!xml.contains("<wp:inline"));
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    const int again = pictures(back->body).at(0);
    QCOMPARE(Docx::pictureFloat(formatAt(back->body, again)).wrap, QStringLiteral("square"));
    Docx::PictureFloat inlineLayout;
    QCOMPARE(Docx::setPictureFloat(&back->body, again, inlineLayout), again);
    const QByteArray inlineBytes = Docx::docxBytes(back->body, back->parts);
    const QByteArray inlineXml = Test::partOf(inlineBytes, QStringLiteral("word/document.xml"));
    QVERIFY(inlineXml.contains("<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"1905000\" cy=\"952500\"/>"));
    QVERIFY(inlineXml.contains("name=\"Logo\" descr=\"Company logo\""));
    QVERIFY2(Test::validates(inlineBytes, &problems), qPrintable(problems));
}

void DocxFloatsTest::libreOfficeRendersAnchoredPictures()
{
    if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) {
        QSKIP("LibreOffice is not installed");
    }
    auto doc = Test::readWord(sourcePackage());
    QVERIFY(doc);
    QTemporaryDir scratch;
    const QString pdf = Test::renderWithLibreOffice(Docx::docxBytes(doc->body, doc->parts), QStringLiteral("m77_anchored_pictures"),
                                                    scratch);
    QVERIFY(!pdf.isEmpty());
    const QString text = Test::pdfText(pdf);
    if (!text.isEmpty()) {
        QVERIFY(text.contains(QStringLiteral("flows around the picture")));
    }
}

QTEST_MAIN(DocxFloatsTest)
#include "tst_docx_floats.moc"
