// SPDX-License-Identifier: GPL-3.0-or-later
// Word's styles, the Format Painter and the Navigation pane in QindaWrite
// through the real window (M73, D-082): the Styles pane lists the
// document's styles and Word's built-ins it lacks and applies them; the
// gallery shows the document's quick styles; Modify Style re-derives every
// paragraph of the style in one undo step and saves a real w:style; New
// Style, Update Style to Match Selection and Delete Style; character styles
// as w:rStyle; Clear Formatting keeps the style; Enter gives the style's
// next style; the Format Painter paints once and, double-clicked, until
// Esc; the Navigation pane follows the headings and moves a section.
// QQO_GRAB_DIR keeps grabs of the Styles pane, Modify Style and the
// Navigation pane at 1280 and 520 px.

#include "write_test_support.h"

#include "../office_docx/docx_fixture.h"
#include "docx_list_labels.h"
#include "docx_properties.h"
#include "qindawrite/src/write_navigation.h"
#include "qindawrite/src/write_painter.h"
#include "qindawrite/src/write_style_draft.h"
#include "qindawrite/src/write_styles.h"
#include "support/quick_dump.h"

#include "ooxml_package.h"

#include <QTextBlock>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
namespace Docx = QindaOffice::Docx;
using QindaOffice::Test::para;

namespace {

const char *kStyles =
    R"(<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri"/><w:sz w:val="22"/></w:rPr></w:rPrDefault>)"
    R"(<w:pPrDefault><w:pPr><w:spacing w:after="160"/></w:pPr></w:pPrDefault></w:docDefaults>)"
    R"(<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style>)"
    R"(<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/>)"
    R"(<w:next w:val="Normal"/><w:link w:val="Heading1Char"/><w:uiPriority w:val="9"/><w:qFormat/><w:pPr><w:keepNext/>)"
    R"(<w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:color w:val="2F5496"/><w:sz w:val="32"/></w:rPr></w:style>)"
    R"(<w:style w:type="character" w:customStyle="1" w:styleId="Heading1Char"><w:name w:val="Heading 1 Char"/>)"
    R"(<w:link w:val="Heading1"/><w:uiPriority w:val="9"/><w:rPr><w:color w:val="2F5496"/><w:sz w:val="32"/></w:rPr></w:style>)"
    R"(<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/>)"
    R"(<w:next w:val="Normal"/><w:uiPriority w:val="9"/><w:qFormat/><w:pPr><w:keepNext/><w:outlineLvl w:val="1"/></w:pPr>)"
    R"(<w:rPr><w:sz w:val="26"/></w:rPr></w:style>)"
    R"(<w:style w:type="paragraph" w:customStyle="1" w:styleId="Note"><w:name w:val="Note"/><w:basedOn w:val="Normal"/><w:qFormat/>)"
    R"(<w:pPr><w:ind w:left="360"/></w:pPr><w:rPr><w:i/></w:rPr></w:style>)";

QString writeFixture(const QTemporaryDir &dir)
{
    QindaOffice::Test::DocxFixture f;
    f.styles = QString::fromLatin1(kStyles);
    const auto h1 = QStringLiteral("<w:pStyle w:val=\"Heading1\"/>");
    const auto h2 = QStringLiteral("<w:pStyle w:val=\"Heading2\"/>");
    f.body = para(QStringLiteral("Introduction"), h1) + para(QStringLiteral("Opening words")) + para(QStringLiteral("Scope"), h2)
        + para(QStringLiteral("A note"), QStringLiteral("<w:pStyle w:val=\"Note\"/>")) + para(QStringLiteral("Plan"), h1)
        + para(QStringLiteral("Budget"), h2) + para(QStringLiteral("Staffing"), h2) + para(QStringLiteral("Closing words"));
    const QString path = dir.filePath(QStringLiteral("report.docx"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(f.build());
    }
    return path;
}

QTextBlock blockWithText(QTextDocument *doc, const QString &text)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.text() == text) {
            return b;
        }
    }
    return {};
}

void caretIn(Scene &scene, const QString &text, int from = -1, int length = 0)
{
    const QTextBlock block = blockWithText(scene.doc->textDocument(), text);
    QTextCursor c(block);
    if (from >= 0) {
        c.setPosition(block.position() + from);
        c.setPosition(block.position() + from + length, QTextCursor::KeepAnchor);
    }
    scene.control()->setTextCursor(c);
}

