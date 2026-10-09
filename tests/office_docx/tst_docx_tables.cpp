// SPDX-License-Identifier: GPL-3.0-or-later
// M70 gates for Word's table formatting (D-079, docx_table_format.h): cell
// shading and borders applied the way Word applies them, table, row and
// cell properties, AutoFit and Distribute — each written exactly as Word
// writes it, validated against the ECMA-376 schemas (QQO_OOXML_SCHEMA_DIR),
// read back equal, and an edited table's untouched cells keep their source
// markup (D-068 rule 3).

#include "docx_fixture.h"
#include "docx_borders.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_table_format.h"
#include "docx_writer.h"
#include "support/ooxml_schema.h"

#include <QElapsedTimer>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QtTest>

using namespace QindaOffice;
using Test::DocxFixture;
using Test::para;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// Word's Table Grid, as Word's normal template defines it.
const char *kStyles =
    R"(<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>)"
    R"(<w:style w:type="table" w:default="1" w:styleId="TableNormal"><w:name w:val="Normal Table"/>)"
    R"(<w:tblPr><w:tblInd w:w="0" w:type="dxa"/><w:tblCellMar><w:top w:w="0" w:type="dxa"/>)"
    R"(<w:left w:w="108" w:type="dxa"/><w:bottom w:w="0" w:type="dxa"/><w:right w:w="108" w:type="dxa"/>)"
    R"(</w:tblCellMar></w:tblPr></w:style>)"
    R"(<w:style w:type="table" w:styleId="TableGrid"><w:name w:val="Table Grid"/><w:basedOn w:val="TableNormal"/>)"
    R"(<w:tblPr><w:tblBorders><w:top w:val="single" w:sz="4" w:space="0" w:color="auto"/>)"
    R"(<w:left w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:bottom w:val="single" w:sz="4" w:space="0" w:color="auto"/>)"
    R"(<w:right w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:insideH w:val="single" w:sz="4" w:space="0" w:color="auto"/>)"
    R"(<w:insideV w:val="single" w:sz="4" w:space="0" w:color="auto"/></w:tblBorders></w:tblPr></w:style>)";

// A 3 × 3 Table Grid table as Word writes one; C3 has a theme fill and B3
// a theme-coloured bottom border, which only source markup can carry.
QString wordTable()
{
    QString rows;
    for (int r = 1; r <= 3; ++r) {
        rows += QStringLiteral("<w:tr>");
        for (const char *column : {"A", "B", "C"}) {
            QString tcPr = QStringLiteral("<w:tcW w:w=\"3117\" w:type=\"dxa\"/>");
            if (r == 3 && QLatin1String(column) == QLatin1String("B")) {
                tcPr += QStringLiteral("<w:tcBorders><w:bottom w:val=\"single\" w:sz=\"12\" w:space=\"0\" w:color=\"4472C4\""
                                       " w:themeColor=\"accent1\"/></w:tcBorders>");
            }
            if (r == 3 && QLatin1String(column) == QLatin1String("C")) {
                tcPr += QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"E7E6E6\" w:themeFill=\"background2\"/>");
            }
            rows += QStringLiteral("<w:tc><w:tcPr>%1</w:tcPr>%2</w:tc>").arg(tcPr, para(QLatin1String(column) + QString::number(r)));
        }
        rows += QStringLiteral("</w:tr>");
    }
    return QStringLiteral("<w:tbl><w:tblPr><w:tblStyle w:val=\"TableGrid\"/><w:tblW w:w=\"0\" w:type=\"auto\"/>"
                          "<w:tblLook w:val=\"04A0\" w:firstRow=\"1\" w:lastRow=\"0\" w:firstColumn=\"1\" w:lastColumn=\"0\""
                          " w:noHBand=\"0\" w:noVBand=\"1\"/></w:tblPr><w:tblGrid><w:gridCol w:w=\"3117\"/>"
                          "<w:gridCol w:w=\"3117\"/><w:gridCol w:w=\"3117\"/></w:tblGrid>%1</w:tbl>%2")
        .arg(rows, para(QStringLiteral("After")));
}

