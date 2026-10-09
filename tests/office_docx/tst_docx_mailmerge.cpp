// SPDX-License-Identifier: GPL-3.0-or-later
// M77 gates for Word's mail merge in office_docx (D-086): the recipients
// (CSV as Word reads a text data source; filters, sorts, duplicates), the
// merge fields and rules evaluated as Word evaluates them, the merge itself
// (a copy per record in its own section, SKIPIF dropping one, blank lines
// suppressed, labels running records across a sheet), the merge settings
// written as Word writes them (w:mailMerge with its text data source) and a
// Word-written w:mailMerge kept, Word's complex fields read and written
// back, and LibreOffice rendering a merged letter.

#include "docx_envelopes.h"
#include "docx_fixture.h"
#include "docx_mailmerge.h"
#include "docx_merge.h"
#include "docx_properties.h"
#include "docx_roundtrip.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextTable>

using namespace QindaOffice;
using Test::DocxFixture;
using Test::para;

namespace {

Docx::MergeData people()
{
    QString error;
    Docx::MergeData data = Docx::parseDelimited(
        "First Name,Last Name,Company,Address Line 1,Address Line 2,City,State,ZIP Code,Country,Amount,Email\r\n"
        "Ada,Lovelace,Analytical Engines,12 St James's Square,,London,,SW1Y 4JH,United Kingdom,120,ada@example.org\r\n"
        "Grace,Hopper,,1 Navy Way,Suite 9,Arlington,VA,22202,United States,80,grace@example.org\r\n"
        "\"Katherine \"\"Kay\"\"\",Johnson,NASA,\"Langley, Bldg 1244\",,Hampton,VA,23681,United States,250,\r\n",
        &error);
    data.source = QStringLiteral("/tmp/people.csv");
    return data;
}

// A main document: a greeting, the address block, a rule, a field.
void letter(QTextDocument *doc)
{
    QTextCursor c(doc);
    Docx::insertMergeField(c, Docx::addressBlockCode({}));
    c.insertBlock();
    c.insertText(QStringLiteral("Line two: "));
    Docx::insertMergeField(c, Docx::mergeFieldCode(QStringLiteral("Address Line 2")));
    c.insertBlock();
    Docx::insertMergeField(c, QStringLiteral("MERGEFIELD \"Address Line 2\""));
    c.insertBlock();
    Docx::insertMergeField(c, Docx::greetingLineCode({}));
    c.insertBlock();
    c.insertText(QStringLiteral("You owe "));
    Docx::insertMergeField(c, QStringLiteral("MERGEFIELD Amount \\# \"$#,##0.00\""));
    c.insertText(QStringLiteral(" — "));
    Docx::insertMergeField(c, Docx::ifCode(QStringLiteral("Amount"), QStringLiteral(">"), QStringLiteral("100"),
                                           QStringLiteral("a large sum"), QStringLiteral("a small sum")));
    c.insertText(QStringLiteral("."));
}

QStringList copiesText(const QTextDocument &doc)
{
    QStringList copies{QString()};
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        copies.last() += b.text() + QLatin1Char('\n');
        if (b.blockFormat().hasProperty(Docx::kSectionXml)) {
            copies.append(QString());
        }
    }
    return copies;
}

} // namespace

class DocxMailMergeTest : public QObject {
    Q_OBJECT
private slots:
    void csvReadsWordsTextSources();
    void filtersSortsAndDuplicates();
    void fieldsEvaluateAsWordDoes();
    void mergeMakesACopyPerRecord();
    void labelsRunRecordsAcrossASheet();
    void settingsWrittenAsWordWritesThem();
    void wordsOwnMailMergeKept();
    void complexFieldsRoundTrip();
    void libreOfficeRendersAMergedLetter();
};

