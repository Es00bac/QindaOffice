// SPDX-License-Identifier: GPL-3.0-or-later
// M77 gates for endnotes in office_docx (D-086): Word's endnotes read as
// reference marks numbered i, ii, iii with their texts; an untouched
// endnotes part copied byte for byte; an edited or added note rewriting
// the part with every unchanged note's own markup; the Footnote and
// Endnote options written to settings.xml and the section, read back; new
// endnotes in a new document; all schema-valid.

#include "docx_fixture.h"
#include "docx_notes.h"
#include "docx_properties.h"
#include "docx_roundtrip.h"

#include <QTextBlock>
#include <QTextCursor>

using namespace QindaOffice;
using Test::DocxFixture;
using Test::para;

namespace {

const char *kEndnotesType = "application/vnd.openxmlformats-officedocument.wordprocessingml.endnotes+xml";

QString endnote(int id, const QString &text)
{
    return QStringLiteral("<w:endnote w:id=\"%1\"><w:p><w:pPr><w:pStyle w:val=\"EndnoteText\"/></w:pPr><w:r><w:rPr>"
                          "<w:rStyle w:val=\"EndnoteReference\"/></w:rPr><w:endnoteRef/></w:r><w:r><w:t xml:space=\"preserve\">"
                          " %2</w:t></w:r><w:r><w:rPr><w:i/></w:rPr><w:t xml:space=\"preserve\"> (in italics)</w:t></w:r></w:p></w:endnote>")
        .arg(id)
        .arg(text);
}

QByteArray sourcePackage()
{
    DocxFixture f;
    const QString ref = QStringLiteral("<w:r><w:rPr><w:rStyle w:val=\"EndnoteReference\"/></w:rPr><w:endnoteReference w:id=\"%1\"/></w:r>");
    f.body = QStringLiteral("<w:p><w:r><w:t>Babbage</w:t></w:r>") + ref.arg(2) + QStringLiteral("<w:r><w:t xml:space=\"preserve\"> and Lovelace</w:t></w:r>")
        + ref.arg(3) + QStringLiteral("</w:p>") + para(QStringLiteral("Second paragraph"));
    const QString notes = QStringLiteral("<w:endnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:endnote>"
                                         "<w:endnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:endnote>")
        + endnote(2, QStringLiteral("The Difference Engine.")) + endnote(3, QStringLiteral("Notes on the Analytical Engine."));
    f.extras.insert(QStringLiteral("rIdEnd"),
                    {QStringLiteral("word/endnotes.xml"), QString::fromUtf16(Ooxml::RelType::Endnotes), QString::fromLatin1(kEndnotesType),
                     QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:endnotes%1>%2</w:endnotes>")
                         .arg(QString::fromLatin1(Test::kWordNamespaces), notes)
                         .toUtf8()});
    return f.build();
}

QStringList marks(const QTextDocument &doc)
{
    QStringList out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().hasProperty(Docx::kEndnoteIdProperty)) {
                out.append(it.fragment().text());
            }
        }
    }
    return out;
}

} // namespace

class DocxNotesTest : public QObject {
    Q_OBJECT
private slots:
    void endnotesReadAsNotes();
    void untouchedEndnotesKeptByteForByte();
    void editedEndnotesRewritten();
    void noteOptionsWrittenAndRead();
    void newEndnotesInANewDocument();
};

void DocxNotesTest::endnotesReadAsNotes()
{
    auto doc = Test::readWord(sourcePackage());
    QVERIFY(doc);
    QCOMPARE(marks(doc->body), QStringList({QStringLiteral("i"), QStringLiteral("ii")}));
    QCOMPARE(doc->parts.endnotes.size(), 2);
    QCOMPARE(doc->parts.endnotes.value(QStringLiteral("docx-2")), QStringLiteral("The Difference Engine. (in italics)"));
    QCOMPARE(doc->body.begin().text(), QStringLiteral("Babbagei and Lovelaceii"));
    QVERIFY(doc->parts.warnings.isEmpty()); // endnotes are no longer an approximation
    QCOMPARE(Docx::noteNumber(4, QStringLiteral("lowerRoman")), QStringLiteral("iv"));
    QCOMPARE(Docx::noteNumber(28, QStringLiteral("upperLetter")), QStringLiteral("BB"));
    QCOMPARE(Docx::noteNumber(6, QStringLiteral("chicago")), QStringLiteral("††"));
}

void DocxNotesTest::untouchedEndnotesKeptByteForByte()
{
    const QByteArray source = sourcePackage();
    auto doc = Test::readWord(source);
    QVERIFY(doc);
    QTextCursor c(&doc->body);
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral(", edited"));
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    QCOMPARE(Test::partOf(bytes, QStringLiteral("word/endnotes.xml")), Test::partOf(source, QStringLiteral("word/endnotes.xml")));
    const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:endnoteReference w:id=\"2\"/>"));
    QVERIFY(xml.contains("<w:endnoteReference w:id=\"3\"/>"));
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
}

