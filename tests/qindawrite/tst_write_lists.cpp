// SPDX-License-Identifier: GPL-3.0-or-later
// Word's lists in QindaWrite through the real window (M73, D-082): every
// library pick and dialog answer is one undo step; Tab / Shift+Tab at an
// item's start change its level, Enter on an empty item ends the list and
// Backspace at an item's start removes its number; Restart at 1, Continue
// Numbering and Set Numbering Value renumber as Word does; the saved .docx
// carries real w:abstractNum / w:num definitions. QQO_GRAB_DIR keeps grabs
// of a multilevel list and of each list dialog at 1280 and 520 px.

#include "write_test_support.h"

#include "docx_list_edit.h"
#include "docx_list_labels.h"
#include "docx_properties.h"
#include "qindawrite/src/write_lists.h"
#include "qindawrite/src/write_styles.h"
#include "support/quick_dump.h"

#include "ooxml_package.h"

#include <QTextBlock>
#include <QTextList>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
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

void caretAt(Scene &scene, const QString &text, bool end = false)
{
    const QTextBlock block = blockWithText(scene.doc->textDocument(), text);
    QTextCursor c(block);
    if (end) {
        c.setPosition(block.position() + block.length() - 1);
    }
    scene.control()->setTextCursor(c);
}

// "text=marker" for every list paragraph, in order.
QStringList markers(Scene &scene)
{
    QStringList out;
    const QTextDocument *doc = scene.doc->textDocument();
    const QHash<int, Docx::ListMarker> all = Docx::listMarkers(*doc, true);
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (all.contains(b.blockNumber())) {
            out.append(b.text() + QLatin1Char('=') + all.value(b.blockNumber()).text);
        }
    }
    return out;
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

void type(Scene &scene, const QString &lines)
{
    QTextCursor(scene.doc->textDocument()).insertText(lines);
}

} // namespace

class WriteListsTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected"))); }
    void librariesApplyInOneStep();
    void keysChangeLevelsAndEndTheList();
    void restartContinueAndSetValue();
    void dialogsApplyInOneStep();
    void numberFormatKeepsTheParentsNumbers();
    void dialogsAtBothWidths();
};

