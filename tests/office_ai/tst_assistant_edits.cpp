// SPDX-License-Identifier: GPL-3.0-or-later
// The assistant as an editor (D-058): the ```qinda-edits protocol parse,
// and AssistantController in edit mode against a scripted SSE model — the
// snapshot and the operation guide go out, the reply's block is applied
// through DocumentTools, the conversation shows what changed instead of
// the JSON, and Undo reverts it.

#include "assistant_controller.h"
#include "document_tools.h"
#include "mock_sse_server.h"
#include "model_catalog.h"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Ai;

namespace {

class FakeTools final : public DocumentTools {
public:
    QList<QJsonArray> applied;
    int undos = 0;
    int snapshots = 0;
    QString documentNoun() const override { return QStringLiteral("document"); }
    QString snapshot(int) override
    {
        ++snapshots;
        return QStringLiteral("[0] text:\nhello wrold");
    }
    QString operationGuide() const override { return QStringLiteral("- {\"op\":\"replace_block\"}"); }
    EditOutcome apply(const QJsonArray &edits) override
    {
        applied.append(edits);
        return {int(edits.size()), {QStringLiteral("rewrote block 0")}, {}};
    }
    void undo() override { ++undos; }
};

QByteArray sse(const QString &text)
{
    const QJsonObject chunk{{QStringLiteral("choices"),
                             QJsonArray{QJsonObject{{QStringLiteral("delta"),
                                                     QJsonObject{{QStringLiteral("content"), text}}}}}}};
    return "data: " + QJsonDocument(chunk).toJson(QJsonDocument::Compact) + "\n\ndata: [DONE]\n\n";
}

} // namespace

class AssistantEditsTest : public QObject {
    Q_OBJECT
private slots:
    void protocolParses();
    void controllerAppliesAndUndoes();
};

void AssistantEditsTest::protocolParses()
{
    auto parsed = EditProtocol::parse(QStringLiteral(
        "Fixed the typo.\n```qinda-edits\n{\"edits\":[{\"op\":\"replace_block\",\"block\":0}]}\n```\nDone."));
    QVERIFY(parsed.edits.has_value());
    QCOMPARE(parsed.edits->size(), 1);
    QCOMPARE(parsed.prose, QStringLiteral("Fixed the typo.\n\nDone."));
    parsed = EditProtocol::parse(QStringLiteral("```qinda-edits\n[{\"op\":\"x\"}]\n```"));
    QCOMPARE(parsed.edits->size(), 1); // a bare array is accepted too
    parsed = EditProtocol::parse(QStringLiteral("Just an answer."));
    QVERIFY(!parsed.edits.has_value() && parsed.error.isEmpty());
    parsed = EditProtocol::parse(QStringLiteral("x\n```qinda-edits\n{\"edits\":[{\"op\""));
    QVERIFY(!parsed.edits.has_value());
    QVERIFY(parsed.error.contains(QStringLiteral("cut off")));
    FakeTools tools;
    const QString system = EditProtocol::systemPrompt(tools);
    QVERIFY(system.contains(QStringLiteral("```qinda-edits")) && system.contains(QStringLiteral("replace_block")));
}

void AssistantEditsTest::controllerAppliesAndUndoes()
{
    MockSseServer server;
    server.setScript({{sse(QStringLiteral("Fixed the spelling.\n```qinda-edits\n"
                                          "{\"edits\":[{\"op\":\"replace_block\",\"block\":0,\"text\":\"hello world\"}]}\n```")),
                       5}});
    QTemporaryDir dir;
    ModelCatalog catalog(dir.filePath(QStringLiteral("providers.json")));
    ProviderConfig row;
    row.id = QStringLiteral("mock");
    row.kind = QStringLiteral("openai");
    row.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port());
    row.model = QStringLiteral("m");
    catalog.addProvider(row);
    AssistantController controller(&catalog, nullptr, nullptr);
    controller.setModelId(QStringLiteral("mock/m"));
    QVERIFY(!controller.canEdit());
    FakeTools tools;
    controller.setDocumentTools(&tools);
    QVERIFY(controller.canEdit() && controller.editMode());

    QSignalSpy streaming(&controller, &AssistantController::streamingChanged);
    controller.send(QStringLiteral("fix the spelling"));
    QTRY_VERIFY(!controller.streaming() && streaming.size() >= 2);
    QCOMPARE(tools.snapshots, 1);
    QCOMPARE(tools.applied.size(), 1);
    QCOMPARE(tools.applied.first().first().toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("hello world"));
    QVERIFY(!controller.conversation().contains(QStringLiteral("qinda-edits")));
    QVERIFY(controller.conversation().contains(QStringLiteral("rewrote block 0")));
    QCOMPARE(controller.lastReply(), QStringLiteral("Fixed the spelling."));
    QCOMPARE(controller.lastEditSummary(), QStringLiteral("rewrote block 0"));
    controller.undoLastEdit();
    QCOMPARE(tools.undos, 1);
    QVERIFY(controller.lastEditSummary().isEmpty());

    // Edit mode off: a reply block is NOT applied (paste-only assistant).
    controller.setEditMode(false);
    controller.send(QStringLiteral("again"));
    QTRY_VERIFY(!controller.streaming() && !controller.lastReply().isEmpty());
    QTRY_VERIFY(controller.lastReply().contains(QStringLiteral("qinda-edits")));
    QCOMPARE(tools.applied.size(), 1);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QQO_NO_SECRET_SERVICE", "1");
    QGuiApplication app(argc, argv);
    AssistantEditsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_assistant_edits.moc"
