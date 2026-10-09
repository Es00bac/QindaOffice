// SPDX-License-Identifier: GPL-3.0-or-later
// Endnotes in QindaWrite through the real window (M77, D-086): Insert
// Endnote (Alt+Ctrl+D) asks for the note, puts a mark numbered i, ii, iii
// at the caret and lays the notes out after the body's last line (more
// pages when they need them); editing a note's text and removing it are
// undo steps like the insertion; the Footnote and Endnote dialog's format,
// start and numbering renumber the marks in one step; Convert turns
// footnotes into endnotes and back; the saved .docx carries endnotes.xml
// and reads back. QQO_GRAB_DIR keeps a grab of a page ending in endnotes.

#include "write_test_support.h"

#include "docx_properties.h"
#include "qindawrite/src/write_endnotes.h"
#include "qindawrite/src/write_notes.h"
#include "support/quick_dump.h"

#include "ooxml_package.h"

#include <QTextBlock>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
namespace Docx = QindaOffice::Docx;

namespace {

QStringList marks(Scene &scene, int property)
{
    QStringList out;
    for (QTextBlock b = scene.doc->textDocument()->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().hasProperty(property)) {
                out.append(it.fragment().text());
            }
        }
    }
    return out;
}

void caretAtEnd(Scene &scene)
{
    QTextCursor c(scene.doc->textDocument());
    c.movePosition(QTextCursor::End);
    scene.control()->setTextCursor(c);
}

} // namespace

class WriteEndnotesTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected"))); }
    void insertEditAndUndo();
    void optionsAndConvert();
    void savedAndReopened();
    void endnotesAtTheEnd();
};

void WriteEndnotesTest::insertEditAndUndo()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("Babbage designed the engine."));
    caretAtEnd(scene);
    NotesController *notes = scene.host.notesController();
    QSignalSpy asked(notes, &NotesController::noteRequested);
    QCOMPARE(scene.host.action(QStringLiteral("writeInsertEndnoteAction"))->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+D")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeInsertEndnoteAction")));
    QCOMPARE(asked.size(), 1);
    QCOMPARE(asked.first().at(0).toString(), QStringLiteral("endnote"));
    notes->commitNote(QStringLiteral("endnote"), QString(), QStringLiteral("Charles Babbage, 1837."));
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i")}));
    QCOMPARE(scene.doc->endnotes().size(), 1);
    // Typing after the mark is text, not more of the mark.
    scene.control()->insertText(QStringLiteral(" He died in 1871."));
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i")}));
    QVERIFY(scene.doc->textDocument()->toPlainText().endsWith(QStringLiteral("engine.i He died in 1871.")));
    scene.control()->undo(); // the typing alone
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i")}));
    QVERIFY(scene.doc->textDocument()->toPlainText().endsWith(QStringLiteral("engine.i")));
    QVERIFY(scene.doc->endnoteArea() != nullptr);
    // A second note before the first renumbers both.
    QTextCursor c(scene.doc->textDocument());
    c.setPosition(7); // after "Babbage"
    scene.control()->setTextCursor(c);
    notes->commitNote(QStringLiteral("endnote"), QString(), QStringLiteral("Born 1791."));
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i"), QStringLiteral("ii")}));
    const QString firstId = scene.doc->endnotes().key(QStringLiteral("Born 1791."));
    // Editing a note is one undo step; so is removing it.
    QVERIFY(notes->editEndnoteAt(7));
    notes->commitNote(QStringLiteral("endnote"), firstId, QStringLiteral("Born 26 December 1791."));
    QCOMPARE(scene.doc->endnoteText(firstId), QStringLiteral("Born 26 December 1791."));
    scene.control()->undo();
    QCOMPARE(scene.doc->endnoteText(firstId), QStringLiteral("Born 1791."));
    scene.control()->redo();
    QCOMPARE(scene.doc->endnoteText(firstId), QStringLiteral("Born 26 December 1791."));
    notes->commitNote(QStringLiteral("endnote"), firstId, QString());
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i")}));
    QCOMPARE(scene.doc->endnotes().size(), 1);
    scene.control()->undo();
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i"), QStringLiteral("ii")}));
    QCOMPARE(scene.doc->endnotes().size(), 2);
    // Undo the second insertion: its mark and its note go together.
    scene.control()->undo();
    scene.control()->undo();
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i")}));
    QCOMPARE(scene.doc->endnotes().size(), 1);
    // Footnotes now go through the same undo steps.
    caretAtEnd(scene);
    scene.host.commitFootnote(QString(), QStringLiteral("A footnote."));
    QCOMPARE(scene.doc->footnotes().size(), 1);
    scene.control()->undo();
    QVERIFY(scene.doc->footnotes().isEmpty());
}

