// SPDX-License-Identifier: GPL-3.0-or-later
// M77 gates for Word's envelopes and labels in office_docx (D-086): an
// envelope as a document of its own (the page is the envelope) and as the
// first section before a letter (its own page size and paper code, the
// letter's page kept), Avery label sheets as Word's fixed-layout table
// (gridCol widths, exact row heights, no borders, the sheet's margins),
// the label products' dimensions fitting their paper, and LibreOffice
// rendering a label sheet.

#include "docx_envelopes.h"
#include "docx_fixture.h"
#include "docx_properties.h"
#include "docx_roundtrip.h"
#include "docx_table_format.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFrame>
#include <QTextTable>

using namespace QindaOffice;
using Test::para;

namespace {

Docx::EnvelopeSpec envelope()
{
    Docx::EnvelopeSpec spec;
    spec.delivery = QStringLiteral("Grace Hopper\n1 Navy Way\nArlington, VA 22202");
    spec.returnAddress = QStringLiteral("Ada Lovelace\n12 St James's Square\nLondon");
    return spec;
}

QString cellText(QTextTable *table, int r, int c)
{
    QTextCursor at = table->cellAt(r, c).firstCursorPosition();
    at.setPosition(table->cellAt(r, c).lastCursorPosition().position(), QTextCursor::KeepAnchor);
    return at.selectedText();
}

} // namespace

class DocxEnvelopesTest : public QObject {
    Q_OBJECT
private slots:
    void envelopeAsADocument();
    void envelopeBeforeALetter();
    void labelSheetAsWordWritesIt();
    void labelRowsAsTallAsThePitch();
    void labelProductsFitTheirPaper();
    void libreOfficeRendersALabelSheet();
};

void DocxEnvelopesTest::envelopeAsADocument()
{
    QTextDocument body;
    Docx::WordParts parts;
    const Docx::EnvelopeSpec spec = envelope();
    parts.layout = Docx::envelopeLayout(spec);
    QTextCursor cursor(&body);
    Docx::insertEnvelope(cursor, spec, QString());
    QCOMPARE(parts.layout.pageSize, QSizeF(912, 396)); // Size 10, landscape
    QString error;
    const QByteArray bytes = Docx::docxBytes(body, parts, {}, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<w:pgSz w:w=\"13680\" w:h=\"5940\" w:orient=\"landscape\"/>"), xml.constData());
    QVERIFY(xml.contains("<w:pStyle w:val=\"EnvelopeReturn\"/>"));
    QVERIFY(xml.contains("<w:pStyle w:val=\"EnvelopeAddress\"/>"));
    const QByteArray styles = Test::partOf(bytes, QStringLiteral("word/styles.xml"));
    QVERIFY(styles.contains("w:styleId=\"EnvelopeAddress\""));
    QVERIFY(styles.contains("<w:name w:val=\"envelope return\"/>"));
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    QCOMPARE(back->parts.layout.pageSize, parts.layout.pageSize);
    QVERIFY(back->body.toPlainText().contains(QStringLiteral("Grace Hopper\n1 Navy Way")));
    // The delivery address starts where Word's Auto puts it: 4 in from the
    // left edge.
    for (QTextBlock b = back->body.begin(); b.isValid(); b = b.next()) {
        if (b.text() == QStringLiteral("Grace Hopper")) {
            QCOMPARE(qRound(b.blockFormat().leftMargin() + back->parts.layout.margins.left()), qRound(912 * 0.42));
        }
    }
}

void DocxEnvelopesTest::envelopeBeforeALetter()
{
    Test::DocxFixture f;
    f.body = para(QStringLiteral("Dear Grace,")) + para(QStringLiteral("Thank you for the compiler."));
    auto doc = Test::readWord(f.build());
    QVERIFY(doc);
    const Docx::PageLayout letter = doc->parts.layout;
    Docx::EnvelopeSpec spec = envelope();
    spec.size = QSizeF(220 * 96 / 25.4, 110 * 96 / 25.4); // DL
    spec.paperCode = 27;
    QTextCursor cursor(&doc->body);
    cursor.insertBlock();
    cursor.movePosition(QTextCursor::PreviousBlock);
    cursor.setBlockFormat(QTextBlockFormat());
    Docx::insertEnvelope(cursor, spec, Docx::envelopeSectionXml(spec));
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
    // The envelope's own section, then the letter's.
    QVERIFY2(xml.contains("<w:sectPr><w:pgSz w:w=\"12472\" w:h=\"6236\" w:orient=\"landscape\" w:code=\"27\"/>"), xml.constData());
    QVERIFY(xml.contains("<w:pgSz w:w=\"12240\" w:h=\"15840\"/>"));
    QVERIFY(xml.indexOf("w:code=\"27\"") < xml.indexOf("Dear Grace,"));
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    // Write shows the letter's page; the envelope keeps its own on save.
    QCOMPARE(back->parts.layout.pageSize, letter.pageSize);
    QVERIFY(back->body.toPlainText().startsWith(QStringLiteral("Ada Lovelace")));
    QCOMPARE(back->body.toPlainText().count(QStringLiteral("Dear Grace,")), 1);
    QTextCursor edit(&back->body);
    edit.movePosition(QTextCursor::End);
    edit.insertText(QStringLiteral(" Yours, Ada"));
    back->parts.layout.margins = QMarginsF(72, 72, 72, 72); // a page setup change keeps the envelope's page
    const QByteArray again = Docx::docxBytes(back->body, back->parts);
    QVERIFY(Test::partOf(again, QStringLiteral("word/document.xml")).contains("w:w=\"12472\" w:h=\"6236\""));
}

