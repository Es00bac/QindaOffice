// SPDX-License-Identifier: GPL-3.0-or-later
// QindaNote live notes (D-057): markdown blocks on paper (screen and print
// share one layout; ==highlights== are painted), and a whole session in the
// real window — Start, speech segments in, the model's revisions landing
// in one markdown block as undo steps, the user's editor never overwritten,
// Stop with a final revision and the transcript stored on the block. The
// model is a local SSE server; audio is off (office_speech has its own
// gates); the panel is looked at through QQO_GRAB_DIR.

#include "note_test_support.h"
#include "support/quick_dump.h"

#include "office_ai/mock_sse_server.h"
#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_live_session.h"
#include "qindanote/src/note_markdown.h"
#include "qindanote/src/note_page_render.h"

#include "provider_settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QPainter>

using namespace NoteTest;
using QindaOffice::Ai::MockSseServer;
using QindaOffice::Apps::Notes::LiveNotesSession;

namespace {

const QString kNotes = QStringLiteral(
    "# Cell biology\n\n## Summary\n- Cells make energy in ==mitochondria==\n\n"
    "## Mitochondria [00:01]\n- **ATP** — the cell's energy currency\n- [ ] Read chapter 2");

QByteArray sseFor(const QString &text)
{
    const QJsonObject chunk{{QStringLiteral("choices"),
                             QJsonArray{QJsonObject{{QStringLiteral("delta"),
                                                     QJsonObject{{QStringLiteral("content"), text}}}}}}};
    return "data: " + QJsonDocument(chunk).toJson(QJsonDocument::Compact) + "\n\ndata: [DONE]\n\n";
}

QJsonObject blockByUid(NoteHost &host, const QString &uid)
{
    const auto *page = host.currentPage();
    for (const QJsonValue &value : QindaOffice::Apps::Notes::blocksArray(page->envelope.blocksJson)) {
        if (value.toObject().value(QStringLiteral("uid")).toString() == uid) {
            return value.toObject();
        }
    }
    return {};
}

} // namespace

class NoteLiveTest : public QObject {
    Q_OBJECT
private slots:
    void markdownBlocksLayOutAndPaintOnPaper();
    void sessionWritesAndRevisesOneBlock();
    void livePanelFollowsTheAiMenu();
    void realPipelineEndToEnd();
};

void NoteLiveTest::markdownBlocksLayOutAndPaintOnPaper()
{
    using namespace QindaOffice::Apps::Notes;
    const QByteArray json = QJsonDocument(QJsonObject{
        {QStringLiteral("blocks"),
         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("markdown")},
                                {QStringLiteral("uid"), QStringLiteral("u1")},
                                {QStringLiteral("text"), kNotes}}}}}).toJson();
    const QList<PageBlock> blocks = layoutBlocks(json, 760);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks.first().kind, PageBlock::Kind::Markdown);
    QCOMPARE(blocks.first().uid, QStringLiteral("u1"));
    // Six rendered lines with two headings: much taller than one text line.
    QVERIFY(blocks.first().rect.height() > 5 * textBlockHeight(QStringLiteral("x"), 680) / 2);
    QVERIFY(markdownPlainText(kNotes).contains(QStringLiteral("mitochondria")));
    QVERIFY(!markdownPlainText(kNotes).contains(QStringLiteral("==")));

    // Printed page: the ==highlight== is a marker-yellow stroke on paper.
    NotePage page;
    page.envelope.blocksJson = json;
    QImage image(760, 1004, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    renderNotePage(&painter, page, QSizeF(760, 1004));
    painter.end();
    int yellow = 0;
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 760; ++x) {
            const QColor c = image.pixelColor(x, y);
            yellow += c.red() > 240 && c.green() > 220 && c.blue() < 150 ? 1 : 0;
        }
    }
    QVERIFY2(yellow > 200, qPrintable(QString::number(yellow)));
    const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR");
    if (!grabs.isEmpty()) {
        image.save(grabs + QStringLiteral("/qindanote_live_markdown.png"));
    }
}

