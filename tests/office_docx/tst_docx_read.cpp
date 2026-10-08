// SPDX-License-Identifier: GPL-3.0-or-later
// M60 gates for the .docx reader (D-068): hand-typed WordprocessingML read
// into QTextDocument + WordParts.

#include "docx_fixture.h"
#include "docx_properties.h"
#include "docx_reader.h"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QtTest>

using namespace QindaOffice;
using Test::DocxFixture;
using Test::para;

class DocxReadTest : public QObject {
    Q_OBJECT
private slots:
    void resolvesStylesThemeFontsAndDirectFormatting();
    void foldsParagraphSpacingLikeWord();
    void numbersListsLikeWord();
    void tablesKeepMergesShadingAndBorders();
    void tableStyleConditionsAndCellSpacing();
    void picturesKeepTheirBytes();
    void preservesWhatItCannotEdit();
    void readsLayoutHeadersFootersAndFields();
    void readsFootnotesLinksBookmarksAndRevisions();
    void pageBreaksAndSymbols();
    void rejectsWhatIsNotWord();
};

namespace {

struct Loaded {
    QTextDocument body;
    Docx::WordParts parts;
    bool ok = false;
    QString error;
};

std::unique_ptr<Loaded> load(const DocxFixture &fixture)
{
    auto out = std::make_unique<Loaded>();
    const auto package = Ooxml::Package::fromData(fixture.build(), &out->error);
    out->ok = package && Docx::readDocx(package, &out->body, &out->parts, &out->error);
    return out;
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

const QString kStyles = QStringLiteral(
    "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:asciiTheme=\"minorHAnsi\" w:hAnsiTheme=\"minorHAnsi\"/>"
    "<w:sz w:val=\"22\"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=\"160\" w:line=\"259\""
    " w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>"
    "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/>"
    "<w:pPr><w:keepNext/><w:spacing w:before=\"240\" w:after=\"0\"/><w:outlineLvl w:val=\"0\"/></w:pPr>"
    "<w:rPr><w:rFonts w:asciiTheme=\"majorHAnsi\" w:hAnsiTheme=\"majorHAnsi\"/><w:b/><w:color w:val=\"2F5496\"/>"
    "<w:sz w:val=\"32\"/></w:rPr></w:style>"
    "<w:style w:type=\"character\" w:styleId=\"Strong\"><w:name w:val=\"Strong\"/><w:rPr><w:b/></w:rPr></w:style>"
    "<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\"><w:name w:val=\"Normal Table\"/>"
    "<w:tblPr><w:tblCellMar><w:left w:w=\"108\" w:type=\"dxa\"/><w:right w:w=\"108\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr></w:style>"
    "<w:style w:type=\"table\" w:styleId=\"TableGrid\"><w:name w:val=\"Table Grid\"/><w:basedOn w:val=\"TableNormal\"/>"
    "<w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr><w:tblPr><w:tblBorders>"
    "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/><w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
    "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/><w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
    "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/><w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
    "</w:tblBorders></w:tblPr><w:tblStylePr w:type=\"firstRow\"><w:rPr><w:b/></w:rPr><w:tcPr><w:shd w:val=\"clear\""
    " w:color=\"auto\" w:fill=\"4472C4\"/></w:tcPr></w:tblStylePr></w:style>");

const QString kTheme = QStringLiteral(
    "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"T\"><a:themeElements>"
    "<a:clrScheme name=\"c\"><a:dk1><a:srgbClr val=\"000000\"/></a:dk1><a:lt1><a:srgbClr val=\"FFFFFF\"/></a:lt1>"
    "<a:dk2><a:srgbClr val=\"44546A\"/></a:dk2><a:lt2><a:srgbClr val=\"E7E6E6\"/></a:lt2>"
    "<a:accent1><a:srgbClr val=\"4472C4\"/></a:accent1></a:clrScheme><a:fontScheme name=\"f\">"
    "<a:majorFont><a:latin typeface=\"Aptos Display\"/></a:majorFont><a:minorFont><a:latin typeface=\"Aptos\"/>"
    "</a:minorFont></a:fontScheme></a:themeElements></a:theme>");

QString cell(const QString &text, const QString &tcPr = QString())
{
    return QStringLiteral("<w:tc>%1%2</w:tc>").arg(tcPr.isEmpty() ? QString() : QStringLiteral("<w:tcPr>%1</w:tcPr>").arg(tcPr), para(text));
}

} // namespace

void DocxReadTest::resolvesStylesThemeFontsAndDirectFormatting()
{
    DocxFixture f;
    f.styles = kStyles;
    f.theme = kTheme;
    f.body = para(QStringLiteral("Title text"), QStringLiteral("<w:pStyle w:val=\"Heading1\"/>"))
        + QStringLiteral("<w:p><w:r><w:t xml:space=\"preserve\">plain </w:t></w:r><w:r><w:rPr><w:rStyle w:val=\"Strong\"/>"
                         "<w:i/><w:u w:val=\"double\"/><w:highlight w:val=\"yellow\"/></w:rPr><w:t>bold italic</w:t></w:r></w:p>");
    auto doc = load(f);
    QVERIFY2(doc->ok, qPrintable(doc->error));
    const QTextBlock heading = blockWithText(doc->body, QStringLiteral("Title text"));
    QVERIFY(heading.isValid());
    QCOMPARE(heading.blockFormat().headingLevel(), 1);
    QCOMPARE(heading.blockFormat().intProperty(Docx::kWriteStyleIdProperty), 2); // StyleCatalog::Heading1
    QCOMPARE(heading.blockFormat().stringProperty(Docx::kParagraphStyleId), QStringLiteral("Heading1"));
    const QTextCharFormat hf = heading.begin().fragment().charFormat();
    QCOMPARE(hf.fontFamilies().toStringList().value(0), QStringLiteral("Aptos Display"));
    QCOMPARE(hf.fontPointSize(), 16.0);
    QCOMPARE(hf.fontWeight(), int(QFont::Bold));
    QCOMPARE(hf.foreground().color(), QColor(0x2F, 0x54, 0x96));
    const QTextBlock body = heading.next();
    auto it = body.begin();
    const QTextCharFormat plain = it.fragment().charFormat();
    QCOMPARE(plain.fontFamilies().toStringList().value(0), QStringLiteral("Aptos"));
    QCOMPARE(plain.fontPointSize(), 11.0);
    QCOMPARE(plain.fontWeight(), int(QFont::Normal));
    ++it;
    const QTextCharFormat styled = it.fragment().charFormat();
    QCOMPARE(it.fragment().text(), QStringLiteral("bold italic"));
    QCOMPARE(styled.fontWeight(), int(QFont::Bold));
    QVERIFY(styled.fontItalic());
    QCOMPARE(styled.underlineStyle(), QTextCharFormat::SingleUnderline);
    QCOMPARE(styled.background().color(), QColor(Qt::yellow));
    QCOMPARE(styled.stringProperty(Docx::kRunStyleId), QStringLiteral("Strong"));
    QVERIFY(styled.hasProperty(Docx::kRunSnapshot));
}

void DocxReadTest::foldsParagraphSpacingLikeWord()
{
    // Word adds 8 pt after + 12 pt before = 20 pt; Qt keeps the larger of
    // two margins, so the whole gap must sit on the second block.
    DocxFixture f;
    f.styles = kStyles;
    f.body = para(QStringLiteral("one")) + para(QStringLiteral("two"), QStringLiteral("<w:pStyle w:val=\"Heading1\"/>"))
        + para(QStringLiteral("a"), QStringLiteral("<w:spacing w:before=\"100\" w:after=\"100\"/><w:contextualSpacing/>"))
        + para(QStringLiteral("b"), QStringLiteral("<w:spacing w:before=\"100\" w:after=\"300\"/><w:contextualSpacing/>"));
    auto doc = load(f);
    QVERIFY(doc->ok);
    const QTextBlock one = blockWithText(doc->body, QStringLiteral("one"));
    const QTextBlock two = blockWithText(doc->body, QStringLiteral("two"));
    QCOMPARE(one.blockFormat().bottomMargin(), 0.0);
    QCOMPARE(two.blockFormat().topMargin(), (160.0 + 240.0) / 15.0);
    // Same style, contextual spacing: no gap between a and b.
    QCOMPARE(blockWithText(doc->body, QStringLiteral("b")).blockFormat().topMargin(), 0.0);
    // The last paragraph keeps its own space after.
    QCOMPARE(blockWithText(doc->body, QStringLiteral("b")).blockFormat().bottomMargin(), 300.0 / 15.0);
    // Line spacing 259/240 = 107.9 %.
    QCOMPARE(qRound(one.blockFormat().lineHeight()), 108);
    QCOMPARE(one.blockFormat().lineHeightType(), int(QTextBlockFormat::ProportionalHeight));
}

void DocxReadTest::numbersListsLikeWord()
{
    DocxFixture f;
    f.numbering = QStringLiteral(
        "<w:abstractNum w:abstractNumId=\"0\"><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/>"
        "<w:lvlText w:val=\"%1.\"/><w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:lvl>"
        "<w:lvl w:ilvl=\"1\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%1.%2.\"/>"
        "<w:pPr><w:ind w:left=\"1440\" w:hanging=\"360\"/></w:pPr></w:lvl></w:abstractNum>"
        "<w:abstractNum w:abstractNumId=\"1\"><w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"bullet\"/>"
        "<w:lvlText w:val=\"\xEF\x82\xB7\"/><w:rPr><w:rFonts w:ascii=\"Symbol\" w:hAnsi=\"Symbol\"/></w:rPr></w:lvl></w:abstractNum>"
        "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num><w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>");
    auto item = [](const QString &text, int numId, int ilvl) {
        return para(text, QStringLiteral("<w:numPr><w:ilvl w:val=\"%1\"/><w:numId w:val=\"%2\"/></w:numPr>").arg(ilvl).arg(numId));
    };
    f.body = item(QStringLiteral("first"), 1, 0) + item(QStringLiteral("sub a"), 1, 1) + item(QStringLiteral("sub b"), 1, 1)
        + para(QStringLiteral("between")) + item(QStringLiteral("second"), 1, 0) + item(QStringLiteral("sub c"), 1, 1)
        + item(QStringLiteral("dot"), 2, 0);
    auto doc = load(f);
    QVERIFY(doc->ok);
    QTextList *top = blockWithText(doc->body, QStringLiteral("first")).textList();
    QVERIFY(top != nullptr);
    QCOMPARE(blockWithText(doc->body, QStringLiteral("second")).textList(), top); // continues past "between"
    QCOMPARE(top->itemNumber(blockWithText(doc->body, QStringLiteral("second"))), 1);
    QTextList *subA = blockWithText(doc->body, QStringLiteral("sub a")).textList();
    QTextList *subC = blockWithText(doc->body, QStringLiteral("sub c")).textList();
    QVERIFY(subA != nullptr && subC != nullptr && subA != subC); // restarted under "second"
    QCOMPARE(blockWithText(doc->body, QStringLiteral("sub b")).textList(), subA);
    QCOMPARE(subA->format().numberPrefix(), QStringLiteral("1."));
    QCOMPARE(subC->format().numberPrefix(), QStringLiteral("2."));
    QCOMPARE(subC->itemText(blockWithText(doc->body, QStringLiteral("sub c"))), QStringLiteral("2.1."));
    QCOMPARE(subA->format().intProperty(Docx::kListNumId), 1);
    QCOMPARE(subA->format().intProperty(Docx::kListLevel), 1);
    const QTextBlock bullet = blockWithText(doc->body, QStringLiteral("dot"));
    QCOMPARE(bullet.textList()->format().style(), QTextListFormat::ListDisc);
    // Word's hanging indent: the text starts at the left indent (0.5").
    QCOMPARE(blockWithText(doc->body, QStringLiteral("first")).blockFormat().leftMargin(), 48.0);
    QCOMPARE(blockWithText(doc->body, QStringLiteral("first")).blockFormat().textIndent(), 0.0);
}

void DocxReadTest::tablesKeepMergesShadingAndBorders()
{
    DocxFixture f;
    f.styles = kStyles;
    const QString shade = QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"FFF2CC\"/>");
    f.body = QStringLiteral(
        "<w:tbl><w:tblPr><w:tblStyle w:val=\"TableGrid\"/><w:tblW w:w=\"0\" w:type=\"auto\"/>"
        "<w:tblLook w:firstRow=\"0\" w:lastRow=\"0\" w:firstColumn=\"0\" w:lastColumn=\"0\" w:noHBand=\"1\" w:noVBand=\"1\"/></w:tblPr>"
        "<w:tblGrid><w:gridCol w:w=\"2000\"/><w:gridCol w:w=\"2000\"/><w:gridCol w:w=\"2000\"/></w:tblGrid>"
        "<w:tr>%1%2</w:tr><w:tr>%3%4%5</w:tr><w:tr>%6%7%8</w:tr></w:tbl>")
        .arg(cell(QStringLiteral("wide"), QStringLiteral("<w:gridSpan w:val=\"2\"/>") + shade),
             cell(QStringLiteral("tall"), QStringLiteral("<w:vMerge w:val=\"restart\"/><w:vAlign w:val=\"center\"/>")),
             cell(QStringLiteral("a")), cell(QStringLiteral("b")), cell(QString(), QStringLiteral("<w:vMerge/>")),
             cell(QStringLiteral("c")), cell(QStringLiteral("d")), cell(QStringLiteral("e")))
        + para(QStringLiteral("after"));
    auto doc = load(f);
    QVERIFY(doc->ok);
    QTextTable *table = firstTable(doc->body);
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 3);
    QCOMPARE(table->columns(), 3);
    const QTextTableCell wide = table->cellAt(0, 0);
    QCOMPARE(wide.columnSpan(), 2); // the first paragraph's format must not reset it
    QCOMPARE(table->cellAt(0, 2).rowSpan(), 2);
    QCOMPARE(table->cellAt(1, 2).row(), 0);
    const QTextTableCellFormat wf = wide.format().toTableCellFormat();
    QCOMPARE(wf.background().color(), QColor(0xFF, 0xF2, 0xCC));
    QCOMPARE(wf.leftPadding(), 108.0 / 15.0);
    QVERIFY(wf.topBorder() > 0); // tblBorders from Table Grid
    QCOMPARE(table->cellAt(0, 2).format().toTableCellFormat().verticalAlignment(), QTextCharFormat::AlignMiddle);
    QVERIFY(table->format().borderCollapse());
    QCOMPARE(table->format().columnWidthConstraints().value(1).rawValue(), 2000.0 / 15.0);
    // Table Grid's own pPr beats Normal: no space after inside cells.
    const QTextBlock a = blockWithText(doc->body, QStringLiteral("a"));
    QCOMPARE(a.blockFormat().bottomMargin(), 0.0);
    QCOMPARE(qRound(a.blockFormat().lineHeight()), 100);
    QVERIFY(blockWithText(doc->body, QStringLiteral("after")).isValid());
}

