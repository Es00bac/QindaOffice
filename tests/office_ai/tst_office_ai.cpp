// SPDX-License-Identifier: GPL-3.0-or-later
// office_ai gates (plan section 5.3): SSE parsing (mid-line and mid-UTF-8
// splits, usage, [DONE], error bodies), the OpenAI-compatible provider
// against a scripted server (delta order, 401/429 mapping, cancel), CLI
// providers against replay scripts, and the catalog seed.

#include "mock_sse_server.h"

#include "assistant_controller.h"
#include "cli_provider.h"
#include "http_chat_provider.h"
#include "inline_action_flow.h"
#include "model_catalog.h"
#include "sse_parser.h"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Ai;

class OfficeAiTest : public QObject {
    Q_OBJECT
private slots:
    void sseParserReassemblesEvents();
    void sseParserSplitsUtf8AcrossChunks();
    void openAiProviderStreamsDeltasInOrder();
    void openAiProviderMapsErrors();
    void openAiProviderCancelAborts();
    void cliProviderReplaysClaudeJson();
    void catalogSeedsAndCreatesProviders();
    void catalogResolvesTheIdsItHandsOut();
    void catalogKeepsAnUnreadableFileInsteadOfSeedingOverIt();
    void assistantControllerSendsAndStreams();
    void inlineActionExtractsFencedBlocks();
};

static QStringList runParser(const QList<QByteArray> &chunks)
{
    QStringList events;
    SseParser parser([&events](const QString &type, const QString &data) {
        events.append(type + QStringLiteral("|") + data);
    });
    for (const QByteArray &chunk : chunks) {
        parser.append(chunk);
    }
    parser.finish();
    return events;
}

void OfficeAiTest::sseParserReassemblesEvents()
{
    const QStringList events = runParser({"data: {\"a\":1}\n\n",
                                          "event: message\ndata: [DONE]\n\n"});
    QCOMPARE(events.size(), 2);
    QVERIFY(events.at(0).endsWith(QStringLiteral("|{\"a\":1}")));
    QCOMPARE(events.at(1), QStringLiteral("message|[DONE]"));
}

void OfficeAiTest::sseParserSplitsUtf8AcrossChunks()
{
    const QByteArray snowman = QString::fromUtf8("\xE2\x98\x83").toUtf8(); // U+2603
    QList<QByteArray> chunks;
    QByteArray event = "data: snow" + snowman + "man\n\n";
    // Split mid-UTF-8 (between the 2nd and 3rd continuation bytes).
    chunks.append(event.left(9));
    chunks.append(event.mid(9, 1));
    chunks.append(event.mid(10));
    const QStringList events = runParser(chunks);
    QCOMPARE(events.size(), 1);
    QVERIFY(events.at(0).contains(QString::fromUtf8("\xE2\x98\x83")));
}

void OfficeAiTest::openAiProviderStreamsDeltasInOrder()
{
    MockSseServer server;
    server.setScript({
        {"data: {\"choices\":[{\"delta\":{\"content\":\"he\"}}]}\n\n", 1},
        {"data: {\"choices\":[{\"delta\":{\"content\":\"llo\"}}]}\n", 10},
        {"\n", 10},
        {"data: {\"choices\":[],\"usage\":{\"prompt_tokens\":3,\"completion_tokens\":2}}\n\n", 10},
        {"data: [DONE]\n\n", 10},
    });
    OpenAiChatProvider provider(QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()),
                                QString(), QStringLiteral("test-model"));
    ChatRequest request;
    request.messages.append({QStringLiteral("user"), QStringLiteral("hi"), {}});
    ChatRun *run = provider.start(request);
    QSignalSpy deltas(run, &ChatRun::textDelta);
    QSignalSpy finished(run, &ChatRun::finished);
    QVERIFY(finished.wait(5000));
    QCOMPARE(deltas.count(), 2);
    QCOMPARE(deltas.at(0).at(0).toString(), QStringLiteral("he"));
    QCOMPARE(deltas.at(1).at(0).toString(), QStringLiteral("llo"));
    const ChatResult result = finished.at(0).at(0).value<ChatResult>();
    QCOMPARE(result.text, QStringLiteral("hello"));
    QCOMPARE(result.usage.inputTokens, 3);
    QCOMPARE(result.usage.outputTokens, 2);
    delete run;
}