void NoteLiveTest::sessionWritesAndRevisesOneBlock()
{
    MockSseServer model;
    model.setScript({{sseFor(kNotes), 5}});
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    LiveNotesSession *live = scene.host.live();
    QVERIFY(live != nullptr);
    live->setAudioEnabled(false);
    const QString id = scene.host.providerSettings()->addCustom(
        {{QStringLiteral("id"), QStringLiteral("mock")},
         {QStringLiteral("name"), QStringLiteral("Mock model")},
         {QStringLiteral("baseUrl"), QStringLiteral("http://127.0.0.1:%1/v1").arg(model.port())},
         {QStringLiteral("model"), QStringLiteral("mock-1")}});
    live->setModelId(id + QStringLiteral("/mock-1"));
    QVERIFY(!live->modelOffMachine());
    live->setTopic(QStringLiteral("Biology 101"));

    scene.host.actions()->trigger(QStringLiteral("noteLiveStartAction"));
    QTRY_COMPARE(live->state(), QStringLiteral("listening"));
    QCOMPARE(scene.host.currentPageTitle(), QStringLiteral("Biology 101"));
    const QString uid = live->target().blockUid;
    QVERIFY(!uid.isEmpty());
    QCOMPARE(blockByUid(scene.host, uid).value(QStringLiteral("type")).toString(), QStringLiteral("markdown"));
    QVERIFY(scene.host.liveNotesText(live->target()).isEmpty()); // the placeholder is not notes
    QVERIFY(!scene.host.actions()->entry(QStringLiteral("noteLiveStartAction"))->isEnabled());
    QVERIFY(scene.host.actions()->entry(QStringLiteral("noteLiveStopAction"))->isEnabled());

    auto *engine = live->notesEngine();
    QVERIFY(engine != nullptr);
    auto options = engine->options();
    options.minWords = 5;
    options.minIntervalMs = 0;
    engine->setOptions(options);
    live->injectSegment(QStringLiteral("Today we look at how mitochondria make ATP for the cell"), 1000);
    QCOMPARE(live->segmentCount(), 1);
    engine->tick();
    QTRY_COMPARE(scene.host.liveNotesText(live->target()), kNotes);
    QVERIFY(scene.page->canUndo()); // one undo step per revision
    QVERIFY(scene.host.currentPageText().contains(QStringLiteral("mitochondria")));

    // The user opens the notes block: a revision must not land under them.
    const int index = scene.page->blockIndexForUid(uid);
    scene.page->beginEdit(index);
    QCOMPARE(scene.page->editingKind(), QStringLiteral("markdown"));
    QVERIFY(!scene.host.applyLiveNotes(live->target(), QStringLiteral("# overwritten")));
    scene.page->commitEdit();
    QTest::qWait(100);
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_live_session"));

    // Stop: the final revision runs from the whole transcript and the
    // transcript is stored on the block (not painted).
    live->injectSegment(QStringLiteral("and that is the Krebs cycle"), 60000);
    scene.host.actions()->trigger(QStringLiteral("noteLiveStopAction"));
    QTRY_COMPARE_WITH_TIMEOUT(live->state(), QStringLiteral("idle"), 10000);
    const QString transcript = blockByUid(scene.host, uid).value(QStringLiteral("transcript")).toString();
    QVERIFY2(transcript.contains(QStringLiteral("[00:01] Today we look at how mitochondria"))
                 && transcript.contains(QStringLiteral("[01:00] and that is the Krebs cycle")),
             qPrintable(transcript));
    QVERIFY(scene.host.actions()->entry(QStringLiteral("noteLiveStartAction"))->isEnabled());
    QVERIFY(live->statusText().contains(QStringLiteral("Finished")));
}

void NoteLiveTest::livePanelFollowsTheAiMenu()
{
    Scene scene(QSize(1280, 760));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    // The dock layout is remembered; the session test above opened the panel.
    QQuickItem *dock = findItem(scene.window->contentItem(), QStringLiteral("noteDock"));
    QVERIFY(dock != nullptr && QMetaObject::invokeMethod(dock, "resetLayout"));
    QTest::qWait(150);
    QQuickItem *panel = findItem(scene.window->contentItem(), QStringLiteral("livePanel"));
    QVERIFY(panel == nullptr || !panel->isVisible());
    auto *entry = scene.host.actions()->entry(QStringLiteral("noteLiveNotesAction"));
    QVERIFY(entry != nullptr && entry->isCheckable());
    scene.host.actions()->trigger(QStringLiteral("noteLiveNotesAction"));
    QTRY_VERIFY((panel = findItem(scene.window->contentItem(), QStringLiteral("livePanel"))) != nullptr
                && panel->isVisible());
    for (const char *name : {"liveStart", "liveStop", "liveSource", "liveTopic", "liveSpeechEngine",
                             "liveModel", "liveProviders", "liveSettings"}) {
        QVERIFY2(findItem(panel, QString::fromLatin1(name)) != nullptr, name);
    }
    QVERIFY(!findItem(panel, QStringLiteral("liveStop"))->isEnabled()); // nothing to stop yet
    QTest::qWait(150);
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_live_panel"));
    // The providers dialog opens from the panel's button.
    QMetaObject::invokeMethod(findItem(panel, QStringLiteral("liveProviders")), "clicked");
    QTest::qWait(250);
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_live_providers"));
}