QByteArray sourcePackage()
{
    DocxFixture f;
    f.styles = QString::fromLatin1(kStyles);
    f.body = para(QStringLiteral("Before")) + wordTable();
    return f.build();
}

void checkSchema(const QByteArray &bytes)
{
    if (!Test::schemaValidationAvailable()) {
        qInfo("schema validation skipped (QQO_OOXML_SCHEMA_DIR unset)");
        return;
    }
    const auto package = Ooxml::Package::fromData(bytes);
    QVERIFY(package);
    const QStringList errors = Test::validatePackage(*package);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
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

// The w:tbl of a package's body and its rows' cells.
const Ooxml::XmlElement *tableOf(const Ooxml::XmlElementPtr &document)
{
    return document ? document->child(kW, u"body")->child(kW, u"tbl") : nullptr;
}

const Ooxml::XmlElement *cellOf(const Ooxml::XmlElement *tbl, int row, int column)
{
    const QList<const Ooxml::XmlElement *> rows = tbl->elements(kW, u"tr");
    return rows.value(row) != nullptr ? rows.value(row)->elements(kW, u"tc").value(column) : nullptr;
}

QString markupOf(const Ooxml::XmlElement *e)
{
    return e != nullptr ? Ooxml::serializeFragment(*e) : QString();
}

// What D-068 rule 3 keeps of an untouched cell: its w:tcPr and the w:pPr
// and w:rPr of its paragraphs and runs (the runs themselves are rebuilt:
// text may be split or spaced differently, never formatted differently).
QString propertiesOf(const Ooxml::XmlElement *tc)
{
    QString out = markupOf(tc->child(kW, u"tcPr"));
    for (const Ooxml::XmlElement *p : tc->elements(kW, u"p")) {
        out += QStringLiteral("|p:") + markupOf(p->child(kW, u"pPr"));
        for (const Ooxml::XmlElement *r : p->elements(kW, u"r")) {
            out += QStringLiteral("|r:") + markupOf(r->child(kW, u"rPr")) + r->text();
        }
    }
    return out;
}

struct Doc {
    QTextDocument body;
    Docx::WordParts parts;
};

std::unique_ptr<Doc> read(const QByteArray &bytes)
{
    auto doc = std::make_unique<Doc>();
    QString error;
    if (!Docx::readDocx(Ooxml::Package::fromData(bytes), &doc->body, &doc->parts, &error)) {
        qWarning() << error;
        return nullptr;
    }
    return doc;
}

Docx::WordBorder pen(const char *style, int size, const char *color)
{
    return {QString::fromLatin1(style), size, QString::fromLatin1(color), 0};
}

} // namespace

class DocxTablesTest : public QObject {
    Q_OBJECT
private slots:
    void shadingAndBordersOnSelectedCells();
    void wholeTableBordersAreTableBorders();
    void tablePropertiesRoundTrip();
    void autoFitAndDistribute();
    void newTableIsWordsTableGrid();
    void qtTablesTakeWordsModel();
};

