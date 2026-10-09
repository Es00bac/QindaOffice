// SPDX-License-Identifier: GPL-3.0-or-later
// M73 gates for Word's lists (D-082, docx_lists.h, docx_list_edit.h,
// docx_list_labels.h): the bullet, numbering and multilevel libraries
// written as real w:abstractNum / w:num, validated against the schemas and
// read back with the same markers; Restart at 1 written as a new w:num
// with w:lvlOverride/w:startOverride over the same definition, Continue
// Numbering as the same w:numId; an untouched Word list keeps its w:num and
// w:abstractNum byte for byte; markers counted as Word counts (multi-level
// text, legal numbering, Article/Section); Word's marker drawn instead of
// Qt's. LibreOffice renders a sample when it is installed (QQO_GRAB_DIR
// keeps the PDF and PNG pages).

#include "docx_fixture.h"
#include "docx_list_edit.h"
#include "docx_list_labels.h"
#include "docx_lists.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_writer.h"
#include "ooxml_namespaces.h"
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
#include <QTextList>
#include <QtTest>

using namespace QindaOffice;
using Test::DocxFixture;
using Test::para;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

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

// The markers of the paragraphs, in order, as "text=marker".
QStringList markers(const QTextDocument &doc)
{
    QStringList out;
    const QHash<int, Docx::ListMarker> all = Docx::listMarkers(doc, true);
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (all.contains(b.blockNumber())) {
            out.append(b.text() + QLatin1Char('=') + all.value(b.blockNumber()).text);
        }
    }
    return out;
}

QByteArray part(const QByteArray &bytes, const QString &name)
{
    return Ooxml::Package::fromData(bytes)->data(name);
}

// The w:num and w:abstractNum elements of a numbering part, as markup.
QStringList numberingElements(const QByteArray &xml)
{
    QStringList out;
    if (const Ooxml::XmlElementPtr root = Ooxml::parseXml(xml)) {
        for (const Ooxml::XmlChild &c : root->children) {
            if (c.element) {
                out.append(Ooxml::serializeFragment(*c.element));
            }
        }
    }
    return out;
}

// A Word document with one numbered list (two levels) and a bullet list.
QByteArray wordList()
{
    DocxFixture f;
    f.numbering = QStringLiteral(
        "<w:abstractNum w:abstractNumId=\"0\"><w:nsid w:val=\"1A2B3C4D\"/><w:multiLevelType w:val=\"hybridMultilevel\"/>"
        "<w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%1.\"/><w:lvlJc w:val=\"left\"/>"
        "<w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:lvl>"
        "<w:lvl w:ilvl=\"1\"><w:start w:val=\"1\"/><w:numFmt w:val=\"lowerLetter\"/><w:lvlText w:val=\"%2)\"/><w:lvlJc w:val=\"left\"/>"
        "<w:pPr><w:ind w:left=\"1440\" w:hanging=\"360\"/></w:pPr></w:lvl></w:abstractNum>"
        "<w:abstractNum w:abstractNumId=\"1\"><w:multiLevelType w:val=\"hybridMultilevel\"/><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/>"
        "<w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"\uF0B7\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr>"
        "<w:rPr><w:rFonts w:ascii=\"Symbol\" w:hAnsi=\"Symbol\" w:hint=\"default\"/></w:rPr></w:lvl></w:abstractNum>"
        "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num><w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>");
    auto item = [](const QString &text, int numId, int ilvl) {
        return para(text, QStringLiteral("<w:numPr><w:ilvl w:val=\"%1\"/><w:numId w:val=\"%2\"/></w:numPr>").arg(ilvl).arg(numId));
    };
    f.body = para(QStringLiteral("Intro")) + item(QStringLiteral("one"), 1, 0) + item(QStringLiteral("one a"), 1, 1)
        + item(QStringLiteral("two"), 1, 0) + item(QStringLiteral("three"), 1, 0) + item(QStringLiteral("four"), 1, 0)
        + para(QStringLiteral("Between")) + item(QStringLiteral("dot"), 2, 0);
    return f.build();
}

} // namespace