void DocxNotesTest::editedEndnotesRewritten()
{
    auto doc = Test::readWord(sourcePackage());
    QVERIFY(doc);
    doc->parts.endnotes.insert(QStringLiteral("docx-3"), QStringLiteral("Sketch of the Analytical Engine."));
    QTextCursor c(&doc->body);
    c.movePosition(QTextCursor::End);
    QTextCharFormat mark;
    mark.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    mark.setProperty(Docx::kEndnoteIdProperty, QStringLiteral("new-1"));
    c.insertText(QStringLiteral("iii"), mark);
    doc->parts.endnotes.insert(QStringLiteral("new-1"), QStringLiteral("A note made in QindaWrite."));
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QByteArray notes = Test::partOf(bytes, QStringLiteral("word/endnotes.xml"));
    // Note 1 as Word wrote it (its italic run included), 2 and 3 written anew.
    QVERIFY2(notes.contains("<w:endnote w:id=\"1\"><w:p><w:pPr><w:pStyle w:val=\"EndnoteText\"/></w:pPr>"), notes.constData());
    QVERIFY(notes.contains("The Difference Engine."));
    QVERIFY(notes.contains("<w:i/>"));
    QVERIFY(notes.contains("<w:endnote w:id=\"2\"><w:p><w:pPr><w:pStyle w:val=\"EndnoteText\"/></w:pPr><w:r><w:rPr>"
                           "<w:rStyle w:val=\"EndnoteReference\"/></w:rPr><w:endnoteRef/></w:r><w:r><w:t xml:space=\"preserve\">"
                           " Sketch of the Analytical Engine.</w:t>"));
    QVERIFY(notes.contains("<w:endnote w:id=\"3\">"));
    QVERIFY(notes.contains("w:type=\"separator\" w:id=\"-1\""));
    const QByteArray settings = Test::partOf(bytes, QStringLiteral("word/settings.xml"));
    QVERIFY2(settings.contains("<w:endnotePr><w:endnote w:id=\"-1\"/><w:endnote w:id=\"0\"/></w:endnotePr>"), settings.constData());
    QVERIFY(Test::partOf(bytes, QStringLiteral("word/styles.xml")).contains("w:styleId=\"EndnoteText\""));
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    QCOMPARE(marks(back->body), QStringList({QStringLiteral("i"), QStringLiteral("ii"), QStringLiteral("iii")}));
    QCOMPARE(back->parts.endnotes.value(QStringLiteral("docx-2")), QStringLiteral("Sketch of the Analytical Engine."));
    QCOMPARE(back->parts.endnotes.value(QStringLiteral("docx-3")), QStringLiteral("A note made in QindaWrite."));
}

void DocxNotesTest::noteOptionsWrittenAndRead()
{
    auto doc = Test::readWord(sourcePackage());
    QVERIFY(doc);
    QCOMPARE(Docx::noteFormat(doc->parts.endnoteOptions, true), QStringLiteral("lowerRoman"));
    doc->parts.endnoteOptions = {QStringLiteral("upperRoman"), 3, QStringLiteral("eachSect"), QStringLiteral("sectEnd")};
    doc->parts.footnoteOptions = {QStringLiteral("lowerLetter"), 1, QString(), QString()};
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QByteArray settings = Test::partOf(bytes, QStringLiteral("word/settings.xml"));
    QVERIFY2(settings.contains("<w:endnotePr><w:pos w:val=\"sectEnd\"/><w:numFmt w:val=\"upperRoman\"/>"
                               "<w:numStart w:val=\"3\"/><w:numRestart w:val=\"eachSect\"/>"),
             settings.constData());
    QVERIFY(settings.contains("<w:footnotePr><w:numFmt w:val=\"lowerLetter\"/></w:footnotePr>"));
    const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<w:footnotePr><w:numFmt w:val=\"lowerLetter\"/></w:footnotePr><w:endnotePr><w:pos w:val=\"sectEnd\"/>"),
             xml.constData());
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    QCOMPARE(back->parts.endnoteOptions, doc->parts.endnoteOptions);
    QCOMPARE(back->parts.footnoteOptions, doc->parts.footnoteOptions);
    QCOMPARE(marks(back->body), QStringList({QStringLiteral("III"), QStringLiteral("IV")}));
}

void DocxNotesTest::newEndnotesInANewDocument()
{
    QTextDocument body;
    Docx::WordParts parts;
    QTextCursor c(&body);
    c.insertText(QStringLiteral("A claim"));
    QTextCharFormat mark;
    mark.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    mark.setProperty(Docx::kEndnoteIdProperty, QStringLiteral("a"));
    c.insertText(QStringLiteral("i"), mark);
    c.insertText(QStringLiteral(" and a footnote"), QTextCharFormat());
    QTextCharFormat foot = mark;
    foot.clearProperty(Docx::kEndnoteIdProperty);
    foot.setProperty(Docx::kFootnoteIdProperty, QStringLiteral("f"));
    c.insertText(QStringLiteral("1"), foot);
    parts.endnotes.insert(QStringLiteral("a"), QStringLiteral("The source of the claim."));
    parts.footnotes.insert(QStringLiteral("f"), QStringLiteral("A footnote."));
    QString error;
    const QByteArray bytes = Docx::docxBytes(body, parts, {}, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    QVERIFY(Test::partOf(bytes, QStringLiteral("word/endnotes.xml")).contains("The source of the claim."));
    QCOMPARE(Ooxml::Package::fromData(bytes)->contentType(QStringLiteral("word/endnotes.xml")), QString::fromLatin1(kEndnotesType));
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    QCOMPARE(back->parts.endnotes.size(), 1);
    QCOMPARE(back->parts.footnotes.size(), 1);
    QCOMPARE(marks(back->body), QStringList({QStringLiteral("i")}));
}

QTEST_MAIN(DocxNotesTest)
#include "tst_docx_notes.moc"