void DocxTablesTest::shadingAndBordersOnSelectedCells()
{
    const QByteArray source = sourcePackage();
    auto doc = read(source);
    QVERIFY(doc);
    QTextTable *table = firstTable(doc->body);
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 3);
    QTextCursor edit(&doc->body);
    edit.beginEditBlock();
    Docx::applyCellShading(table, {0, 1, 1, 1}, QColor(0xFF, 0xF2, 0xCC));
    // A double red line under the first row: the second row's top follows.
    Docx::applyTableBorders(table, {0, 0, 1, 3}, Docx::BorderTarget::Bottom, pen("double", 12, "C00000"));
    edit.endEditBlock();
    // What Qt draws: the row's bottom and the next row's top.
    const QTextTableCellFormat a1 = table->cellAt(0, 0).format().toTableCellFormat();
    QCOMPARE(a1.bottomBorderStyle(), QTextFrameFormat::BorderStyle_Double);
    QCOMPARE(a1.bottomBorderBrush().color(), QColor(0xC0, 0x00, 0x00));
    QCOMPARE(table->cellAt(1, 2).format().toTableCellFormat().topBorderStyle(), QTextFrameFormat::BorderStyle_Double);
    QCOMPARE(table->cellAt(0, 1).format().toTableCellFormat().background().color(), QColor(0xFF, 0xF2, 0xCC));

    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    const auto out = Ooxml::Package::fromData(bytes);
    const auto in = Ooxml::Package::fromData(source);
    const Ooxml::XmlElementPtr outDoc = out->xml(QStringLiteral("word/document.xml"));
    const Ooxml::XmlElementPtr inDoc = in->xml(QStringLiteral("word/document.xml"));
    const Ooxml::XmlElement *outTbl = tableOf(outDoc);
    const Ooxml::XmlElement *inTbl = tableOf(inDoc);
    QVERIFY(outTbl != nullptr && inTbl != nullptr);
    // Word's spelling of a fill and of a border side.
    const QString b1 = markupOf(cellOf(outTbl, 0, 1)->child(kW, u"tcPr"));
    QVERIFY2(b1.contains(QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"FFF2CC\"/>")), qPrintable(b1));
    QVERIFY2(b1.contains(QStringLiteral("<w:tcBorders><w:bottom w:val=\"double\" w:sz=\"12\" w:space=\"0\" w:color=\"C00000\"/></w:tcBorders>")),
             qPrintable(b1));
    QVERIFY(markupOf(cellOf(outTbl, 1, 0)->child(kW, u"tcPr"))
                .contains(QStringLiteral("<w:top w:val=\"double\" w:sz=\"12\" w:space=\"0\" w:color=\"C00000\"/>")));
    // Untouched: the third row's cells and the table's own properties,
    // theme fill and theme border included.
    for (int c = 0; c < 3; ++c) {
        QCOMPARE(propertiesOf(cellOf(outTbl, 2, c)), propertiesOf(cellOf(inTbl, 2, c)));
    }
    QCOMPARE(markupOf(outTbl->child(kW, u"tblPr")), markupOf(inTbl->child(kW, u"tblPr")));
    QCOMPARE(markupOf(outTbl->child(kW, u"tblGrid")), markupOf(inTbl->child(kW, u"tblGrid")));

    auto back = read(bytes);
    QVERIFY(back);
    QTextTable *again = firstTable(back->body);
    const auto own = [&](int r, int c) {
        return Docx::parseBordersSpec(again->cellAt(r, c).format().stringProperty(Docx::kCellBorders));
    };
    QCOMPARE(own(0, 0).value(QStringLiteral("bottom")), pen("double", 12, "C00000"));
    QCOMPARE(own(1, 1).value(QStringLiteral("top")), pen("double", 12, "C00000"));
    QCOMPARE(own(2, 1).value(QStringLiteral("bottom")).color, QStringLiteral("4472C4"));
    QCOMPARE(again->cellAt(0, 1).format().toTableCellFormat().background().color(), QColor(0xFF, 0xF2, 0xCC));
    QCOMPARE(again->cellAt(2, 2).format().toTableCellFormat().background().color(), QColor(0xE7, 0xE6, 0xE6));
}