void WriteListsTest::librariesApplyInOneStep()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    type(scene, QStringLiteral("Plan\nBuy\nCook\nEat"));
    selectBlocks(scene, QStringLiteral("Buy"), QStringLiteral("Eat"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBullet4")));
    QCOMPARE(markers(scene), (QStringList{QStringLiteral("Buy=➢"), QStringLiteral("Cook=➢"), QStringLiteral("Eat=➢")}));
    QVERIFY(scene.host.actions()->entry(QStringLiteral("formatListBulletAction"))->isChecked());
    // Word's marker, not Qt's: the paragraph switches Qt's off.
    QCOMPARE(blockWithText(scene.doc->textDocument(), QStringLiteral("Buy")).blockFormat().intProperty(QTextFormat::ListStyle), 0);
    // Another bullet from the library changes the list's bullet.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBullet1")));
    QCOMPARE(markers(scene).first(), QStringLiteral("Buy=o"));
    // A numbering pick on a bulleted list makes the paragraphs numbered.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeNumbering2")));
    QCOMPARE(markers(scene), (QStringList{QStringLiteral("Buy=I."), QStringLiteral("Cook=II."), QStringLiteral("Eat=III.")}));
    QVERIFY(scene.host.actions()->entry(QStringLiteral("formatListNumberedAction"))->isChecked());
    // One step back each time.
    scene.control()->undo();
    QCOMPARE(markers(scene).first(), QStringLiteral("Buy=o"));
    scene.control()->undo();
    QCOMPARE(markers(scene).first(), QStringLiteral("Buy=➢"));
    scene.control()->undo();
    QVERIFY(markers(scene).isEmpty());
    scene.control()->redo();
    QCOMPARE(markers(scene).size(), 3);
    // Multilevel: the list takes Word's 1. 1.1. 1.1.1.
    selectBlocks(scene, QStringLiteral("Buy"), QStringLiteral("Eat"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMultilevel0")));
    QCOMPARE(markers(scene), (QStringList{QStringLiteral("Buy=1."), QStringLiteral("Cook=2."), QStringLiteral("Eat=3.")}));
    // The Numbering button turns it off, in one step.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatListNumberedAction")));
    QVERIFY(markers(scene).isEmpty());
    QCOMPARE(blockWithText(scene.doc->textDocument(), QStringLiteral("Cook")).blockFormat().leftMargin(), 0.0);
    scene.control()->undo();
    QCOMPARE(markers(scene).size(), 3);
    const QByteArray numbering = savedPart(scene, QStringLiteral("word/numbering.xml"));
    QVERIFY2(numbering.contains("<w:lvlText w:val=\"%1.%2.\"/>"), numbering.constData());
    QVERIFY(numbering.contains("<w:multiLevelType w:val=\"multilevel\"/>"));
    // A paragraph style on a list item: it stays an item, Word's number kept.
    caretAt(scene, QStringLiteral("Cook"));
    QVERIFY(scene.host.stylesController()->apply(QStringLiteral("Heading2")));
    const QTextBlock cook = blockWithText(scene.doc->textDocument(), QStringLiteral("Cook"));
    QVERIFY(cook.textList() != nullptr);
    QCOMPARE(cook.blockFormat().intProperty(QTextFormat::ListStyle), 0);
    QCOMPARE(markers(scene).at(1), QStringLiteral("Cook=2."));
}

void WriteListsTest::keysChangeLevelsAndEndTheList()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    type(scene, QStringLiteral("One\nTwo\nThree"));
    selectBlocks(scene, QStringLiteral("One"), QStringLiteral("Three"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMultilevel0")));
    // Tab at an item's start: the next level.
    caretAt(scene, QStringLiteral("Two"));
    QTest::keyClick(scene.window, Qt::Key_Tab);
    QCOMPARE(markers(scene), (QStringList{QStringLiteral("One=1."), QStringLiteral("Two=1.1."), QStringLiteral("Three=2.")}));
    QCOMPARE(blockWithText(scene.doc->textDocument(), QStringLiteral("Two")).text(), QStringLiteral("Two")); // no tab typed
    QVERIFY(scene.host.actions()->entry(QStringLiteral("writeListLevel2"))->isChecked());
    // Shift+Tab: back up.
    QTest::keyClick(scene.window, Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(markers(scene).at(1), QStringLiteral("Two=2."));
    scene.control()->undo(); // one step back: level 2 again
    QCOMPARE(markers(scene).at(1), QStringLiteral("Two=1.1."));
    // Tab in the middle of an item types a tab.
    QTextCursor mid(blockWithText(scene.doc->textDocument(), QStringLiteral("Three")));
    mid.setPosition(mid.position() + 2);
    scene.control()->setTextCursor(mid);
    QTest::keyClick(scene.window, Qt::Key_Tab);
    QVERIFY(blockWithText(scene.doc->textDocument(), QStringLiteral("Th\tree")).isValid());
    // Enter at an item's end: a new item; Enter on it again ends the list.
    caretAt(scene, QStringLiteral("Th\tree"), true);
    QTest::keyClick(scene.window, Qt::Key_Return);
    QCOMPARE(markers(scene).size(), 4);
    QCOMPARE(markers(scene).last(), QStringLiteral("=3."));
    QTest::keyClick(scene.window, Qt::Key_Return);
    QCOMPARE(markers(scene).size(), 3);
    QVERIFY(scene.control()->textCursor().block().textList() == nullptr);
    QVERIFY(scene.control()->textCursor().block().text().isEmpty());
    // Backspace at an item's start: the number goes, the text stays put.
    caretAt(scene, QStringLiteral("One"));
    const qreal left = blockWithText(scene.doc->textDocument(), QStringLiteral("One")).blockFormat().leftMargin();
    QTest::keyClick(scene.window, Qt::Key_Backspace);
    const QTextBlock one = blockWithText(scene.doc->textDocument(), QStringLiteral("One"));
    QVERIFY(one.textList() == nullptr);
    QCOMPARE(one.blockFormat().leftMargin(), left);
    // Word: a second-level item with no first-level item before it shows 0.
    QCOMPARE(markers(scene).first(), QStringLiteral("Two=0.1."));
}

void WriteListsTest::restartContinueAndSetValue()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    type(scene, QStringLiteral("A\nB\nC\nD"));
    selectBlocks(scene, QStringLiteral("A"), QStringLiteral("D"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeNumbering0")));
    caretAt(scene, QStringLiteral("C"));
    QVERIFY(!scene.host.actions()->entry(QStringLiteral("writeContinueNumberingAction"))->isEnabled());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeRestartNumberingAction")));
    QCOMPARE(markers(scene), (QStringList{QStringLiteral("A=1."), QStringLiteral("B=2."), QStringLiteral("C=1."), QStringLiteral("D=2.")}));
    QVERIFY(scene.host.actions()->entry(QStringLiteral("writeContinueNumberingAction"))->isEnabled());
    const QByteArray numbering = savedPart(scene, QStringLiteral("word/numbering.xml"));
    QVERIFY2(numbering.contains("<w:lvlOverride w:ilvl=\"0\"><w:startOverride w:val=\"1\"/></w:lvlOverride>"), numbering.constData());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeContinueNumberingAction")));
    QCOMPARE(markers(scene).at(2), QStringLiteral("C=3."));
    ListController *lists = scene.host.listController();
    QCOMPARE(lists->numberingValueInfo().value(QStringLiteral("value")).toInt(), 3);
    QVERIFY(lists->applyNumberingValue({{QStringLiteral("mode"), QStringLiteral("new")}, {QStringLiteral("value"), 7}}));
    QCOMPARE(markers(scene).mid(2), (QStringList{QStringLiteral("C=7."), QStringLiteral("D=8.")}));
    scene.control()->undo();
    QCOMPARE(markers(scene).at(2), QStringLiteral("C=3."));
}

void WriteListsTest::dialogsApplyInOneStep()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    type(scene, QStringLiteral("First\nSecond\nThird"));
    selectBlocks(scene, QStringLiteral("First"), QStringLiteral("Third"));
    ListController *lists = scene.host.listController();
    // Define New Bullet: any character, its colour.
    QVERIFY(lists->applyDefineBullet({{QStringLiteral("glyph"), QStringLiteral("★")}, {QStringLiteral("font"), QString()},
                                      {QStringLiteral("color"), QStringLiteral("#c00000")}, {QStringLiteral("justification"), QStringLiteral("left")}}));
    QCOMPARE(markers(scene).first(), QStringLiteral("First=★"));
    QCOMPARE(Docx::listMarker(blockWithText(scene.doc->textDocument(), QStringLiteral("First"))).format.foreground().color(),
             QColor(0xC0, 0x00, 0x00));
    scene.control()->undo();
    QVERIFY(markers(scene).isEmpty());
    // Define New Number Format: "Step 3:".
    selectBlocks(scene, QStringLiteral("First"), QStringLiteral("Third"));
    QVERIFY(lists->applyNumberFormat({{QStringLiteral("format"), QStringLiteral("decimal")}, {QStringLiteral("text"), QStringLiteral("Step %1:")},
                                      {QStringLiteral("start"), 3}, {QStringLiteral("justification"), QStringLiteral("left")}}));
    QCOMPARE(markers(scene), (QStringList{QStringLiteral("First=Step 3:"), QStringLiteral("Second=Step 4:"), QStringLiteral("Third=Step 5:")}));
    QCOMPARE(lists->numberFormatInfo().value(QStringLiteral("text")).toString(), QStringLiteral("Step %1:"));
    // Define New Multilevel List on that list: level 1 "Part I", level 2 "(a)".
    QVariantMap info = lists->multilevelInfo();
    QVariantList levels = info.value(QStringLiteral("levels")).toList();
    QVariantMap one = levels.at(0).toMap();
    one.insert(QStringLiteral("format"), QStringLiteral("upperRoman"));
    one.insert(QStringLiteral("text"), QStringLiteral("Part %1"));
    one.insert(QStringLiteral("start"), 1);
    levels[0] = one;
    QVariantMap two = levels.at(1).toMap();
    two.insert(QStringLiteral("text"), QStringLiteral("(%2)"));
    levels[1] = two;
    QVERIFY(lists->applyMultilevelDefinition({{QStringLiteral("levels"), levels}}));
    caretAt(scene, QStringLiteral("Second"));
    QVERIFY(lists->setLevel(1));
    QCOMPARE(markers(scene), (QStringList{QStringLiteral("First=Part I"), QStringLiteral("Second=(a)"), QStringLiteral("Third=Part II")}));
    // Adjust List Indents: number at 0, text at 0.75 in, a space after it.
    caretAt(scene, QStringLiteral("First"));
    QVERIFY(lists->applyIndents({{QStringLiteral("numberPosition"), 0.0}, {QStringLiteral("textIndent"), 72.0},
                                 {QStringLiteral("suffix"), QStringLiteral("space")}, {QStringLiteral("tab"), -1}}));
    const QTextBlock first = blockWithText(scene.doc->textDocument(), QStringLiteral("First"));
    QCOMPARE(first.blockFormat().leftMargin(), 72.0);
    QCOMPARE(first.blockFormat().doubleProperty(Docx::kListHanging), 72.0);
    QVERIFY(first.blockFormat().textIndent() < 0); // the text follows "Part I " on the first line
    scene.control()->undo();
    QVERIFY(blockWithText(scene.doc->textDocument(), QStringLiteral("First")).blockFormat().leftMargin() < 72.0);
    const QByteArray numbering = savedPart(scene, QStringLiteral("word/numbering.xml"));
    QVERIFY2(numbering.contains("<w:numFmt w:val=\"upperRoman\"/><w:lvlText w:val=\"Part %1\"/>"), numbering.constData());
}

// Define New Number Format on "1.1.": Word's own %1 … %9 (one "%1" cannot
// name both levels), the preview from the list's own parents, one step.
void WriteListsTest::numberFormatKeepsTheParentsNumbers()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    type(scene, QStringLiteral("One\nTwo\nThree"));
    selectBlocks(scene, QStringLiteral("One"), QStringLiteral("Three"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMultilevel0")));
    caretAt(scene, QStringLiteral("Two"));
    ListController *lists = scene.host.listController();
    QVERIFY(lists->setLevel(1));
    QVariantMap info = lists->numberFormatInfo();
    QCOMPARE(info.value(QStringLiteral("text")).toString(), QStringLiteral("%1.%2."));
    QVERIFY(info.value(QStringLiteral("levelText")).toBool());
    QCOMPARE(info.value(QStringLiteral("level")).toInt(), 1);
    const QVariantList parents = lists->multilevelInfo().value(QStringLiteral("levels")).toList().mid(0, 1);
    const QVariantMap spec{{QStringLiteral("format"), QStringLiteral("lowerLetter")}, {QStringLiteral("text"), QStringLiteral("%1.%2)")},
                           {QStringLiteral("start"), 1}};
    QCOMPARE(lists->previewLabel(parents + QVariantList{spec}, 1, 2), QStringLiteral("1.b)"));
    QVERIFY(lists->applyNumberFormat({{QStringLiteral("format"), QStringLiteral("lowerLetter")}, {QStringLiteral("text"), QStringLiteral("%1.%2)")},
                                      {QStringLiteral("start"), 1}, {QStringLiteral("justification"), QStringLiteral("left")},
                                      {QStringLiteral("levelText"), true}}));
    QCOMPARE(markers(scene), (QStringList{QStringLiteral("One=1."), QStringLiteral("Two=1.a)"), QStringLiteral("Three=2.")}));
    const QByteArray numbering = savedPart(scene, QStringLiteral("word/numbering.xml"));
    QVERIFY2(numbering.contains("<w:numFmt w:val=\"lowerLetter\"/><w:lvlText w:val=\"%1.%2)\"/>"), numbering.constData());
    scene.control()->undo();
    QCOMPARE(markers(scene).at(1), QStringLiteral("Two=1.1."));
    // A level showing only its own number still reads "%1." there.
    selectBlocks(scene, QStringLiteral("One"), QStringLiteral("Three"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("formatListNumberedAction"))); // off
    QVERIFY(markers(scene).isEmpty());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeNumbering0")));
    caretAt(scene, QStringLiteral("Two"));
    QVERIFY(lists->setLevel(1));
    info = lists->numberFormatInfo();
    QCOMPARE(info.value(QStringLiteral("text")).toString(), QStringLiteral("%1."));
    QVERIFY(!info.value(QStringLiteral("levelText")).toBool());
}

void WriteListsTest::dialogsAtBothWidths()
{
    for (const int width : {1280, 520}) {
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        type(scene, QStringLiteral("Project plan\nScope\nGoals\nMeasures\nBudget\nStaffing\nRisks\nChecklist"));
        selectBlocks(scene, QStringLiteral("Scope"), QStringLiteral("Risks"));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMultilevel0")));
        for (const char *text : {"Goals", "Measures"}) {
            caretAt(scene, QString::fromLatin1(text));
            QTest::keyClick(scene.window, Qt::Key_Tab);
        }
        caretAt(scene, QStringLiteral("Measures"));
        QTest::keyClick(scene.window, Qt::Key_Tab);
        caretAt(scene, QStringLiteral("Checklist"));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBullet5")));
        QCOMPARE(markers(scene).mid(0, 4), (QStringList{QStringLiteral("Scope=1."), QStringLiteral("Goals=1.1."),
                                                       QStringLiteral("Measures=1.1.1."), QStringLiteral("Budget=2.")}));
        caretAt(scene, QStringLiteral("Project plan"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m73_multilevel_%1").arg(width));
        caretAt(scene, QStringLiteral("Goals"));
        for (const auto &[action, name] : {std::pair{"writeDefineBulletAction", "defineBulletDialog"},
                                           std::pair{"writeDefineNumberAction", "defineNumberDialog"},
                                           std::pair{"writeDefineMultilevelAction", "defineMultilevelDialog"},
                                           std::pair{"writeSetNumberingValueAction", "numberingValueDialog"},
                                           std::pair{"writeAdjustListIndentsAction", "listIndentsDialog"}}) {
            QObject *dialog = openDialog(scene, action, name);
            QVERIFY2(dialog != nullptr, name);
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m73_%1_%2").arg(QLatin1String(name)).arg(width));
            QMetaObject::invokeMethod(dialog, "close");
            QTRY_VERIFY(!dialog->property("visible").toBool());
        }
    }
}

WRITE_TEST_MAIN(WriteListsTest)

#include "tst_write_lists.moc"
