// SPDX-License-Identifier: GPL-3.0-or-later
// The assistant's edits in QindaShow (D-058): the snapshot shows every
// slide's text boxes as [slide.box] and its notes; rewriting boxes, adding,
// moving and deleting slides and writing notes is ONE undo step.

#include "show_test_support.h"

#include "assistant_controller.h"
#include "presentation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUndoStack>

using namespace ShowTest;

class ShowToolsTest : public QObject {
    Q_OBJECT
private slots:
    void snapshotEditAndUndo();
};

void ShowToolsTest::snapshotEditAndUndo()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    auto *deck = scene.doc->presentation();
    const qsizetype slides = deck->slides.size();
    QVERIFY(slides >= 1);
    auto *tools = scene.host.assistant()->documentTools();
    QVERIFY(tools != nullptr);
    const QString snapshot = tools->snapshot(60000);
    QVERIFY2(snapshot.contains(QStringLiteral("Slide 1 (layout ")) && snapshot.contains(QStringLiteral("[1.1]")),
             qPrintable(snapshot));

    const QJsonArray edits = QJsonDocument::fromJson(R"([
        {"op":"set_text","slide":1,"box":1,"text":"Quarterly review"},
        {"op":"add_slide","after":1,"layout":"title+content","title":"Results",
         "body":"- Revenue up\n- Costs flat","notes":"Pause here."},
        {"op":"set_notes","slide":1,"text":"Welcome everyone."},
        {"op":"set_text","slide":9,"box":1,"text":"nope"}])").array();
    const int before = scene.doc->undoStack()->count();
    const auto outcome = tools->apply(edits);
    QCOMPARE(outcome.applied, 3);
    QCOMPARE(outcome.errors.size(), 1);
    QCOMPARE(scene.doc->undoStack()->count(), before + 1);
    QCOMPARE(deck->slides.size(), slides + 1);
    QCOMPARE(deck->slides.at(0).shapes.first().paragraphs.first().runs.first().text,
             QStringLiteral("Quarterly review"));
    QCOMPARE(deck->slides.at(0).notes, QStringLiteral("Welcome everyone."));
    const auto &added = deck->slides.at(1);
    QCOMPARE(added.notes, QStringLiteral("Pause here."));
    QCOMPARE(added.shapes.at(1).paragraphs.size(), 2);
    QVERIFY(added.shapes.at(1).paragraphs.first().bullet);

    tools->undo();
    QCOMPARE(deck->slides.size(), slides);
    QVERIFY(deck->slides.at(0).notes != QStringLiteral("Welcome everyone."));
}

SHOW_TEST_MAIN(ShowToolsTest)

#include "tst_show_tools.moc"
