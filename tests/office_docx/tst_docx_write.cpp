// SPDX-License-Identifier: GPL-3.0-or-later
// M60 gates for the .docx writer (D-068): documents built in the editor's
// model and documents read from Word are written, validated against the
// ECMA-376 schemas (when QQO_OOXML_SCHEMA_DIR is set) and read back.

#include "docx_fixture.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_writer.h"
#include "support/ooxml_schema.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QtTest>

using namespace QindaOffice;
using Test::DocxFixture;
using Test::para;

class DocxWriteTest : public QObject {
    Q_OBJECT
private slots:
    void writesANewDocumentWordAccepts();
    void keepsUnchangedSourceMarkup();
    void carriesPreservedObjectsAndTheirParts();
    void rewritesEditedFootnotesAndTables();
};

namespace {

void checkSchema(const QByteArray &bytes)
{
    if (!Test::schemaValidationAvailable()) {
        qInfo("schema validation skipped (QQO_OOXML_SCHEMA_DIR unset)");
        return;
    }
    const auto package = Ooxml::Package::fromData(bytes);
    QVERIFY(package);
    QStringList errors = Test::validatePackage(*package);
    // Parts carried verbatim from the hand-typed fixtures (a stub theme, a
    // stub chart) are the fixtures' business, not the writer's.
    errors.removeIf([](const QString &e) {
        return e.startsWith(u"word/theme/") || e.startsWith(u"word/charts/");
    });
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

QTextBlock blockWithText(const QTextDocument &doc, const QString &text)
{
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (b.text() == text) {
            return b;
        }
    }
    return {};
}

QTextTable *firstTable(QTextDocument &doc)
{
    for (auto it = doc.rootFrame()->begin(); !it.atEnd(); ++it) {
        if (auto *t = qobject_cast<QTextTable *>(it.currentFrame())) {
            return t;
        }
    }
    return nullptr;
}

struct Reread {
    QTextDocument body;
    Docx::WordParts parts;
};

std::unique_ptr<Reread> reread(const QByteArray &bytes)
{
    auto out = std::make_unique<Reread>();
    QString error;
    if (!Docx::readDocx(Ooxml::Package::fromData(bytes), &out->body, &out->parts, &error)) {
        qWarning() << error;
        return nullptr;
    }
    return out;
}

} // namespace

