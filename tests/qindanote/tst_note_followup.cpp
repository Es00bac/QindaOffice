// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real note host and HTTP provider, not a second implementation
// of the follow-up logic. Every request uses synthetic, inspectable evidence.
#include "note_test_support.h"
#include "support/quick_dump.h"
#include "office_ai/mock_sse_server.h"
#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_evidence.h"
#include "provider_settings.h"
#include "paths.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
using namespace NoteTest;
using namespace QindaOffice::Apps::Notes;
using QindaOffice::Ai::MockSseServer;
namespace {
QByteArray sse(const QString &text)
{
    const QJsonObject chunk{{QStringLiteral("choices"), QJsonArray{QJsonObject{
        {QStringLiteral("delta"), QJsonObject{{QStringLiteral("content"), text}}}}}}};
    return "data: " + QJsonDocument(chunk).toJson(QJsonDocument::Compact) + "\n\ndata: [DONE]\n\n";
}
QString modelId(NoteHost &host, quint16 port)
{
    return host.providerSettings()->addCustom({
        {QStringLiteral("id"), QStringLiteral("followup-test")},
        {QStringLiteral("baseUrl"), QStringLiteral("http://127.0.0.1:%1/v1").arg(port)},
        {QStringLiteral("model"), QStringLiteral("m")}}) + QStringLiteral("/m");
}
QJsonArray evidence()
{
    return {QJsonObject{{QStringLiteral("type"), QStringLiteral("markdown")},
        {QStringLiteral("uid"), QStringLiteral("session-1")},
        {QStringLiteral("text"), QStringLiteral("# Driver debugging\n- reboot failed\n- retry tomorrow; owner unassigned")},
        {QStringLiteral("transcript"), QStringLiteral("[02:15] Speaker 1: firmware 2.3 restored audio; cause still unknown")},
        {QStringLiteral("session_started_at"), QStringLiteral("2026-09-14T15:00:00Z")}}};
}
QString handoffDir()
{
    return QindaOffice::Core::stateDirFor(QindaOffice::Core::appLeaf()) + QStringLiteral("/handoff");
}
}
class NoteFollowUpTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            ".*(Binding loop|TypeError|ReferenceError|Unable to assign).*")));
    }
    void realModelWorkflowReview();
    void readAssistantUsesSpeechAndEvidenceBounds()
    {
        MockSseServer model;
        model.setScript({{sse(QStringLiteral("Firmware 2.3 restored audio [02:15].")), 5}});
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(scene.page->replaceBlocks(evidence(), QStringLiteral("seed")));
        auto *assistant = scene.host.assistant();
        assistant->setModelId(modelId(scene.host, model.port()));
        assistant->setEditMode(false);
        assistant->setScope(2);
        assistant->send(QStringLiteral("What actually restored audio?"));
        QTRY_VERIFY(!assistant->streaming());
        QTRY_COMPARE(model.requests().size(), 1);
        const auto request = QJsonDocument(model.requests().first()).toJson();
        QVERIFY(request.contains("firmware 2.3 restored audio"));
        QVERIFY(request.contains("reboot failed"));
        QVERIFY(request.contains("SOURCE RULES"));
        for (int budget : {0, 10, 512, 60000}) QVERIFY(noteEvidence(&scene.host, budget).size() <= budget);
        const QString longSource = QStringLiteral("EARLY") + QString(10000, QLatin1Char('x')) + QStringLiteral("LATE");
        const QString bounded = boundedEvidence(longSource, 512);
        QCOMPARE(bounded.size(), 512);
        QVERIFY(bounded.startsWith(QStringLiteral("EARLY")) && bounded.endsWith(QStringLiteral("LATE")));
        QVERIFY(bounded.contains(QStringLiteral("omitted")));
    }
    void draftNeedsReviewAndHasContext()
    {
        MockSseServer model;
        model.setScript({{sse(QStringLiteral("# Troubleshooting\n- Firmware 2.3 restored audio [02:15].\n- Cause: not recorded.")), 5}});
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.page->replaceBlocks(evidence(), QStringLiteral("seed")));
        const QString source = scene.host.currentPageId();
        const QByteArray original = scene.host.currentPage()->envelope.blocksJson;
        auto *live = scene.host.live();
        live->setModelId(modelId(scene.host, model.port()));
        scene.host.actions()->trigger(QStringLiteral("noteTroubleshootingAction"));
        QVERIFY(live->working());
        QTRY_VERIFY(!live->working());
        QVERIFY2(live->errorText().isEmpty(), qPrintable(live->errorText()));
        QVERIFY(!live->draftText().isEmpty());
        QCOMPARE(scene.host.currentSection()->pages.size(), 1);
        QCOMPARE(scene.host.currentPage()->envelope.blocksJson, original);
        QTRY_COMPARE(model.requests().size(), 1);
        const auto request = QJsonDocument(model.requests().first()).toJson();
        QVERIFY(request.contains("firmware 2.3 restored audio") && request.contains("failed attempts"));
        QTest::qWait(200);
        auto *preview = findItem(scene.window->contentItem(), QStringLiteral("sessionDraftPreview"));
        QVERIFY(preview && preview->isVisible());
        const auto *picker = findItem(scene.window->contentItem(), QStringLiteral("sessionDraftKind"));
        QVERIFY(picker && picker->property("currentIndex").toInt() == 2);
        QVERIFY(preview->width() > 0 && preview->height() > 0);
        const QPointF top = preview->mapToScene(QPointF());
        qInfo("TextArea#sessionDraftPreview at %g,%g %gx%g in %d px window",
            top.x(), top.y(), preview->width(), preview->height(), scene.window->width());
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_followup_1280"));
        QVERIFY(live->acceptDraft());
        QCOMPARE(scene.host.currentSection()->pages.size(), 2);
        QCOMPARE(scene.host.currentPageId(), source);
        QCOMPARE(scene.host.currentPage()->envelope.blocksJson, original);
        QVERIFY(!live->acceptDraft()); // accepting twice never duplicates a page
    }
    void humanEditInvalidatesReply()
    {
        MockSseServer model;
        model.setScript({{sse(QStringLiteral("# Old project plan")), 300}});
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(scene.page->replaceBlocks(evidence(), QStringLiteral("seed")));
        auto *live = scene.host.live();
        live->setModelId(modelId(scene.host, model.port()));
        live->makeDraft(QStringLiteral("plan"));
        QTRY_COMPARE(model.requests().size(), 1);
        scene.page->addMarkdownBlock(QStringLiteral("Human correction: deadline withdrawn"));
        QTRY_VERIFY(!live->working());
        QVERIFY(live->draftText().isEmpty());
        QVERIFY(live->errorText().contains(QStringLiteral("changed")));
        QCOMPARE(scene.host.currentSection()->pages.size(), 1);
        QVERIFY(scene.host.currentPageText().contains(QStringLiteral("deadline withdrawn")));
    }
    void navigationAndCancelDiscardReply()
    {
        MockSseServer model;
        model.setScript({{sse(QStringLiteral("# Wrong page draft")), 300}});
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(scene.page->replaceBlocks(evidence(), QStringLiteral("seed")));
        auto *live = scene.host.live();
        live->setModelId(modelId(scene.host, model.port()));
        live->makeDraft(QStringLiteral("recap"));
        QTRY_COMPARE(model.requests().size(), 1);
        scene.host.actions()->trigger(QStringLiteral("notePageNew"));
        const QString destination = scene.host.currentPageId();
        QTest::qWait(500);
        QVERIFY(!live->working() && live->draftText().isEmpty());
        QCOMPARE(scene.host.currentPageId(), destination);
        QCOMPARE(scene.host.currentSection()->pages.size(), 2);
        QVERIFY(scene.host.currentPageText().isEmpty());
        scene.page->addMarkdownBlock(QStringLiteral("New notes"));
        live->makeDraft(QStringLiteral("questions"));
        live->clearDraft();
        QTest::qWait(500);
        QVERIFY(!live->working() && live->draftText().isEmpty());
        QCOMPARE(scene.host.currentSection()->pages.size(), 2);
    }
    void failuresReleaseBusy()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(scene.page->replaceBlocks(evidence(), QStringLiteral("seed")));
        auto *live = scene.host.live();
        live->setModelId(QStringLiteral("missing/model"));
        live->makeDraft(QStringLiteral("study"));
        QTRY_VERIFY(!live->working());
        QVERIFY(!live->errorText().isEmpty());
        MockSseServer model;
        model.setScript({{sse(QString()), 5}});
        live->setModelId(modelId(scene.host, model.port()));
        live->makeDraft(QStringLiteral("recap"));
        QTRY_VERIFY(!live->working());
        QVERIFY(live->draftText().isEmpty() && !live->errorText().isEmpty());
    }
    void tasksUseSessionDateAndReviewedHandoff()
    {
        QDir(handoffDir()).removeRecursively();
        MockSseServer model;
        model.setScript({{sse(QStringLiteral(R"([{"title":"Retry tomorrow","owner":"","due":"2026-09-15","notes":"Source block 0"}])")), 5}});
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(scene.page->replaceBlocks(evidence(), QStringLiteral("seed")));
        auto *live = scene.host.live();
        live->setModelId(modelId(scene.host, model.port()));
        live->sendActionItemsToMail();
        QTRY_VERIFY(!live->working());
        QVERIFY2(!live->draftText().isEmpty(), qPrintable(live->errorText()));
        QVERIFY(live->draftText().contains(QStringLiteral("Unassigned")));
        QVERIFY(!QDir(handoffDir()).exists());
        QTRY_COMPARE(model.requests().size(), 1);
        QVERIFY(QJsonDocument(model.requests().first()).toJson().contains("2026-09-14"));
        QVERIFY(live->acceptDraft());
        const QStringList files = QDir(handoffDir()).entryList({QStringLiteral("*.ics")}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile calendar(QDir(handoffDir()).filePath(files.first()));
        QVERIFY(calendar.open(QIODevice::ReadOnly));
        QVERIFY(calendar.readAll().contains("DUE;VALUE=DATE:20260915"));
        QVERIFY(!live->acceptDraft());
        QCOMPARE(QDir(handoffDir()).entryList({QStringLiteral("*.ics")}, QDir::Files).size(), 1);
        QVERIFY(live->statusText().contains(QStringLiteral("not opened")));
    }
    void oldNotesDoNotUseTodaysDate()
    {
        MockSseServer model;
        model.setScript({{sse(QStringLiteral(R"([{"title":"Retry tomorrow","due":"","notes":"Relative date; original session date unknown"}])")), 5}});
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        auto blocks = evidence();
        auto block = blocks.first().toObject();
        block.remove(QStringLiteral("session_started_at"));
        blocks.replace(0, block);
        QVERIFY(scene.page->replaceBlocks(blocks, QStringLiteral("old notes")));
        auto *live = scene.host.live();
        live->setModelId(modelId(scene.host, model.port()));
        live->makeDraft(QStringLiteral("tasks"));
        QTRY_VERIFY(!live->working());
        QVERIFY(live->draftText().contains(QStringLiteral("No date recorded")));
        QTRY_COMPARE(model.requests().size(), 1);
        QVERIFY(QJsonDocument(model.requests().first()).toJson().contains("No reliable session date"));
    }
    void priorityStructuresAndNarrowPreview()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        auto *live = scene.host.live();
        QCOMPARE(live->scenarios().size(), 14);
        for (const auto *id : {"technical", "planning", "troubleshooting", "appointment"}) {
            const auto setup = live->suggestedSetup(QString::fromLatin1(id));
            QVERIFY(!setup.value(QStringLiteral("sections")).toString().isEmpty());
            QVERIFY(setup.value(QStringLiteral("openQuestions")).toBool());
        }
        QVERIFY(scene.page->replaceBlocks(evidence(), QStringLiteral("seed")));
        MockSseServer model;
        model.setScript({{sse(QStringLiteral("# Review\n- Confirmed audio restored.\n- Root cause not recorded.")), 5}});
        live->setModelId(modelId(scene.host, model.port()));
        live->makeDraft(QStringLiteral("recap"));
        QTRY_VERIFY(!live->working());
        QTest::qWait(200);
        auto *preview = findItem(scene.window->contentItem(), QStringLiteral("sessionDraftPreview"));
        QVERIFY(preview && preview->isVisible());
        QVERIFY(preview->width() > 0 && preview->width() < scene.window->width());
        const QPointF top = preview->mapToScene(QPointF());
        QVERIFY(top.x() >= 0 && top.x() + preview->width() <= scene.window->width());
        qInfo("TextArea#sessionDraftPreview at %g,%g %gx%g in %d px window",
            top.x(), top.y(), preview->width(), preview->height(), scene.window->width());
        QVERIFY(!scene.host.actions()->entry(QStringLiteral("noteShowNavigatorAction"))->isChecked());
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_followup_520"));
    }
};