void DocxMailMergeTest::csvReadsWordsTextSources()
{
    const Docx::MergeData data = people();
    QCOMPARE(data.fields.size(), 11);
    QCOMPARE(data.records.size(), 3);
    QCOMPARE(data.records.at(2).at(0), QStringLiteral("Katherine \"Kay\""));
    QCOMPARE(data.value(2, QStringLiteral("address line 1")), QStringLiteral("Langley, Bldg 1244"));
    // Semicolons and a BOM, as Excel in Europe exports them; Latin-1 text.
    QString error;
    const Docx::MergeData semi = Docx::parseDelimited("\xEF\xBB\xBFName;City\nZo\xC3\xAB;Gen\xC3\xA8ve\n", &error);
    QCOMPARE(semi.fields, QStringList({QStringLiteral("Name"), QStringLiteral("City")}));
    QCOMPARE(semi.records.at(0).at(1), QStringLiteral("Genève"));
    const Docx::MergeData latin = Docx::parseDelimited("Name\tCity\nZo\xEB\tGen\xE8ve\n", &error);
    QCOMPARE(latin.records.at(0).at(0), QStringLiteral("Zoë"));
    // Written back as Word and Excel read CSV: quoted where needed.
    const Docx::MergeData back = Docx::parseDelimited(Docx::csvBytes(data), &error);
    QCOMPARE(back.fields, data.fields);
    QCOMPARE(back.records, data.records);
    QVERIFY(Docx::csvBytes(data).contains("\"Katherine \"\"Kay\"\"\",Johnson"));
}

void DocxMailMergeTest::filtersSortsAndDuplicates()
{
    Docx::MergeData data = people();
    data.records.append(data.records.at(1)); // Grace twice
    Docx::MailMergeSettings s;
    QCOMPARE(Docx::recipientOrder(data, s), QList<int>({0, 1, 2, 3}));
    s.filters = {{QStringLiteral("and"), QStringLiteral("State"), QStringLiteral("equal"), QStringLiteral("va")}};
    QCOMPARE(Docx::recipientOrder(data, s), QList<int>({1, 2, 3}));
    s.filters.append({QStringLiteral("or"), QStringLiteral("Amount"), QStringLiteral("greater"), QStringLiteral("100")});
    s.filters.append({QStringLiteral("and"), QStringLiteral("City"), QStringLiteral("contains"), QStringLiteral("lond")});
    QCOMPARE(Docx::recipientOrder(data, s), QList<int>({0, 1, 2, 3}));
    s.filters.clear();
    s.sorts = {{QStringLiteral("Amount"), true}}; // numbers as numbers: 250, 120, 80, 80
    QCOMPARE(Docx::recipientOrder(data, s), QList<int>({2, 0, 1, 3}));
    s.excluded = {3};
    QCOMPARE(Docx::recipientOrder(data, s), QList<int>({2, 0, 1}));
    QCOMPARE(Docx::filteredOrder(data, s).size(), 4);
    QCOMPARE(Docx::duplicateGroups(data), QList<QList<int>>({{1, 3}}));
    // Word's query for these settings, and back.
    s.dataSource = QStringLiteral("/home/me/people.csv");
    s.filters = {{QStringLiteral("and"), QStringLiteral("State"), QStringLiteral("equal"), QStringLiteral("VA")},
                 {QStringLiteral("or"), QStringLiteral("City"), QStringLiteral("contains"), QStringLiteral("O'Hare")}};
    const QString query = Docx::mergeQuery(s);
    QCOMPARE(query, QStringLiteral("SELECT * FROM /home/me/people.csv WHERE ((`State` = 'VA') OR (`City` LIKE '%O''Hare%'))"
                                   " ORDER BY `Amount` DESC"));
    QList<Docx::MergeFilter> filters;
    QList<Docx::MergeSort> sorts;
    QVERIFY(Docx::parseMergeQuery(query, &filters, &sorts));
    QCOMPARE(filters, s.filters);
    QCOMPARE(sorts, s.sorts);
}