void DocxWriteTest::writesANewDocumentWordAccepts()
{
    QTextDocument doc;
    Docx::WordParts parts;
    parts.layout.pageSize = QSizeF(1123, 794); // A4 landscape
    parts.layout.margins = QMarginsF(72, 60, 72, 96);
    QTextCursor c(&doc);
    QTextBlockFormat heading;
    heading.setHeadingLevel(1);
    heading.setProperty(Docx::kWriteStyleIdProperty, 2);
    c.setBlockFormat(heading);
    QTextCharFormat big;
    big.setFontPointSize(20);
    big.setFontWeight(QFont::Bold);
    c.insertText(QStringLiteral("Quarterly report"), big);
    c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    QTextCharFormat plain;
    c.insertText(QStringLiteral("Revenue rose "), plain);
    QTextCharFormat strong;
    strong.setFontWeight(QFont::Bold);
    strong.setForeground(QColor(0xC0, 0x00, 0x00));
    strong.setBackground(QColor(Qt::yellow));
    c.insertText(QStringLiteral("12 %"), strong);
    QTextCharFormat link;
    link.setAnchor(true);
    link.setAnchorHref(QStringLiteral("https://example.org/report?q=1&x=2"));
    c.insertText(QStringLiteral(" (details)"), link);
    QTextCharFormat note;
    note.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    note.setProperty(Docx::kFootnoteIdProperty, QStringLiteral("n1"));
    // The marker's glyph is the note's number (QindaWrite renumbers it).
    c.insertText(QStringLiteral("1"), note);
    parts.footnotes.insert(QStringLiteral("n1"), QStringLiteral("Audited figures."));
    QTextCharFormat inserted;
    inserted.setProperty(Docx::kRevisionInsertProperty, QStringLiteral("Ana"));
    c.insertText(QStringLiteral(" new"), inserted);
    c.insertBlock();
    QTextListFormat bullets;
    bullets.setStyle(QTextListFormat::ListDisc);
    bullets.setIndent(1);
    c.createList(bullets);
    c.insertText(QStringLiteral("North"));
    c.insertBlock();
    c.insertText(QStringLiteral("South"));
    c.insertBlock(QTextBlockFormat());
    QTextListFormat numbers;
    numbers.setStyle(QTextListFormat::ListUpperRoman);
    numbers.setIndent(1);
    c.createList(numbers);
    c.insertText(QStringLiteral("Step"));
    c.insertBlock(QTextBlockFormat());
    QTextTableFormat tf;
    tf.setBorder(1);
    QTextTable *table = c.insertTable(2, 3, tf);
    table->mergeCells(0, 0, 1, 2);
    table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("merged"));
    QTextTableCellFormat shaded = table->cellAt(1, 2).format().toTableCellFormat();
    shaded.setBackground(QColor(0xDD, 0xEE, 0xFF));
    table->cellAt(1, 2).setFormat(shaded);
    table->cellAt(1, 2).firstCursorPosition().insertText(QStringLiteral("shaded"));
    c = table->lastCursorPosition();
    c.movePosition(QTextCursor::NextBlock);
    QTextBlockFormat broken;
    broken.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    c.setBlockFormat(broken);
    const QString pic = QStringLiteral("qwmedia:image1.png");
    parts.media.insert(pic, Test::pngBytes(30, 15));
    doc.addResource(QTextDocument::ImageResource, QUrl(pic), QImage::fromData(parts.media.value(pic)));
    QTextImageFormat image;
    image.setName(pic);
    image.setWidth(60);
    image.setHeight(30);
    c.insertImage(image);
    c.insertText(QStringLiteral("\tTab & <angle>"));
    auto &footer = parts.footer(Docx::HeaderFooterSlot::Default);
    footer.document = std::make_unique<QTextDocument>();
    QTextCursor fc(footer.document.get());
    fc.insertText(QStringLiteral("Page "));
    QTextCharFormat field;
    field.setProperty(Docx::kFieldInstruction, QStringLiteral("PAGE"));
    fc.insertText(QStringLiteral("1"), field);

    QString error;
    const QByteArray bytes = Docx::docxBytes(doc, parts, {}, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    checkSchema(bytes);
    auto back = reread(bytes);
    QVERIFY(back);
    QCOMPARE(back->body.toPlainText(), doc.toPlainText());
    QCOMPARE(back->parts.layout.pageSize, QSizeF(1123, 794));
    QCOMPARE(back->parts.layout.margins, QMarginsF(72, 60, 72, 96));
    const QTextBlock h = blockWithText(back->body, QStringLiteral("Quarterly report"));
    QCOMPARE(h.blockFormat().headingLevel(), 1);
    QCOMPARE(h.begin().fragment().charFormat().fontPointSize(), 20.0);
    const QTextBlock body = h.next();
    QString href;
    QString footnote;
    QString author;
    for (auto it = body.begin(); !it.atEnd(); ++it) {
        const QTextCharFormat f = it.fragment().charFormat();
        if (it.fragment().text() == QStringLiteral("12 %")) {
            QCOMPARE(f.fontWeight(), int(QFont::Bold));
            QCOMPARE(f.foreground().color(), QColor(0xC0, 0x00, 0x00));
            QCOMPARE(f.background().color(), QColor(Qt::yellow));
        }
        href = f.anchorHref().isEmpty() ? href : f.anchorHref();
        footnote = f.stringProperty(Docx::kFootnoteIdProperty).isEmpty() ? footnote : f.stringProperty(Docx::kFootnoteIdProperty);
        author = f.stringProperty(Docx::kRevisionInsertProperty).isEmpty() ? author : f.stringProperty(Docx::kRevisionInsertProperty);
    }
    QCOMPARE(href, QStringLiteral("https://example.org/report?q=1&x=2"));
    QCOMPARE(back->parts.footnotes.value(footnote), QStringLiteral("Audited figures."));
    QCOMPARE(author, QStringLiteral("Ana"));
    QCOMPARE(blockWithText(back->body, QStringLiteral("North")).textList()->format().style(), QTextListFormat::ListDisc);
    QCOMPARE(blockWithText(back->body, QStringLiteral("South")).textList(),
             blockWithText(back->body, QStringLiteral("North")).textList());
    QCOMPARE(blockWithText(back->body, QStringLiteral("Step")).textList()->format().style(), QTextListFormat::ListUpperRoman);
    QTextTable *t = firstTable(back->body);
    QVERIFY(t != nullptr);
    QCOMPARE(t->cellAt(0, 0).columnSpan(), 2);
    QCOMPARE(t->cellAt(1, 2).format().toTableCellFormat().background().color(), QColor(0xDD, 0xEE, 0xFF));
    const QTextBlock last = back->body.lastBlock();
    QVERIFY(last.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore);
    const QTextImageFormat img = last.begin().fragment().charFormat().toImageFormat();
    QCOMPARE(img.width(), 60.0);
    QCOMPARE(back->parts.media.value(img.name()), Test::pngBytes(30, 15));
    QVERIFY(back->parts.footer(Docx::HeaderFooterSlot::Default).document);
    QCOMPARE(back->parts.footer(Docx::HeaderFooterSlot::Default).document->toPlainText(), QStringLiteral("Page 1"));
}