void DocxTablesTest::wholeTableBordersAreTableBorders()
{
    auto doc = read(sourcePackage());
    QVERIFY(doc);
    QTextTable *table = firstTable(doc->body);
    Docx::applyTableBorders(table, Docx::wholeTable(table), Docx::BorderTarget::Outside, pen("thick", 18, "1F4E79"));
    Docx::applyTableBorders(table, Docx::wholeTable(table), Docx::BorderTarget::InsideVertical, Docx::WordBorder::none());
    QCOMPARE(table->cellAt(0, 0).format().toTableCellFormat().topBorder(), Docx::displayBorder(pen("thick", 18, "1F4E79")).width);
    QCOMPARE(table->cellAt(1, 1).format().toTableCellFormat().leftBorder(), 0.0);
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    const auto out = Ooxml::Package::fromData(bytes);
    const QString tblPr = markupOf(tableOf(out->xml(QStringLiteral("word/document.xml")))->child(kW, u"tblPr"));
    QVERIFY2(tblPr.contains(QStringLiteral("<w:top w:val=\"thick\" w:sz=\"18\" w:space=\"0\" w:color=\"1F4E79\"/>")), qPrintable(tblPr));
    QVERIFY(tblPr.contains(QStringLiteral("<w:insideV w:val=\"nil\"/>")));
    QVERIFY(tblPr.contains(QStringLiteral("<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>")));
    // No cell carries a border of its own: the table says it.
    QVERIFY(!out->data(QStringLiteral("word/document.xml")).contains("<w:tcBorders><w:top"));
    auto back = read(bytes);
    QTextTable *again = firstTable(back->body);
    const Docx::WordBorders level = Docx::parseBordersSpec(again->format().stringProperty(Docx::kTableBorders));
    QCOMPARE(level.value(QStringLiteral("left")), pen("thick", 18, "1F4E79"));
    QVERIFY(level.value(QStringLiteral("insideV")).isNone());
    QCOMPARE(again->cellAt(2, 2).format().toTableCellFormat().rightBorderStyle(), QTextFrameFormat::BorderStyle_Solid);
}

void DocxTablesTest::tablePropertiesRoundTrip()
{
    auto doc = read(sourcePackage());
    QVERIFY(doc);
    QTextTable *table = firstTable(doc->body);
    const qreal content = doc->parts.layout.contentWidth();
    Docx::TableProperties p = Docx::tableProperties(table);
    QCOMPARE(p.margins.left, 108);
    p.width = {QStringLiteral("pct"), 4000};
    p.alignment = Qt::AlignHCenter;
    p.layout = QStringLiteral("fixed");
    p.margins = {72, 144, 72, 144};
    p.headerRows = 1;
    Docx::setTableProperties(table, p, content);
    Docx::setRowProperties(table, 1, {720, true, true});
    Docx::setColumnWidth(table, 2, 2000, content);
    Docx::setCellMargins(table, {2, 0, 1, 1}, {0, 288, 0, 288});
    Docx::applyCellVerticalAlignment(table, {0, 0, 1, 3}, QTextCharFormat::AlignBottom);
    QCOMPARE(table->format().headerRowCount(), 1);
    QCOMPARE(table->format().alignment() & Qt::AlignHorizontal_Mask, Qt::AlignHCenter);
    QCOMPARE(table->cellAt(2, 0).format().toTableCellFormat().leftPadding(), 288.0 / 15);

    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    const auto out = Ooxml::Package::fromData(bytes);
    const Ooxml::XmlElementPtr outDoc = out->xml(QStringLiteral("word/document.xml"));
    const Ooxml::XmlElement *tbl = tableOf(outDoc);
    const QString tblPr = markupOf(tbl->child(kW, u"tblPr"));
    QVERIFY2(tblPr.contains(QStringLiteral("<w:tblW w:w=\"4000\" w:type=\"pct\"/><w:jc w:val=\"center\"/>")), qPrintable(tblPr));
    QVERIFY(tblPr.contains(QStringLiteral("<w:tblLayout w:type=\"fixed\"/>")));
    QVERIFY(tblPr.contains(QStringLiteral("<w:tblCellMar><w:top w:w=\"72\" w:type=\"dxa\"/><w:left w:w=\"144\" w:type=\"dxa\"/>")));
    const QString row1 = markupOf(tbl->elements(kW, u"tr").at(1)->child(kW, u"trPr"));
    QVERIFY2(row1.contains(QStringLiteral("<w:cantSplit/><w:trHeight w:val=\"720\" w:hRule=\"exact\"/>")), qPrintable(row1));
    QVERIFY(markupOf(tbl->elements(kW, u"tr").at(0)->child(kW, u"trPr")).contains(QStringLiteral("<w:tblHeader/>")));
    QVERIFY(markupOf(cellOf(tbl, 0, 0)).contains(QStringLiteral("<w:vAlign w:val=\"bottom\"/>")));
    QVERIFY(markupOf(cellOf(tbl, 2, 0)).contains(QStringLiteral("<w:tcMar><w:top w:w=\"0\" w:type=\"dxa\"/><w:left w:w=\"288\" w:type=\"dxa\"/>")));
    QVERIFY(markupOf(tbl->child(kW, u"tblGrid")).contains(QStringLiteral("<w:gridCol w:w=\"2000\"/>")));

    auto back = read(bytes);
    QTextTable *again = firstTable(back->body);
    const Docx::TableProperties q = Docx::tableProperties(again);
    QCOMPARE(q.width, p.width);
    QCOMPARE(q.alignment, p.alignment);
    QCOMPARE(q.layout, p.layout);
    QCOMPARE(q.margins, p.margins);
    QCOMPARE(q.headerRows, 1);
    const Docx::RowProperties row = Docx::rowProperties(again, 1);
    QCOMPARE(row.height, 720);
    QVERIFY(row.exact);
    QVERIFY(row.cantSplit);
    QCOMPARE(Docx::columnWidths(again, back->parts.layout.contentWidth()).at(2), 2000);
    QCOMPARE(Docx::cellMargins(again, 2, 0).left, 288);
}