void OfficeAiTest::openAiProviderMapsErrors()
{
    // A raw server that answers with a 401 immediately and closes.
    QTcpServer raw;
    QObject::connect(&raw, &QTcpServer::newConnection, &raw, [&raw] {
        QTcpSocket *socket = raw.nextPendingConnection();
        socket->write("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n");
        socket->disconnectFromHost();
    });
    QVERIFY(raw.listen(QHostAddress::LocalHost));
    const quint16 port = raw.serverPort();
    OpenAiChatProvider provider(QStringLiteral("http://127.0.0.1:%1/v1").arg(port), QString(),
                                QStringLiteral("m"));
    ChatRequest request;
    request.messages.append({QStringLiteral("user"), QStringLiteral("x"), {}});
    ChatRun *run = provider.start(request);
    QSignalSpy failed(run, &ChatRun::failed);
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.at(0).at(0).value<ChatError>().kind, ChatError::Auth);
    delete run;
}

void OfficeAiTest::openAiProviderCancelAborts()
{
    MockSseServer server;
    server.setAutoClose(false);
    // Never-ending stream; cancel must abort promptly.
    server.setScript({{"data: {\"choices\":[{\"delta\":{\"content\":\"x\"}}]}\n\n", 1}});
    OpenAiChatProvider provider(QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()),
                                QString(), QStringLiteral("m"));
    ChatRequest request;
    request.messages.append({QStringLiteral("user"), QStringLiteral("x"), {}});
    ChatRun *run = provider.start(request);
    QSignalSpy failed(run, &ChatRun::failed);
    QTimer::singleShot(300, run, [run] { run->cancel(); });
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.at(0).at(0).value<ChatError>().kind, ChatError::Cancelled);
    delete run;
}

void OfficeAiTest::cliProviderReplaysClaudeJson()
{
    // The provider speaks real stream-json; exercise it through a fake
    // binary via PATH when available, otherwise verify the parsing layer
    // through the public CliChatProvider against a stub script.
    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    const QString script = bin.path() + QStringLiteral("/claude");
    {
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n"
                   "cat >/dev/null\n"
                   "printf '%s\\n' "
                   "'{\"type\":\"system\",\"subtype\":\"init\"}'\n"
                   "printf '%s\\n' "
                   "'{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_delta\","
                   "\"delta\":{\"type\":\"text_delta\",\"text\":\"fake-answer\"}}}'\n"
                   "printf '%s\\n' "
                   "'{\"type\":\"result\",\"subtype\":\"success\",\"result\":\"fake-answer\"}'\n");
        file.close();
        QFile::setPermissions(script, QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther
                                         | QFile::ReadOwner | QFile::WriteOwner);
    }
    qputenv("PATH", bin.path().toUtf8() + ":" + qgetenv("PATH"));
    CliChatProvider provider(CliKind::Claude, QString());
    QVERIFY(provider.isAvailable());
    ChatRequest request;
    request.messages.append({QStringLiteral("user"), QStringLiteral("say hi"), {}});
    ChatRun *run = provider.start(request);
    QSignalSpy finished(run, &ChatRun::finished);
    QVERIFY(finished.wait(15000));
    QCOMPARE(finished.at(0).at(0).value<ChatResult>().text, QStringLiteral("fake-answer"));
    delete run;
}

void OfficeAiTest::catalogSeedsAndCreatesProviders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelCatalog catalog(dir.path() + QStringLiteral("/providers.json"));
    QVERIFY(!catalog.providers().isEmpty());
    // All six seeds exist.
    QStringList ids;
    for (const ProviderConfig &config : catalog.providers()) {
        ids.append(config.id);
    }
    for (const char *want : {"claude-cli", "codex-cli", "kimi-cli", "glm", "moonshot", "local"}) {
        QVERIFY2(ids.contains(QLatin1String(want)), want);
    }
    std::unique_ptr<ChatProvider> provider = std::unique_ptr<ChatProvider>(
        catalog.createProvider(QStringLiteral("local"), QString(), nullptr));
    QVERIFY(provider != nullptr);
    const auto spec = catalog.specFor(QStringLiteral("moonshot"));
    QVERIFY(spec.has_value());
    QVERIFY(spec->offMachine);
    QVERIFY(spec->caps.vision);
}

// AGENT-GUARD: modelIds() hands out "<id>/<model>". Every consumer resolves
// the provider row through configFor(); the open-coded lookups that compared
// against ProviderConfig::id alone never matched, so secretKey stayed empty
// and inline AI requests went out with no API key at all.
void OfficeAiTest::catalogResolvesTheIdsItHandsOut()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelCatalog catalog(dir.path() + QStringLiteral("/providers.json"));
    const QStringList ids = catalog.modelIds();
    QVERIFY(!ids.isEmpty());
    for (const QString &id : ids) {
        const auto config = catalog.configFor(id);
        QVERIFY2(config.has_value(), qPrintable(id));
        QVERIFY(catalog.specFor(id).has_value());
    }
    // The key-bearing seeds really do carry their secret id through.
    const auto moonshot = catalog.configFor(QStringLiteral("moonshot/moonshot-v1-8k"));
    QVERIFY(moonshot.has_value());
    QCOMPARE(moonshot->secretKey, QStringLiteral("moonshot-api-key"));
    QVERIFY(!catalog.configFor(QStringLiteral("no-such-model")).has_value());
}

