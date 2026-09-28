// SPDX-License-Identifier: GPL-3.0-or-later
// QindaNote M56 (D-061) in the real window: the "this matters" hotkey, speaker
// names and the usage meter during a session; the recording kept beside
// the notebook; the Session Review panel's data (sections with times,
// unclear spots) and its commands (fix a spot, split by topic, export
// Markdown and Anki, a study page from the model); prep material and series
// memory reaching the note-taker; silence auto-pause; the assistant's
// snapshot carrying the transcript. The model is a local SSE server.

#include "note_test_support.h"
#include "support/quick_dump.h"

#include "office_ai/mock_sse_server.h"
#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_live_session.h"

#include "provider_settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPdfWriter>
#include <QPainter>

using namespace NoteTest;
using QindaOffice::Ai::MockSseServer;
using QindaOffice::Apps::Notes::LiveNotesSession;

namespace {

const QString kNotes = QStringLiteral(
    "# Cells\n\n## Summary\n- energy\n\n## Mitochondria [00:01]\n- make ATP *(unclear at 00:02: cristae?)*\n"
    "- [[ai: ATP is the cell's energy currency]]\n\n## Membranes [01:30]\n- lipid bilayer\n\n"
    "## Action items\n- [ ] read ch. 4");

QByteArray sseFor(const QString &text)
{
    const QJsonObject chunk{{QStringLiteral("choices"),
                             QJsonArray{QJsonObject{{QStringLiteral("delta"),
                                                     QJsonObject{{QStringLiteral("content"), text}}}}}}};
    return "data: " + QJsonDocument(chunk).toJson(QJsonDocument::Compact) + "\n\ndata: [DONE]\n\n";
}

QString addMockModel(NoteHost &host, quint16 port)
{
    const QString id = host.providerSettings()->addCustom(
        {{QStringLiteral("id"), QStringLiteral("mock")},
         {QStringLiteral("baseUrl"), QStringLiteral("http://127.0.0.1:%1/v1").arg(port)},
         {QStringLiteral("model"), QStringLiteral("m")}});
    return id + QStringLiteral("/m");
}

} // namespace

class NoteSessionTest : public QObject {
    Q_OBJECT
private slots:
    void duringASession();
    void reviewAndPageCommands();
    void prepMaterialAndSeriesMemory();
    void movingAPageKeepsTheViewOnIt();
};

void NoteSessionTest::duringASession()
{
    MockSseServer model;
    model.setScript({{sseFor(kNotes), 5}});
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    LiveNotesSession *live = scene.host.live();
    live->setAudioEnabled(false);
    live->setAskBeforeStart(false);
    live->setKeepRecording(true);
    live->setModelId(addMockModel(scene.host, model.port()));
    scene.host.actions()->trigger(QStringLiteral("noteLiveStartAction"));
    QTRY_COMPARE(live->state(), QStringLiteral("listening"));
    auto options = live->notesEngine()->options();
    options.minWords = 3;
    options.minIntervalMs = 0;
    live->notesEngine()->setOptions(options);

    live->injectSegment(QStringLiteral("mitochondria make ATP for the whole cell"), 1000, QStringLiteral("Speaker 1"));
    QVERIFY(scene.host.actions()->entry(QStringLiteral("noteLiveFlagAction"))->isEnabled());
    scene.host.actions()->trigger(QStringLiteral("noteLiveFlagAction")); // F9
    live->injectSegment(QStringLiteral("and this is the part on the exam"), 4000, QStringLiteral("Speaker 2"));
    QCOMPARE(live->flagCount(), 1);
    QVERIFY(live->notesEngine()->transcript().last().flagged);
    QCOMPARE(live->speakers().size(), 2);
    live->setSpeakerName(QStringLiteral("Speaker 2"), QStringLiteral("Dr. Lee"));
    QCOMPARE(live->notesEngine()->speakerNames().value(QStringLiteral("Speaker 2")), QStringLiteral("Dr. Lee"));
    live->notesEngine()->tick();
    QTRY_VERIFY(live->updateCount() >= 1);
    const QVariantMap usage = live->usage();
    QVERIFY(usage.value(QStringLiteral("requests")).toInt() >= 1);
    QVERIFY(usage.contains(QStringLiteral("audioMinutes")));

    // Silence: 20 s of quiet pauses sending audio; sound resumes it.
    live->observeLevel(0.0);
    for (int i = 0; i < 21; ++i) {
        live->silenceTick();
    }
    QVERIFY(live->silencePaused());
    live->observeLevel(0.6);
    QVERIFY(!live->silencePaused());

    // The recording is kept for the notes block.
    const QString uid = live->target().blockUid;
    emit scene.host.liveNotesToggleRequested(true);
    QTest::qWait(200);
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_live_session_m56"));
    scene.host.actions()->trigger(QStringLiteral("noteLiveStopAction"));
    QTRY_COMPARE_WITH_TIMEOUT(live->state(), QStringLiteral("idle"), 10000);
    const QVariantMap session = scene.host.pageSession();
    QVERIFY2(!session.value(QStringLiteral("audioUrl")).toString().isEmpty(), "the recording was not kept");
    QVERIFY(QFile::exists(QUrl(session.value(QStringLiteral("audioUrl")).toString()).toLocalFile()));
    Q_UNUSED(uid);
}

