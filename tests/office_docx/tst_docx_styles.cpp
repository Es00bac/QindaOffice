// SPDX-License-Identifier: GPL-3.0-or-later
// M73 gates for Word's styles (D-082, docx_style_edit.h, docx_restyle.h):
// a modified style re-derives every paragraph and run that uses it while
// they keep their own formatting, and is written as a real w:style with
// w:basedOn / w:next / w:link / w:qFormat — the paragraphs still saying only
// <w:pStyle/>; a new style made from a selection's formatting; a character
// style written as <w:rStyle/> and removed again; a deleted style's
// children re-based on its parent; Clear Formatting keeping the style; the
// Styles pane's list (linked pairs, built-ins the document lacks, styles in
// use); every package schema-valid and read back equal. LibreOffice renders
// a sample with a modified Heading 1 and a multilevel list when installed.

#include "docx_fixture.h"
#include "docx_format.h"
#include "docx_list_edit.h"
#include "docx_lists.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_restyle.h"
#include "docx_style_edit.h"
#include "docx_writer.h"
#include "ooxml_namespaces.h"
#include "support/ooxml_schema.h"
#include "support/soffice_env.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

using namespace QindaOffice;
using Test::DocxFixture;
using Test::para;

namespace {

const char *kStyles =
    R"(<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri"/><w:sz w:val="22"/></w:rPr></w:rPrDefault>)"
    R"(<w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="259" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>)"
    R"(<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style>)"
    R"(<w:style w:type="character" w:default="1" w:styleId="DefaultParagraphFont"><w:name w:val="Default Paragraph Font"/>)"
    R"(<w:uiPriority w:val="1"/><w:semiHidden/><w:unhideWhenUsed/></w:style>)"
    R"(<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/>)"
    R"(<w:link w:val="Heading1Char"/><w:uiPriority w:val="9"/><w:qFormat/><w:pPr><w:keepNext/><w:spacing w:before="240" w:after="0"/>)"
    R"(<w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:color w:val="2F5496"/><w:sz w:val="32"/></w:rPr></w:style>)"
    R"(<w:style w:type="character" w:customStyle="1" w:styleId="Heading1Char"><w:name w:val="Heading 1 Char"/>)"
    R"(<w:basedOn w:val="DefaultParagraphFont"/><w:link w:val="Heading1"/><w:uiPriority w:val="9"/><w:rPr><w:color w:val="2F5496"/>)"
    R"(<w:sz w:val="32"/></w:rPr></w:style>)"
    R"(<w:style w:type="paragraph" w:customStyle="1" w:styleId="Note"><w:name w:val="Note"/><w:basedOn w:val="Normal"/><w:qFormat/>)"
    R"(<w:pPr><w:ind w:left="360"/></w:pPr><w:rPr><w:i/></w:rPr></w:style>)"
    R"(<w:style w:type="paragraph" w:customStyle="1" w:styleId="SubNote"><w:name w:val="Sub Note"/><w:basedOn w:val="Note"/>)"
    R"(<w:rPr><w:color w:val="595959"/></w:rPr></w:style>)";

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

QTextCharFormat runOf(const QTextDocument &doc, const QString &text)
{
    QTextCursor c(blockWithText(doc, text));
    c.movePosition(QTextCursor::NextCharacter);
    return c.charFormat();
}

QByteArray part(const QByteArray &bytes, const QString &name)
{
    return Ooxml::Package::fromData(bytes)->data(name);
}

QByteArray source()
{
    DocxFixture f;
    f.styles = QString::fromLatin1(kStyles);
    f.body = para(QStringLiteral("Overview"), QStringLiteral("<w:pStyle w:val=\"Heading1\"/>"))
        + para(QStringLiteral("Body text"))
        + para(QStringLiteral("Centred heading"), QStringLiteral("<w:pStyle w:val=\"Heading1\"/><w:jc w:val=\"center\"/>"))
        + para(QStringLiteral("A note"), QStringLiteral("<w:pStyle w:val=\"Note\"/>"))
        + para(QStringLiteral("A sub note"), QStringLiteral("<w:pStyle w:val=\"SubNote\"/>"))
        + para(QStringLiteral("Some strong words"));
    return f.build();
}

