// SPDX-License-Identifier: GPL-3.0-or-later
// Word content controls (docx_controls.h): a block-level control around
// paragraphs and a table, and inline check box, drop-down, date and bound
// plain-text controls read with their kinds, symbols, items and text;
// written back unchanged; patched for a ticked box, a chosen item, a date
// and edited text; a copied control written without a second w:id.
// Output XSD-validated with QQO_OOXML_SCHEMA_DIR.

#include "docx_controls.h"
#include "docx_fixture.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_writer.h"
#include "support/ooxml_schema.h"

#include <QFile>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextTable>
#include <QtTest>

using namespace QindaOffice;
using Kind = Docx::ContentControl::Kind;

namespace {

const char *kBody =
    // A rich-text control around two paragraphs and a table.
    R"(<w:sdt><w:sdtPr><w:alias w:val="Summary"/><w:tag w:val="summary"/><w:id w:val="100"/></w:sdtPr>)"
    R"(<w:sdtContent><w:p><w:r><w:t>First inside.</w:t></w:r></w:p><w:p><w:r><w:t>Second inside.</w:t></w:r></w:p>)"
    R"(<w:tbl><w:tblPr><w:tblW w:w="0" w:type="auto"/></w:tblPr><w:tblGrid><w:gridCol w:w="4000"/></w:tblGrid>)"
    R"(<w:tr><w:tc><w:p><w:r><w:t>Cell</w:t></w:r></w:p></w:tc></w:tr></w:tbl></w:sdtContent></w:sdt>)"
    // A paragraph with a check box, a drop-down and a date.
    R"(<w:p><w:r><w:t xml:space="preserve">Agreed </w:t></w:r>)"
    R"(<w:sdt><w:sdtPr><w:id w:val="101"/><w14:checkbox><w14:checked w14:val="0"/>)"
    R"(<w14:checkedState w14:val="2612" w14:font="MS Gothic"/><w14:uncheckedState w14:val="2610" w14:font="MS Gothic"/>)"
    R"(</w14:checkbox></w:sdtPr><w:sdtContent><w:r><w:rPr><w:rFonts w:ascii="MS Gothic" w:hAnsi="MS Gothic"/></w:rPr>)"
    R"(<w:t>☐</w:t></w:r></w:sdtContent></w:sdt><w:r><w:t xml:space="preserve"> colour </w:t></w:r>)"
    R"(<w:sdt><w:sdtPr><w:id w:val="102"/><w:showingPlcHdr/><w:dropDownList>)"
    R"(<w:listItem w:displayText="Choose an item." w:value=""/><w:listItem w:displayText="Red" w:value="R"/>)"
    R"(<w:listItem w:displayText="Green" w:value="G"/></w:dropDownList></w:sdtPr>)"
    R"(<w:sdtContent><w:r><w:t>Choose an item.</w:t></w:r></w:sdtContent></w:sdt>)"
    R"(<w:r><w:t xml:space="preserve"> on </w:t></w:r>)"
    R"(<w:sdt><w:sdtPr><w:id w:val="103"/><w:showingPlcHdr/><w:date><w:dateFormat w:val="M/d/yyyy"/>)"
    R"(<w:lid w:val="en-US"/><w:storeMappedDataAs w:val="dateTime"/><w:calendar w:val="gregorian"/></w:date></w:sdtPr>)"
    R"(<w:sdtContent><w:r><w:t>Click to enter a date.</w:t></w:r></w:sdtContent></w:sdt></w:p>)"
    // A plain-text control bound to the document's title.
    R"(<w:p><w:sdt><w:sdtPr><w:alias w:val="Title"/><w:id w:val="104"/>)"
    R"(<w:dataBinding w:prefixMappings="xmlns:ns0='http://purl.org/dc/elements/1.1/' )"
    R"(xmlns:ns1='http://schemas.openxmlformats.org/package/2006/metadata/core-properties'" )"
    R"(w:xpath="/ns1:coreProperties[1]/ns0:title[1]" w:storeItemID="{6C3C8BC8-F283-45AE-878A-BAB7291924A1}"/>)"
    R"(<w:text/></w:sdtPr><w:sdtContent><w:r><w:t>Old title</w:t></w:r></w:sdtContent></w:sdt></w:p>)"
    R"(<w:p><w:r><w:t>After.</w:t></w:r></w:p>)";

struct Loaded {
    QTextDocument body;
    Docx::WordParts parts;
};

std::unique_ptr<Loaded> read(const std::shared_ptr<const Ooxml::Package> &package)
{
    auto loaded = std::make_unique<Loaded>();
    QString error;
    if (!package || !Docx::readDocx(package, &loaded->body, &loaded->parts, &error)) {
        qWarning() << error;
        return nullptr;
    }
    return loaded;
}

std::unique_ptr<Loaded> load()
{
    Test::DocxFixture fixture;
    fixture.body = QString::fromUtf8(kBody);
    QString error;
    return read(Ooxml::Package::fromData(fixture.build(), &error));
}

