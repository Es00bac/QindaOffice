// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_test_support.h"
#include "agent_host.h"
#include "qindanote/src/note_blocks.h"
#include "provider_settings.h"
#include <QJsonDocument>
using namespace NoteTest;
using namespace QindaOffice::Apps::Notes;
namespace {
QJsonObject invoke(QindaTK::AgentHost &service, QindaTK::AgentProvider *provider,
                   const QString &action, const QJsonObject &args, const QString &requestId,
                   const QString &revision = {})
{
    return service.request(QStringLiteral("action.invoke"), {
        {QStringLiteral("document_id"), provider->id()}, {QStringLiteral("action"), action},
        {QStringLiteral("arguments"), args}, {QStringLiteral("request_id"), requestId},
        {QStringLiteral("if_revision"), revision.isEmpty() ? provider->revision() : revision}});
}
QJsonArray edits(const QByteArray &text) { return QJsonDocument::fromJson(text).array(); }
QString status(const QJsonObject &object) { return object.value(QStringLiteral("status")).toString(); }
}
class NoteAgentTest final : public QObject {
    Q_OBJECT
private slots:
    void grantsAndEvidence()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        auto *provider = scene.host.agentProvider();
        QVERIFY(provider);
        QCOMPARE(scene.host.agentAccess(), 0);
        scene.host.actions()->trigger(QStringLiteral("agentRead"));
        QCOMPARE(scene.host.agentAccess(), 1);
        scene.host.actions()->trigger(QStringLiteral("agentEdit"));
        QCOMPARE(scene.host.agentAccess(), 2);
        scene.host.actions()->trigger(QStringLiteral("agentDisconnect"));
        QCOMPARE(scene.host.agentAccess(), 0);
        const auto blocks = edits(R"([{"type":"markdown","uid":"session-1","text":"\u0023 Review\n- choose sockets", "transcript":"[02:10] Speaker 1: keep this local", "audio":"assets/session.wav"}, {"type":"check","text":"Review protocol","checked":false}])");
        QVERIFY(scene.page->replaceBlocks(blocks, QStringLiteral("seed")));
        const auto state = provider->state();
        QCOMPARE(state.value(QStringLiteral("type")).toString(), QStringLiteral("notebook"));
        QCOMPARE(state.value(QStringLiteral("page_id")).toString(), scene.host.currentPageId());
        QVERIFY(state.value(QStringLiteral("view")).toObject().value(QStringLiteral("contentY")).isDouble());
        const auto first = state.value(QStringLiteral("blocks")).toArray().first().toObject();
        QVERIFY(first.value(QStringLiteral("transcript")).toString().contains(QStringLiteral("keep this local")));
        QVERIFY(first.value(QStringLiteral("has_recording")).toBool());
        QindaTK::AgentHost service;
        QVERIFY(service.addProvider(provider, QindaTK::AgentHost::Grant::Read));
        const auto reply = invoke(service, provider, QStringLiteral("note.edit"),
            {{QStringLiteral("edits"), edits(R"([{"op":"set_checked","block":1,"checked":true}])")}}, QStringLiteral("read-only"));
        QVERIFY(status(reply) != QStringLiteral("completed"));
        QVERIFY(!blocksArray(scene.host.currentPage()->envelope.blocksJson).at(1).toObject().value(QStringLiteral("checked")).toBool());
    }
    void atomicEditsReceiptsAndUndo()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.page->replaceBlocks(edits(R"([{"type":"text","text":"before"}, {"type":"check","text":"Test","checked":false}])"), QStringLiteral("seed")));
        auto *provider = scene.host.agentProvider();
        QindaTK::AgentHost service;
        QVERIFY(service.addProvider(provider, QindaTK::AgentHost::Grant::Edit));
        const int before = scene.page->historyIndex();
        const QString originalTitle = scene.host.currentPageTitle();
        const QString oldRevision = provider->revision();
        const QJsonObject args{{QStringLiteral("edits"), edits(R"([{"op":"replace_block","block":0,"text":"after"}, {"op":"rename_page","title":"Design review"}, {"op":"set_checked","block":1,"checked":true}])")}};
        const auto reply = invoke(service, provider, QStringLiteral("note.edit"), args, QStringLiteral("edit-1"), oldRevision);
        QCOMPARE(status(reply), QStringLiteral("completed"));
        QCOMPARE(scene.page->historyIndex(), before + 1);
        QCOMPARE(scene.host.currentPageTitle(), QStringLiteral("Design review"));
        const auto replay = invoke(service, provider, QStringLiteral("note.edit"), args, QStringLiteral("edit-1"), oldRevision);
        QVERIFY(replay.value(QStringLiteral("replayed")).toBool());
        QCOMPARE(scene.page->historyIndex(), before + 1);
        const auto undo = invoke(service, provider, QStringLiteral("note.undo"),
            {{QStringLiteral("undo_ref"), reply.value(QStringLiteral("undo_ref"))}}, QStringLiteral("undo-1"));
        QCOMPARE(status(undo), QStringLiteral("undone"));
        QCOMPARE(scene.host.currentPageTitle(), originalTitle);
        QCOMPARE(scene.page->historyIndex(), before);
        QCOMPARE(scene.host.currentPageText().section(QLatin1Char('\n'), 0, 0), QStringLiteral("before"));
        const auto invalid = invoke(service, provider, QStringLiteral("note.edit"),
            {{QStringLiteral("edits"), edits(R"([{"op":"rename_page","title":"Should not land"}, {"op":"delete_block","block":99}])")}}, QStringLiteral("bad"));
        QCOMPARE(status(invalid), QStringLiteral("validation_failed"));
        QCOMPARE(scene.host.currentPageTitle(), originalTitle);
        QCOMPARE(scene.page->historyIndex(), before);
        const auto stale = invoke(service, provider, QStringLiteral("note.edit"), args, QStringLiteral("stale"), oldRevision);
        QCOMPARE(status(stale), QStringLiteral("stale_revision")); // edit/undo ABA
    }
    void newerHumanEditsAndUnfinishedInput()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        auto *provider = scene.host.agentProvider();
        QindaTK::AgentHost service;
        QVERIFY(service.addProvider(provider, QindaTK::AgentHost::Grant::Edit));
        const auto added = invoke(service, provider, QStringLiteral("note.edit"),
            {{QStringLiteral("edits"), edits(R"([{"op":"insert_block","text":"agent"}])")}}, QStringLiteral("added"));
        QCOMPARE(status(added), QStringLiteral("completed"));
        scene.page->addMarkdownBlock(QStringLiteral("human"));
        const auto undo = invoke(service, provider, QStringLiteral("note.undo"),
            {{QStringLiteral("undo_ref"), added.value(QStringLiteral("undo_ref"))}}, QStringLiteral("old-undo"));
        QVERIFY(status(undo) != QStringLiteral("undone"));
        QVERIFY(scene.host.currentPageText().contains(QStringLiteral("human")));
        scene.page->beginEdit(0);
        const auto refused = invoke(service, provider, QStringLiteral("note.edit"),
            {{QStringLiteral("edits"), edits(R"([{"op":"replace_block","block":0,"text":"lost human input"}])")}}, QStringLiteral("draft"));
        QCOMPARE(status(refused), QStringLiteral("disabled"));
        QCOMPARE(scene.page->editingBlock(), 0);
    }
    void rewritingPreservesSessionAndImages()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        const auto seed = edits(R"([{"type":"markdown","uid":"u","text":"long notes","transcript":"[00:12] exact words","audio":"assets/u.wav","session_started_at":"2026-09-14T15:00:00Z"}, {"type":"image","data":"cGljdHVyZQ=="}])");
        QVERIFY(scene.page->replaceBlocks(seed, QStringLiteral("seed")));
        auto *tools = scene.host.assistant()->documentTools();
        static_cast<void>(tools->snapshot(60000));
        QCOMPARE(tools->apply(edits(R"([{"op":"replace_page","markdown":"\u0023 Short notes"}])")).applied, 1);
        const auto result = blocksArray(scene.host.currentPage()->envelope.blocksJson);
        QCOMPARE(result.size(), 2);
        QCOMPARE(result.first().toObject().value(QStringLiteral("transcript")).toString(), QStringLiteral("[00:12] exact words"));
        QCOMPARE(result.first().toObject().value(QStringLiteral("audio")).toString(), QStringLiteral("assets/u.wav"));
        QCOMPARE(result.last().toObject().value(QStringLiteral("data")), seed.last().toObject().value(QStringLiteral("data")));
        tools->undo();
        QCOMPARE(blocksArray(scene.host.currentPage()->envelope.blocksJson), seed);
    }
};
NOTE_TEST_MAIN(NoteAgentTest)
#include "tst_note_agent.moc"
