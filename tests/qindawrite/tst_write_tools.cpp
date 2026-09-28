// SPDX-License-Identifier: GPL-3.0-or-later
// The assistant's edits in QindaWrite (D-058): the snapshot is the
// document as Markdown with the selection, and every operation of one
// reply lands in ONE undo step — whole-document and section rewrites,
// find/replace, inserts — with refusals for what is not there.

#include "write_test_support.h"

#include "assistant_controller.h"
#include "qindawrite/src/write_document.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTextDocument>

using namespace WriteTest;

namespace {

QJsonArray edits(const char *json)
{
    return QJsonDocument::fromJson(json).array();
}

} // namespace

class WriteToolsTest : public QObject {
    Q_OBJECT
private slots:
    void snapshotEditAndUndo();
};

void WriteToolsTest::snapshotEditAndUndo()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTextDocument *text = scene.doc->textDocument();
    text->setMarkdown(QStringLiteral("# Report\n\nIntro teh text.\n\n## Results\n\nOld results.\n\n"
                                     "## Next\n\nKeep this."));
    text->clearUndoRedoStacks();
    auto *tools = scene.host.assistant()->documentTools();
    QVERIFY(tools != nullptr);
    QVERIFY(scene.host.assistant()->canEdit());
    const QString snapshot = tools->snapshot(60000);
    QVERIFY(snapshot.contains(QStringLiteral("## Results")));
    QVERIFY(snapshot.contains(QStringLiteral("cursor is in the paragraph")));

    const auto outcome = tools->apply(edits(R"([
        {"op":"replace_text","find":"teh","replace":"the"},
        {"op":"replace_section","heading":"Results","markdown":"## Results\n\n- **Up** 12%\n- Down 3%"},
        {"op":"insert","markdown":"## Appendix\n\nMore.","at":"end"},
        {"op":"replace_section","heading":"Nowhere","markdown":"x"},
        {"op":"replace_selection","markdown":"x"}])"));
    QCOMPARE(outcome.applied, 3);
    QCOMPARE(outcome.errors.size(), 2); // no such heading; nothing selected
    const QString after = text->toMarkdown();
    QVERIFY2(after.contains(QStringLiteral("Intro the text.")), qPrintable(after));
    QVERIFY(after.contains(QStringLiteral("**Up** 12%")));
    QVERIFY2(after.contains(QStringLiteral("## Results")), qPrintable(after)); // still a heading
    QVERIFY(!after.contains(QStringLiteral("Old results")));
    QVERIFY(after.contains(QStringLiteral("Keep this.")));   // the next section survived
    QVERIFY(after.contains(QStringLiteral("## Appendix")));
    QVERIFY(scene.doc->isDirty());

    // One undo step for all three.
    tools->undo();
    const QString undone = text->toMarkdown();
    QVERIFY2(undone.contains(QStringLiteral("Intro teh text.")) && undone.contains(QStringLiteral("Old results"))
                 && !undone.contains(QStringLiteral("Appendix")),
             qPrintable(undone));

    const auto whole = tools->apply(edits(R"([{"op":"replace_document","markdown":"# New\n\nOnly this."}])"));
    QCOMPARE(whole.applied, 1);
    QCOMPARE(text->toPlainText().trimmed(), QStringLiteral("New\nOnly this."));
}

WRITE_TEST_MAIN(WriteToolsTest)

#include "tst_write_tools.moc"