std::shared_ptr<const Ooxml::Package> written(const Loaded &doc)
{
    QString error;
    auto package = Ooxml::Package::fromData(Docx::docxBytes(doc.body, doc.parts), &error);
    if (package && Test::schemaValidationAvailable()) {
        const QStringList errors = Test::validatePackage(*package);
        if (!errors.isEmpty()) {
            qWarning().noquote() << errors.join(QLatin1Char('\n'));
            return nullptr;
        }
    }
    return package;
}

// The control of `kind` (the first one).
QString keyOf(const Docx::WordParts &parts, Kind kind)
{
    for (auto it = parts.contentControls.cbegin(); it != parts.contentControls.cend(); ++it) {
        if (it->kind == kind) {
            return it.key();
        }
    }
    return {};
}

// Replaces a control's whole content, keeping its first run's format.
void setControlText(QTextDocument &body, const QString &key, const QString &text)
{
    const QHash<QString, QString> texts = Docx::controlTexts(body);
    const int at = body.toPlainText().indexOf(texts.value(key));
    QVERIFY(at >= 0);
    QTextCursor cursor(&body);
    cursor.setPosition(at + 1);
    const QTextCharFormat format = cursor.charFormat();
    cursor.setPosition(at);
    cursor.setPosition(at + int(texts.value(key).size()), QTextCursor::KeepAnchor);
    cursor.insertText(text, format);
}

} // namespace

class DocxControlsTest : public QObject {
    Q_OBJECT
private slots:
    void readsKindsAndContent();
    void untouchedControlsRoundTrip();
    void editsPatchTheProperties();
    void aCopyDropsItsId();
    void controlAtFindsTheContent();
};

void DocxControlsTest::readsKindsAndContent()
{
    auto doc = load();
    QVERIFY(doc);
    QCOMPARE(doc->parts.contentControls.size(), 5);
    const Docx::ContentControl box = doc->parts.contentControls.value(keyOf(doc->parts, Kind::CheckBox));
    QCOMPARE(box.checkedSymbol, QChar(0x2612));
    QCOMPARE(box.uncheckedFont, QStringLiteral("MS Gothic"));
    QCOMPARE(box.sourceText, QStringLiteral("☐"));
    const Docx::ContentControl list = doc->parts.contentControls.value(keyOf(doc->parts, Kind::DropDown));
    QCOMPARE(list.itemTexts, (QStringList{QStringLiteral("Choose an item."), QStringLiteral("Red"), QStringLiteral("Green")}));
    QCOMPARE(list.itemValues.at(2), QStringLiteral("G"));
    QVERIFY(list.showingPlaceholder);
    QCOMPARE(doc->parts.contentControls.value(keyOf(doc->parts, Kind::Date)).dateFormat, QStringLiteral("M/d/yyyy"));
    const Docx::ContentControl rich = doc->parts.contentControls.value(keyOf(doc->parts, Kind::RichText));
    QVERIFY(rich.block);
    QCOMPARE(rich.title, QStringLiteral("Summary"));
    QCOMPARE(rich.sourceText, QStringLiteral("First inside.\nSecond inside.\nCell"));
    // The paragraphs inside carry the block key, the table too, its cell not.
    const QString richKey = keyOf(doc->parts, Kind::RichText);
    const QTextBlock first = doc->body.begin();
    QCOMPARE(first.blockFormat().property(Docx::kBlockControls).toStringList(), QStringList{richKey});
    QTextCursor inCell(&doc->body);
    inCell.setPosition(doc->body.toPlainText().indexOf(QStringLiteral("Cell")));
    QVERIFY(inCell.currentTable() != nullptr);
    QCOMPARE(inCell.currentTable()->format().property(Docx::kBlockControls).toStringList(), QStringList{richKey});
    QVERIFY(!inCell.block().blockFormat().hasProperty(Docx::kBlockControls));
    QCOMPARE(doc->parts.contentControls.value(keyOf(doc->parts, Kind::PlainText)).sourceText, QStringLiteral("Old title"));
}

void DocxControlsTest::untouchedControlsRoundTrip()
{
    auto doc = load();
    QVERIFY(doc);
    const auto out = written(*doc);
    QVERIFY(out);
    auto again = read(out);
    QVERIFY(again);
    QCOMPARE(again->parts.contentControls.size(), 5);
    QCOMPARE(again->body.toPlainText(), doc->body.toPlainText());
    for (const Kind kind : {Kind::RichText, Kind::CheckBox, Kind::DropDown, Kind::Date, Kind::PlainText}) {
        QCOMPARE(again->parts.contentControls.value(keyOf(again->parts, kind)).propertiesXml,
                 doc->parts.contentControls.value(keyOf(doc->parts, kind)).propertiesXml);
    }
    // The block control still wraps both paragraphs and the table.
    const QString xml = QString::fromUtf8(out->data(QStringLiteral("word/document.xml")));
    const int sdt = xml.indexOf(QStringLiteral("<w:sdtContent>"));
    QVERIFY(sdt >= 0);
    QVERIFY(xml.indexOf(QStringLiteral("First inside.")) > sdt);
    QVERIFY(xml.indexOf(QStringLiteral("</w:tbl></w:sdtContent>")) > 0);
    QVERIFY(xml.indexOf(QStringLiteral("After.")) > xml.lastIndexOf(QStringLiteral("</w:sdt>")));
}