QTextCharFormat runOf(Scene &scene, const QString &text, int offset = 1)
{
    QTextCursor c(blockWithText(scene.doc->textDocument(), text));
    c.setPosition(c.position() + offset);
    return c.charFormat();
}

QString styleOf(Scene &scene, const QString &text)
{
    return blockWithText(scene.doc->textDocument(), text).blockFormat().stringProperty(Docx::kParagraphStyleId);
}

QByteArray savedPart(Scene &scene, const QString &part)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("saved.docx"));
    QString error;
    if (!scene.doc->save(path, &error)) {
        qWarning() << error;
        return {};
    }
    return QindaOffice::Ooxml::Package::load(path)->data(part);
}

QVariantMap row(StylesController *styles, const QString &id)
{
    for (const QVariant &v : styles->styles()) {
        if (v.toMap().value(QStringLiteral("id")).toString() == id) {
            return v.toMap();
        }
    }
    return {};
}

bool load(Scene &scene, const QTemporaryDir &dir)
{
    QString error;
    const bool ok = scene.doc->load(writeFixture(dir), &error);
    if (!ok) {
        qWarning() << error;
    }
    return ok;
}

} // namespace

class WriteStylesTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected"))); }
    void paneListsAndApplies();
    void modifyStyleUpdatesEveryParagraph();
    void newUpdateAndDeleteStyle();
    void clearFormattingAndNextStyle();
    void formatPainterOnceAndLocked();
    void navigationPaneFollowsHeadings();
    void panesAndDialogAtBothWidths();
};