// Sets the document's styles to `root` and re-derives what `changed` reaches.
void restyle(Doc &doc, const Ooxml::XmlElementPtr &root, const QSet<QString> &changed)
{
    const Docx::StyleSheet before = Docx::DocumentStyles(doc.parts).sheet();
    doc.parts.styles = root;
    Docx::DocumentStyles(doc.parts).restyle(&doc.body, before, changed);
}

} // namespace

class DocxStylesTest : public QObject {
    Q_OBJECT
private slots:
    void paneListsTheDocumentsStyles();
    void modifiedStyleRestylesItsParagraphs();
    void newStyleFromTheSelection();
    void characterStylesRoundTrip();
    void deleteAndClearFormatting();
    void styleNumberingIsWritten();
    void libreOfficeRendersTheSample();
};

void DocxStylesTest::paneListsTheDocumentsStyles()
{
    auto doc = read(source());
    QVERIFY(doc);
    const Ooxml::XmlElementPtr root = Docx::stylesRoot(doc->parts, doc->body.defaultFont());
    const QList<Docx::StyleEntry> entries = Docx::styleEntries(*root, doc->body);
    const auto find = [&](const QString &id) {
        return *std::find_if(entries.cbegin(), entries.cend(), [&](const Docx::StyleEntry &e) { return e.id == id; });
    };
    QCOMPARE(find(QStringLiteral("Heading1")).type, QStringLiteral("linked"));
    QCOMPARE(find(QStringLiteral("Heading1")).name, QStringLiteral("Heading 1"));
    QVERIFY(find(QStringLiteral("Heading1")).inUse);
    QVERIFY(std::none_of(entries.cbegin(), entries.cend(), [](const Docx::StyleEntry &e) { return e.id == u"Heading1Char"; }));
    QVERIFY(std::none_of(entries.cbegin(), entries.cend(), [](const Docx::StyleEntry &e) { return e.id == u"DefaultParagraphFont"; }));
    QVERIFY(!find(QStringLiteral("Strong")).inDocument); // Word's built-in the document lacks
    QCOMPARE(find(QStringLiteral("Strong")).type, QStringLiteral("character"));
    QVERIFY(find(QStringLiteral("SubNote")).inDocument);
    // The gallery: Word's order — Normal first, then by priority.
    const QList<Docx::StyleEntry> quick = Docx::quickStyles(*root, doc->body);
    QCOMPARE(quick.first().id, QStringLiteral("Normal"));
    QVERIFY(std::any_of(quick.cbegin(), quick.cend(), [](const Docx::StyleEntry &e) { return e.id == u"Note"; }));
    QCOMPARE(Docx::displayStyleName(QStringLiteral("toc 2")), QStringLiteral("TOC 2"));
}