void DocxReadTest::tableStyleConditionsAndCellSpacing()
{
    DocxFixture f;
    f.styles = kStyles;
    f.body = QStringLiteral(
        "<w:tbl><w:tblPr><w:tblStyle w:val=\"TableGrid\"/><w:tblLook w:val=\"04A0\" w:firstRow=\"1\" w:lastRow=\"0\""
        " w:firstColumn=\"1\" w:lastColumn=\"0\" w:noHBand=\"0\" w:noVBand=\"1\"/></w:tblPr>"
        "<w:tblGrid><w:gridCol w:w=\"3000\"/><w:gridCol w:w=\"3000\"/></w:tblGrid>"
        "<w:tr><w:trPr><w:tblHeader/></w:trPr>%1%2</w:tr><w:tr>%3%4</w:tr></w:tbl>")
        .arg(cell(QStringLiteral("H1")), cell(QStringLiteral("H2")), cell(QStringLiteral("x")), cell(QStringLiteral("y")))
        + para(QString());
    auto doc = load(f);
    QVERIFY(doc->ok);
    QTextTable *table = firstTable(doc->body);
    QVERIFY(table != nullptr);
    QCOMPARE(table->format().headerRowCount(), 1);
    QCOMPARE(table->cellAt(0, 1).format().toTableCellFormat().background().color(), QColor(0x44, 0x72, 0xC4));
    QVERIFY(!table->cellAt(1, 1).format().toTableCellFormat().hasProperty(QTextFormat::BackgroundBrush));
    const QTextBlock h2 = blockWithText(doc->body, QStringLiteral("H2"));
    QCOMPARE(h2.begin().fragment().charFormat().fontWeight(), int(QFont::Bold)); // firstRow rPr
}