void DocxControlsTest::editsPatchTheProperties()
{
    auto doc = load();
    QVERIFY(doc);
    setControlText(doc->body, keyOf(doc->parts, Kind::CheckBox), QStringLiteral("☒"));
    setControlText(doc->body, keyOf(doc->parts, Kind::DropDown), QStringLiteral("Green"));
    setControlText(doc->body, keyOf(doc->parts, Kind::Date), QStringLiteral("10/8/2026"));
    setControlText(doc->body, keyOf(doc->parts, Kind::PlainText), QStringLiteral("New title"));
    const auto out = written(*doc);
    QVERIFY(out);
    if (const QString dir = qEnvironmentVariable("QQO_GRAB_DIR"); !dir.isEmpty()) {
        QFile file(dir + QStringLiteral("/docx_controls_edited.docx")); // for a look in Word or LibreOffice
        if (file.open(QIODevice::WriteOnly)) {
            file.write(Docx::docxBytes(doc->body, doc->parts));
        }
    }
    auto again = read(out);
    QVERIFY(again);
    auto pr = [&](Kind kind) { return again->parts.contentControls.value(keyOf(again->parts, kind)).propertiesXml; };
    QVERIFY(pr(Kind::CheckBox).contains(QStringLiteral("w14:val=\"1\"")));
    QVERIFY(!pr(Kind::DropDown).contains(QStringLiteral("showingPlcHdr")));
    QVERIFY(pr(Kind::DropDown).contains(QStringLiteral("w:lastValue=\"G\"")));
    QVERIFY(!pr(Kind::Date).contains(QStringLiteral("showingPlcHdr")));
    QVERIFY(pr(Kind::Date).contains(QStringLiteral("w:fullDate=\"2026-10-08T00:00:00Z\"")));
    QVERIFY(!pr(Kind::PlainText).contains(QStringLiteral("dataBinding")));
    QVERIFY(pr(Kind::PlainText).contains(QStringLiteral("w:alias")));
    QVERIFY(again->body.toPlainText().contains(QStringLiteral("Agreed ☒ colour Green on 10/8/2026")));
    QVERIFY(QString::fromUtf8(out->data(QStringLiteral("word/document.xml"))).contains(QStringLiteral("mc:Ignorable")));
}

void DocxControlsTest::aCopyDropsItsId()
{
    auto doc = load();
    QVERIFY(doc);
    // Paste a copy of the check box at the end of the last paragraph.
    const int at = doc->body.toPlainText().indexOf(QChar(0x2610));
    QTextCursor source(&doc->body);
    source.setPosition(at);
    source.setPosition(at + 1, QTextCursor::KeepAnchor);
    const QTextDocumentFragment copy = source.selection();
    QTextCursor end(&doc->body);
    end.movePosition(QTextCursor::End);
    end.insertFragment(copy);
    const auto out = written(*doc);
    QVERIFY(out);
    const QString xml = QString::fromUtf8(out->data(QStringLiteral("word/document.xml")));
    QCOMPARE(xml.count(QStringLiteral("<w14:checkbox>")), 2);
    QCOMPARE(xml.count(QStringLiteral("w:val=\"101\"")), 1);
}

void DocxControlsTest::controlAtFindsTheContent()
{
    auto doc = load();
    QVERIFY(doc);
    const QString text = doc->body.toPlainText();
    const int box = int(text.indexOf(QChar(0x2610)));
    for (const int position : {box, box + 1}) {
        const Docx::ControlSpan span = Docx::controlAt(doc->body, position);
        QCOMPARE(span.key, keyOf(doc->parts, Kind::CheckBox));
        QCOMPARE(span.start, box);
        QCOMPARE(span.end, box + 1);
    }
    const int list = int(text.indexOf(QStringLiteral("Choose an item.")));
    const Docx::ControlSpan item = Docx::controlAt(doc->body, list + 3);
    QCOMPARE(item.key, keyOf(doc->parts, Kind::DropDown));
    QCOMPARE(text.mid(item.start, item.end - item.start), QStringLiteral("Choose an item."));
    const Docx::ControlSpan block = Docx::controlAt(doc->body, 2);
    QCOMPARE(block.key, keyOf(doc->parts, Kind::RichText));
    QCOMPARE(block.start, 0);
    QVERIFY(Docx::controlAt(doc->body, int(text.indexOf(QStringLiteral("After.")))).key.isEmpty());
    QCOMPARE(Docx::qtDateFormat(QStringLiteral("h:mm AM/PM")), QStringLiteral("h:mm AP"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    DocxControlsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_docx_controls.moc"