void DocxTablesTest::autoFitAndDistribute()
{
    auto doc = read(sourcePackage());
    QVERIFY(doc);
    QTextTable *table = firstTable(doc->body);
    const qreal content = doc->parts.layout.contentWidth();
    Docx::setColumnWidth(table, 0, 4000, content);
    Docx::distributeColumns(table, Docx::wholeTable(table), content);
    QList<int> widths = Docx::columnWidths(table, content);
    QCOMPARE(widths.at(0), widths.at(2));
    QCOMPARE(widths.at(0), (4000 + 3117 + 3117) / 3);
    Docx::distributeRows(table, Docx::wholeTable(table));
    QVERIFY(Docx::rowProperties(table, 0).height > 0);
    QCOMPARE(Docx::rowProperties(table, 0).height, Docx::rowProperties(table, 2).height);

    Docx::autoFit(table, Docx::AutoFit::Window, content);
    QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    QByteArray xml = Ooxml::Package::fromData(bytes)->data(QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:tblW w:w=\"5000\" w:type=\"pct\"/>"));
    QVERIFY(xml.contains("w:type=\"pct\"/></w:tcPr>") || xml.contains("<w:tcW w:w=\"1667\" w:type=\"pct\"/>"));

    Docx::autoFit(table, Docx::AutoFit::Contents, content);
    bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    xml = Ooxml::Package::fromData(bytes)->data(QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:tblW w:w=\"0\" w:type=\"auto\"/>"));
    QVERIFY(xml.contains("<w:tcW w:w=\"0\" w:type=\"auto\"/>"));
    QVERIFY(!xml.contains("<w:tblLayout"));

    Docx::autoFit(table, Docx::AutoFit::Fixed, content);
    bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    xml = Ooxml::Package::fromData(bytes)->data(QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:tblLayout w:type=\"fixed\"/>"));
    QVERIFY(xml.contains("w:type=\"dxa\"/>"));
    auto back = read(bytes);
    QCOMPARE(Docx::tableProperties(firstTable(back->body)).layout, QStringLiteral("fixed"));
}