void NoteSessionTest::reviewAndPageCommands()
{
    MockSseServer model;
    model.setScript({{sseFor(QStringLiteral("# Study: Cells\n\n## Flashcards\n| Front | Back |\n|---|---|\n"
                                            "| What makes ATP? | Mitochondria |\n\n## Practice questions\n1. Why?\n   - Answer: energy")),
                      5}});
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    QJsonObject block{{QStringLiteral("type"), QStringLiteral("markdown")},
                      {QStringLiteral("uid"), QStringLiteral("u1")},
                      {QStringLiteral("text"), kNotes},
                      {QStringLiteral("transcript"), QStringLiteral("[00:01] Speaker 1: mitochondria make ATP")}};
    QindaOffice::Apps::Notes::setBlockRect(&block, QRectF(40, 40, 680, 0));
    QVERIFY(scene.page->replaceBlocks(QJsonArray{block}, QStringLiteral("seed")));
    QTRY_COMPARE(scene.host.pageSession().value(QStringLiteral("sections")).toList().size(), 2);
    const QVariantMap session = scene.host.pageSession();
    QCOMPARE(session.value(QStringLiteral("unclear")).toList().size(), 1);
    QCOMPARE(session.value(QStringLiteral("unclear")).toList().first().toMap().value(QStringLiteral("ms")).toLongLong(), 2000);

    // Ctrl+click target: the [01:30] of the second section.
    const auto laid = scene.page->blocks();
    bool hit = false;
    for (qreal y = laid.first().rect.top(); y < laid.first().rect.bottom() && !hit; y += 2) {
        for (qreal x = laid.first().rect.left(); x < laid.first().rect.right(); x += 4) {
            if (scene.page->timestampAt(QPointF(x, y)) == 90000) {
                hit = true;
                break;
            }
        }
    }
    QVERIFY2(hit, "no [01:30] under any point of the rendered notes");

    // The assistant can answer from the transcript.
    const QString snapshot = scene.host.assistant()->documentTools()->snapshot(60000);
    QVERIFY(snapshot.contains(QStringLiteral("TRANSCRIPT of the live session")));

    // Fix the unclear spot (one undo step).
    QVERIFY(scene.host.resolveUnclear(QStringLiteral("*(unclear at 00:02: cristae?)*"), QStringLiteral("cristae")));
    QVERIFY(scene.host.currentPageNotes().contains(QStringLiteral("make ATP cristae")));
    QTRY_COMPARE(scene.host.pageSession().value(QStringLiteral("unclear")).toList().size(), 0);

    // Export.
    QTemporaryDir dir;
    QVERIFY(scene.host.exportMarkdownTo(QUrl::fromLocalFile(dir.filePath(QStringLiteral("n.md")))));
    QFile md(dir.filePath(QStringLiteral("n.md")));
    QVERIFY(md.open(QIODevice::ReadOnly) && md.readAll().startsWith("# Cells"));
    QVERIFY(!scene.host.exportAnkiTo(QUrl::fromLocalFile(dir.filePath(QStringLiteral("a.txt"))))); // no cards yet

    // A study page from the model, right after this page; then Anki.
    LiveNotesSession *live = scene.host.live();
    live->setModelId(addMockModel(scene.host, model.port()));
    const QString notesPage = scene.host.currentPageId();
    live->makeStudyPage();
    QTRY_VERIFY(!live->working());
    QVERIFY2(live->errorText().isEmpty(), qPrintable(live->errorText()));
    const auto *section = scene.host.currentSection();
    qsizetype at = -1;
    for (qsizetype i = 0; i < section->pages.size(); ++i) {
        if (section->pages.at(i).id == notesPage) {
            at = i;
        }
    }
    QVERIFY(at >= 0 && at + 1 < section->pages.size());
    QCOMPARE(section->pages.at(at + 1).title, QStringLiteral("Study: %1").arg(scene.host.currentPageTitle()));
    scene.host.selectPage(section->id, section->pages.at(at + 1).id);
    QVERIFY(scene.host.pageSession().value(QStringLiteral("hasCards")).toBool());
    QVERIFY(scene.host.exportAnkiTo(QUrl::fromLocalFile(dir.filePath(QStringLiteral("a.txt")))));

    // Split by topic: back on the notes page.
    scene.host.selectPage(section->id, notesPage);
    const qsizetype pages = scene.host.currentSection()->pages.size();
    scene.host.actions()->trigger(QStringLiteral("noteSplitByTopicAction"));
    QCOMPARE(scene.host.currentSection()->pages.size(), pages + 2);
    QVERIFY(scene.host.currentPageNotes().contains(QStringLiteral("## Topics")));
    QVERIFY(!scene.host.currentPageNotes().contains(QStringLiteral("lipid bilayer")));

    // The Session panel.
    scene.host.actions()->trigger(QStringLiteral("noteSessionPanelAction"));
    QQuickItem *panel = nullptr;
    QTRY_VERIFY((panel = findItem(scene.window->contentItem(), QStringLiteral("sessionPanel"))) != nullptr
                && panel->isVisible());
    QTest::qWait(200);
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_session_panel"));
}