void DocxMailMergeTest::fieldsEvaluateAsWordDoes()
{
    const Docx::MergeData data = people();
    Docx::MergeCursor cursor;
    cursor.data = &data;
    cursor.order = {0, 1, 2};
    auto eval = [&](const QString &code) { return Docx::evaluateMergeField(code, cursor); };
    QCOMPARE(eval(QStringLiteral("MERGEFIELD First_Name")), QStringLiteral("Ada"));
    QCOMPARE(eval(QStringLiteral("MERGEFIELD \"Last Name\" \\* Upper")), QStringLiteral("LOVELACE"));
    QCOMPARE(eval(QStringLiteral("MERGEFIELD Company \\b \"of \" \\f \", Ltd\"")), QStringLiteral("of Analytical Engines, Ltd"));
    QCOMPARE(eval(QStringLiteral("MERGEFIELD \"Address Line 2\" \\b \"of \"")), QString());
    QCOMPARE(eval(QStringLiteral("MERGEFIELD Amount \\# \"$#,##0.00\"")), QStringLiteral("$120.00"));
    QCOMPARE(eval(QStringLiteral("IF { MERGEFIELD Amount } > 100 \"large\" \"small\"")), QStringLiteral("large"));
    QCOMPARE(eval(QStringLiteral("IF { MERGEFIELD City } = \"Lon*\" \"UK: { MERGEFIELD City }\" \"\"")), QStringLiteral("UK: London"));
    QCOMPARE(eval(QStringLiteral("IF { MERGEFIELD City } = \"london\" \"same\" \"case matters\"")), QStringLiteral("case matters"));
    QCOMPARE(eval(Docx::addressBlockCode({})),
             QStringLiteral("Ada Lovelace\nAnalytical Engines\n12 St James's Square\nLondon SW1Y 4JH\nUnited Kingdom"));
    QCOMPARE(eval(Docx::greetingLineCode({})), QStringLiteral("Dear Lovelace,")); // no courtesy title
    QCOMPARE(eval(QStringLiteral("MERGEREC")), QStringLiteral("1"));
    QVERIFY(!cursor.skip);
    QCOMPARE(eval(QStringLiteral("SKIPIF { MERGEFIELD Country } = \"United Kingdom\"")), QString());
    QVERIFY(cursor.skip);
    cursor.skip = false;
    QCOMPARE(eval(QStringLiteral("NEXT")), QString());
    QCOMPARE(cursor.position, 1);
    // Grace: the US is the home country, so no country line; Address 2 joins.
    QCOMPARE(eval(Docx::addressBlockCode({})), QStringLiteral("Grace Hopper\n1 Navy Way\nSuite 9\nArlington, VA 22202"));
    QCOMPARE(eval(QStringLiteral("NEXTIF { MERGEFIELD State } <> \"VA\"")), QString());
    QCOMPARE(cursor.position, 1); // VA: no move
    QCOMPARE(eval(QStringLiteral("NEXTIF { MERGEFIELD State } = \"VA\"")), QString());
    QCOMPARE(cursor.position, 2);
    QCOMPARE(eval(QStringLiteral("MERGEREC")), QStringLiteral("3"));
    // A recipient without a name gets the fallback greeting.
    Docx::MergeData nameless = data;
    nameless.records[2][0].clear();
    nameless.records[2][1].clear();
    cursor.data = &nameless;
    QCOMPARE(eval(Docx::greetingLineCode({})), QStringLiteral("Dear Sir or Madam,"));
    // Field labels a main document shows.
    QCOMPARE(Docx::mergeFieldLabel(QStringLiteral("MERGEFIELD \"First Name\"")), QStringLiteral("«First Name»"));
    QCOMPARE(Docx::mergeFieldLabel(QStringLiteral("NEXT")), QStringLiteral("«Next Record»"));
    QCOMPARE(Docx::mergeFieldLabel(QStringLiteral("SKIPIF { MERGEFIELD A } = \"\"")), QStringLiteral("«Skip Record If…»"));
    QVERIFY(!Docx::isMergeField(QStringLiteral("IF { PAGE } = 1 \"a\" \"b\"")));
}