void DocxStylesTest::modifiedStyleRestylesItsParagraphs()
{
    auto doc = read(source());
    QVERIFY(doc);
    const Ooxml::XmlElementPtr root = Docx::stylesRoot(doc->parts, doc->body.defaultFont());
    // Modify Heading 1: 20 pt, dark red, centred, 18 pt before.
    const Docx::StyleSheet sheet = Docx::StyleSheet::parse(root.get());
    const Ooxml::XmlElementPtr heading = Docx::editedStyle(
        *Docx::styleElement(*root, QStringLiteral("Heading1")), QStringLiteral("heading 1"), QStringLiteral("Normal"),
        QStringLiteral("Normal"), true,
        Ooxml::parseFragment(QStringLiteral("<w:pPr xmlns:w=\"%1\"><w:keepNext/><w:spacing w:before=\"360\" w:after=\"0\"/>"
                                            "<w:jc w:val=\"center\"/><w:outlineLvl w:val=\"0\"/></w:pPr>")
                                 .arg(QString::fromUtf16(Ooxml::Ns::W))),
        Ooxml::parseFragment(QStringLiteral("<w:rPr xmlns:w=\"%1\"><w:color w:val=\"C00000\"/><w:sz w:val=\"40\"/></w:rPr>")
                                 .arg(QString::fromUtf16(Ooxml::Ns::W))));
    QVERIFY(sheet.style(QStringLiteral("Heading1")) != nullptr);
    QTextCursor edit(&doc->body);
    edit.beginEditBlock();
    restyle(*doc, Docx::withStyle(*root, heading), {QStringLiteral("Heading1")});
    edit.endEditBlock();
    for (const char *text : {"Overview", "Centred heading"}) {
        const QTextCharFormat run = runOf(doc->body, QString::fromLatin1(text));
        QCOMPARE(run.fontPointSize(), 20.0);
        QCOMPARE(run.foreground().color(), QColor(0xC0, 0x00, 0x00));
        QCOMPARE(blockWithText(doc->body, QString::fromLatin1(text)).blockFormat().alignment() & Qt::AlignHorizontal_Mask,
                 Qt::AlignHCenter);
    }
    QCOMPARE(runOf(doc->body, QStringLiteral("Body text")).fontPointSize(), 11.0); // Normal untouched
    // One Undo takes the paragraphs' new look back (the styles part is the caller's).
    doc->body.undo();
    QCOMPARE(runOf(doc->body, QStringLiteral("Overview")).fontPointSize(), 16.0);
    doc->body.redo();
    QCOMPARE(runOf(doc->body, QStringLiteral("Overview")).fontPointSize(), 20.0);

    const QByteArray saved = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(saved);
    const QByteArray styles = part(saved, QStringLiteral("word/styles.xml"));
    QVERIFY2(styles.contains("<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/>"
                             "<w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:link w:val=\"Heading1Char\"/><w:uiPriority w:val=\"9\"/>"
                             "<w:qFormat/>"),
             styles.constData());
    QVERIFY(styles.contains("<w:rPr><w:color w:val=\"C00000\"/><w:sz w:val=\"40\"/></w:rPr>"));
    // The paragraphs say only their style (the centred one its own jc too);
    // the runs say nothing: the style does.
    const QByteArray xml = part(saved, QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Overview"), xml.constData());
    QVERIFY(xml.contains("<w:pPr><w:pStyle w:val=\"Heading1\"/><w:jc w:val=\"center\"/></w:pPr><w:r><w:t>Centred"));
    QVERIFY(!xml.contains("C00000"));
    auto back = read(saved);
    QVERIFY(back);
    QCOMPARE(runOf(back->body, QStringLiteral("Overview")).fontPointSize(), 20.0);
    QCOMPARE(runOf(back->body, QStringLiteral("Overview")).foreground().color(), QColor(0xC0, 0x00, 0x00));
}

void DocxStylesTest::newStyleFromTheSelection()
{
    auto doc = read(source());
    QVERIFY(doc);
    // "Body text", centred and bold in Write: a new linked style from it.
    const QTextBlock body = blockWithText(doc->body, QStringLiteral("Body text"));
    QTextBlockFormat centred = body.blockFormat();
    centred.setAlignment(Qt::AlignHCenter);
    QTextCursor(body).setBlockFormat(centred);
    QTextCursor words(body);
    words.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    words.mergeCharFormat(bold);
    const Ooxml::XmlElementPtr pPr = Docx::paragraphFormatting(body, doc->parts);
    const Ooxml::XmlElementPtr rPr = Docx::runFormatting(runOf(doc->body, QStringLiteral("Body text")), doc->body, doc->parts);
    QVERIFY(pPr && rPr);
    QCOMPARE(Ooxml::serializeFragment(*pPr).count(QStringLiteral("<w:jc w:val=\"center\"/>")), 1);
    QVERIFY(Ooxml::serializeFragment(*rPr).contains(QStringLiteral("<w:b/>")));
    const Ooxml::XmlElementPtr root = Docx::stylesRoot(doc->parts, doc->body.defaultFont());
    const QString id = Docx::newStyleId(*root, QStringLiteral("Note"));
    QCOMPARE(id, QStringLiteral("Note2"));
    const Ooxml::XmlElementPtr style = Docx::makeStyle(QStringLiteral("paragraph"), id, QStringLiteral("Note"), QStringLiteral("Normal"),
                                                       id, true, pPr, rPr);
    doc->parts.styles = Docx::withStyle(*root, style);
    Docx::DocumentStyles(doc->parts).apply(body, id);
    QCOMPARE(body.blockFormat().stringProperty(Docx::kParagraphStyleId), id);
    QCOMPARE(body.blockFormat().alignment() & Qt::AlignHorizontal_Mask, Qt::AlignHCenter); // the style's now
    const QByteArray saved = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(saved);
    const QByteArray styles = part(saved, QStringLiteral("word/styles.xml"));
    QVERIFY2(styles.contains("<w:style w:type=\"paragraph\" w:customStyle=\"1\" w:styleId=\"Note2\"><w:name w:val=\"Note\"/>"
                             "<w:basedOn w:val=\"Normal\"/><w:next w:val=\"Note2\"/><w:qFormat/><w:pPr>"),
             styles.constData());
    const QByteArray xml = part(saved, QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:pPr><w:pStyle w:val=\"Note2\"/></w:pPr>"));
    auto back = read(saved);
    QCOMPARE(blockWithText(back->body, QStringLiteral("Body text")).blockFormat().alignment() & Qt::AlignHorizontal_Mask,
             Qt::AlignHCenter);
    QCOMPARE(runOf(back->body, QStringLiteral("Body text")).fontWeight(), int(QFont::Bold));
}