void DocxWriteTest::keepsUnchangedSourceMarkup()
{
    DocxFixture f;
    f.styles = QStringLiteral("<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
                              "<w:style w:type=\"paragraph\" w:styleId=\"Fancy\"><w:name w:val=\"Fancy\"/></w:style>");
    f.theme = QStringLiteral("<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"Mine\">"
                             "<a:themeElements/></a:theme>");
    f.header = para(QStringLiteral("Head"));
    const QString border = QStringLiteral("<w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" w:color=\"4472C4\"/></w:pBdr>");
    f.body = para(QStringLiteral("Boxed"), QStringLiteral("<w:pStyle w:val=\"Fancy\"/><w:keepNext/>") + border,
                  QStringLiteral("<w:rFonts w:ascii=\"Georgia\" w:hAnsi=\"Georgia\"/><w:w w:val=\"90\"/>"))
        + para(QStringLiteral("Second"), QStringLiteral("<w:jc w:val=\"left\"/>") + border);
    const QByteArray source = f.build();
    QTextDocument doc;
    Docx::WordParts parts;
    QVERIFY(Docx::readDocx(Ooxml::Package::fromData(source), &doc, &parts));
    // Edit the second paragraph only.
    QTextCursor c(blockWithText(doc, QStringLiteral("Second")));
    QTextBlockFormat centred = c.blockFormat();
    centred.setAlignment(Qt::AlignHCenter);
    c.setBlockFormat(centred);
    const QByteArray bytes = Docx::docxBytes(doc, parts);
    QVERIFY(!bytes.isEmpty());
    checkSchema(bytes);
    const auto out = Ooxml::Package::fromData(bytes);
    const QByteArray xml = out->data(QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:pStyle w:val=\"Fancy\"/><w:keepNext/><w:pBdr>"));
    QVERIFY2(xml.contains("<w:rFonts w:ascii=\"Georgia\" w:hAnsi=\"Georgia\"/><w:w w:val=\"90\"/>"), xml.constData());
    QCOMPARE(xml.count("w:pBdr><w:bottom"), 2);
    QVERIFY(xml.contains("<w:jc w:val=\"center\"/>"));
    QVERIFY(!xml.contains("<w:jc w:val=\"left\"/>"));
    // The theme and the untouched header came along byte for byte.
    QVERIFY(out->data(QStringLiteral("word/theme/theme1.xml")).contains("name=\"Mine\""));
    const auto in = Ooxml::Package::fromData(source);
    QCOMPARE(out->data(QStringLiteral("word/header1.xml")), in->data(QStringLiteral("word/header1.xml")));
    QVERIFY(out->data(QStringLiteral("word/styles.xml")).contains("w:styleId=\"Fancy\""));
}