void WriteEndnotesTest::optionsAndConvert()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    NotesController *notes = scene.host.notesController();
    QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("One two three."));
    for (const int at : {3, 7}) {
        QTextCursor c(scene.doc->textDocument());
        c.setPosition(at);
        scene.control()->setTextCursor(c);
        notes->commitNote(QStringLiteral("endnote"), QString(), QStringLiteral("Note at %1").arg(at));
    }
    caretAtEnd(scene);
    scene.host.commitFootnote(QString(), QStringLiteral("A footnote."));
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i"), QStringLiteral("ii")}));
    const QVariantMap info = notes->noteOptions();
    QCOMPARE(info.value(QStringLiteral("endnote")).toMap().value(QStringLiteral("format")).toString(), QStringLiteral("lowerRoman"));
    QVERIFY(notes->applyNoteOptions({{QStringLiteral("kind"), QStringLiteral("endnote")}, {QStringLiteral("format"), QStringLiteral("upperLetter")},
                                     {QStringLiteral("start"), 3}, {QStringLiteral("restart"), QStringLiteral("continuous")},
                                     {QStringLiteral("position"), QStringLiteral("docEnd")}}));
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("C"), QStringLiteral("D")}));
    scene.control()->undo(); // the options and the numbers together
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i"), QStringLiteral("ii")}));
    QCOMPARE(Docx::noteFormat(scene.doc->noteOptions(true), true), QStringLiteral("lowerRoman"));
    // Convert all footnotes to endnotes: three endnotes, in text order.
    QVERIFY(notes->convertNotes(QStringLiteral("toEndnotes")));
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i"), QStringLiteral("ii"), QStringLiteral("iii")}));
    QVERIFY(marks(scene, Docx::kFootnoteIdProperty).isEmpty());
    QCOMPARE(scene.doc->endnotes().size(), 3);
    QVERIFY(scene.doc->footnotes().isEmpty());
    scene.control()->undo();
    QCOMPARE(marks(scene, Docx::kFootnoteIdProperty), QStringList({QStringLiteral("1")}));
    QCOMPARE(scene.doc->footnotes().size(), 1);
    QVERIFY(notes->convertNotes(QStringLiteral("swap")));
    QCOMPARE(marks(scene, Docx::kFootnoteIdProperty), QStringList({QStringLiteral("1"), QStringLiteral("2")}));
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i")}));
    QVERIFY(notes->convertNotes(QStringLiteral("toFootnotes")));
    QVERIFY(scene.doc->endnotes().isEmpty());
}

void WriteEndnotesTest::savedAndReopened()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("notes.docx"));
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("A claim."));
        caretAtEnd(scene);
        scene.host.notesController()->commitNote(QStringLiteral("endnote"), QString(), QStringLiteral("Its source."));
        QString error;
        QVERIFY2(scene.doc->save(path, &error), qPrintable(error));
        const auto package = QindaOffice::Ooxml::Package::load(path);
        QVERIFY(package->data(QStringLiteral("word/endnotes.xml")).contains("Its source."));
        QVERIFY(package->data(QStringLiteral("word/document.xml")).contains("<w:endnoteReference w:id=\"1\"/>"));
    }
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(scene.host.loadPath(path));
    QCOMPARE(marks(scene, Docx::kEndnoteIdProperty), QStringList({QStringLiteral("i")}));
    QCOMPARE(scene.doc->endnotes().values(), QStringList({QStringLiteral("Its source.")}));
}

void WriteEndnotesTest::endnotesAtTheEnd()
{
    for (const int width : {1280, 520}) {
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("The analytical engine weaves algebraic patterns."));
        const int pages = scene.page->pageCount();
        for (int i = 0; i < 30; ++i) {
            caretAtEnd(scene);
            scene.control()->insertText(QStringLiteral(" Step %1 of the computation.").arg(i + 1)); // typed
            caretAtEnd(scene);
            scene.host.notesController()->commitNote(
                QStringLiteral("endnote"), QString(),
                QStringLiteral("Note %1. Menabrea's account of the engine, translated and annotated in 1843, follows the "
                               "cards through each operation of the computation; the annotations are longer than the "
                               "account itself and end with a table of the steps.")
                    .arg(i + 1));
        }
        const auto area = scene.doc->endnoteArea();
        QVERIFY(area != nullptr);
        QCOMPARE(area->firstPage, 0);
        QVERIFY(area->pages >= 2); // thirty three-line notes run onto another page
        QTRY_COMPARE(scene.page->pageCount(), pages + area->pages - 1);
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m77_endnotes_%1").arg(width));
    }
}

WRITE_TEST_MAIN(WriteEndnotesTest)

#include "tst_write_endnotes.moc"
