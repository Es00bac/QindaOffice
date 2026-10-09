// SPDX-License-Identifier: GPL-3.0-or-later
// Word's Paragraph, Tabs, Borders and Shading, Font and Change Case in
// QindaWrite through the real window (M70, D-079): each dialog's answer and
// each command is one undo step, keep-with-next drives the pagination, tab
// leaders are drawn, the saved .docx carries Word's markup, and the
// dialogs open from their commands. QQO_GRAB_DIR keeps grabs of every
// dialog and of a dotted-leader line at 1280 and 520 px, and a PDF.

#include "write_test_support.h"

#include "docx_borders.h"
#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "qindawrite/src/write_borders.h"
#include "qindawrite/src/write_font.h"
#include "qindawrite/src/write_pagination.h"
#include "qindawrite/src/write_paragraph.h"
#include "support/quick_dump.h"

#include "ooxml_package.h"

#include <QDir>
#include <QJsonDocument>
#include <QTextBlock>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
using QindaOffice::Quick::ActionEntry;
namespace Docx = QindaOffice::Docx;

namespace {

QTextBlock blockWithText(QTextDocument *doc, const QString &text)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.text() == text) {
            return b;
        }
    }
    return {};
}

void selectBlocks(Scene &scene, const QString &first, const QString &last)
{
    QTextCursor c(blockWithText(scene.doc->textDocument(), first));
    const QTextBlock end = blockWithText(scene.doc->textDocument(), last);
    c.setPosition(end.position() + end.length() - 1, QTextCursor::KeepAnchor);
    scene.control()->setTextCursor(c);
}

QByteArray savedXml(Scene &scene, const QString &part = QStringLiteral("word/document.xml"))
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

QObject *openDialog(Scene &scene, const char *action, const char *name)
{
    if (!scene.host.actions()->trigger(QString::fromLatin1(action))) {
        return nullptr;
    }
    QObject *dialog = scene.window->findChild<QObject *>(QString::fromLatin1(name));
    if (dialog == nullptr || !QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); })) {
        return nullptr;
    }
    QTest::qWait(150);
    return dialog;
}

} // namespace

class WriteFormattingTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected"))); }
    void paragraphDialogAppliesInOneStep();
    void keepWithNextDrivesPagination();
    void tabStopsWithLeaders();
    void paragraphBordersAndShading();
    void fontDialogAndChangeCase();
    void dialogsOpenAtBothWidths();
};

void WriteFormattingTest::paragraphDialogAppliesInOneStep()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("One\nTwo\nThree"));
    selectBlocks(scene, QStringLiteral("Two"), QStringLiteral("Two"));
    ParagraphController *paragraph = scene.host.paragraphController();
    const QVariantMap info = paragraph->paragraphInfo();
    QCOMPARE(info.value(QStringLiteral("alignment")).toString(), QStringLiteral("left"));
    QCOMPARE(info.value(QStringLiteral("outline")).toInt(), 9);
    QVERIFY(paragraph->applyParagraph({{QStringLiteral("changed"),
                                        QStringList{QStringLiteral("alignment"), QStringLiteral("left"), QStringLiteral("special"),
                                                    QStringLiteral("by"), QStringLiteral("before"), QStringLiteral("after"),
                                                    QStringLiteral("lineRule"), QStringLiteral("lineAt"), QStringLiteral("outline"),
                                                    QStringLiteral("contextual"), QStringLiteral("widow")}},
                                       {QStringLiteral("alignment"), QStringLiteral("center")}, {QStringLiteral("left"), 48},
                                       {QStringLiteral("special"), QStringLiteral("hanging")}, {QStringLiteral("by"), 24},
                                       {QStringLiteral("before"), 12}, {QStringLiteral("after"), 6},
                                       {QStringLiteral("lineRule"), QStringLiteral("exactly")}, {QStringLiteral("lineAt"), 18},
                                       {QStringLiteral("outline"), 1}, {QStringLiteral("contextual"), true},
                                       {QStringLiteral("widow"), false}}));
    const QTextBlockFormat f = blockWithText(scene.doc->textDocument(), QStringLiteral("Two")).blockFormat();
    QCOMPARE(f.alignment() & Qt::AlignHorizontal_Mask, Qt::AlignHCenter);
    QCOMPARE(f.leftMargin(), 48.0);
    QCOMPARE(f.textIndent(), -24.0);
    QCOMPARE(f.lineHeightType(), int(QTextBlockFormat::FixedHeight));
    QCOMPARE(Docx::outlineLevel(f), 1);
    QCOMPARE(Docx::ownSpacing(f).before, 240);
    const QByteArray xml = savedXml(scene);
    QVERIFY2(xml.contains("<w:widowControl w:val=\"0\"/>"), xml.constData());
    QVERIFY(xml.contains("<w:spacing w:before=\"240\" w:after=\"120\" w:line=\"360\" w:lineRule=\"exact\"/>"));
    QVERIFY(xml.contains("<w:ind w:left=\"720\" w:right=\"0\" w:hanging=\"360\"/><w:contextualSpacing/>"));
    QVERIFY(xml.contains("<w:jc w:val=\"center\"/><w:outlineLvl w:val=\"1\"/>"));
    // One Undo takes all of it back.
    scene.control()->undo();
    const QTextBlockFormat g = blockWithText(scene.doc->textDocument(), QStringLiteral("Two")).blockFormat();
    QCOMPARE(g.alignment() & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
    QCOMPARE(g.leftMargin(), 0.0);
    QVERIFY(!g.hasProperty(Docx::kOutlineLevel));
}