void NoteSessionTest::prepMaterialAndSeriesMemory()
{
    QTemporaryDir dir;
    QFile text(dir.filePath(QStringLiteral("agenda.md")));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("# Agenda\n1. Krebs cycle\n2. Electron transport chain\n");
    text.close();
    const QString pdfPath = dir.filePath(QStringLiteral("slides.pdf"));
    {
        QPdfWriter pdf(pdfPath);
        QPainter painter(&pdf);
        painter.setFont(QFont(QStringLiteral("Sans"), 24));
        painter.drawText(QPointF(200, 400), QStringLiteral("Oxidative phosphorylation"));
    }
    QVERIFY(LiveNotesSession::readPrepFile(pdfPath).contains(QStringLiteral("Oxidative phosphorylation")));

    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    // An earlier page in the section with notes: series memory reads it.
    QJsonObject earlier{{QStringLiteral("type"), QStringLiteral("markdown")},
                        {QStringLiteral("text"), QStringLiteral("# Lecture 3\n- glycolysis")}};
    QVERIFY(scene.page->replaceBlocks(QJsonArray{earlier}, QStringLiteral("seed")));
    scene.host.actions()->trigger(QStringLiteral("notePageNew"));
    QVERIFY(scene.host.earlierNotesInSection(12000).contains(QStringLiteral("glycolysis")));

    MockSseServer model;
    model.setScript({{sseFor(QStringLiteral("[\"Krebs cycle\", \"NADH\"]")), 5}});
    LiveNotesSession *live = scene.host.live();
    live->setAudioEnabled(false);
    live->setAskBeforeStart(false);
    live->setContinueSeries(true);
    live->addPrepFile(QUrl::fromLocalFile(text.fileName()));
    live->addPrepFile(QUrl::fromLocalFile(pdfPath));
    QCOMPARE(live->prepFiles().size(), 2);
    live->setModelId(addMockModel(scene.host, model.port()));
    live->start();
    QTRY_COMPARE(live->state(), QStringLiteral("listening"));
    const auto context = live->notesEngine()->options().context;
    QVERIFY(context.reference.contains(QStringLiteral("Electron transport chain")));
    QVERIFY(context.reference.contains(QStringLiteral("Oxidative phosphorylation")));
    QVERIFY(context.previousNotes.contains(QStringLiteral("glycolysis")));
    // Key terms learned from the prep material join the glossary.
    QTRY_VERIFY(live->notesEngine()->options().glossary.contains(QStringLiteral("NADH")));
    QCOMPARE(live->usage().value(QStringLiteral("terms")).toInt(), 2);
    live->cancel();
}

// Regression (found with M56's addNotesPage): Move Page Up/Down swapped the
// section's QVector elements under the page item's pointer, so the view —
// and the next edit — silently switched to the neighbouring page.
void NoteSessionTest::movingAPageKeepsTheViewOnIt()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    scene.host.actions()->trigger(QStringLiteral("notePageNew"));
    const QString second = scene.host.currentPageId();
    scene.host.actions()->trigger(QStringLiteral("notePageMoveUp"));
    QCOMPARE(scene.host.currentPageId(), second);
    QCOMPARE(scene.page->page()->id, second);
    scene.page->addMarkdownBlock(QStringLiteral("typed after the move"));
    QVERIFY(scene.host.currentPageNotes().contains(QStringLiteral("typed after the move")));
}

#define main note_session_main_unused
NOTE_TEST_MAIN(NoteSessionTest)
#undef main

int main(int argc, char *argv[])
{
    qputenv("QQO_NO_SECRET_SERVICE", "1");
    return note_session_main_unused(argc, argv);
}

#include "tst_note_session.moc"