void DocxStylesTest::characterStylesRoundTrip()
{
    auto doc = read(source());
    QVERIFY(doc);
    const Ooxml::XmlElementPtr root = Docx::stylesRoot(doc->parts, doc->body.defaultFont());
    doc->parts.styles = Docx::withStyle(*root, Docx::builtInStyleFor(*root, QStringLiteral("Strong")));
    QTextCursor strong(blockWithText(doc->body, QStringLiteral("Some strong words")));
    strong.setPosition(strong.position() + 5);
    strong.setPosition(strong.position() + 6, QTextCursor::KeepAnchor); // "strong"
    Docx::DocumentStyles(doc->parts).applyCharacter(strong, QStringLiteral("Strong"));
    const QTextCharFormat run = runOf(doc->body, QStringLiteral("Some strong words")); // "S" stays plain
    QCOMPARE(run.fontWeight(), int(QFont::Normal));
    QTextCursor inside(blockWithText(doc->body, QStringLiteral("Some strong words")));
    inside.setPosition(inside.position() + 7);
    QCOMPARE(inside.charFormat().stringProperty(Docx::kRunStyleId), QStringLiteral("Strong"));
    QCOMPARE(inside.charFormat().fontWeight(), int(QFont::Bold));
    QByteArray saved = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(saved);
    QByteArray xml = part(saved, QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<w:r><w:rPr><w:rStyle w:val=\"Strong\"/></w:rPr><w:t>strong</w:t></w:r>"),
             xml.constData());
    QVERIFY(part(saved, QStringLiteral("word/styles.xml")).contains("w:styleId=\"Strong\""));
    auto back = read(saved);
    QTextCursor again(blockWithText(back->body, QStringLiteral("Some strong words")));
    again.setPosition(again.position() + 7);
    QCOMPARE(again.charFormat().stringProperty(Docx::kRunStyleId), QStringLiteral("Strong"));
    QCOMPARE(again.charFormat().fontWeight(), int(QFont::Bold));
    // Default Paragraph Font: the character style goes, the run is plain.
    again.setPosition(again.block().position() + 5);
    again.setPosition(again.position() + 6, QTextCursor::KeepAnchor);
    Docx::DocumentStyles(back->parts).applyCharacter(again, QString());
    saved = Docx::docxBytes(back->body, back->parts);
    checkSchema(saved);
    xml = part(saved, QStringLiteral("word/document.xml"));
    QVERIFY(!xml.contains("<w:rStyle w:val=\"Strong\"/>"));
    QVERIFY(!xml.contains("<w:b/>"));
}