void DocxMailMergeTest::mergeMakesACopyPerRecord()
{
    QTextDocument main;
    letter(&main);
    QTextCursor end(&main);
    end.movePosition(QTextCursor::End);
    end.insertBlock();
    Docx::insertMergeField(end, QStringLiteral("SKIPIF { MERGEFIELD Last_Name } = \"Hopper\""));
    const Docx::MergeData data = people();
    QTextDocument out;
    Docx::MergeRequest request;
    request.main = &main;
    request.data = &data;
    request.order = {0, 1, 2};
    request.sectionXml = QStringLiteral("<w:sectPr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"/>");
    const Docx::MergeResult result = Docx::mergeDocument(&out, request);
    QCOMPARE(result.copies, 2);
    QCOMPARE(result.records, QList<int>({0, 2}));
    QCOMPARE(result.skipped, QList<int>({1}));
    const QStringList copies = copiesText(out);
    QCOMPARE(copies.size(), 2);
    QVERIFY2(copies.at(0).startsWith(QStringLiteral("Ada Lovelace\u2028Analytical Engines\u2028")), qPrintable(copies.at(0)));
    QVERIFY(copies.at(0).contains(QStringLiteral("Line two: \n"))); // text beside the empty field stays
    QVERIFY(!copies.at(0).contains(QStringLiteral("\n\n"))); // the line holding only it is gone
    QVERIFY(copies.at(0).contains(QStringLiteral("You owe $120.00 — a large sum.")));
    QVERIFY(copies.at(1).contains(QStringLiteral("Dear Johnson,")));
    QVERIFY(copies.at(1).contains(QStringLiteral("You owe $250.00 — a large sum.")));
    QVERIFY(!out.toPlainText().contains(QStringLiteral("«")));
    // No field is left in the merged text.
    for (QTextBlock b = out.begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            QVERIFY(!it.fragment().charFormat().hasProperty(Docx::kFieldInstruction));
        }
    }
    // A directory runs every record on, with no section break.
    QTextDocument directory;
    request.kind = QStringLiteral("directory");
    QCOMPARE(Docx::mergeDocument(&directory, request).copies, 2);
    QCOMPARE(copiesText(directory).size(), 1);
    // One copy for Preview Results.
    QTextDocument one;
    request.kind = QStringLiteral("letters");
    request.first = 2;
    request.singleCopy = true;
    QCOMPARE(Docx::mergeDocument(&one, request).records, QList<int>({2}));
    QVERIFY(one.toPlainText().startsWith(QStringLiteral("Katherine \"Kay\" Johnson")));
}

void DocxMailMergeTest::labelsRunRecordsAcrossASheet()
{
    QTextDocument main;
    QTextCursor cursor(&main);
    const Docx::LabelProduct sheet = Docx::labelProduct(QStringLiteral("Avery US Letter|5160"));
    Docx::insertLabelSheet(cursor, sheet, [](QTextCursor &cell, int index) {
        if (index > 0) {
            Docx::insertMergeField(cell, QStringLiteral("NEXT"));
        }
        Docx::insertMergeField(cell, QStringLiteral("MERGEFIELD First_Name"));
    });
    const Docx::MergeData data = people();
    QTextDocument out;
    Docx::MergeRequest request;
    request.main = &main;
    request.kind = QStringLiteral("labels");
    request.data = &data;
    request.order = {0, 1, 2};
    const Docx::MergeResult result = Docx::mergeDocument(&out, request);
    QCOMPARE(result.copies, 1); // three records fill three labels of one sheet
    QCOMPARE(result.records, QList<int>({0, 1, 2}));
    QTextTable *table = qobject_cast<QTextTable *>(out.rootFrame()->childFrames().value(0));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 10);
    QCOMPARE(table->columns(), 5); // three labels and two spacers across
    auto label = [&](int r, int c) {
        QTextCursor at = table->cellAt(r, c).firstCursorPosition();
        at.setPosition(table->cellAt(r, c).lastCursorPosition().position(), QTextCursor::KeepAnchor);
        return at.selectedText();
    };
    QCOMPARE(label(0, 0), QStringLiteral("Ada"));
    QCOMPARE(label(0, 2), QStringLiteral("Grace"));
    QCOMPARE(label(0, 4), QStringLiteral("Katherine \"Kay\""));
    QCOMPARE(label(1, 0), QString()); // past the last record the labels stay blank
    QCOMPARE(label(0, 1), QString());
}