void DocxEnvelopesTest::labelSheetAsWordWritesIt()
{
    QTextDocument body;
    Docx::WordParts parts;
    const Docx::LabelProduct product = Docx::labelProduct(QStringLiteral("Avery US Letter|5160"));
    parts.layout = Docx::labelLayout(product);
    QTextCursor cursor(&body);
    QVERIFY(Docx::insertLabelSheet(cursor, product, [](QTextCursor &cell, int index) {
                cell.insertText(QStringLiteral("Label %1").arg(index + 1));
            }) >= 0);
    QString error;
    const QByteArray bytes = Docx::docxBytes(body, parts, {}, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<w:tblLayout w:type=\"fixed\"/>"), xml.constData());
    QVERIFY(xml.contains("<w:gridCol w:w=\"3780\"/><w:gridCol w:w=\"180\"/><w:gridCol w:w=\"3780\"/>"
                         "<w:gridCol w:w=\"180\"/><w:gridCol w:w=\"3780\"/>"));
    QCOMPARE(xml.count("<w:trHeight w:val=\"1440\" w:hRule=\"exact\"/>"), 10);
    QCOMPARE(xml.count("<w:cantSplit/>"), 10);
    QVERIFY(xml.contains("<w:top w:val=\"nil\"/>"));
    QVERIFY(xml.contains("<w:pgMar w:top=\"720\" w:right=\"270\" w:bottom=\"0\" w:left=\"270\" w:header=\"0\""));
    // The block Qt keeps before the table has no paragraph behind it.
    QVERIFY(xml.indexOf("<w:body><w:tbl>") >= 0);
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    QTextTable *table = qobject_cast<QTextTable *>(back->body.rootFrame()->childFrames().value(0));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 10);
    QCOMPARE(table->columns(), 5);
    QCOMPARE(cellText(table, 0, 0), QStringLiteral("Label 1"));
    QCOMPARE(cellText(table, 0, 4), QStringLiteral("Label 3"));
    QCOMPARE(cellText(table, 9, 4), QStringLiteral("Label 30"));
    QCOMPARE(cellText(table, 0, 1), QString());
    QCOMPARE(Docx::rowProperties(table, 3).height, 1440);
    QVERIFY(Docx::rowProperties(table, 3).exact);
}