void DocxWriteTest::carriesPreservedObjectsAndTheirParts()
{
    DocxFixture f;
    f.extras.insert(QStringLiteral("rIdChart"),
                    {QStringLiteral("word/charts/chart1.xml"), QString::fromUtf16(Ooxml::RelType::Chart),
                     QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml"),
                     QByteArray("<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"><c:chart/></c:chartSpace>")});
    f.body = QStringLiteral(
        "<w:p><w:r><w:t xml:space=\"preserve\">Sales </w:t></w:r><w:r><w:drawing><wp:inline><wp:extent cx=\"1905000\" cy=\"952500\"/>"
        "<wp:docPr id=\"9\" name=\"Chart 9\"/><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
        "<c:chart r:id=\"rIdChart\"/></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>"
        "<w:p><m:oMathPara><m:oMath><m:r><m:t>a²+b²</m:t></m:r></m:oMath></m:oMathPara></w:p>");
    QTextDocument doc;
    Docx::WordParts parts;
    QVERIFY(Docx::readDocx(Ooxml::Package::fromData(f.build()), &doc, &parts));
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("Q3 "));
    const QByteArray bytes = Docx::docxBytes(doc, parts);
    checkSchema(bytes);
    const auto out = Ooxml::Package::fromData(bytes);
    const QByteArray xml = out->data(QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<m:oMathPara>"));
    QVERIFY(xml.contains("Q3 "));
    const auto rel = out->firstOfType(QStringLiteral("word/document.xml"), QString::fromUtf16(Ooxml::RelType::Chart));
    QVERIFY(rel);
    QVERIFY(xml.contains(QStringLiteral("<c:chart r:id=\"%1\"/>").arg(rel->id).toUtf8()));
    QVERIFY(out->data(out->targetPart(QStringLiteral("word/document.xml"), *rel)).contains("<c:chart/>"));
}

void DocxWriteTest::rewritesEditedFootnotesAndTables()
{
    DocxFixture f;
    f.footnotes = QStringLiteral(
        "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
        "<w:footnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:footnote>"
        "<w:footnote w:id=\"1\"><w:p><w:r><w:footnoteRef/></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>Kept.</w:t></w:r></w:p></w:footnote>"
        "<w:footnote w:id=\"2\"><w:p><w:r><w:footnoteRef/></w:r><w:r><w:t>Old.</w:t></w:r></w:p></w:footnote>");
    f.body = QStringLiteral("<w:p><w:r><w:t>A</w:t></w:r><w:r><w:footnoteReference w:id=\"1\"/></w:r><w:r><w:t>B</w:t></w:r>"
                            "<w:r><w:footnoteReference w:id=\"2\"/></w:r></w:p>"
                            "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr><w:tblGrid><w:gridCol w:w=\"3000\"/>"
                            "<w:gridCol w:w=\"3000\"/></w:tblGrid><w:tr><w:tc>%1</w:tc><w:tc>%2</w:tc></w:tr></w:tbl>%3")
                 .arg(para(QStringLiteral("x")), para(QStringLiteral("y")), para(QStringLiteral("end")));
    QTextDocument doc;
    Docx::WordParts parts;
    QVERIFY(Docx::readDocx(Ooxml::Package::fromData(f.build()), &doc, &parts));
    parts.footnotes[QStringLiteral("docx-2")] = QStringLiteral("New.");
    QTextTable *table = firstTable(doc);
    QVERIFY(table != nullptr);
    table->appendRows(1);
    table->insertColumns(1, 1);
    table->cellAt(1, 2).firstCursorPosition().insertText(QStringLiteral("added"));
    const QByteArray bytes = Docx::docxBytes(doc, parts);
    checkSchema(bytes);
    const auto out = Ooxml::Package::fromData(bytes);
    const QByteArray notes = out->data(QStringLiteral("word/footnotes.xml"));
    QVERIFY(notes.contains("<w:b/>")); // the unchanged note kept its markup
    QVERIFY(notes.contains("New."));
    QVERIFY(!notes.contains("Old."));
    QVERIFY(out->data(QStringLiteral("word/settings.xml")).contains("<w:footnotePr>"));
    auto back = reread(bytes);
    QVERIFY(back);
    QTextTable *t = firstTable(back->body);
    QCOMPARE(t->rows(), 2);
    QCOMPARE(t->columns(), 3);
    QVERIFY(back->body.toPlainText().contains(QStringLiteral("added")));
}

QTEST_MAIN(DocxWriteTest)
#include "tst_docx_write.moc"