void DocxMailMergeTest::settingsWrittenAsWordWritesThem()
{
    QTextDocument body;
    letter(&body);
    Docx::WordParts parts;
    parts.mailMerge.documentType = QStringLiteral("formLetters");
    parts.mailMerge.dataSource = QStringLiteral("/home/me/Mail Merge/people.csv");
    parts.mailMerge.filters = {{QStringLiteral("and"), QStringLiteral("State"), QStringLiteral("equal"), QStringLiteral("VA")}};
    parts.mailMerge.sorts = {{QStringLiteral("Last Name"), false}};
    parts.mailMerge.addressField = QStringLiteral("Email");
    parts.mailMerge.mailSubject = QStringLiteral("Your statement");
    parts.mailMerge.excluded = {2};
    parts.mailMerge.activeRecord = 2;
    parts.mailMerge.fieldMap.insert(QStringLiteral("Postal Code"), QStringLiteral("ZIP Code"));
    QString error;
    const QByteArray bytes = Docx::docxBytes(body, parts, {}, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QByteArray settings = Test::partOf(bytes, QStringLiteral("word/settings.xml"));
    QVERIFY2(settings.contains("<w:mailMerge><w:mainDocumentType w:val=\"formLetters\"/><w:dataType w:val=\"textFile\"/>"
                               "<w:query w:val=\"SELECT * FROM /home/me/Mail Merge/people.csv WHERE ((`State` = 'VA'))"
                               " ORDER BY `Last Name`\"/><w:dataSource r:id=\"rId"),
             settings.constData());
    QVERIFY(settings.contains("<w:addressFieldName w:val=\"Email\"/><w:mailSubject w:val=\"Your statement\"/>"
                              "<w:activeRecord w:val=\"2\"/>"));
    QVERIFY(settings.contains("<w:fieldMapData><w:type w:val=\"dbColumn\"/><w:name w:val=\"ZIP Code\"/>"
                              "<w:mappedName w:val=\"Postal Code\"/></w:fieldMapData>"));
    QVERIFY(settings.contains("<w:docVar w:name=\"QindaWriteMergeExcluded\" w:val=\"2\"/>"));
    const QByteArray rels = Test::partOf(bytes, QStringLiteral("word/_rels/settings.xml.rels"));
    QVERIFY2(rels.contains("relationships/mailMergeSource\" Target=\"file:///home/me/Mail%20Merge/people.csv\""
                           " TargetMode=\"External\""),
             rels.constData());
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    QCOMPARE(back->parts.mailMerge.documentType, QStringLiteral("formLetters"));
    QCOMPARE(back->parts.mailMerge.dataSource, parts.mailMerge.dataSource);
    QCOMPARE(back->parts.mailMerge.filters, parts.mailMerge.filters);
    QCOMPARE(back->parts.mailMerge.sorts, parts.mailMerge.sorts);
    QCOMPARE(back->parts.mailMerge.excluded, parts.mailMerge.excluded);
    QCOMPARE(back->parts.mailMerge.fieldMap, parts.mailMerge.fieldMap);
    // The fields are still fields, with their codes.
    const QList<Docx::MergeFieldSpan> spans = Docx::mergeFieldSpans(back->body);
    QCOMPARE(spans.size(), 6);
    QCOMPARE(spans.first().instruction, Docx::addressBlockCode({}));
    QCOMPARE(spans.last().instruction.left(24), QStringLiteral("IF { MERGEFIELD Amount }"));
    // An ordinary document again: the element and the docVar go.
    back->parts.mailMerge = {};
    const QByteArray plain = Docx::docxBytes(back->body, back->parts);
    QVERIFY(!Test::partOf(plain, QStringLiteral("word/settings.xml")).contains("mailMerge"));
    QVERIFY(!Test::partOf(plain, QStringLiteral("word/settings.xml")).contains("QindaWriteMergeExcluded"));
}

void DocxMailMergeTest::wordsOwnMailMergeKept()
{
    // A main document as Word saves one over an Excel workbook.
    const QString merge = QStringLiteral(
        "<w:mailMerge><w:mainDocumentType w:val=\"formLetters\"/><w:linkToQuery/><w:dataType w:val=\"native\"/>"
        "<w:connectString w:val=\"Provider=Microsoft.ACE.OLEDB.12.0;User ID=Admin;Data Source=C:\\Users\\me\\list.xlsx\"/>"
        "<w:query w:val=\"SELECT * FROM `Sheet1$` \"/><w:dataSource r:id=\"rId1\"/><w:viewMergedData/>"
        "<w:odso><w:udl w:val=\"Provider=Microsoft.ACE.OLEDB.12.0\"/><w:table w:val=\"Sheet1$\"/><w:src r:id=\"rId2\"/>"
        "<w:colDelim w:val=\"9\"/><w:type w:val=\"database\"/><w:fHdr/>"
        "<w:fieldMapData><w:type w:val=\"dbColumn\"/><w:name w:val=\"Surname\"/><w:mappedName w:val=\"Last Name\"/>"
        "<w:column w:val=\"1\"/><w:lid w:val=\"en-US\"/></w:fieldMapData></w:odso></w:mailMerge>");
    Ooxml::PackageWriter w;
    DocxFixture f;
    f.body = para(QStringLiteral("Dear ")) + QStringLiteral("<w:p><w:fldSimple w:instr=\" MERGEFIELD Surname \"><w:r><w:t>«Surname»</w:t></w:r></w:fldSimple></w:p>");
    QByteArray package = f.build();
    {
        // Add settings.xml with its relationships to the fixture package.
        const auto source = Ooxml::Package::fromData(package);
        Ooxml::PackageWriter out;
        for (const QString &name : {QStringLiteral("word/document.xml")}) {
            out.addPart(name, source->data(name), source->contentType(name));
        }
        out.addRelationship(QString(), QString::fromUtf16(Ooxml::RelType::OfficeDocument), QStringLiteral("word/document.xml"));
        const QString t = QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml");
        out.addPart(QStringLiteral("word/settings.xml"),
                    QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:settings %1>%2<w:defaultTabStop w:val=\"720\"/></w:settings>")
                        .arg(QString::fromLatin1(Test::kWordNamespaces), merge)
                        .toUtf8(),
                    t);
        out.addRelationshipWithId(QStringLiteral("word/document.xml"), QStringLiteral("rIdSettings"),
                                  QString::fromUtf16(Ooxml::RelType::Settings), QStringLiteral("word/settings.xml"));
        const QString mergeRel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/mailMergeSource");
        out.addRelationshipWithId(QStringLiteral("word/settings.xml"), QStringLiteral("rId1"), mergeRel,
                                  QStringLiteral("file:///C:\\Users\\me\\list.xlsx"), true);
        out.addRelationshipWithId(QStringLiteral("word/settings.xml"), QStringLiteral("rId2"), mergeRel,
                                  QStringLiteral("file:///C:\\Users\\me\\list.xlsx"), true);
        package = out.toZip();
    }
    auto doc = Test::readWord(package);
    QVERIFY(doc);
    QCOMPARE(doc->parts.mailMerge.documentType, QStringLiteral("formLetters"));
    QCOMPARE(doc->parts.mailMerge.table, QStringLiteral("Sheet1"));
    QVERIFY(doc->parts.mailMerge.viewMergedData);
    QCOMPARE(doc->parts.mailMerge.fieldMap.value(QStringLiteral("Last Name")), QStringLiteral("Surname"));
    QTextCursor c(&doc->body);
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral(" (edited)"));
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    const QByteArray settings = Test::partOf(bytes, QStringLiteral("word/settings.xml"));
    // Word's own element, untouched: every child as it was (the ids renumbered).
    QVERIFY2(settings.contains("<w:mailMerge><w:mainDocumentType w:val=\"formLetters\"/><w:linkToQuery/>"
                               "<w:dataType w:val=\"native\"/>"),
             settings.constData());
    QVERIFY(settings.contains("<w:query w:val=\"SELECT * FROM `Sheet1$` \"/>"));
    QVERIFY(settings.contains("<w:colDelim w:val=\"9\"/><w:type w:val=\"database\"/><w:fHdr/><w:fieldMapData>"));
    QVERIFY(Test::partOf(bytes, QStringLiteral("word/_rels/settings.xml.rels")).contains("file:///C:\\Users\\me\\list.xlsx"));
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
}