void DocxStylesTest::deleteAndClearFormatting()
{
    auto doc = read(source());
    QVERIFY(doc);
    const Ooxml::XmlElementPtr root = Docx::stylesRoot(doc->parts, doc->body.defaultFont());
    // Delete Note: Sub Note is based on Note's parent now; Note's paragraph
    // falls back to that parent too.
    const Ooxml::XmlElementPtr without = Docx::withoutStyle(*root, QStringLiteral("Note"));
    const Docx::StyleSheet sheet = Docx::StyleSheet::parse(without.get());
    QVERIFY(sheet.style(QStringLiteral("Note")) == nullptr);
    QCOMPARE(sheet.style(QStringLiteral("SubNote"))->basedOn, QStringLiteral("Normal"));
    doc->parts.styles = without;
    Docx::DocumentStyles(doc->parts).apply(blockWithText(doc->body, QStringLiteral("A note")), QStringLiteral("Normal"));
    QCOMPARE(blockWithText(doc->body, QStringLiteral("A note")).blockFormat().leftMargin(), 0.0);
    // Clear Formatting on the centred heading: still Heading 1, left aligned
    // as the style says; an italic word loses its italic.
    QTextBlock heading = blockWithText(doc->body, QStringLiteral("Centred heading"));
    QTextCursor word(heading);
    word.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
    QTextCharFormat italic;
    italic.setFontItalic(true);
    word.mergeCharFormat(italic);
    QTextCursor all(heading);
    all.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    Docx::DocumentStyles(doc->parts).clearFormatting(all);
    heading = blockWithText(doc->body, QStringLiteral("Centred heading"));
    QCOMPARE(heading.blockFormat().stringProperty(Docx::kParagraphStyleId), QStringLiteral("Heading1"));
    QCOMPARE(heading.blockFormat().alignment() & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
    QVERIFY(!runOf(doc->body, QStringLiteral("Centred heading")).fontItalic());
    QCOMPARE(runOf(doc->body, QStringLiteral("Centred heading")).fontPointSize(), 16.0);
    const QByteArray saved = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(saved);
    const QByteArray xml = part(saved, QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Centred"),
             xml.constData());
    QVERIFY(!xml.contains("<w:i/>"));
    QVERIFY(!part(saved, QStringLiteral("word/styles.xml")).contains("w:styleId=\"Note\""));
}

// Modify Style ▸ Numbering (QindaWrite): the style's paragraphs become one
// list and the style names it; the style's w:numPr points at the w:num the
// list was written with, the paragraphs inherit it and read back numbered.
void DocxStylesTest::styleNumberingIsWritten()
{
    auto doc = read(source());
    QVERIFY(doc);
    const QList<QTextBlock> headings{blockWithText(doc->body, QStringLiteral("Overview")),
                                     blockWithText(doc->body, QStringLiteral("Centred heading"))};
    const int numId = Docx::makeList(headings, Docx::numberingDefinition(QStringLiteral("decimal"), QStringLiteral("%1.")), 0, false);
    const Ooxml::XmlElementPtr root = Docx::stylesRoot(doc->parts, doc->body.defaultFont());
    const Ooxml::XmlElementPtr element = Docx::styleElement(*root, QStringLiteral("Heading1"));
    Ooxml::XmlElementPtr pPr = Ooxml::parseFragment(Ooxml::serializeFragment(*element->child(QString::fromUtf16(Ooxml::Ns::W), u"pPr")));
    pPr->insertElementBefore(Ooxml::parseFragment(QStringLiteral("<w:numPr xmlns:w=\"%1\"><w:numId w:val=\"%2\"/></w:numPr>")
                                                      .arg(QString::fromUtf16(Ooxml::Ns::W))
                                                      .arg(numId)),
                             {u"spacing", u"outlineLvl"});
    doc->parts.styles = Docx::withStyle(*root, Docx::editedStyle(*element, QStringLiteral("heading 1"), QStringLiteral("Normal"),
                                                                QStringLiteral("Normal"), true, pPr,
                                                                Ooxml::parseFragment(Ooxml::serializeFragment(
                                                                    *element->child(QString::fromUtf16(Ooxml::Ns::W), u"rPr")))));
    const QByteArray saved = Docx::docxBytes(doc->body, doc->parts);
    checkSchema(saved);
    const QByteArray styles = part(saved, QStringLiteral("word/styles.xml"));
    QVERIFY2(styles.contains(QStringLiteral("<w:numPr><w:numId w:val=\"%1\"/></w:numPr>").arg(numId).toUtf8()), styles.constData());
    QVERIFY(!part(saved, QStringLiteral("word/document.xml")).contains("<w:numPr>"));
    auto back = read(saved);
    QVERIFY(back);
    const Docx::WordListItem item = Docx::wordListItem(blockWithText(back->body, QStringLiteral("Centred heading")));
    QVERIFY(item.isValid());
    QCOMPARE(item.numId, numId);
}

void DocxStylesTest::libreOfficeRendersTheSample()
{
    if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) {
        QSKIP("LibreOffice is not installed");
    }
    auto doc = read(source());
    QVERIFY(doc);
    const Ooxml::XmlElementPtr root = Docx::stylesRoot(doc->parts, doc->body.defaultFont());
    Ooxml::XmlElementPtr heading = Docx::makeStyle(
        QStringLiteral("paragraph"), QStringLiteral("Heading1"), QStringLiteral("heading 1"), QStringLiteral("Normal"),
        QStringLiteral("Normal"), true, nullptr,
        Ooxml::parseFragment(QStringLiteral("<w:rPr xmlns:w=\"%1\"><w:b/><w:color w:val=\"C00000\"/><w:sz w:val=\"40\"/></w:rPr>")
                                 .arg(QString::fromUtf16(Ooxml::Ns::W))));
    restyle(*doc, Docx::withStyle(*root, heading), {QStringLiteral("Heading1")});
    QTextCursor end(&doc->body);
    end.movePosition(QTextCursor::End);
    for (const char *text : {"Plan", "Scope", "Goals", "Budget"}) {
        end.insertBlock();
        end.insertText(QString::fromLatin1(text));
    }
    QList<QTextBlock> items;
    for (const char *text : {"Plan", "Scope", "Goals", "Budget"}) {
        items.append(blockWithText(doc->body, QString::fromLatin1(text)));
    }
    Docx::makeList(items, Docx::multilevelDefinition(0));
    Docx::setListLevel(items.at(1), 1);
    Docx::setListLevel(items.at(2), 2);
    const QString keep = qEnvironmentVariable("QQO_GRAB_DIR");
    QTemporaryDir scratch;
    const QString dir = keep.isEmpty() ? scratch.path() : keep;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("m73_styles_sample.docx"));
    QString error;
    QVERIFY2(Docx::writeDocx(path, doc->body, doc->parts, {}, &error), qPrintable(error));
    QTemporaryDir profile;
    QProcess soffice;
    Test::startSoffice(soffice, {QStringLiteral("-env:UserInstallation=file://") + profile.path(), QStringLiteral("--headless"),
                                 QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), dir, path});
    QVERIFY(soffice.waitForFinished(120000));
    const QString pdf = QDir(dir).filePath(QStringLiteral("m73_styles_sample.pdf"));
    QVERIFY2(QFileInfo(pdf).size() > 1000, qPrintable(QString::fromUtf8(soffice.readAllStandardError())));
    if (!keep.isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()) {
        QProcess::execute(QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("70"),
                                                        pdf, QDir(dir).filePath(QStringLiteral("m73_styles_sample"))});
    }
}

QTEST_MAIN(DocxStylesTest)
#include "tst_docx_styles.moc"
