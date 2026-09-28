// SPDX-License-Identifier: GPL-3.0-or-later
// The provider plumbing borrowed from OpenCode (D-057): key references,
// env variables and OpenCode's auth.json, the models.dev presets, the
// catalog's new fields, and the opencode.json(c) import.

#include "api_key_resolver.h"
#include "model_catalog.h"
#include "provider_presets.h"

#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Ai;

class AiProvidersTest : public QObject {
    Q_OBJECT
private slots:
    void keyReferencesEnvAndOpenCode();
    void presetsLoadFromTheResource();
    void catalogRoundTripsTheNewFields();
    void openCodeConfigsImport();
};

void AiProvidersTest::keyReferencesEnvAndOpenCode()
{
    QTemporaryDir dir;
    QFile keyFile(dir.filePath(QStringLiteral("api-key")));
    QVERIFY(keyFile.open(QIODevice::WriteOnly));
    keyFile.write("file-secret\n");
    keyFile.close();
    QCOMPARE(ApiKeyResolver::fromReference(QStringLiteral("{file:%1}").arg(keyFile.fileName())),
             QStringLiteral("file-secret"));
    qputenv("QQO_TEST_KEY", "env-secret");
    QCOMPARE(ApiKeyResolver::fromReference(QStringLiteral("{env:QQO_TEST_KEY}")), QStringLiteral("env-secret"));
    QVERIFY(ApiKeyResolver::fromReference(QStringLiteral("sk-literal")).isEmpty());
    QVERIFY(!ApiKeyResolver::isReference(QStringLiteral("sk-literal")));
    QString which;
    QCOMPARE(ApiKeyResolver::fromEnvironment({QStringLiteral("QQO_UNSET_KEY"), QStringLiteral("QQO_TEST_KEY")}, &which),
             QStringLiteral("env-secret"));
    QCOMPARE(which, QStringLiteral("QQO_TEST_KEY"));

    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("opencode"))));
    QFile auth(dir.filePath(QStringLiteral("opencode/auth.json")));
    QVERIFY(auth.open(QIODevice::WriteOnly));
    auth.write(R"({"openrouter":{"type":"api","key":"or-secret"},"anthropic":{"type":"oauth","access":"x"}})");
    auth.close();
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    QCOMPARE(ApiKeyResolver::fromOpenCode(QStringLiteral("openrouter")), QStringLiteral("or-secret"));
    QVERIFY(ApiKeyResolver::fromOpenCode(QStringLiteral("anthropic")).isEmpty()); // oauth is not a key

    // The chain: no keyring id → reference → env → OpenCode.
    KeySources sources;
    sources.envVars = {QStringLiteral("QQO_UNSET_KEY")};
    sources.openCodeId = QStringLiteral("openrouter");
    QString got;
    QString origin;
    ApiKeyResolver::resolve(sources, this, [&](const QString &key, const QString &from) {
        got = key;
        origin = from;
    });
    QCOMPARE(got, QStringLiteral("or-secret"));
    QCOMPARE(origin, QStringLiteral("OpenCode"));
    qunsetenv("QQO_TEST_KEY");
}

void AiProvidersTest::presetsLoadFromTheResource()
{
    const auto anthropic = ProviderPresets::find(QStringLiteral("anthropic"));
    QVERIFY(anthropic.has_value());
    QCOMPARE(anthropic->kind, QStringLiteral("anthropic"));
    QCOMPARE(anthropic->baseUrl, QStringLiteral("https://api.anthropic.com"));
    QVERIFY(anthropic->env.contains(QStringLiteral("ANTHROPIC_API_KEY")));
    QVERIFY(!anthropic->models.isEmpty());
    for (const char *id : {"openai", "moonshotai", "zai", "deepseek", "openrouter", "google", "llama-server"}) {
        QVERIFY2(ProviderPresets::find(QString::fromLatin1(id)).has_value(), id);
    }
    const ProviderConfig openai = ProviderPresets::find(QStringLiteral("openai"))->toConfig();
    QCOMPARE(openai.options.value(QStringLiteral("maxTokensField")).toString(),
             QStringLiteral("max_completion_tokens"));
    QCOMPARE(openai.secretKey, QStringLiteral("openai-api-key"));
    QVERIFY(ProviderPresets::find(QStringLiteral("llama-server"))->toConfig().secretKey.isEmpty());
}