void DocxMailMergeTest::complexFieldsRoundTrip()
{
    // As Word writes rules: IF with a nested field, a SKIPIF, NEXT with no
    // result, an Address Block whose format breaks lines.
    auto fld = [](const char *type) {
        return QStringLiteral("<w:r><w:fldChar w:fldCharType=\"%1\"/></w:r>").arg(QLatin1String(type));
    };
    auto instr = [](const QString &text) {
        return QStringLiteral("<w:r><w:instrText xml:space=\"preserve\">%1</w:instrText></w:r>").arg(text);
    };
    DocxFixture f;
    f.body = QStringLiteral("<w:p>") + fld("begin") + instr(QStringLiteral(" IF ")) + fld("begin")
        + instr(QStringLiteral(" MERGEFIELD Country ")) + fld("separate")
        + QStringLiteral("<w:r><w:t>«Country»</w:t></w:r>") + fld("end")
        + instr(QStringLiteral(" = \"US\" \"Domestic\" \"International\" ")) + fld("separate")
        + QStringLiteral("<w:r><w:t>Domestic</w:t></w:r>") + fld("end") + QStringLiteral("</w:p>")
        + QStringLiteral("<w:p>") + fld("begin") + instr(QStringLiteral(" SKIPIF ")) + fld("begin")
        + instr(QStringLiteral(" MERGEFIELD Last ")) + fld("end") + instr(QStringLiteral(" = \"\" ")) + fld("end")
        + fld("begin") + instr(QStringLiteral(" NEXT ")) + fld("end")
        + QStringLiteral("<w:r><w:t xml:space=\"preserve\"> Total: </w:t></w:r>")
        + QStringLiteral("<w:fldSimple w:instr=\" MERGEREC \"><w:r><w:t>«Merge Record #»</w:t></w:r></w:fldSimple></w:p>")
        + QStringLiteral("<w:p>") + fld("begin") + instr(QStringLiteral(" ADDRESSBLOCK \\f \"&lt;&lt;_FIRST0_&gt;&gt;"))
        + QStringLiteral("<w:r><w:br/></w:r>") + instr(QStringLiteral("&lt;&lt;_CITY_&gt;&gt;\" \\c 0 ")) + fld("separate")
        + QStringLiteral("<w:r><w:t>«AddressBlock»</w:t></w:r>") + fld("end") + QStringLiteral("</w:p>");
    auto doc = Test::readWord(f.build());
    QVERIFY(doc);
    const QList<Docx::MergeFieldSpan> spans = Docx::mergeFieldSpans(doc->body);
    QCOMPARE(spans.size(), 5);
    QCOMPARE(spans.at(0).instruction, QStringLiteral("IF { MERGEFIELD Country } = \"US\" \"Domestic\" \"International\""));
    QCOMPARE(spans.at(1).instruction, QStringLiteral("SKIPIF { MERGEFIELD Last } = \"\""));
    QCOMPARE(spans.at(2).instruction, QStringLiteral("NEXT"));
    QCOMPARE(spans.at(3).instruction, QStringLiteral("MERGEREC"));
    QCOMPARE(spans.at(4).instruction, QStringLiteral("ADDRESSBLOCK \\f \"<<_FIRST0_>>\n<<_CITY_>>\" \\c 0"));
    QCOMPARE(doc->body.begin().text(), QStringLiteral("Domestic"));
    QCOMPARE(doc->body.begin().next().text(), QStringLiteral("«Skip Record If…»«Next Record» Total: «Merge Record #»"));
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    const QByteArray xml = Test::partOf(bytes, QStringLiteral("word/document.xml"));
    // NEXT and SKIPIF without a result, as Word writes them.
    QVERIFY2(xml.contains("<w:instrText xml:space=\"preserve\"> NEXT </w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"end\"/>"),
             xml.constData());
    QVERIFY(xml.contains("<w:instrText xml:space=\"preserve\"> MERGEFIELD Country </w:instrText></w:r><w:r>"
                         "<w:fldChar w:fldCharType=\"end\"/>"));
    QVERIFY(xml.contains("<w:r><w:br/></w:r>"));
    QVERIFY(xml.contains("<w:fldSimple w:instr=\" MERGEREC \">"));
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    const QList<Docx::MergeFieldSpan> again = Docx::mergeFieldSpans(back->body);
    QCOMPARE(again.size(), spans.size());
    for (int i = 0; i < spans.size(); ++i) {
        QCOMPARE(again.at(i).instruction, spans.at(i).instruction);
    }
    QCOMPARE(back->body.toPlainText(), doc->body.toPlainText());
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
}