void WriteFormattingTest::keepWithNextDrivesPagination()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    scene.doc->keepRules()->setDelay(0);
    QTextCursor c(scene.doc->textDocument());
    c.insertText(QStringLiteral("Intro\nHeading\nBody"));
    // Body starts a page: the heading ends the one before it, alone.
    QTextCursor body(blockWithText(scene.doc->textDocument(), QStringLiteral("Body")));
    QTextBlockFormat breakBefore = body.blockFormat();
    breakBefore.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    body.setBlockFormat(breakBefore);
    QTRY_COMPARE(KeepRules::count(scene.doc->textDocument()), 0);
    selectBlocks(scene, QStringLiteral("Heading"), QStringLiteral("Heading"));
    QVERIFY(scene.host.paragraphController()->applyParagraph(
        {{QStringLiteral("changed"), QStringList{QStringLiteral("keepNext")}}, {QStringLiteral("keepNext"), true}}));
    // Word moves the heading to the page of what it keeps with.
    QTRY_COMPARE(KeepRules::count(scene.doc->textDocument()), 1);
    QVERIFY(blockWithText(scene.doc->textDocument(), QStringLiteral("Heading")).blockFormat().hasProperty(Docx::kSyntheticBreak));
    QVERIFY(savedXml(scene).contains("<w:keepNext/>"));
}

void WriteFormattingTest::tabStopsWithLeaders()
{
    for (const int width : {1280, 520}) {
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("Contents\nChapter one\t7\nChapter two\t19"));
        selectBlocks(scene, QStringLiteral("Chapter one\t7"), QStringLiteral("Chapter two\t19"));
        ParagraphController *paragraph = scene.host.paragraphController();
        const QVariantList stops{QVariantMap{{QStringLiteral("position"), 8640}, {QStringLiteral("alignment"), QStringLiteral("right")},
                                             {QStringLiteral("leader"), QStringLiteral("dot")}}};
        QVERIFY(paragraph->applyTabs({{QStringLiteral("stops"), stops}, {QStringLiteral("defaultStop"), 72}}));
        const QTextBlock one = blockWithText(scene.doc->textDocument(), QStringLiteral("Chapter one\t7"));
        QCOMPARE(one.blockFormat().tabPositions().size(), 1);
        QCOMPARE(one.blockFormat().tabPositions().first().type, QTextOption::RightTab);
        QCOMPARE(paragraph->tabsInfo().value(QStringLiteral("stops")).toList().size(), 1);
        // One step back takes both paragraphs' stops; Redo returns them.
        scene.control()->undo();
        QVERIFY(blockWithText(scene.doc->textDocument(), QStringLiteral("Chapter two\t19")).blockFormat().tabPositions().isEmpty());
        QVERIFY(one.blockFormat().tabPositions().isEmpty());
        scene.control()->redo();
        QCOMPARE(one.blockFormat().tabPositions().size(), 1);
        scene.control()->setTextCursor(QTextCursor(blockWithText(scene.doc->textDocument(), QStringLiteral("Contents"))));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m70_leaders_%1").arg(width));
        if (width != 1280) {
            continue;
        }
        const QByteArray xml = savedXml(scene);
        QVERIFY2(xml.contains("<w:tabs><w:tab w:val=\"right\" w:leader=\"dot\" w:pos=\"8640\"/></w:tabs>"), xml.constData());
        QVERIFY(savedXml(scene, QStringLiteral("word/settings.xml")).contains("<w:defaultTabStop w:val=\"1080\"/>"));
        if (const QString dir = qEnvironmentVariable("QQO_GRAB_DIR"); !dir.isEmpty()) {
            QDir().mkpath(dir);
            scene.host.exportPdfTo(QUrl::fromLocalFile(dir + QStringLiteral("/qindawrite_m70_leaders.pdf")));
        }
    }
}