// Manual end-to-end run, skipped unless asked for: real speech (a 16 kHz
// mono s16le file in QQO_LIVE_E2E_PCM, played through a pw-record
// stand-in) → local Whisper → a real model (QQO_LIVE_E2E_BASE, _MODEL,
// _KEYREF, optional _BODY JSON) → the page. Prints the notes and timings.
void NoteLiveTest::realPipelineEndToEnd()
{
    const QString pcm = qEnvironmentVariable("QQO_LIVE_E2E_PCM");
    if (pcm.isEmpty()) {
        QSKIP("set QQO_LIVE_E2E_PCM (and _BASE/_MODEL/_KEYREF) to run the real pipeline");
    }
    QTemporaryDir bin;
    const QString recorder = bin.filePath(QStringLiteral("pw-record"));
    QFile script(recorder);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\ncat '" + pcm.toUtf8() + "'\nexec sleep 3600\n");
    script.close();
    script.setPermissions(script.permissions() | QFileDevice::ExeOwner);
    qputenv("QQO_PW_RECORD", recorder.toUtf8());

    Scene scene(QSize(1280, 900));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    LiveNotesSession *live = scene.host.live();
    const QString id = scene.host.providerSettings()->addCustom(
        {{QStringLiteral("id"), QStringLiteral("e2e")},
         {QStringLiteral("name"), QStringLiteral("E2E model")},
         {QStringLiteral("baseUrl"), qEnvironmentVariable("QQO_LIVE_E2E_BASE")},
         {QStringLiteral("model"), qEnvironmentVariable("QQO_LIVE_E2E_MODEL")},
         {QStringLiteral("apiKeyRef"), qEnvironmentVariable("QQO_LIVE_E2E_KEYREF")},
         {QStringLiteral("body"), qEnvironmentVariable("QQO_LIVE_E2E_BODY")}});
    live->setModelId(id + QLatin1Char('/') + qEnvironmentVariable("QQO_LIVE_E2E_MODEL"));
    live->setEngine(QStringLiteral("whisper"));
    live->setTopic(QStringLiteral("Research brief (podcast)"));
    scene.host.actions()->trigger(QStringLiteral("noteLiveStartAction"));
    QTRY_COMPARE_WITH_TIMEOUT(live->state(), QStringLiteral("listening"), 30000);
    auto options = live->notesEngine()->options();
    options.minIntervalMs = 15000;
    live->notesEngine()->setOptions(options);
    QElapsedTimer clock;
    clock.start();
    int lastSegments = -1;
    qint64 stableSince = 0;
    while (clock.elapsed() < 600000) {
        QTest::qWait(1000);
        if (live->segmentCount() != lastSegments) {
            lastSegments = live->segmentCount();
            stableSince = clock.elapsed();
            qInfo("%6.1fs: %d segments, %d updates, %s", clock.elapsed() / 1000.0, lastSegments,
                  live->updateCount(), qPrintable(live->statusText()));
        }
        if (lastSegments > 0 && clock.elapsed() - stableSince > 30000 && !live->updating()) {
            break; // the file is transcribed and the model caught up
        }
    }
    qInfo("transcribed in ~%.0fs; stopping", stableSince / 1000.0);
    scene.host.actions()->trigger(QStringLiteral("noteLiveStopAction"));
    QTRY_COMPARE_WITH_TIMEOUT(live->state(), QStringLiteral("idle"), 600000);
    qInfo("done at %.1fs: %s %s", clock.elapsed() / 1000.0, qPrintable(live->statusText()),
          qPrintable(live->errorText()));
    const QString notes = scene.host.liveNotesText(live->target());
    qInfo("NOTES:\n%s", qPrintable(notes));
    QVERIFY(!notes.isEmpty());
    QTest::qWait(300);
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_live_e2e"));
    qunsetenv("QQO_PW_RECORD");
}

#define main note_live_main_unused
NOTE_TEST_MAIN(NoteLiveTest)
#undef main

int main(int argc, char *argv[])
{
    qputenv("QQO_NO_SECRET_SERVICE", "1"); // never touch the real keyring
    return note_live_main_unused(argc, argv);
}

#include "tst_note_live.moc"