class DocxListsTest : public QObject {
    Q_OBJECT
private slots:
    void readListsAreWordLists();
    void untouchedListKeepsItsNumbering();
    void librariesWriteAndReadBack();
    void multilevelLabels();
    void restartAndContinueAsWordWritesThem();
    void levelsAndRemovalAreWritten();
    void libreOfficeRendersTheSample();
};

void DocxListsTest::readListsAreWordLists()
{
    auto doc = read(wordList());
    QVERIFY(doc);
    const QTextBlock one = blockWithText(doc->body, QStringLiteral("one"));
    const Docx::WordListItem item = Docx::wordListItem(one);
    QVERIFY(item.isValid());
    QCOMPARE(item.numId, 1);
    QCOMPARE(item.abstractKey, QStringLiteral("src:0"));
    // Word's marker replaces Qt's: the block switches Qt's off.
    QCOMPARE(one.blockFormat().intProperty(QTextFormat::ListStyle), int(QTextListFormat::ListStyleUndefined));
    QCOMPARE(one.blockFormat().doubleProperty(Docx::kListHanging), 24.0); // 0.25 in
    QCOMPARE(one.blockFormat().leftMargin(), 48.0);
    QCOMPARE(markers(doc->body), (QStringList{QStringLiteral("one=1."), QStringLiteral("one a=a)"), QStringLiteral("two=2."),
                                              QStringLiteral("three=3."), QStringLiteral("four=4."),
                                              QStringLiteral("dot=•")}));
}

void DocxListsTest::untouchedListKeepsItsNumbering()
{
    const QByteArray source = wordList();
    auto doc = read(source);
    QVERIFY(doc);
    // An edit elsewhere: the lists stay as Word wrote them.
    QTextCursor(blockWithText(doc->body, QStringLiteral("Intro"))).insertText(QStringLiteral("New "));
    const QByteArray saved = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(saved);
    QCOMPARE(numberingElements(part(saved, QStringLiteral("word/numbering.xml"))),
             numberingElements(part(source, QStringLiteral("word/numbering.xml"))));
    const QByteArray xml = part(saved, QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:numPr><w:ilvl w:val=\"1\"/><w:numId w:val=\"1\"/></w:numPr>"));
    QVERIFY(!xml.contains("<w:ind")); // the level's indents, nothing of the paragraph's own
}

void DocxListsTest::librariesWriteAndReadBack()
{
    QTextDocument doc;
    Docx::WordParts parts;
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("Tasks"));
    for (const char *text : {"Buy", "Cook", "Eat", "First", "Second", "Arrow", "Dash"}) {
        c.insertBlock();
        c.insertText(QString::fromLatin1(text));
    }
    c.beginEditBlock();
    Docx::makeList({blockWithText(doc, QStringLiteral("Buy")), blockWithText(doc, QStringLiteral("Cook")),
                    blockWithText(doc, QStringLiteral("Eat"))},
                   Docx::bulletDefinition(QString(QChar(0xF0B7)), QStringLiteral("Symbol")));
    Docx::makeList({blockWithText(doc, QStringLiteral("First")), blockWithText(doc, QStringLiteral("Second"))},
                   Docx::numberingDefinition(QStringLiteral("upperRoman"), QStringLiteral("%1."), QStringLiteral("right")));
    Docx::makeList({blockWithText(doc, QStringLiteral("Arrow"))}, Docx::bulletDefinition(QString(QChar(0xF0D8)), QStringLiteral("Wingdings"), QStringLiteral("C00000")));
    Docx::makeList({blockWithText(doc, QStringLiteral("Dash"))}, Docx::bulletDefinition(QString(QChar(0x2013)), QString()));
    Docx::layoutListText(&doc, parts.defaultTabStop);
    c.endEditBlock();
    const QStringList expected{QStringLiteral("Buy=•"),   QStringLiteral("Cook=•"),  QStringLiteral("Eat=•"),
                               QStringLiteral("First=I."),     QStringLiteral("Second=II."),  QStringLiteral("Arrow=➢"),
                               QStringLiteral("Dash=–")};
    QCOMPARE(markers(doc), expected);
    // One Undo takes the four lists away.
    doc.undo();
    QVERIFY(blockWithText(doc, QStringLiteral("Buy")).textList() == nullptr);
    QVERIFY(markers(doc).isEmpty());
    doc.redo();
    QCOMPARE(markers(doc), expected);

    const QByteArray saved = Docx::docxBytes(doc, parts);
    checkSchema(saved);
    const QByteArray numbering = part(saved, QStringLiteral("word/numbering.xml"));
    QVERIFY2(numbering.contains("<w:numFmt w:val=\"upperRoman\"/><w:lvlText w:val=\"%1.\"/><w:lvlJc w:val=\"right\"/>"),
             numbering.constData());
    QVERIFY(numbering.contains("<w:rFonts w:ascii=\"Wingdings\" w:hAnsi=\"Wingdings\" w:hint=\"default\"/><w:color w:val=\"C00000\"/>"));
    QVERIFY(numbering.contains("<w:nsid w:val="));
    QCOMPARE(numbering.count("<w:abstractNum "), 4);
    QCOMPARE(numbering.count("<w:num "), 4);
    auto back = read(saved);
    QVERIFY(back);
    QCOMPARE(markers(back->body), expected);
    const QTextBlock first = blockWithText(back->body, QStringLiteral("First"));
    QCOMPARE(first.textList()->format().style(), QTextListFormat::ListUpperRoman);
    QCOMPARE(Docx::listMarker(first).justification, QStringLiteral("right"));
    QCOMPARE(Docx::listMarker(blockWithText(back->body, QStringLiteral("Arrow"))).format.foreground().color(), QColor(0xC0, 0x00, 0x00));
}