void DocxReadTest::picturesKeepTheirBytes()
{
    DocxFixture f;
    f.media.insert(QStringLiteral("rIdPic"), Test::pngBytes(40, 20));
    f.body = QStringLiteral(
        "<w:p><w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"952500\" cy=\"476250\"/>"
        "<wp:docPr id=\"5\" name=\"Logo\" descr=\"Company logo\"/><a:graphic><a:graphicData"
        " uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr><pic:cNvPr id=\"0\" name=\"logo.png\"/>"
        "<pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"rIdPic\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"952500\" cy=\"476250\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/>"
        "</a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>");
    auto doc = load(f);
    QVERIFY(doc->ok);
    const QTextBlock b = doc->body.begin();
    const QTextCharFormat cf = b.begin().fragment().charFormat();
    QVERIFY(cf.isImageFormat());
    const QTextImageFormat image = cf.toImageFormat();
    QCOMPARE(image.width(), 100.0);
    QCOMPARE(image.height(), 50.0);
    QCOMPARE(doc->parts.media.value(image.name()), Test::pngBytes(40, 20));
    QVERIFY(image.stringProperty(Docx::kDrawingXml).contains(QStringLiteral("Company logo")));
    QVERIFY(!doc->body.resource(QTextDocument::ImageResource, QUrl(image.name())).isNull());
}