// Opt-in quality review through the actual app/model path, with synthetic
// sessions only. Deterministic tests above enforce the application contract;
// this writes model outputs for a human grounding/usefulness assessment.
void NoteFollowUpTest::realModelWorkflowReview()
{
    const QString base = qEnvironmentVariable("QQO_NOTE_EVAL_BASE");
    if (base.isEmpty()) QSKIP("set QQO_NOTE_EVAL_BASE/_MODEL/_KEYREF to review a real model");
    struct Case { QString kind, title, text, transcript; };
    const QList<Case> cases{
        {QStringLiteral("recap"), QStringLiteral("Agent API design review"),
            QStringLiteral("# Design review\nGoal: local agents can read running apps. Screenshots are expensive. "
                "A semantic Unix socket is proposed. Authentication review is still pending."),
            QStringLiteral("[00:40] Alex: I propose a Unix socket, but we will decide after authentication review.\n"
                "[01:20] Casey: I will write the API prototype. We have not agreed a due date.\n"
                "[02:20] Alex: App reads should stay on this machine.")},
        {QStringLiteral("plan"), QStringLiteral("Starter release planning"),
            QStringLiteral("# Release\nThe package build is complete. Documentation is still missing. "
                "The installer depends on the signed package."),
            QStringLiteral("[01:10] Casey: I will finish the installation guide. No date agreed yet.\n"
                "[02:30] Alex: October 16 is an option for release, not an agreed deadline.\n"
                "[03:20] Casey: The package signing failure blocks the installer.")},
        {QStringLiteral("troubleshooting"), QStringLiteral("Audio incident"),
            QStringLiteral("# Audio incident\nSystem: Linux 6.18.4; firmware 2.2. No audio after suspend. "
                "A reboot did not restore it. Command recorded: uname -r; output: 6.18.4."),
            QStringLiteral("[02:15] Alex: Firmware 2.3 restored audio in this test; root cause still unknown.\n"
                "[03:20] Casey: We still need to verify after another suspend.\n"
                "[04:10] Alex: If it regresses, roll back to firmware 2.2.")},
        {QStringLiteral("questions"), QStringLiteral("Coaching follow-up"),
            QStringLiteral("# Coaching\nResume feedback: add concrete project outcomes. "
                "Coach suggested applying to three roles; no agreement to do that."),
            QStringLiteral("[00:50] Alex: I will send a resume draft before our next meeting.\n"
                "[01:40] Coach: We have not scheduled that meeting yet.")},
        {QStringLiteral("tasks"), QStringLiteral("Engineering commitments"),
            QStringLiteral("# Tasks\n- [x] Backup completed.\n- Proposed only: migrate the database.\n"
                "- Agreed: Casey sends the installation guide tomorrow."),
            QStringLiteral("[01:00] Casey: I will send the installation guide tomorrow.\n"
                "[02:00] Alex: Migrating the database is only a suggestion, not a commitment.")}
    };
    QJsonArray outputs;
    const auto chosen = qEnvironmentVariable("QQO_NOTE_EVAL_KINDS").split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const auto &entry : cases) {
        if (!chosen.isEmpty() && !chosen.contains(entry.kind)) continue;
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        auto blocks = evidence();
        auto block = blocks.first().toObject();
        block.insert(QStringLiteral("text"), entry.text);
        block.insert(QStringLiteral("transcript"), entry.transcript);
        blocks.replace(0, block);
        QVERIFY(scene.page->replaceContent(blocks, entry.title, QStringLiteral("seed")));
        const QByteArray original = scene.host.currentPage()->envelope.blocksJson;
        const QString model = qEnvironmentVariable("QQO_NOTE_EVAL_MODEL");
        const QString id = scene.host.providerSettings()->addCustom({
            {QStringLiteral("id"), QStringLiteral("real-evaluation")},
            {QStringLiteral("baseUrl"), base}, {QStringLiteral("model"), model},
            {QStringLiteral("apiKeyRef"), qEnvironmentVariable("QQO_NOTE_EVAL_KEYREF")},
            {QStringLiteral("body"), qEnvironmentVariable("QQO_NOTE_EVAL_BODY")}});
        auto *live = scene.host.live();
        live->setModelId(id + QLatin1Char('/') + model);
        QElapsedTimer elapsed;
        elapsed.start();
        live->makeDraft(entry.kind);
        QTRY_VERIFY_WITH_TIMEOUT(!live->working(), 300000);
        QVERIFY2(!live->draftText().isEmpty(), qPrintable(live->errorText()));
        QCOMPARE(scene.host.currentSection()->pages.size(), 1);
        QCOMPARE(scene.host.currentPage()->envelope.blocksJson, original);
        outputs.append(QJsonObject{{QStringLiteral("kind"), entry.kind}, {QStringLiteral("title"), entry.title},
            {QStringLiteral("notes"), entry.text}, {QStringLiteral("transcript"), entry.transcript},
            {QStringLiteral("draft"), live->draftText()}, {QStringLiteral("elapsed_ms"), elapsed.elapsed()}});
        qInfo("Prepared %s in %.1f seconds; source unchanged", qPrintable(entry.kind), elapsed.elapsed() / 1000.0);
    }
    const QString directory = qEnvironmentVariable("QQO_NOTE_EVAL_DIR");
    if (!directory.isEmpty()) {
        QVERIFY(QDir().mkpath(directory));
        QSaveFile report(QDir(directory).filePath(QStringLiteral("model-workflows.json")));
        QVERIFY(report.open(QIODevice::WriteOnly));
        const auto data = QJsonDocument(QJsonObject{{QStringLiteral("model"), qEnvironmentVariable("QQO_NOTE_EVAL_MODEL")},
            {QStringLiteral("cases"), outputs}}).toJson();
        QCOMPARE(report.write(data), data.size());
        QVERIFY(report.commit());
    }
}
NOTE_TEST_MAIN(NoteFollowUpTest)
#include "tst_note_followup.moc"