void WriteFormattingTest::paragraphBordersAndShading()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("Before\nNote one\nNote two\nAfter"));
    selectBlocks(scene, QStringLiteral("Note one"), QStringLiteral("Note two"));
    BordersController *borders = scene.host.bordersController();
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBordersOutsideAction")));
    const QTextBlockFormat one = blockWithText(scene.doc->textDocument(), QStringLiteral("Note one")).blockFormat();
    QCOMPARE(Docx::paragraphBorders(one).size(), 4);
    QVERIFY(!one.stringProperty(Docx::kParagraphBorders).isEmpty());
    // The same box on both paragraphs: drawn as one, as Word draws it.
    QCOMPARE(one.stringProperty(Docx::kParagraphBorders),
             blockWithText(scene.doc->textDocument(), QStringLiteral("Note two")).blockFormat().stringProperty(Docx::kParagraphBorders));
    borders->applyShading(QColor(0xFF, 0xF2, 0xCC));
    QCOMPARE(blockWithText(scene.doc->textDocument(), QStringLiteral("Note two")).blockFormat().background().color(),
             QColor(0xFF, 0xF2, 0xCC));
    // One step back: the shading goes, the box stays; Redo returns it.
    scene.control()->undo();
    QVERIFY(!blockWithText(scene.doc->textDocument(), QStringLiteral("Note one")).blockFormat().hasProperty(QTextFormat::BackgroundBrush));
    QCOMPARE(Docx::paragraphBorders(blockWithText(scene.doc->textDocument(), QStringLiteral("Note one")).blockFormat()).size(), 4);
    scene.control()->redo();
    selectBlocks(scene, QStringLiteral("Note one"), QStringLiteral("Note two")); // Undo moved the caret
    // The dialog: a double red box instead.
    const QVariantMap info = borders->dialogInfo();
    QCOMPARE(info.value(QStringLiteral("context")).toString(), QStringLiteral("paragraph"));
    const QVariantMap red{{QStringLiteral("style"), QStringLiteral("double")}, {QStringLiteral("size"), 6},
                          {QStringLiteral("color"), QStringLiteral("C00000")}};
    const QString before = blockWithText(scene.doc->textDocument(), QStringLiteral("Note one")).blockFormat().stringProperty(Docx::kParagraphBorderSpec);
    QVERIFY(borders->applyDialog({{QStringLiteral("applyTo"), QStringLiteral("paragraph")},
                                  {QStringLiteral("edges"), QVariantMap{{QStringLiteral("top"), red}, {QStringLiteral("bottom"), red},
                                                                         {QStringLiteral("left"), red}, {QStringLiteral("right"), red}}},
                                  {QStringLiteral("fillChanged"), false}}));
    // The dialog's answer is one step too.
    scene.control()->undo();
    QCOMPARE(blockWithText(scene.doc->textDocument(), QStringLiteral("Note two")).blockFormat().stringProperty(Docx::kParagraphBorderSpec), before);
    scene.control()->redo();
    selectBlocks(scene, QStringLiteral("Note one"), QStringLiteral("Note two"));
    const QByteArray xml = savedXml(scene);
    QVERIFY2(xml.contains("<w:pBdr><w:top w:val=\"double\" w:sz=\"6\" w:space=\"0\" w:color=\"C00000\"/>"), xml.constData());
    QVERIFY(xml.contains("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"FFF2CC\"/>"));
    // No Border takes the box away again (nil sides).
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBordersNoneAction")));
    QVERIFY(blockWithText(scene.doc->textDocument(), QStringLiteral("Note one")).blockFormat().stringProperty(Docx::kParagraphBorders).isEmpty());
}