void DocxListsTest::multilevelLabels()
{
    QTextDocument doc;
    QTextCursor c(&doc);
    const QStringList texts{QStringLiteral("A"), QStringLiteral("A1"), QStringLiteral("A1x"), QStringLiteral("A2"),
                            QStringLiteral("B"), QStringLiteral("B1")};
    const QList<int> levels{0, 1, 2, 1, 0, 1};
    c.insertText(texts.first());
    for (qsizetype i = 1; i < texts.size(); ++i) {
        c.insertBlock();
        c.insertText(texts.at(i));
    }
    QList<QTextBlock> blocks;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        blocks.append(b);
    }
    Docx::makeList(blocks, Docx::multilevelDefinition(0));
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        Docx::setListLevel(blocks.at(i), levels.at(i));
    }
    QCOMPARE(markers(doc), (QStringList{QStringLiteral("A=1."), QStringLiteral("A1=1.1."), QStringLiteral("A1x=1.1.1."),
                                        QStringLiteral("A2=1.2."), QStringLiteral("B=2."), QStringLiteral("B1=2.1.")}));
    // A level's indents follow it: 1.1. at 0.55 in (792 twips), hanging 0.3 in.
    QCOMPARE(qRound(Ooxml::Units::pxToTwips(blocks.at(1).blockFormat().leftMargin())), 792);
    QCOMPARE(qRound(Ooxml::Units::pxToTwips(blocks.at(1).blockFormat().doubleProperty(Docx::kListHanging))), 432);
    // The whole list takes another definition: 1) a) i).
    const int numId = Docx::wordListItem(blocks.first()).numId;
    Docx::redefineList(&doc, numId, Docx::multilevelDefinition(1));
    QCOMPARE(markers(doc), (QStringList{QStringLiteral("A=1)"), QStringLiteral("A1=a)"), QStringLiteral("A1x=i)"),
                                        QStringLiteral("A2=b)"), QStringLiteral("B=2)"), QStringLiteral("B1=a)")}));
    // Article / Section with legal numbering, and the text after a number
    // wider than its hanging indent at the next default tab stop.
    Docx::redefineList(&doc, Docx::wordListItem(blocks.first()).numId, Docx::multilevelDefinition(3));
    QCOMPARE(markers(doc), (QStringList{QStringLiteral("A=Article I."), QStringLiteral("A1=Section 1.01"),
                                        QStringLiteral("A1x=(a)"), QStringLiteral("A2=Section 1.02"),
                                        QStringLiteral("B=Article II."), QStringLiteral("B1=Section 2.01")}));
    QVERIFY(Docx::layoutListText(&doc, 48.0) > 0);
    QVERIFY(blocks.first().blockFormat().textIndent() >= 48.0);
    QCOMPARE(std::fmod(blocks.first().blockFormat().textIndent(), 48.0), 0.0);
    // Word's library item for one level: "Step 1:" from Define New Number Format.
    const QString step = Docx::withLevel(Docx::numberingDefinition(QStringLiteral("decimal"), QStringLiteral("%1.")), 0,
                                         {{QStringLiteral("text"), QStringLiteral("Step %1:")}, {QStringLiteral("start"), 3}});
    const QVariantMap spec = Docx::levelSpec(step, 0);
    QCOMPARE(spec.value(QStringLiteral("text")).toString(), QStringLiteral("Step %1:"));
    QCOMPARE(spec.value(QStringLiteral("start")).toInt(), 3);
    QCOMPARE(Docx::wordNumberText(4, QStringLiteral("ordinal")), QStringLiteral("4th"));
    QCOMPARE(Docx::wordNumberText(21, QStringLiteral("cardinalText")), QStringLiteral("Twenty-One"));
    QCOMPARE(Docx::wordNumberText(12, QStringLiteral("ordinalText")), QStringLiteral("Twelfth"));
}