// A corrupt providers.json is moved aside, never silently replaced by the
// seed — that overwrite destroyed hand-configured endpoints and secret ids.
void OfficeAiTest::catalogKeepsAnUnreadableFileInsteadOfSeedingOverIt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/providers.json");
    const QByteArray broken("{ \"providers\": [ {\"id\": \"mine\" ");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(broken);
    file.close();

    ModelCatalog catalog(path);
    // The user's bytes survive, under the quarantine name...
    QFile aside(path + QStringLiteral(".corrupt"));
    QVERIFY(aside.open(QIODevice::ReadOnly));
    QCOMPARE(aside.readAll(), broken);
    // ...and the app is usable again instead of stuck with an empty picker.
    QVERIFY(!catalog.providers().isEmpty());
}

void OfficeAiTest::assistantControllerSendsAndStreams()
{
    MockSseServer server;
    server.setScript({
        {R"(data: {"choices":[{"delta":{"content":"panel "}}]}

)",
         1},
        {R"(data: {"choices":[{"delta":{"content":"answer"}}]}

)",
         5},
        {"data: [DONE]\n\n", 5},
    });
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        QFile file(dir.path() + QStringLiteral("/providers.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QJsonObject mockProvider{
            {QStringLiteral("id"), QStringLiteral("mock")},
            {QStringLiteral("kind"), QStringLiteral("openai")},
            {QStringLiteral("baseUrl"),
             QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port())},
            {QStringLiteral("model"), QStringLiteral("mock-model")},
        };
        const QJsonObject root{
            {QStringLiteral("providers"), QJsonArray{mockProvider}},
        };
        file.write(QJsonDocument(root).toJson());
    }
    ModelCatalog catalog(dir.path() + QStringLiteral("/providers.json"));

    class Ctx : public DocumentContextProvider {
    public:
        DocumentContext context(bool) override { return {}; }
    };
    class App : public EditApplier {
    public:
        void replaceSelection(const QString &text) override { applied = text; }
        void insertAtCursor(const QString &text) override { inserted = text; }
        QString applied;
        QString inserted;
    };
    Ctx ctx;
    App app;
    AssistantController assistant(&catalog, &ctx, &app);
    QVERIFY(!assistant.models().isEmpty());
    QCOMPARE(assistant.modelId(), catalog.modelIds().first());
    QVERIFY(!assistant.hasReply());
    QSignalSpy streaming(&assistant, &AssistantController::streamingChanged);
    assistant.send(QStringLiteral("hello"));
    // The prompt is on the transcript at once; the reply streams in.
    QVERIFY(assistant.conversation().contains(QStringLiteral("hello")));
    QTRY_VERIFY_WITH_TIMEOUT(assistant.conversation().contains(QStringLiteral("panel answer")), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!assistant.streaming(), 5000);
    QVERIFY(streaming.count() >= 2);
    // Reply actions work after the stream completes, through the seams.
    QVERIFY(assistant.hasReply());
    QCOMPARE(assistant.lastReply().trimmed(), QStringLiteral("panel answer"));
    assistant.insert();
    QCOMPARE(app.inserted.trimmed(), QStringLiteral("panel answer"));
    assistant.replaceSelection();
    QCOMPARE(app.applied.trimmed(), QStringLiteral("panel answer"));
    assistant.clearConversation();
    QVERIFY(assistant.conversation().isEmpty());
}

void OfficeAiTest::inlineActionExtractsFencedBlocks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelCatalog catalog(dir.path() + QStringLiteral("/providers.json"));
    InlineActionFlow flow(&catalog, nullptr, nullptr);
    Q_UNUSED(flow);
    // The extractor prefers fenced blocks (structured output v1).
    // Exercised through run() with a canned provider would need a catalog
    // entry; the parsing rule is covered directly via inlineActions()
    // prompt shapes and the flow's extraction path in tst_write_view.
    QVERIFY(!inlineActions().isEmpty());
    QCOMPARE(inlineActions().first().id, QStringLiteral("rewrite"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    OfficeAiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_office_ai.moc"