void DocxReadTest::preservesWhatItCannotEdit()
{
    DocxFixture f;
    f.extras.insert(QStringLiteral("rIdChart"),
                    {QStringLiteral("word/charts/chart1.xml"), QString::fromUtf16(Ooxml::RelType::Chart),
                     QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml"),
                     QByteArray("<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"/>")});
    f.body = QStringLiteral(
        "<w:p><w:r><w:drawing><wp:inline><wp:extent cx=\"1905000\" cy=\"952500\"/><wp:docPr id=\"9\" name=\"Chart 9\"/>"
        "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"><c:chart r:id=\"rIdChart\"/>"
        "</a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>"
        "<w:p><m:oMath><m:r><m:t>x=2</m:t></m:r></m:oMath></w:p>");
    auto doc = load(f);
    QVERIFY(doc->ok);
    const QTextCharFormat chart = doc->body.begin().begin().fragment().charFormat();
    QVERIFY(chart.hasProperty(Docx::kInlineXml));
    QCOMPARE(chart.stringProperty(Docx::kInlineLabel), QStringLiteral("Chart"));
    QCOMPARE(chart.toImageFormat().width(), 200.0);
    const QTextBlock math = doc->body.begin().next();
    QCOMPARE(math.text(), QStringLiteral("x=2"));
    QVERIFY(math.begin().fragment().charFormat().stringProperty(Docx::kInlineXml).contains(QStringLiteral("oMath")));
    QVERIFY(!doc->parts.warnings.isEmpty());
    QVERIFY(doc->parts.warnings.first().contains(QStringLiteral("chart")));
}

void DocxReadTest::readsLayoutHeadersFootersAndFields()
{
    DocxFixture f;
    f.media.insert(QStringLiteral("rIdPic"), Test::pngBytes(10, 10));
    f.header = para(QStringLiteral("Letterhead"), QStringLiteral("<w:jc w:val=\"right\"/>"));
    f.footer = QStringLiteral("<w:p><w:r><w:t xml:space=\"preserve\">Page </w:t></w:r><w:r><w:fldChar w:fldCharType=\"begin\"/>"
                              "</w:r><w:r><w:instrText xml:space=\"preserve\"> PAGE </w:instrText></w:r><w:r>"
                              "<w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>1</w:t></w:r><w:r><w:fldChar"
                              " w:fldCharType=\"end\"/></w:r><w:r><w:t xml:space=\"preserve\"> of </w:t></w:r>"
                              "<w:fldSimple w:instr=\" NUMPAGES \"><w:r><w:t>3</w:t></w:r></w:fldSimple></w:p>");
    f.body = para(QStringLiteral("Body"))
        + QStringLiteral("<w:sectPr><w:headerReference w:type=\"default\" r:id=\"rIdHeader\"/><w:footerReference"
                         " w:type=\"default\" r:id=\"rIdFooter\"/><w:pgSz w:w=\"15840\" w:h=\"12240\" w:orient=\"landscape\"/>"
                         "<w:pgMar w:top=\"720\" w:right=\"1080\" w:bottom=\"1440\" w:left=\"1800\" w:header=\"360\""
                         " w:footer=\"540\" w:gutter=\"0\"/><w:titlePg/></w:sectPr>");
    auto doc = load(f);
    QVERIFY(doc->ok);
    const Docx::PageLayout &l = doc->parts.layout;
    QCOMPARE(l.pageSize, QSizeF(1056, 816));
    QVERIFY(l.landscape());
    QCOMPARE(l.margins, QMarginsF(120, 48, 72, 96));
    QCOMPARE(l.headerDistance, 24.0);
    QCOMPARE(l.footerDistance, 36.0);
    QVERIFY(doc->parts.differentFirstPage);
    const auto &header = doc->parts.header(Docx::HeaderFooterSlot::Default);
    QVERIFY(header.document);
    QCOMPARE(header.document->toPlainText(), QStringLiteral("Letterhead"));
    QCOMPARE(header.sourcePart, QStringLiteral("word/header1.xml"));
    QCOMPARE(header.document->begin().blockFormat().alignment() & Qt::AlignHorizontal_Mask, Qt::AlignRight);
    QVERIFY(!header.document->isModified());
    const auto &footer = doc->parts.footer(Docx::HeaderFooterSlot::Default);
    QVERIFY(footer.document);
    QCOMPARE(footer.document->toPlainText(), QStringLiteral("Page 1 of 3"));
    QStringList fields;
    for (auto it = footer.document->begin().begin(); !it.atEnd(); ++it) {
        if (it.fragment().charFormat().hasProperty(Docx::kFieldInstruction)) {
            fields.append(it.fragment().charFormat().stringProperty(Docx::kFieldInstruction).trimmed());
        }
    }
    QCOMPARE(fields, QStringList({QStringLiteral("PAGE"), QStringLiteral("NUMPAGES")}));
}

void DocxReadTest::readsFootnotesLinksBookmarksAndRevisions()
{
    DocxFixture f;
    f.links.insert(QStringLiteral("rIdLink"), QStringLiteral("https://example.org/a"));
    f.footnotes = QStringLiteral(
        "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
        "<w:footnote w:id=\"1\"><w:p><w:r><w:footnoteRef/></w:r><w:r><w:t xml:space=\"preserve\"> See the appendix.</w:t></w:r></w:p></w:footnote>");
    f.body = QStringLiteral(
        "<w:p><w:bookmarkStart w:id=\"0\" w:name=\"intro\"/><w:r><w:t>Intro</w:t></w:r><w:bookmarkEnd w:id=\"0\"/>"
        "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:footnoteReference w:id=\"1\"/></w:r></w:p>"
        "<w:p><w:hyperlink r:id=\"rIdLink\"><w:r><w:t>site</w:t></w:r></w:hyperlink><w:r><w:t xml:space=\"preserve\"> </w:t></w:r>"
        "<w:hyperlink w:anchor=\"intro\"><w:r><w:t>back</w:t></w:r></w:hyperlink></w:p>"
        "<w:p><w:ins w:id=\"1\" w:author=\"Ana\" w:date=\"2026-10-01T10:00:00Z\"><w:r><w:t>new</w:t></w:r></w:ins>"
        "<w:del w:id=\"2\" w:author=\"Ben\"><w:r><w:delText>old</w:delText></w:r></w:del></w:p>");
    auto doc = load(f);
    QVERIFY(doc->ok);
    const QTextBlock first = doc->body.begin();
    QCOMPARE(first.begin().fragment().charFormat().anchorNames(), QStringList(QStringLiteral("intro")));
    QString noteId;
    for (auto it = first.begin(); !it.atEnd(); ++it) {
        if (it.fragment().charFormat().hasProperty(Docx::kFootnoteIdProperty)) {
            noteId = it.fragment().charFormat().stringProperty(Docx::kFootnoteIdProperty);
        }
    }
    QCOMPARE(noteId, QStringLiteral("docx-1"));
    QCOMPARE(doc->parts.footnotes.value(noteId), QStringLiteral("See the appendix."));
    const QTextBlock links = first.next();
    auto it = links.begin();
    QCOMPARE(it.fragment().charFormat().anchorHref(), QStringLiteral("https://example.org/a"));
    ++it;
    ++it;
    QCOMPARE(it.fragment().charFormat().anchorHref(), QStringLiteral("#intro"));
    const QTextBlock revs = links.next();
    auto r = revs.begin();
    QCOMPARE(r.fragment().charFormat().stringProperty(Docx::kRevisionInsertProperty), QStringLiteral("Ana"));
    QCOMPARE(r.fragment().charFormat().property(Docx::kRevisionDate).toDateTime(),
             QDateTime(QDate(2026, 10, 1), QTime(10, 0), QTimeZone::UTC));
    ++r;
    QCOMPARE(r.fragment().text(), QStringLiteral("old"));
    QCOMPARE(r.fragment().charFormat().stringProperty(Docx::kRevisionDeleteProperty), QStringLiteral("Ben"));
    QVERIFY(r.fragment().charFormat().fontStrikeOut());
}

void DocxReadTest::pageBreaksAndSymbols()
{
    DocxFixture f;
    f.body = QStringLiteral(
        "<w:p><w:r><w:t>before</w:t></w:r><w:r><w:br w:type=\"page\"/></w:r><w:r><w:t>after</w:t></w:r></w:p>"
        "<w:p><w:r><w:br w:type=\"page\"/></w:r><w:r><w:t>top</w:t></w:r></w:p>"
        "<w:p><w:r><w:sym w:font=\"Wingdings\" w:char=\"F0FC\"/><w:tab/><w:t>done</w:t><w:br/><w:t>next</w:t></w:r></w:p>");
    auto doc = load(f);
    QVERIFY(doc->ok);
    QCOMPARE(doc->body.blockCount(), 4);
    const QTextBlock after = blockWithText(doc->body, QStringLiteral("after"));
    QVERIFY(after.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore);
    QVERIFY(after.blockFormat().boolProperty(Docx::kPageBreakRun));
    QVERIFY(blockWithText(doc->body, QStringLiteral("top")).blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore);
    QCOMPARE(doc->body.lastBlock().text(), QStringLiteral("✓\tdone") + QChar(QChar::LineSeparator) + QStringLiteral("next"));
}

void DocxReadTest::rejectsWhatIsNotWord()
{
    QTextDocument body;
    Docx::WordParts parts;
    QString error;
    QVERIFY(!Docx::readDocx(Ooxml::Package::fromData(QByteArray("not a zip")), &body, &parts, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(DocxReadTest)
#include "tst_docx_read.moc"