void DocxListsTest::restartAndContinueAsWordWritesThem()
{
    auto doc = read(wordList());
    QVERIFY(doc);
    const QTextBlock three = blockWithText(doc->body, QStringLiteral("three"));
    QTextCursor c(three);
    c.beginEditBlock();
    QVERIFY(Docx::restartNumbering(three, 1) > 0);
    c.endEditBlock();
    QCOMPARE(markers(doc->body).mid(2, 3), (QStringList{QStringLiteral("two=2."), QStringLiteral("three=1."),
                                                       QStringLiteral("four=2.")}));
    QByteArray saved = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(saved);
    QByteArray numbering = part(saved, QStringLiteral("word/numbering.xml"));
    // A new instance over the SAME definition, restarting level 1.
    QVERIFY2(numbering.contains("<w:num w:numId=\"3\"><w:abstractNumId w:val=\"0\"/><w:lvlOverride w:ilvl=\"0\">"
                                "<w:startOverride w:val=\"1\"/></w:lvlOverride></w:num>"),
             numbering.constData());
    QCOMPARE(numbering.count("<w:abstractNum "), 2); // no new definition
    QByteArray xml = part(saved, QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:numId w:val=\"3\"/>"));
    auto back = read(saved);
    QCOMPARE(markers(back->body).mid(2, 3), (QStringList{QStringLiteral("two=2."), QStringLiteral("three=1."),
                                                        QStringLiteral("four=2.")}));
    // Continue Numbering: the restarted items join the list before them.
    const QTextBlock again = blockWithText(back->body, QStringLiteral("three"));
    QCOMPARE(Docx::continuableNumId(again), 1);
    QVERIFY(Docx::continueNumbering(again));
    QCOMPARE(markers(back->body).mid(2, 3), (QStringList{QStringLiteral("two=2."), QStringLiteral("three=3."),
                                                        QStringLiteral("four=4.")}));
    saved = Docx::docxBytes(back->body, back->parts);
    checkSchema(saved);
    xml = part(saved, QStringLiteral("word/document.xml"));
    QVERIFY(!xml.contains("<w:numId w:val=\"3\"/>")); // every item on the same w:numId again
    QCOMPARE(xml.count("<w:numId w:val=\"1\"/>"), 5);
    // Set Numbering Value: a new list from "two" starting at 7.
    Docx::restartNumbering(blockWithText(back->body, QStringLiteral("two")), 7);
    QCOMPARE(markers(back->body).mid(2, 3), (QStringList{QStringLiteral("two=7."), QStringLiteral("three=8."),
                                                        QStringLiteral("four=9.")}));
}