void DocxMailMergeTest::libreOfficeRendersAMergedLetter()
{
    if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) {
        QSKIP("LibreOffice is not installed");
    }
    QTextDocument main;
    letter(&main);
    const Docx::MergeData data = people();
    QTextDocument out;
    Docx::MergeRequest request;
    request.main = &main;
    request.data = &data;
    request.order = {0, 1, 2};
    Docx::WordParts parts;
    request.sectionXml = Docx::sectionPropertiesXml(parts);
    QCOMPARE(Docx::mergeDocument(&out, request).copies, 3);
    QTemporaryDir scratch;
    const QString pdf = Test::renderWithLibreOffice(Docx::docxBytes(out, parts), QStringLiteral("m77_merged_letters"), scratch);
    QVERIFY(!pdf.isEmpty());
    const QString text = Test::pdfText(pdf);
    if (!text.isEmpty()) {
        QVERIFY(text.contains(QStringLiteral("Analytical Engines")));
        QVERIFY(text.contains(QStringLiteral("Dear Hopper,")));
        QVERIFY(text.contains(QStringLiteral("$250.00")));
        QCOMPARE(text.count(QLatin1Char('\f')), 3); // a page per record
    }
}

QTEST_MAIN(DocxMailMergeTest)
#include "tst_docx_mailmerge.moc"