void AiProvidersTest::catalogRoundTripsTheNewFields()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("providers.json"));
    {
        ModelCatalog catalog(path);
        ProviderConfig row;
        row.id = QStringLiteral("ornith");
        row.kind = QStringLiteral("openai");
        row.baseUrl = QStringLiteral("http://qinda.example:8100/v1");
        row.model = QStringLiteral("ornith");
        row.apiKeyRef = QStringLiteral("{file:~/.config/qinda-llm/api-key}");
        row.options.insert(QStringLiteral("body"),
                           QJsonObject{{QStringLiteral("chat_template_kwargs"),
                                        QJsonObject{{QStringLiteral("enable_thinking"), false}}}});
        QSignalSpy changed(&catalog, &ModelCatalog::changed);
        QCOMPARE(catalog.addProvider(row), QStringLiteral("ornith"));
        QCOMPARE(catalog.addProvider(row), QStringLiteral("ornith-2")); // unique ids
        QCOMPARE(changed.size(), 2);
        QVERIFY(catalog.save());
    }
    ModelCatalog reloaded(path);
    const auto row = reloaded.configFor(QStringLiteral("ornith"));
    QVERIFY(row.has_value());
    QCOMPARE(row->apiKeyRef, QStringLiteral("{file:~/.config/qinda-llm/api-key}"));
    QVERIFY(row->options.value(QStringLiteral("body")).toObject().contains(QStringLiteral("chat_template_kwargs")));
    QVERIFY(ModelCatalog::isOffMachine(*row));
    ProviderConfig local = *row;
    local.baseUrl = QStringLiteral("http://127.0.0.1:8080/v1");
    QVERIFY(!ModelCatalog::isOffMachine(local));
    // A literal key in the file is never honoured (D-052).
    const ProviderConfig literal = ProviderConfig::fromJson(QJsonObject{{QStringLiteral("apiKey"), QStringLiteral("sk-123")}});
    QVERIFY(literal.apiKeyRef.isEmpty());
    QVERIFY(reloaded.removeProvider(QStringLiteral("ornith-2")));
}

void AiProvidersTest::openCodeConfigsImport()
{
    QTemporaryDir dir;
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("qinda-llm"))));
    QFile config(dir.filePath(QStringLiteral("qinda-llm/opencode.json")));
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write(R"({
  // JSONC: comments and trailing commas are fine
  "$schema": "https://opencode.ai/config.json",
  "provider": {
    "qinda": {
      "npm": "@ai-sdk/openai-compatible",
      "name": "Qinda llama-server",
      "options": { "baseURL": "http://qinda.example:8100/v1", "apiKey": "{file:api-key}", },
      "models": { "ornith": { "name": "Ornith 1.5 35B-A3B" } },
    },
    "anthropic": { "options": { "timeout": 5 } }
  }
})");
    config.close();
    qputenv("XDG_DATA_HOME", dir.filePath(QStringLiteral("nothing")).toUtf8());
    const QList<ProviderConfig> rows = ModelCatalog::openCodeProviders({config.fileName()});
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().baseUrl, QStringLiteral("http://qinda.example:8100/v1"));
    QCOMPARE(rows.first().model, QStringLiteral("ornith"));
    QCOMPARE(rows.first().displayName, QStringLiteral("Qinda llama-server — Ornith 1.5 35B-A3B"));
    // A relative {file:} is resolved against the config's directory.
    QCOMPARE(rows.first().apiKeyRef,
             QStringLiteral("{file:%1}").arg(dir.filePath(QStringLiteral("qinda-llm/api-key"))));
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    QVERIFY(ModelCatalog::defaultOpenCodeConfigs().contains(config.fileName()));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QQO_NO_SECRET_SERVICE", "1"); // never touch the real keyring
    QCoreApplication app(argc, argv);
    AiProvidersTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_ai_providers.moc"
