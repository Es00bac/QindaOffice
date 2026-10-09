// SPDX-License-Identifier: GPL-3.0-or-later
// M70 gates for Word's paragraph and font formatting (D-079,
// docx_paragraph_format.h): a paragraph's own spacing folded as the reader
// folds Word's, contextual spacing, outline level, the line and page break
// switches, tab stops with leaders (a style's stop cleared), borders and
// shading; underline styles and colour, double strikethrough, caps, hidden
// text, character spacing, scale, position and kerning. Each written as
// Word writes it, validated against the schemas, read back equal; a sample
// is rendered by LibreOffice when it is installed (QQO_GRAB_DIR keeps the
// PDF and PNG pages for review).

#include "docx_fixture.h"
#include "docx_format.h"
#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_table_format.h"
#include "docx_writer.h"
#include "ooxml_units.h"
#include "support/ooxml_schema.h"
#include "support/soffice_env.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QtTest>

using namespace QindaOffice;
using Test::DocxFixture;
using Test::para;

namespace {

const char *kStyles =
    R"(<w:docDefaults><w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="259" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>)"
    R"(<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>)"
    R"(<w:style w:type="paragraph" w:styleId="TOC1"><w:name w:val="toc 1"/><w:basedOn w:val="Normal"/>)"
    R"(<w:pPr><w:tabs><w:tab w:val="right" w:leader="dot" w:pos="9350"/></w:tabs><w:spacing w:after="100"/></w:pPr></w:style>)";

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

struct Doc {
    QTextDocument body;
    Docx::WordParts parts;
};

std::unique_ptr<Doc> read(const QByteArray &bytes)
{
    auto doc = std::make_unique<Doc>();
    if (!Docx::readDocx(Ooxml::Package::fromData(bytes), &doc->body, &doc->parts)) {
        return nullptr;
    }
    return doc;
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

QByteArray documentXml(const QByteArray &bytes)
{
    return Ooxml::Package::fromData(bytes)->data(QStringLiteral("word/document.xml"));
}

QByteArray sourcePackage()
{
    DocxFixture f;
    f.styles = QString::fromLatin1(kStyles);
    f.body = para(QStringLiteral("One")) + para(QStringLiteral("Two")) + para(QStringLiteral("Three"))
        + para(QStringLiteral("Contents\t1"), QStringLiteral("<w:pStyle w:val=\"TOC1\"/>"))
        + para(QStringLiteral("Styled"), QString(), QStringLiteral("<w:b/>"));
    return f.build();
}

} // namespace

class DocxParagraphsTest : public QObject {
    Q_OBJECT
private slots:
    void spacingFoldsLikeTheReader();
    void paragraphSwitchesRoundTrip();
    void tabStopsWithLeaders();
    void bordersAndShading();
    void fontFormattingRoundTrip();
    void libreOfficeRendersTheSample();
};

void DocxParagraphsTest::spacingFoldsLikeTheReader()
{
    auto doc = read(sourcePackage());
    QVERIFY(doc);
    const QTextBlock two = blockWithText(doc->body, QStringLiteral("Two"));
    QCOMPARE(Docx::ownSpacing(two.blockFormat()).after, 160);
    // 12 pt before, nothing after: the gap above grows by 12 pt, the one
    // below loses Two's 8 pt.
    Docx::setParagraphSpacing(two, {240, 0, false, false, false});
    const qreal twoTop = blockWithText(doc->body, QStringLiteral("Two")).blockFormat().topMargin();
    const qreal threeTop = blockWithText(doc->body, QStringLiteral("Three")).blockFormat().topMargin();
    QCOMPARE(qRound(twoTop * 15), 160 + 240);
    QCOMPARE(qRound(threeTop * 15), 0);
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    QVERIFY(documentXml(bytes).contains("<w:spacing w:before=\"240\" w:after=\"0\"/>"));
    // The file folds to what the edit showed.
    auto back = read(bytes);
    QCOMPARE(blockWithText(back->body, QStringLiteral("Two")).blockFormat().topMargin(), twoTop);
    QCOMPARE(blockWithText(back->body, QStringLiteral("Three")).blockFormat().topMargin(), threeTop);
    const Docx::OwnSpacing own = Docx::ownSpacing(blockWithText(back->body, QStringLiteral("Two")).blockFormat());
    QCOMPARE(own.before, 240);
    QCOMPARE(own.after, 0);
    // Contextual spacing between paragraphs of one style takes the gap away.
    QTextBlock one = blockWithText(back->body, QStringLiteral("One"));
    Docx::setParagraphSpacing(one, {0, 160, false, false, true});
    QCOMPARE(qRound(blockWithText(back->body, QStringLiteral("Two")).blockFormat().topMargin() * 15), 240);
    const QByteArray again = Docx::docxBytes(back->body, back->parts);
    checkSchema(again);
    QVERIFY(documentXml(again).contains("<w:contextualSpacing/>"));
    auto third = read(again);
    QCOMPARE(qRound(blockWithText(third->body, QStringLiteral("Two")).blockFormat().topMargin() * 15), 240);
}

void DocxParagraphsTest::paragraphSwitchesRoundTrip()
{
    auto doc = read(sourcePackage());
    QVERIFY(doc);
    QTextBlock three = blockWithText(doc->body, QStringLiteral("Three"));
    QTextBlockFormat f = three.blockFormat();
    Docx::setOutlineLevel(f, 2);
    f.setProperty(Docx::kWidowControl, false);
    f.setProperty(Docx::kKeepWithNext, true);
    f.setNonBreakableLines(true);
    f.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    f.setAlignment(Qt::AlignJustify);
    f.setLineHeight(Ooxml::Units::ptToPx(18), QTextBlockFormat::FixedHeight);
    f.setLeftMargin(48);
    f.setTextIndent(-24);
    QTextCursor(three).setBlockFormat(f);
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    const QByteArray xml = documentXml(bytes);
    QVERIFY2(xml.contains("<w:keepNext/><w:keepLines/><w:pageBreakBefore/><w:widowControl w:val=\"0\"/>"), xml.constData());
    QVERIFY(xml.contains("w:line=\"360\" w:lineRule=\"exact\"/>"));
    QVERIFY(xml.contains("<w:ind w:left=\"720\" w:right=\"0\" w:hanging=\"360\"/>"));
    QVERIFY(xml.contains("<w:jc w:val=\"both\"/><w:outlineLvl w:val=\"2\"/>"));
    auto back = read(bytes);
    const QTextBlockFormat g = blockWithText(back->body, QStringLiteral("Three")).blockFormat();
    QCOMPARE(Docx::outlineLevel(g), 2);
    QVERIFY(!g.boolProperty(Docx::kWidowControl) && g.hasProperty(Docx::kWidowControl));
    QVERIFY(g.boolProperty(Docx::kKeepWithNext));
    QVERIFY(g.nonBreakableLines());
    QVERIFY(Docx::ownPageBreaks(g) & QTextFormat::PageBreak_AlwaysBefore);
    QCOMPARE(Docx::ownLineSpacing(g).type, int(QTextBlockFormat::FixedHeight));
    QCOMPARE(g.textIndent(), -24.0);
}

void DocxParagraphsTest::tabStopsWithLeaders()
{
    auto doc = read(sourcePackage());
    QVERIFY(doc);
    const QTextBlock toc = blockWithText(doc->body, QStringLiteral("Contents\t1"));
    QList<Docx::TabStop> stops = Docx::tabStops(toc.blockFormat());
    QCOMPARE(stops.size(), 1);
    QCOMPARE(stops.first(), (Docx::TabStop{9350, QStringLiteral("right"), QStringLiteral("dot")}));
    // Replace the style's dotted stop with a dashed left stop and a
    // decimal one; Word needs the style's stop cleared.
    QTextBlockFormat f = toc.blockFormat();
    Docx::setTabStops(f, {{1440, QStringLiteral("left"), QStringLiteral("hyphen")},
                          {7200, QStringLiteral("decimal"), QStringLiteral("none")}});
    QTextCursor(toc).setBlockFormat(f);
    QCOMPARE(toc.blockFormat().tabPositions().size(), 2);
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    QVERIFY2(documentXml(bytes).contains("<w:tabs><w:tab w:val=\"left\" w:leader=\"hyphen\" w:pos=\"1440\"/>"
                                         "<w:tab w:val=\"decimal\" w:pos=\"7200\"/><w:tab w:val=\"clear\" w:pos=\"9350\"/></w:tabs>"),
             documentXml(bytes).constData());
    auto back = read(bytes);
    stops = Docx::tabStops(blockWithText(back->body, QStringLiteral("Contents\t1")).blockFormat());
    QCOMPARE(stops, (QList<Docx::TabStop>{{1440, QStringLiteral("left"), QStringLiteral("hyphen")},
                                          {7200, QStringLiteral("decimal"), QStringLiteral("none")}}));
    // The default stop distance lands in settings.
    back->parts.defaultTabStop = 72; // 0.75 in
    const QByteArray settings = Ooxml::Package::fromData(Docx::docxBytes(back->body, back->parts))
                                    ->data(QStringLiteral("word/settings.xml"));
    QVERIFY(settings.contains("<w:defaultTabStop w:val=\"1080\"/>"));
}

void DocxParagraphsTest::bordersAndShading()
{
    auto doc = read(sourcePackage());
    QVERIFY(doc);
    const QTextBlock one = blockWithText(doc->body, QStringLiteral("One"));
    QTextBlockFormat f = one.blockFormat();
    Docx::WordBorders box;
    for (const char *side : {"top", "left", "bottom", "right"}) {
        box.insert(QString::fromLatin1(side), {QStringLiteral("single"), 6, QStringLiteral("2F5496"),
                                               QLatin1String(side) == QLatin1String("left") || QLatin1String(side) == QLatin1String("right") ? 4 : 1});
    }
    Docx::setParagraphBorders(f, box);
    f.setBackground(QColor(0xDE, 0xEA, 0xF6));
    QTextCursor(one).setBlockFormat(f);
    QVERIFY(!one.blockFormat().stringProperty(Docx::kParagraphBorders).isEmpty());
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    QVERIFY2(documentXml(bytes).contains("<w:pBdr><w:top w:val=\"single\" w:sz=\"6\" w:space=\"1\" w:color=\"2F5496\"/>"
                                         "<w:left w:val=\"single\" w:sz=\"6\" w:space=\"4\" w:color=\"2F5496\"/>"),
             documentXml(bytes).constData());
    QVERIFY(documentXml(bytes).contains("</w:pBdr><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"DEEAF6\"/>"));
    auto back = read(bytes);
    const QTextBlockFormat g = blockWithText(back->body, QStringLiteral("One")).blockFormat();
    QCOMPARE(Docx::paragraphBorders(g), box);
    QCOMPARE(g.background().color(), QColor(0xDE, 0xEA, 0xF6));
    // Removing the box: nil sides only where the paragraph had borders.
    QTextBlockFormat h = g;
    Docx::WordBorders none;
    for (auto it = box.cbegin(); it != box.cend(); ++it) {
        none.insert(it.key(), Docx::WordBorder::none());
    }
    Docx::setParagraphBorders(h, none);
    QTextCursor(blockWithText(back->body, QStringLiteral("One"))).setBlockFormat(h);
    QVERIFY(h.stringProperty(Docx::kParagraphBorders).isEmpty());
    QVERIFY(documentXml(Docx::docxBytes(back->body, back->parts)).contains("<w:pBdr><w:top w:val=\"nil\"/>"));
}

void DocxParagraphsTest::fontFormattingRoundTrip()
{
    auto doc = read(sourcePackage());
    QVERIFY(doc);
    QTextCursor run(blockWithText(doc->body, QStringLiteral("Styled")));
    run.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QTextCharFormat f;
    f.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    f.setProperty(Docx::kUnderlineWord, QStringLiteral("double"));
    f.setUnderlineColor(QColor(0xC0, 0x00, 0x00));
    f.setFontStrikeOut(true);
    f.setProperty(Docx::kDoubleStrike, true);
    f.setFontCapitalization(QFont::SmallCaps);
    f.setProperty(Docx::kHiddenText, true);
    f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    f.setFontLetterSpacing(Ooxml::Units::ptToPx(2));
    f.setFontStretch(150);
    f.setBaselineOffset(3.0 / 11.0 * 100.0);
    f.setProperty(Docx::kKerning, 28);
    run.mergeCharFormat(f);
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(bytes);
    const QByteArray xml = documentXml(bytes);
    QVERIFY2(xml.contains("<w:smallCaps/><w:dstrike/><w:vanish/>"), xml.constData());
    QVERIFY(xml.contains("<w:spacing w:val=\"40\"/><w:w w:val=\"150\"/><w:kern w:val=\"28\"/>"));
    QVERIFY(xml.contains("<w:u w:val=\"double\" w:color=\"C00000\"/>"));
    QVERIFY(!xml.contains("<w:strike/>"));
    auto back = read(bytes);
    QTextCursor at(blockWithText(back->body, QStringLiteral("Styled")));
    at.movePosition(QTextCursor::NextCharacter);
    const QTextCharFormat g = at.charFormat();
    QCOMPARE(g.stringProperty(Docx::kUnderlineWord), QStringLiteral("double"));
    QCOMPARE(g.underlineColor(), QColor(0xC0, 0x00, 0x00));
    QVERIFY(g.boolProperty(Docx::kDoubleStrike) && g.fontStrikeOut());
    QCOMPARE(g.fontCapitalization(), QFont::SmallCaps);
    QVERIFY(g.boolProperty(Docx::kHiddenText));
    QCOMPARE(qRound(g.fontLetterSpacing() * 15), 40);
    QCOMPARE(g.fontStretch(), 150);
    QCOMPARE(g.intProperty(Docx::kKerning), 28);
    QCOMPARE(g.fontWeight(), int(QFont::Bold)); // the run's own bold stayed
}

void DocxParagraphsTest::libreOfficeRendersTheSample()
{
    if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) {
        QSKIP("LibreOffice is not installed");
    }
    // A new document: a dotted contents line, a boxed and shaded note and a
    // shaded, bordered table.
    QTextDocument doc;
    Docx::WordParts parts;
    QTextCursor c(&doc);
    QTextBlockFormat line;
    Docx::setTabStops(line, {{9000, QStringLiteral("right"), QStringLiteral("dot")}});
    c.setBlockFormat(line);
    c.insertText(QStringLiteral("Chapter one\t7"));
    QTextBlockFormat note;
    Docx::WordBorders box;
    for (const char *side : {"top", "left", "bottom", "right"}) {
        box.insert(QString::fromLatin1(side), {QStringLiteral("double"), 6, QStringLiteral("C00000"), 4});
    }
    Docx::setParagraphBorders(note, box);
    note.setBackground(QColor(0xFF, 0xF2, 0xCC));
    c.insertBlock(note);
    c.insertText(QStringLiteral("Boxed and shaded note"));
    c.insertBlock(QTextBlockFormat());
    QTextTable *table = c.insertTable(3, 3, Docx::newWordTableFormat(3, parts.layout.contentWidth()));
    Docx::initWordTable(table);
    Docx::applyCellShading(table, {0, 0, 1, 3}, QColor(0x1F, 0x4E, 0x79));
    Docx::applyTableBorders(table, Docx::wholeTable(table), Docx::BorderTarget::Outside,
                            {QStringLiteral("thick"), 18, QStringLiteral("1F4E79"), 0});
    Docx::applyTableBorders(table, {1, 1, 2, 2}, Docx::BorderTarget::All,
                            {QStringLiteral("dashed"), 8, QStringLiteral("C00000"), 0});
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) {
            table->cellAt(r, k).firstCursorPosition().insertText(QStringLiteral("R%1C%2").arg(r + 1).arg(k + 1));
        }
    }
    const QString keep = qEnvironmentVariable("QQO_GRAB_DIR");
    QTemporaryDir scratch;
    const QString dir = keep.isEmpty() ? scratch.path() : keep;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("m70_formatting_sample.docx"));
    QString error;
    QVERIFY2(Docx::writeDocx(path, doc, parts, {}, &error), qPrintable(error));
    QTemporaryDir profile;
    QProcess soffice;
    Test::startSoffice(soffice, {QStringLiteral("-env:UserInstallation=file://") + profile.path(), QStringLiteral("--headless"),
                                 QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), dir, path});
    QVERIFY(soffice.waitForFinished(120000));
    const QString pdf = QDir(dir).filePath(QStringLiteral("m70_formatting_sample.pdf"));
    QVERIFY2(QFileInfo(pdf).size() > 1000, qPrintable(QString::fromUtf8(soffice.readAllStandardError())));
    if (!keep.isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()) {
        QProcess::execute(QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("70"),
                                                        pdf, QDir(dir).filePath(QStringLiteral("m70_formatting_sample"))});
    }
}

QTEST_MAIN(DocxParagraphsTest)
#include "tst_docx_paragraphs.moc"
