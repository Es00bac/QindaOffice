// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_fixture.h"
#include "docx_roundtrip.h"
#include "docx_bibliography.h"
#include "docx_reference_fields.h"
#include <QTextBlock>
#include <QTextCursor>
using namespace QindaOffice;
using namespace QindaOffice::Test;
namespace {
Docx::BibliographySource source(const QString &tag, const QString &title)
{
    Docx::BibliographySource out;
    out.values = {{QStringLiteral("tag"), tag}, {QStringLiteral("title"), title}, {QStringLiteral("type"), QStringLiteral("Book")},
        {QStringLiteral("author"), QStringLiteral("Rivera, Ana")}, {QStringLiteral("year"), QStringLiteral("2026")},
        {QStringLiteral("publisher"), QStringLiteral("Example Press")}};
    return out;
}
QString documentXml(const QByteArray &bytes) { return QString::fromUtf8(partOf(bytes, QStringLiteral("word/document.xml"))); }
}
class DocxReferencesTest : public QObject {
    Q_OBJECT
private slots:
    void bibliographyNativeSchemaAndClone();
    void importedSourcesPreservedAndPatched();
    void fieldsAcrossParagraphsAndBookmarks();
    void identicalAdjacentFieldsStaySeparate();
    void bibliographyLibreOffice();
    void emptyFieldsAndNativeLinks();
    void copiedFieldOccurrencesStaySeparate();
};
void DocxReferencesTest::bibliographyNativeSchemaAndClone()
{
    WordDoc doc; doc.body.setPlainText(QStringLiteral("Research references"));
    doc.parts.bibliography.sources.append(source(QStringLiteral("Rivera2026"), QStringLiteral("Reliable systems")));
    auto web = source(QStringLiteral("Web2026"), QStringLiteral("Open reference guide"));
    web.values.insert(QStringLiteral("type"), QStringLiteral("InternetSite")); web.values.insert(QStringLiteral("corporate"), true);
    web.values.insert(QStringLiteral("author"), QStringLiteral("Example Institute")); web.values.insert(QStringLiteral("accessed"), QStringLiteral("2026-10-10"));
    doc.parts.bibliography.sources.append(web);
    const auto bytes = Docx::docxBytes(doc.body, doc.parts); QVERIFY(!bytes.isEmpty()); QString problems; QVERIFY2(validates(bytes, &problems), qPrintable(problems));
    const auto read = readWord(bytes); QVERIFY(read); QCOMPARE(read->parts.bibliography.sources.size(), 2);
    QCOMPARE(read->parts.bibliography.find(QStringLiteral("Web2026"))->values.value(QStringLiteral("accessed")).toString(), QStringLiteral("2026-10-10"));
    auto clone = Docx::cloneWordParts(read->parts); QCOMPARE(clone.bibliography, read->parts.bibliography);
    QCOMPARE(Docx::citationText(read->parts.bibliography.sources.first(), QStringLiteral("authorDate"), 1), QStringLiteral("(Rivera, 2026)"));
    QCOMPARE(Docx::citationText(read->parts.bibliography.sources.first(), QStringLiteral("numeric"), 2, QStringLiteral("9–11")), QStringLiteral("[2, p. 9–11]"));
    QVERIFY(!Docx::validateSource({{QStringLiteral("tag"), QStringLiteral("bad tag")}}));
}
void DocxReferencesTest::importedSourcesPreservedAndPatched()
{
    DocxFixture f; f.body = para(QStringLiteral("Imported references"));
    const QByteArray original = R"(<b:Sources xmlns:b="http://schemas.openxmlformats.org/officeDocument/2006/bibliography" SelectedStyle="Custom.XSL" StyleName="Custom" URI="urn:custom"><b:Source><b:Tag>Imported</b:Tag><b:SourceType>Book</b:SourceType><b:Title>Original title</b:Title><b:StandardNumber>12345</b:StandardNumber><b:Author><b:Editor><b:NameList><b:Person><b:Last>Editor</b:Last></b:Person></b:NameList></b:Editor></b:Author></b:Source><b:Source><b:Tag>Unusual</b:Tag><b:SourceType>Film</b:SourceType><b:Title>Film source</b:Title><b:Comments>Keep me</b:Comments></b:Source></b:Sources>)";
    f.extras.insert(QStringLiteral("rIdSources"), {QStringLiteral("customXml/item1.xml"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml"), QStringLiteral("application/xml"), original});
    auto doc = readWord(f.build()); QVERIFY(doc); QCOMPARE(doc->parts.bibliography.style, QStringLiteral("preserved"));
    auto bytes = Docx::docxBytes(doc->body, doc->parts); QCOMPARE(partOf(bytes, QStringLiteral("customXml/item1.xml")), original);
    doc->parts.bibliography.sources.first().values.insert(QStringLiteral("title"), QStringLiteral("Edited title"));
    bytes = Docx::docxBytes(doc->body, doc->parts); auto read = readWord(bytes); QVERIFY(read);
    QCOMPARE(read->parts.bibliography.sources.size(), 2); QCOMPARE(read->parts.bibliography.style, QStringLiteral("preserved"));
    const QString patched = QString::fromUtf8(partOf(bytes, QStringLiteral("customXml/item1.xml")));
    QVERIFY(patched.contains(QStringLiteral("12345"))); QVERIFY(patched.contains(QStringLiteral("Keep me"))); QVERIFY(patched.contains(QStringLiteral("Editor")));
    QVERIFY(patched.contains(QStringLiteral("Edited title"))); QString problems; QVERIFY2(validates(bytes, &problems), qPrintable(problems));
    doc->parts.bibliography.sources.removeLast(); bytes = Docx::docxBytes(doc->body, doc->parts); read = readWord(bytes); QCOMPARE(read->parts.bibliography.sources.size(), 1);
}
void DocxReferencesTest::fieldsAcrossParagraphsAndBookmarks()
{
    DocxFixture f;
    f.body = QStringLiteral(R"(<w:p><w:bookmarkStart w:id="1" w:name="FigureOne"/><w:r><w:t>Figure </w:t></w:r><w:fldSimple w:instr="SEQ Figure"><w:r><w:t>1</w:t></w:r></w:fldSimple><w:r><w:t>: Sample</w:t></w:r><w:bookmarkEnd w:id="1"/></w:p><w:p><w:r><w:fldChar w:fldCharType="begin" w:fldLock="true"/></w:r><w:r><w:instrText> BIBLIOGRAPHY \l 1033 </w:instrText></w:r><w:r><w:fldChar w:fldCharType="separate"/></w:r><w:r><w:t>First source</w:t></w:r></w:p><w:p><w:r><w:t>Second source</w:t></w:r><w:r><w:fldChar w:fldCharType="end"/></w:r><w:r><w:t> after</w:t></w:r></w:p><w:p><w:fldSimple w:instr="REF FigureOne \h"><w:r><w:t>Figure 1: Sample</w:t></w:r></w:fldSimple></w:p>)");
    auto doc = readWord(f.build()); QVERIFY(doc); auto spans = Docx::referenceSpans(doc->body); QCOMPARE(spans.size(), 3);
    auto ranges = Docx::bookmarkRanges(doc->body); QVERIFY(ranges.contains(QStringLiteral("FigureOne"))); QCOMPARE(ranges.value(QStringLiteral("FigureOne")).end, 16);
    const auto bytes = Docx::docxBytes(doc->body, doc->parts); QString problems; QVERIFY2(validates(bytes, &problems), qPrintable(problems));
    auto read = readWord(bytes); QVERIFY(read); QCOMPARE(read->body.toPlainText(), doc->body.toPlainText());
    spans = Docx::referenceSpans(read->body); QCOMPARE(spans.size(), 3); QCOMPARE(spans.at(1).end - spans.at(1).start, 26);
    QVERIFY(spans.at(1).field.value(QStringLiteral("beginXml")).toString().contains(QStringLiteral("fldLock")));
    QCOMPARE(Docx::bookmarkRanges(read->body).value(QStringLiteral("FigureOne")).end, 16);
    QVERIFY(!spans.at(1).field.value(QStringLiteral("code")).toString().isEmpty());
}
void DocxReferencesTest::identicalAdjacentFieldsStaySeparate()
{
    DocxFixture f; f.body = QStringLiteral(R"(<w:p><w:fldSimple w:instr="CITATION Rivera2026"><w:r><w:t>(Rivera, 2026)</w:t></w:r></w:fldSimple><w:fldSimple w:instr="CITATION Rivera2026"><w:r><w:t>(Rivera, 2026)</w:t></w:r></w:fldSimple><w:fldSimple w:instr="UNSUPPORTED \x custom"><w:r><w:t>Untouched result</w:t></w:r></w:fldSimple></w:p>)");
    auto doc = readWord(f.build()); QVERIFY(doc); QCOMPARE(Docx::referenceSpans(doc->body).size(), 3);
    auto bytes = Docx::docxBytes(doc->body, doc->parts); auto read = readWord(bytes); QVERIFY(read);
    QCOMPARE(Docx::referenceSpans(read->body).size(), 3); QCOMPARE(read->body.toPlainText(), doc->body.toPlainText());
    QVERIFY(documentXml(bytes).contains(QStringLiteral("UNSUPPORTED")));
}
void DocxReferencesTest::copiedFieldOccurrencesStaySeparate()
{
    WordDoc doc; QTextCursor cursor(&doc.body); QTextCharFormat field;
    field.setProperty(Docx::kReferenceFields, QVariantList{Docx::referenceField(QStringLiteral("CITATION A"))});
    cursor.insertText(QStringLiteral("First result"), field); cursor.insertText(QStringLiteral(" gap "), QTextCharFormat()); cursor.insertText(QStringLiteral("Copied result"), field);
    QCOMPARE(Docx::referenceSpans(doc.body).size(), 2);
    const auto bytes = Docx::docxBytes(doc.body, doc.parts); const auto read = readWord(bytes); QVERIFY(read); QCOMPARE(Docx::referenceSpans(read->body).size(), 2);
    QCOMPARE(read->body.toPlainText(), doc.body.toPlainText()); QString errors; QVERIFY2(validates(bytes, &errors), qPrintable(errors));
}
void DocxReferencesTest::emptyFieldsAndNativeLinks()
{
    DocxFixture fixture;
    fixture.body = QStringLiteral(R"(<w:p><w:bookmarkStart w:id="1" w:name="Target"/><w:r><w:t>Target text</w:t></w:r><w:bookmarkEnd w:id="1"/></w:p><w:p><w:fldSimple w:instr="BIBLIOGRAPHY"/><w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText> UNRECOGNIZED </w:instrText></w:r><w:r><w:fldChar w:fldCharType="end"/></w:r></w:p><w:p><w:fldSimple w:instr="REF Target \h"><w:r><w:t>Target text</w:t></w:r></w:fldSimple></w:p>)");
    auto doc = readWord(fixture.build()); QVERIFY(doc); QCOMPARE(Docx::referenceSpans(doc->body).size(), 3);
    QVERIFY(doc->body.lastBlock().begin().fragment().charFormat().anchorHref() == QStringLiteral("#Target"));
    const auto bytes = Docx::docxBytes(doc->body, doc->parts); const auto read = readWord(bytes); QVERIFY(read); QCOMPARE(Docx::referenceSpans(read->body).size(), 3);
    QString errors; QVERIFY2(validates(bytes, &errors), qPrintable(errors));
}
void DocxReferencesTest::bibliographyLibreOffice()
{
    if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) QSKIP("LibreOffice unavailable");
    WordDoc doc; doc.parts.bibliography.sources.append(source(QStringLiteral("Rivera2026"), QStringLiteral("Reliable systems")));
    QTextCursor c(&doc.body); c.insertText(QStringLiteral("Research references\n"));
    QTextCharFormat format; format.setProperty(Docx::kReferenceFields, QVariantList{Docx::referenceField(QStringLiteral("BIBLIOGRAPHY"))});
    c.insertText(Docx::bibliographyText(doc.parts.bibliography.sources.first(), QStringLiteral("authorDate"), 1), format);
    const auto bytes = Docx::docxBytes(doc.body, doc.parts); QTemporaryDir temp;
    const auto pdf = renderWithLibreOffice(bytes, QStringLiteral("m94_native_references"), temp); QVERIFY(!pdf.isEmpty());
    const auto text = pdfText(pdf); QVERIFY(text.contains(QStringLiteral("Research references"))); QVERIFY(text.contains(QStringLiteral("Reliable systems")));
}
QTEST_MAIN(DocxReferencesTest)
#include "tst_docx_references.moc"