void WriteStylesTest::paneListsAndApplies()
{
    QTemporaryDir dir;
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(load(scene, dir));
    StylesController *styles = scene.host.stylesController();
    QCOMPARE(row(styles, QStringLiteral("Heading1")).value(QStringLiteral("type")).toString(), QStringLiteral("linked"));
    QVERIFY(row(styles, QStringLiteral("Heading1")).value(QStringLiteral("inUse")).toBool());
    QVERIFY(!row(styles, QStringLiteral("Strong")).value(QStringLiteral("inDocument")).toBool());
    QVERIFY(row(styles, QStringLiteral("Heading1Char")).isEmpty()); // the linked pair's character half
    // The gallery: the document's quick styles, Normal first.
    QCOMPARE(styles->quickStyleIds().first(), QStringLiteral("Normal"));
    QCOMPARE(scene.host.actions()->entry(QStringLiteral("writeQuickStyle0"))->text(), QStringLiteral("Normal"));
    const int note = int(styles->quickStyleIds().indexOf(QStringLiteral("Note")));
    QVERIFY(note > 0);
    // A gallery slot applies its style, in one step.
    caretIn(scene, QStringLiteral("Opening words"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeQuickStyle%1").arg(note)));
    QCOMPARE(styleOf(scene, QStringLiteral("Opening words")), QStringLiteral("Note"));
    QVERIFY(runOf(scene, QStringLiteral("Opening words")).fontItalic());
    QCOMPARE(styles->currentStyle(), QStringLiteral("Note"));
    scene.control()->undo();
    QCOMPARE(styleOf(scene, QStringLiteral("Opening words")), QStringLiteral("Normal"));
    // A built-in the document lacks: Strong, as a character style.
    caretIn(scene, QStringLiteral("Closing words"), 0, 7);
    QVERIFY(styles->apply(QStringLiteral("Strong")));
    QCOMPARE(runOf(scene, QStringLiteral("Closing words")).stringProperty(Docx::kRunStyleId), QStringLiteral("Strong"));
    QCOMPARE(runOf(scene, QStringLiteral("Closing words")).fontWeight(), int(QFont::Bold));
    QTRY_VERIFY(row(styles, QStringLiteral("Strong")).value(QStringLiteral("inDocument")).toBool());
    const QByteArray xml = savedPart(scene, QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<w:rStyle w:val=\"Strong\"/>"), xml.constData());
    QVERIFY(savedPart(scene, QStringLiteral("word/styles.xml")).contains("w:styleId=\"Strong\""));
    scene.control()->undo(); // the run and the styles part, in one step
    QVERIFY(runOf(scene, QStringLiteral("Closing words")).stringProperty(Docx::kRunStyleId).isEmpty());
    QTRY_VERIFY(!row(styles, QStringLiteral("Strong")).value(QStringLiteral("inDocument")).toBool());
    // The linked Heading 1 on part of a paragraph: its character half.
    caretIn(scene, QStringLiteral("Closing words"), 0, 7);
    QVERIFY(styles->apply(QStringLiteral("Heading1")));
    QCOMPARE(runOf(scene, QStringLiteral("Closing words")).stringProperty(Docx::kRunStyleId), QStringLiteral("Heading1Char"));
    QCOMPARE(styleOf(scene, QStringLiteral("Closing words")), QStringLiteral("Normal"));
    // The pane opens from its command (Alt+Ctrl+Shift+S).
    if (!scene.host.actions()->entry(QStringLiteral("writeStylesPaneAction"))->isChecked()) {
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeStylesPaneAction")));
    }
    QTRY_VERIFY(scene.item(QStringLiteral("stylesPanel")) != nullptr && scene.item(QStringLiteral("stylesPanel"))->isVisible());
}

void WriteStylesTest::modifyStyleUpdatesEveryParagraph()
{
    QTemporaryDir dir;
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(load(scene, dir));
    StylesController *styles = scene.host.stylesController();
    caretIn(scene, QStringLiteral("Plan"));
    QCOMPARE(styles->currentStyle(), QStringLiteral("Heading1"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeModifyStyleAction")));
    StyleDraft *draft = styles->draft();
    QCOMPARE(draft->mode(), QStringLiteral("modify"));
    QCOMPARE(draft->name(), QStringLiteral("Heading 1"));
    QCOMPARE(draft->look().value(QStringLiteral("size")).toReal(), 16.0);
    // M70's Font and Paragraph dialogs, on the style.
    QVERIFY(draft->applyFont({{QStringLiteral("changed"), QStringList{QStringLiteral("size"), QStringLiteral("color")}},
                              {QStringLiteral("size"), 20}, {QStringLiteral("color"), QStringLiteral("#c00000")}}));
    QVERIFY(draft->applyParagraph({{QStringLiteral("changed"), QStringList{QStringLiteral("alignment")}},
                                   {QStringLiteral("alignment"), QStringLiteral("center")}}));
    QVERIFY(draft->description().contains(QStringLiteral("20 pt")));
    QVERIFY(styles->commitDraft());
    for (const char *heading : {"Introduction", "Plan"}) {
        QCOMPARE(runOf(scene, QString::fromLatin1(heading)).fontPointSize(), 20.0);
        QCOMPARE(runOf(scene, QString::fromLatin1(heading)).foreground().color(), QColor(0xC0, 0x00, 0x00));
        QCOMPARE(blockWithText(scene.doc->textDocument(), QString::fromLatin1(heading)).blockFormat().alignment()
                     & Qt::AlignHorizontal_Mask,
                 Qt::AlignHCenter);
    }
    QCOMPARE(runOf(scene, QStringLiteral("Scope")).fontPointSize(), 13.0); // Heading 2 untouched
    const QByteArray stylesXml = savedPart(scene, QStringLiteral("word/styles.xml"));
    QVERIFY2(stylesXml.contains("<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/>"
                                "<w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:link w:val=\"Heading1Char\"/>"),
             stylesXml.constData());
    QVERIFY(stylesXml.contains("<w:jc w:val=\"center\"/>"));
    QVERIFY(stylesXml.contains("<w:color w:val=\"C00000\"/>"));
    const QByteArray xml = savedPart(scene, QStringLiteral("word/document.xml"));
    QVERIFY(!xml.contains("C00000")); // the paragraphs say only <w:pStyle/>
    // One Undo: every heading and the style back.
    scene.control()->undo();
    QCOMPARE(runOf(scene, QStringLiteral("Introduction")).fontPointSize(), 16.0);
    QCOMPARE(runOf(scene, QStringLiteral("Plan")).fontPointSize(), 16.0);
    QVERIFY(!savedPart(scene, QStringLiteral("word/styles.xml")).contains("C00000"));
    // Format ▸ Numbering: Heading 2 numbered; the style says so (w:numPr),
    // its paragraphs inherit it and read back numbered.
    caretIn(scene, QStringLiteral("Budget"));
    QVERIFY(styles->modify(QStringLiteral("Heading2")));
    draft->setNumbering(QStringLiteral("number"));
    QVERIFY(styles->commitDraft());
    QCOMPARE(Docx::listMarker(blockWithText(scene.doc->textDocument(), QStringLiteral("Budget"))).text, QStringLiteral("2."));
    const QByteArray numbered = savedPart(scene, QStringLiteral("word/styles.xml"));
    QVERIFY2(numbered.contains("w:styleId=\"Heading2\"") && numbered.contains("<w:numPr><w:numId w:val="), numbered.constData());
    const QByteArray body = savedPart(scene, QStringLiteral("word/document.xml"));
    QVERIFY2(!body.contains("<w:numPr>"), body.constData()); // inherited from the style
    QTemporaryDir again;
    const QString path = again.filePath(QStringLiteral("numbered.docx"));
    QVERIFY(scene.doc->save(path));
    Scene reopened;
    QVERIFY(reopened.doc->load(path));
    QCOMPARE(Docx::listMarker(blockWithText(reopened.doc->textDocument(), QStringLiteral("Staffing"))).text, QStringLiteral("3."));
}

void WriteStylesTest::newUpdateAndDeleteStyle()
{
    QTemporaryDir dir;
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(load(scene, dir));
    StylesController *styles = scene.host.stylesController();
    // "Opening words": bold and right-aligned, then a style of it.
    caretIn(scene, QStringLiteral("Opening words"), 0, 13);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatBoldAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatAlignRightAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeNewStyleAction")));
    StyleDraft *draft = styles->draft();
    QCOMPARE(draft->mode(), QStringLiteral("new"));
    QCOMPARE(draft->basedOn(), QStringLiteral("Normal"));
    draft->setName(QStringLiteral("Callout"));
    QVERIFY(styles->commitDraft());
    // A linked style: a paragraph style and its character half, each naming the other.
    caretIn(scene, QStringLiteral("A note"));
    styles->newStyle();
    draft->setName(QStringLiteral("Aside"));
    draft->setType(QStringLiteral("linked"));
    QVERIFY(styles->commitDraft());
    const QByteArray linked = savedPart(scene, QStringLiteral("word/styles.xml"));
    QVERIFY2(linked.contains("w:styleId=\"Aside\"><w:name w:val=\"Aside\"/><w:basedOn w:val=\"Note\"/><w:next w:val=\"Aside\"/>"
                             "<w:link w:val=\"AsideChar\"/>"),
             linked.constData());
    QVERIFY(linked.contains("w:styleId=\"AsideChar\"><w:name w:val=\"Aside Char\"/><w:link w:val=\"Aside\"/>"));
    QCOMPARE(row(styles, QStringLiteral("Aside")).value(QStringLiteral("type")).toString(), QStringLiteral("linked"));
    QCOMPARE(styleOf(scene, QStringLiteral("Opening words")), QStringLiteral("Callout"));
    QByteArray stylesXml = savedPart(scene, QStringLiteral("word/styles.xml"));
    QVERIFY2(stylesXml.contains("<w:style w:type=\"paragraph\" w:customStyle=\"1\" w:styleId=\"Callout\"><w:name w:val=\"Callout\"/>"
                                "<w:basedOn w:val=\"Normal\"/><w:next w:val=\"Callout\"/><w:qFormat/>"),
             stylesXml.constData());
    QVERIFY(savedPart(scene, QStringLiteral("word/document.xml")).contains("<w:pStyle w:val=\"Callout\"/></w:pPr>"));
    // Another paragraph takes it; then Update Callout to Match Selection on
    // an italic one: both follow.
    caretIn(scene, QStringLiteral("Closing words"));
    QVERIFY(styles->apply(QStringLiteral("Callout")));
    QCOMPARE(runOf(scene, QStringLiteral("Closing words")).fontWeight(), int(QFont::Bold));
    caretIn(scene, QStringLiteral("Closing words"), 0, 13);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatItalicAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeUpdateStyleAction")));
    QVERIFY(runOf(scene, QStringLiteral("Opening words")).fontItalic());
    QVERIFY(savedPart(scene, QStringLiteral("word/styles.xml")).contains("<w:i/>"));
    // Delete Callout: its paragraphs fall back to Normal.
    QVERIFY(styles->canRemove(QStringLiteral("Callout")));
    QVERIFY(!styles->canRemove(QStringLiteral("Heading1"))); // Word's own
    QVERIFY(styles->remove(QStringLiteral("Callout")));
    QCOMPARE(styleOf(scene, QStringLiteral("Opening words")), QStringLiteral("Normal"));
    QCOMPARE(blockWithText(scene.doc->textDocument(), QStringLiteral("Opening words")).blockFormat().alignment()
                 & Qt::AlignHorizontal_Mask,
             Qt::AlignLeft);
    QVERIFY(!savedPart(scene, QStringLiteral("word/styles.xml")).contains("Callout"));
    scene.control()->undo();
    QCOMPARE(styleOf(scene, QStringLiteral("Opening words")), QStringLiteral("Callout"));
}

void WriteStylesTest::clearFormattingAndNextStyle()
{
    QTemporaryDir dir;
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(load(scene, dir));
    // A heading with an italic word and its own centring: Clear Formatting
    // keeps Heading 1 and drops the rest.
    caretIn(scene, QStringLiteral("Plan"), 0, 4);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatItalicAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatAlignCenterAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatClearFormatAction")));
    QCOMPARE(styleOf(scene, QStringLiteral("Plan")), QStringLiteral("Heading1"));
    QVERIFY(!runOf(scene, QStringLiteral("Plan")).fontItalic());
    QCOMPARE(runOf(scene, QStringLiteral("Plan")).fontPointSize(), 16.0);
    QCOMPARE(blockWithText(scene.doc->textDocument(), QStringLiteral("Plan")).blockFormat().alignment() & Qt::AlignHorizontal_Mask,
             Qt::AlignLeft);
    scene.control()->undo();
    QVERIFY(runOf(scene, QStringLiteral("Plan")).fontItalic());
    // Enter at a heading's end: the document's own Normal follows.
    caretIn(scene, QStringLiteral("Introduction"));
    QTextCursor end = scene.control()->textCursor();
    end.movePosition(QTextCursor::EndOfBlock);
    scene.control()->setTextCursor(end);
    QTest::keyClick(scene.window, Qt::Key_Return);
    for (const char c : {'B', 'o', 'd', 'y'}) {
        QTest::keyClick(scene.window, c);
    }
    QCOMPARE(styleOf(scene, QStringLiteral("Body")), QStringLiteral("Normal"));
    QCOMPARE(runOf(scene, QStringLiteral("Body")).fontPointSize(), 11.0);
}

void WriteStylesTest::formatPainterOnceAndLocked()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("Source text\nOne target\nTwo target\nThree target"));
    caretIn(scene, QStringLiteral("Source text"), 0, 6);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatBoldAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatAlignCenterAction")));
    FormatPainter *painter = scene.host.formatPainter();
    // Once: the next mouse selection is painted, then the brush is put away.
    caretIn(scene, QStringLiteral("Source text"), 0, 6);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeFormatPainterAction")));
    QVERIFY(painter->isActive() && !painter->isLocked());
    caretIn(scene, QStringLiteral("One target"), 0, 3);
    emit scene.page->selectionReleased();
    QCOMPARE(runOf(scene, QStringLiteral("One target")).fontWeight(), int(QFont::Bold));
    QCOMPARE(runOf(scene, QStringLiteral("One target"), 5).fontWeight(), int(QFont::Normal));
    QVERIFY(!painter->isActive());
    scene.control()->undo(); // one step
    QCOMPARE(runOf(scene, QStringLiteral("One target")).fontWeight(), int(QFont::Normal));
    // Locked: a second click in time keeps it until Esc.
    caretIn(scene, QStringLiteral("Source text"), 0, 6);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeFormatPainterAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeFormatPainterAction")));
    QVERIFY(painter->isLocked());
    for (const char *text : {"One target", "Two target"}) {
        caretIn(scene, QString::fromLatin1(text), 0, 3);
        emit scene.page->selectionReleased();
        QCOMPARE(runOf(scene, QString::fromLatin1(text)).fontWeight(), int(QFont::Bold));
    }
    QVERIFY(painter->isActive());
    QTest::keyClick(scene.window, Qt::Key_Escape);
    QVERIFY(!painter->isActive());
    // Ctrl+Shift+C / Ctrl+Shift+V: a whole paragraph's look (centred) too.
    QTextCursor whole(blockWithText(scene.doc->textDocument(), QStringLiteral("Source text")));
    whole.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    scene.control()->setTextCursor(whole);
    QTest::keyClick(scene.window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    caretIn(scene, QStringLiteral("Three target"), 0, 5);
    QTest::keyClick(scene.window, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(runOf(scene, QStringLiteral("Three target")).fontWeight(), int(QFont::Bold));
    QCOMPARE(blockWithText(scene.doc->textDocument(), QStringLiteral("Three target")).blockFormat().alignment()
                 & Qt::AlignHorizontal_Mask,
             Qt::AlignHCenter);
}

void WriteStylesTest::navigationPaneFollowsHeadings()
{
    QTemporaryDir dir;
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(load(scene, dir));
    NavigationModel *navigation = scene.host.navigation();
    navigation->refresh();
    QStringList titles;
    QList<int> levels;
    for (const QVariant &v : navigation->headings()) {
        titles << v.toMap().value(QStringLiteral("text")).toString();
        levels << v.toMap().value(QStringLiteral("level")).toInt();
    }
    QCOMPARE(titles, (QStringList{QStringLiteral("Introduction"), QStringLiteral("Scope"), QStringLiteral("Plan"),
                                  QStringLiteral("Budget"), QStringLiteral("Staffing")}));
    QCOMPARE(levels, (QList<int>{1, 2, 1, 2, 2}));
    navigation->go(2);
    QCOMPARE(scene.control()->textCursor().block().text(), QStringLiteral("Plan"));
    QCOMPARE(navigation->current(), 2);
    // Collapse Introduction: Scope is hidden under it.
    navigation->toggle(0);
    QVERIFY(!navigation->headings().at(1).toMap().value(QStringLiteral("visible")).toBool());
    QVERIFY(navigation->headings().at(3).toMap().value(QStringLiteral("visible")).toBool());
    navigation->toggle(0);
    // Move Budget down: Staffing comes first, in one undo step.
    QVERIFY(navigation->move(3, false));
    QCOMPARE(navigation->headings().at(3).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Staffing"));
    QCOMPARE(scene.control()->textCursor().block().text(), QStringLiteral("Budget"));
    QCOMPARE(styleOf(scene, QStringLiteral("Budget")), QStringLiteral("Heading2"));
    scene.control()->undo();
    navigation->refresh();
    QCOMPARE(navigation->headings().at(3).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Budget"));
    // Move Introduction down past Plan's section.
    QVERIFY(navigation->move(0, false));
    QCOMPARE(navigation->headings().first().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Plan"));
    QVERIFY(blockWithText(scene.doc->textDocument(), QStringLiteral("Closing words")).isValid());
    if (!scene.host.actions()->entry(QStringLiteral("writeNavigationPaneAction"))->isChecked()) {
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeNavigationPaneAction")));
    }
    QTRY_VERIFY(scene.item(QStringLiteral("navigationPanel")) != nullptr && scene.item(QStringLiteral("navigationPanel"))->isVisible());
}

void WriteStylesTest::panesAndDialogAtBothWidths()
{
    for (const int width : {1280, 520}) {
        QTemporaryDir dir;
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(load(scene, dir));
        caretIn(scene, QStringLiteral("Plan"));
        // The dock remembers its panes between windows: show, never toggle off.
        for (const char *pane : {"writeStylesPaneAction", "writeNavigationPaneAction"}) {
            if (!scene.host.actions()->entry(QString::fromLatin1(pane))->isChecked()) {
                QVERIFY(scene.host.actions()->trigger(QString::fromLatin1(pane)));
            }
        }
        QTRY_VERIFY(scene.item(QStringLiteral("stylesPanel")) != nullptr && scene.item(QStringLiteral("stylesPanel"))->isVisible());
        QTRY_VERIFY(scene.item(QStringLiteral("navigationPanel")) != nullptr && scene.item(QStringLiteral("navigationPanel"))->isVisible());
        QTest::qWait(200);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m73_panes_%1").arg(width));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeModifyStyleAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("styleDialog"));
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m73_modifyStyle_%1").arg(width));
        QMetaObject::invokeMethod(dialog, "close");
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
}

WRITE_TEST_MAIN(WriteStylesTest)

#include "tst_write_styles.moc"