void WriteFormattingTest::fontDialogAndChangeCase()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    scene.selectFirstWord(); // "hello" of "hello world"
    FontController *font = scene.host.fontController();
    QCOMPARE(font->fontInfo().value(QStringLiteral("underline")).toString(), QStringLiteral("none"));
    QVERIFY(font->applyFont({{QStringLiteral("changed"),
                              QStringList{QStringLiteral("underline"), QStringLiteral("underlineColor"), QStringLiteral("doubleStrike"),
                                          QStringLiteral("strike"), QStringLiteral("smallCaps"), QStringLiteral("allCaps"),
                                          QStringLiteral("spacing"), QStringLiteral("scale"), QStringLiteral("position"),
                                          QStringLiteral("kerning"), QStringLiteral("kernAt"), QStringLiteral("hidden")}},
                             {QStringLiteral("underline"), QStringLiteral("double")}, {QStringLiteral("underlineColor"), QStringLiteral("#c00000")},
                             {QStringLiteral("doubleStrike"), true}, {QStringLiteral("strike"), false},
                             {QStringLiteral("smallCaps"), true}, {QStringLiteral("allCaps"), false},
                             {QStringLiteral("spacing"), 1.5}, {QStringLiteral("scale"), 120}, {QStringLiteral("position"), 3},
                             {QStringLiteral("kerning"), true}, {QStringLiteral("kernAt"), 12}, {QStringLiteral("hidden"), true}}));
    QTextCursor at(scene.doc->textDocument());
    at.setPosition(1);
    const QTextCharFormat f = at.charFormat();
    QCOMPARE(f.stringProperty(Docx::kUnderlineWord), QStringLiteral("double"));
    QCOMPARE(f.fontCapitalization(), QFont::SmallCaps);
    QVERIFY(f.boolProperty(Docx::kDoubleStrike));
    const QVariantMap info = font->fontInfo();
    QCOMPARE(info.value(QStringLiteral("underline")).toString(), QStringLiteral("double"));
    QCOMPARE(qRound(info.value(QStringLiteral("position")).toReal()), 3);
    const QByteArray xml = savedXml(scene);
    QVERIFY2(xml.contains("<w:smallCaps/><w:dstrike/><w:vanish/>"), xml.constData());
    QVERIFY(xml.contains("<w:spacing w:val=\"30\"/><w:w w:val=\"120\"/><w:kern w:val=\"24\"/><w:position w:val=\"6\"/>"));
    QVERIFY(xml.contains("<w:u w:val=\"double\" w:color=\"C00000\"/>"));
    scene.control()->undo(); // every effect in one step, the text kept
    at.setPosition(1);
    QVERIFY(!at.charFormat().hasProperty(Docx::kUnderlineWord));
    QCOMPARE(at.charFormat().fontCapitalization(), QFont::MixedCase);
    QVERIFY(!at.charFormat().boolProperty(Docx::kHiddenText));
    QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("hello world"));

    // Shift+F3: lowercase → UPPERCASE → Capitalize Each Word → Sentence case.
    QTextCursor all(scene.doc->textDocument());
    all.select(QTextCursor::Document);
    scene.control()->setTextCursor(all);
    ActionEntry *cycle = scene.host.action(QStringLiteral("writeChangeCaseAction"));
    QCOMPARE(cycle->shortcut(), QKeySequence(QStringLiteral("Shift+F3")));
    cycle->trigger();
    QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("HELLO WORLD"));
    cycle->trigger();
    QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("Hello World"));
    cycle->trigger();
    QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("Hello world"));
    cycle->trigger();
    QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("hello world"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeCaseToggleAction")));
    QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("HELLO WORLD"));
    scene.control()->undo();
    QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("hello world"));
}

void WriteFormattingTest::dialogsOpenAtBothWidths()
{
    for (const int width : {1280, 520}) {
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.selectFirstWord();
        for (const auto &[action, name] : {std::pair{"writeParagraphAction", "paragraphDialog"},
                                           std::pair{"writeTabsAction", "tabsDialog"},
                                           std::pair{"writeFontAction", "fontDialog"},
                                           std::pair{"writeBordersShadingAction", "bordersShadingDialog"}}) {
            QObject *dialog = openDialog(scene, action, name);
            QVERIFY2(dialog != nullptr, name);
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m70_%1_%2").arg(QLatin1String(name)).arg(width));
            QMetaObject::invokeMethod(dialog, "close");
            QTRY_VERIFY(!dialog->property("visible").toBool());
        }
    }
    // The page's context menu offers them, as Word's does.
    WriteHost host;
    const QString menu = QString::fromUtf8(QJsonDocument::fromVariant(host.contextMenuNode()).toJson());
    QVERIFY(menu.contains(QStringLiteral("writeFontAction")));
    QVERIFY(menu.contains(QStringLiteral("writeParagraphAction")));
}

WRITE_TEST_MAIN(WriteFormattingTest)

#include "tst_write_formatting.moc"