void DocxEnvelopesTest::labelRowsAsTallAsThePitch()
{
    // Qt's tables have no row heights: each row is padded to Avery's pitch,
    // the pad split so every label (the padded first one too) is centred,
    // and padded again when what the labels hold changes (a merge).
    QTextDocument body;
    const Docx::LabelProduct product = Docx::labelProduct(QStringLiteral("Avery US Letter|5160"));
    const Docx::PageLayout layout = Docx::labelLayout(product);
    body.setPageSize(layout.pageSize);
    body.setDocumentMargin(0);
    QTextFrameFormat root = body.rootFrame()->frameFormat();
    root.setMargin(0);
    root.setLeftMargin(layout.margins.left());
    root.setRightMargin(layout.margins.right());
    root.setTopMargin(layout.margins.top());
    root.setBottomMargin(layout.margins.bottom());
    body.rootFrame()->setFrameFormat(root);
    const QString three = QStringLiteral("Ada Lovelace\u2028Analytical Engines\u2028London");
    QTextCursor cursor(&body);
    QVERIFY(Docx::insertLabelSheet(cursor, product, [&](QTextCursor &cell, int index) {
                cell.insertText(index == 0 ? QStringLiteral("Ada Lovelace") : three);
            }) >= 0);
    QTextTable *table = qobject_cast<QTextTable *>(body.rootFrame()->childFrames().value(0));
    QVERIFY(table != nullptr);
    auto centre = [&](int r, int c) {
        return body.documentLayout()->blockBoundingRect(table->cellAt(r, c).firstCursorPosition().block()).center().y();
    };
    for (int r = 0; r + 1 < table->rows(); ++r) {
        QVERIFY2(qAbs(centre(r + 1, 2) - centre(r, 2) - product.verticalPitch) < 0.5,
                 qPrintable(QStringLiteral("row %1: %2").arg(r).arg(centre(r + 1, 2) - centre(r, 2))));
        QVERIFY2(qAbs(centre(r, 0) - centre(r, 2)) < 0.5, qPrintable(QStringLiteral("row %1 not centred").arg(r)));
    }
    QCOMPARE(body.pageCount(), 1);
    // The first label now holds three lines too: its row is padded anew.
    QTextCursor first = table->cellAt(0, 0).firstCursorPosition();
    first.setPosition(table->cellAt(0, 0).lastCursorPosition().position(), QTextCursor::KeepAnchor);
    first.insertText(three);
    Docx::refreshRowHeights(&body);
    QVERIFY(qAbs(centre(1, 2) - centre(0, 2) - product.verticalPitch) < 0.5);
    QVERIFY(qAbs(centre(0, 0) - centre(0, 2)) < 0.5);
    QCOMPARE(body.pageCount(), 1);
}

void DocxEnvelopesTest::labelProductsFitTheirPaper()
{
    QStringList seen;
    for (const Docx::LabelProduct &p : Docx::labelProducts()) {
        QVERIFY2(!seen.contains(p.id()), qPrintable(p.id()));
        seen.append(p.id());
        const qreal width = p.side + (p.across - 1) * p.horizontalPitch + p.width;
        const qreal height = p.top + (p.down - 1) * p.verticalPitch + p.height;
        QVERIFY2(width <= p.page.width() + 1 && height <= p.page.height() + 1, qPrintable(p.id()));
        QVERIFY2(p.horizontalPitch >= p.width - 0.5 && p.verticalPitch >= p.height - 0.5, qPrintable(p.id()));
    }
    for (const char *product : {"5160", "5161", "5162", "5163", "5164", "5167", "5168", "L7160", "L7163"}) {
        QVERIFY2(seen.contains(QStringLiteral("Avery US Letter|") + QLatin1String(product))
                     || seen.contains(QStringLiteral("Avery A4/A5|") + QLatin1String(product)),
                 product);
    }
    // Avery's own figures: 5160 is 1 × 2 5/8 in, thirty to a sheet; L7163 is
    // 99.1 × 38.1 mm, fourteen.
    const Docx::LabelProduct p5160 = Docx::labelProduct(QStringLiteral("5160"));
    QCOMPARE(p5160.count(), 30);
    QCOMPARE(qRound(p5160.width * 1000 / 96), 2625);
    const Docx::LabelProduct l7163 = Docx::labelProduct(QStringLiteral("L7163"));
    QCOMPARE(l7163.count(), 14);
    QCOMPARE(qRound(l7163.width * 254 / 96), 991);
    for (const Docx::EnvelopeSize &size : Docx::envelopeSizes()) {
        QVERIFY(size.size.width() > size.size.height());
    }
}

void DocxEnvelopesTest::libreOfficeRendersALabelSheet()
{
    if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) {
        QSKIP("LibreOffice is not installed");
    }
    QTextDocument body;
    Docx::WordParts parts;
    const Docx::LabelProduct product = Docx::labelProduct(QStringLiteral("5163"));
    parts.layout = Docx::labelLayout(product);
    QTextCursor cursor(&body);
    Docx::insertLabelSheet(cursor, product, [](QTextCursor &cell, int index) {
        cell.insertText(QStringLiteral("Recipient %1").arg(index + 1));
        cell.insertBlock();
        cell.insertText(QStringLiteral("1 Navy Way"));
        cell.insertBlock();
        cell.insertText(QStringLiteral("Arlington, VA 22202"));
    });
    QTemporaryDir scratch;
    const QString pdf = Test::renderWithLibreOffice(Docx::docxBytes(body, parts), QStringLiteral("m77_label_sheet"), scratch);
    QVERIFY(!pdf.isEmpty());
    const QString text = Test::pdfText(pdf);
    if (!text.isEmpty()) {
        QVERIFY(text.contains(QStringLiteral("Recipient 10")));
        QCOMPARE(text.count(QLatin1Char('\f')), 1); // one sheet
    }
}

QTEST_MAIN(DocxEnvelopesTest)
#include "tst_docx_envelopes.moc"