void DocxListsTest::levelsAndRemovalAreWritten()
{
    auto doc = read(wordList());
    QVERIFY(doc);
    // Tab on "two": level 2 of the same list, the level's indents.
    Docx::setListLevel(blockWithText(doc->body, QStringLiteral("two")), 1);
    Docx::removeFromList(blockWithText(doc->body, QStringLiteral("four")), false);
    QCOMPARE(markers(doc->body).mid(0, 4), (QStringList{QStringLiteral("one=1."), QStringLiteral("one a=a)"),
                                                       QStringLiteral("two=b)"), QStringLiteral("three=2.")}));
    const QByteArray saved = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(saved);
    const QByteArray xml = part(saved, QStringLiteral("word/document.xml"));
    const qsizetype two = xml.indexOf(">two<");
    QVERIFY2(xml.lastIndexOf("<w:numPr><w:ilvl w:val=\"1\"/><w:numId w:val=\"1\"/></w:numPr>", two) > xml.lastIndexOf("<w:p>", two),
             xml.mid(xml.lastIndexOf("<w:p>", two), 400).constData());
    QVERIFY2(!xml.contains("<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t xml:space=\"preserve\">four"),
             xml.constData());
    auto back = read(saved);
    QVERIFY(blockWithText(back->body, QStringLiteral("four")).textList() == nullptr);
    QCOMPARE(Docx::wordListItem(blockWithText(back->body, QStringLiteral("two"))).level, 1);
}

void DocxListsTest::libreOfficeRendersTheSample()
{
    if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) {
        QSKIP("LibreOffice is not installed");
    }
    QTextDocument doc;
    Docx::WordParts parts;
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("Multilevel list"));
    const QStringList texts{QStringLiteral("Scope"), QStringLiteral("Goals"), QStringLiteral("Measures"),
                            QStringLiteral("Budget"), QStringLiteral("Staff"), QStringLiteral("Bullet")};
    for (const QString &text : texts) {
        c.insertBlock();
        c.insertText(text);
    }
    QList<QTextBlock> items;
    for (const QString &text : texts.mid(0, 5)) {
        items.append(blockWithText(doc, text));
    }
    Docx::makeList(items, Docx::multilevelDefinition(0));
    Docx::setListLevel(items.at(1), 1);
    Docx::setListLevel(items.at(2), 2);
    Docx::makeList({blockWithText(doc, QStringLiteral("Bullet"))}, Docx::bulletDefinition(QString(QChar(0xF0D8)), QStringLiteral("Wingdings")));
    Docx::layoutListText(&doc, parts.defaultTabStop);
    const QString keep = qEnvironmentVariable("QQO_GRAB_DIR");
    QTemporaryDir scratch;
    const QString dir = keep.isEmpty() ? scratch.path() : keep;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("m73_lists_sample.docx"));
    QString error;
    QVERIFY2(Docx::writeDocx(path, doc, parts, {}, &error), qPrintable(error));
    QTemporaryDir profile;
    QProcess soffice;
    Test::startSoffice(soffice, {QStringLiteral("-env:UserInstallation=file://") + profile.path(), QStringLiteral("--headless"),
                                 QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), dir, path});
    QVERIFY(soffice.waitForFinished(120000));
    const QString pdf = QDir(dir).filePath(QStringLiteral("m73_lists_sample.pdf"));
    QVERIFY2(QFileInfo(pdf).size() > 1000, qPrintable(QString::fromUtf8(soffice.readAllStandardError())));
    if (!QStandardPaths::findExecutable(QStringLiteral("pdftotext")).isEmpty()) {
        QProcess text;
        text.start(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), pdf, QStringLiteral("-")});
        QVERIFY(text.waitForFinished(30000));
        const QString out = QString::fromUtf8(text.readAllStandardOutput());
        QVERIFY2(out.contains(QStringLiteral("1.1.1.")) && out.contains(QStringLiteral("2.")), qPrintable(out));
    }
    if (!keep.isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()) {
        QProcess::execute(QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("70"),
                                                        pdf, QDir(dir).filePath(QStringLiteral("m73_lists_sample"))});
    }
}

QTEST_MAIN(DocxListsTest)
#include "tst_docx_lists.moc"