void DocxTablesTest::newTableIsWordsTableGrid()
{
    QTextDocument doc;
    Docx::WordParts parts;
    QTextCursor cursor(&doc);
    cursor.insertText(QStringLiteral("Prices"));
    QTextTable *table = cursor.insertTable(2, 2, Docx::newWordTableFormat(2, parts.layout.contentWidth()));
    Docx::initWordTable(table);
    table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("Item"));
    QCOMPARE(table->cellAt(0, 0).format().toTableCellFormat().leftPadding(), 108.0 / 15);
    QVERIFY(table->cellAt(1, 1).format().toTableCellFormat().bottomBorder() > 0);
    Docx::applyCellShading(table, {0, 0, 1, 2}, QColor(0xD9, 0xE2, 0xF3));
    Docx::applyTableBorders(table, {1, 0, 1, 2}, Docx::BorderTarget::None, Docx::WordBorder());
    const QByteArray bytes = Docx::docxBytes(doc, parts);
    checkSchema(bytes);
    const QByteArray xml = Ooxml::Package::fromData(bytes)->data(QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:tblStyle w:val=\"TableGrid\"/>"));
    QVERIFY(xml.contains("<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"));
    QVERIFY(xml.contains("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"D9E2F3\"/>"));
    QVERIFY(xml.contains("<w:tcBorders><w:top w:val=\"nil\"/>"));
    auto back = read(bytes);
    QTextTable *again = firstTable(back->body);
    QVERIFY(again != nullptr);
    QCOMPARE(again->cellAt(1, 0).format().toTableCellFormat().bottomBorder(), 0.0);
    QCOMPARE(again->cellAt(0, 1).format().toTableCellFormat().background().color(), QColor(0xD9, 0xE2, 0xF3));
    // The first row's bottom gave way too (Word resolves the shared edge).
    QCOMPARE(again->cellAt(0, 0).format().toTableCellFormat().bottomBorder(), 0.0);
}

// A table from HTML or an older Write (Qt's separate borders, spaced
// cells) takes Word's model at its first border command: collapsed edges,
// its frame line as w:tblBorders, its padding as cell margins. A big table
// formats in well under a second.
void DocxTablesTest::qtTablesTakeWordsModel()
{
    QTextDocument doc;
    Docx::WordParts parts;
    QTextCursor cursor(&doc);
    QTextTableFormat html;
    html.setBorder(1);
    html.setCellSpacing(2);
    html.setCellPadding(4);
    QTextTable *table = cursor.insertTable(120, 6, html);
    QElapsedTimer timer;
    timer.start();
    Docx::applyCellShading(table, Docx::wholeTable(table), QColor(0xED, 0xED, 0xED));
    Docx::applyTableBorders(table, {1, 1, 100, 4}, Docx::BorderTarget::All, pen("single", 8, "auto"));
    QVERIFY2(timer.elapsed() < 3000, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(table->format().borderCollapse());
    QCOMPARE(table->format().cellSpacing(), 0.0);
    QCOMPARE(Docx::parseBordersSpec(table->format().stringProperty(Docx::kTableBorders)).value(QStringLiteral("top")).size, 6);
    QCOMPARE(table->cellAt(0, 0).format().toTableCellFormat().leftPadding(), 4.0);
    const QByteArray bytes = Docx::docxBytes(doc, parts);
    checkSchema(bytes);
    const QByteArray xml = Ooxml::Package::fromData(bytes)->data(QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:tblCellMar><w:top w:w=\"60\" w:type=\"dxa\"/>"));
    // Qt's frame line is grey: the table keeps its look.
    QVERIFY(xml.contains("<w:insideH w:val=\"single\" w:sz=\"6\" w:space=\"0\" w:color=\"808080\"/>"));
    QVERIFY(xml.contains("<w:tcBorders><w:top w:val=\"single\" w:sz=\"8\" w:space=\"0\" w:color=\"auto\"/>"));
}

QTEST_MAIN(DocxTablesTest)
#include "tst_docx_tables.moc"
