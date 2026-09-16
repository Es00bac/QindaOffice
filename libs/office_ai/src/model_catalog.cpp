// SPDX-License-Identifier: GPL-3.0-or-later
#include "model_catalog.h"

#include "anthropic_provider.h"
#include "cli_provider.h"
#include "http_chat_provider.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace QindaOffice::Ai {

ModelCatalog::ModelCatalog(QString path, QObject *parent)
    : QObject(parent)
    , m_path(std::move(path))
{
    QString error;
    if (load(&error)) {
        return;
    }
    // AGENT-GUARD: never seed ON TOP of an existing file. load() also fails
    // for a corrupt or unreadable providers.json, and the old unconditional
    // seed wrote over it — one JSON typo silently destroyed the user's
    // endpoints, models and secret-key ids. Move the bad file aside (same
    // contract as Core::JsonSettings) and seed a fresh one, so nothing is
    // lost and the picker is not left permanently empty.
    if (QFile::exists(m_path)) {
        const QString aside = m_path + QStringLiteral(".corrupt");
        QFile::remove(aside); // only ever an older quarantine of this file
        if (!QFile::rename(m_path, aside)) {
            qWarning().noquote() << "AI model catalog:" << m_path << "is unusable ("
                                 << error << ") and could not be moved aside; "
                                            "refusing to overwrite it";
            return;
        }
        qWarning().noquote() << "AI model catalog:" << m_path << "was unusable (" << error
                             << "); moved to" << aside << "and re-seeded";
    }
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning().noquote() << "AI model catalog: cannot seed" << m_path
                             << file.errorString();
        return;
    }
    file.write(seedDocument());
    file.close();
    static_cast<void>(load(nullptr));
}

QByteArray ModelCatalog::seedDocument()
{
    const QJsonArray providers = {
        QJsonObject{{QStringLiteral("id"), QStringLiteral("claude-cli")},
                    {QStringLiteral("displayName"), QStringLiteral("Claude (subscription CLI)")},
                    {QStringLiteral("kind"), QStringLiteral("claude-cli")},
                    {QStringLiteral("model"), QString()}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("codex-cli")},
                    {QStringLiteral("displayName"), QStringLiteral("Codex (subscription CLI)")},
                    {QStringLiteral("kind"), QStringLiteral("codex-cli")},
                    {QStringLiteral("model"), QString()}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("kimi-cli")},
                    {QStringLiteral("displayName"), QStringLiteral("Kimi (subscription CLI)")},
                    {QStringLiteral("kind"), QStringLiteral("kimi-cli")},
                    {QStringLiteral("model"), QString()}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("glm")},
                    {QStringLiteral("displayName"), QStringLiteral("GLM (api.z.ai)")},
                    {QStringLiteral("kind"), QStringLiteral("openai")},
                    {QStringLiteral("baseUrl"), QStringLiteral("https://api.z.ai/api/paas/v4")},
                    {QStringLiteral("model"), QStringLiteral("glm-4.6")},
                    {QStringLiteral("secretKey"), QStringLiteral("glm-api-key")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("moonshot")},
                    {QStringLiteral("displayName"), QStringLiteral("Moonshot (Kimi API)")},
                    {QStringLiteral("kind"), QStringLiteral("openai")},
                    {QStringLiteral("baseUrl"), QStringLiteral("https://api.moonshot.ai/v1")},
                    {QStringLiteral("model"), QStringLiteral("moonshot-v1-8k")},
                    {QStringLiteral("secretKey"), QStringLiteral("moonshot-api-key")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("local")},
                    {QStringLiteral("displayName"), QStringLiteral("Local (llama.cpp)")},
                    {QStringLiteral("kind"), QStringLiteral("openai")},
                    {QStringLiteral("baseUrl"), QStringLiteral("http://127.0.0.1:8080/v1")},
                    {QStringLiteral("model"), QStringLiteral("local-model")}},
    };
    return QJsonDocument(QJsonObject{{QStringLiteral("providers"), providers}}).toJson();
}

bool ModelCatalog::load(QString *error)
{
    QFile file(m_path);
    if (!file.exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    // Fail honestly: a corrupt file previously parsed to an empty root and
    // load() returned true, leaving a silently empty provider picker.
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 is not valid JSON").arg(m_path);
        }
        return false;
    }
    const QJsonObject root = document.object();
    m_providers.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("providers")).toArray()) {
        const QJsonObject object = value.toObject();
        ProviderConfig config;
        config.id = object.value(QStringLiteral("id")).toString();
        config.displayName = object.value(QStringLiteral("displayName")).toString();
        config.kind = object.value(QStringLiteral("kind")).toString();
        config.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
        config.model = object.value(QStringLiteral("model")).toString();
        config.secretKey = object.value(QStringLiteral("secretKey")).toString();
        config.extra = object.value(QStringLiteral("extra")).toObject();
        m_providers.append(config);
    }
    return true;
}

bool ModelCatalog::save(QString *error) const
{
    QJsonArray providers;
    for (const ProviderConfig &config : m_providers) {
        providers.append(QJsonObject{
            {QStringLiteral("id"), config.id},
            {QStringLiteral("displayName"), config.displayName},
            {QStringLiteral("kind"), config.kind},
            {QStringLiteral("baseUrl"), config.baseUrl},
            {QStringLiteral("model"), config.model},
            {QStringLiteral("secretKey"), config.secretKey},
            {QStringLiteral("extra"), config.extra},
        });
    }
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("providers"), providers}}).toJson());
    return true;
}

QStringList ModelCatalog::modelIds() const
{
    QStringList ids;
    for (const ProviderConfig &config : m_providers) {
        ids.append(config.id + QStringLiteral("/") + config.model);
    }
    return ids;
}

std::optional<ProviderConfig> ModelCatalog::configFor(const QString &modelId) const
{
    for (const ProviderConfig &config : m_providers) {
        if (modelId == config.id || modelId == config.id + QStringLiteral("/") + config.model) {
            return config;
        }
    }
    return std::nullopt;
}

std::optional<ModelSpec> ModelCatalog::specFor(const QString &modelId) const
{
    for (const ProviderConfig &config : m_providers) {
        if (modelId == config.id || modelId == config.id + QStringLiteral("/") + config.model) {
            ModelSpec spec;
            spec.id = config.id;
            spec.displayName = config.displayName.isEmpty() ? config.id : config.displayName;
            spec.providerId = config.id;
            spec.caps.streaming = true;
            spec.caps.vision = config.kind == QLatin1String("openai")
                || config.kind == QLatin1String("anthropic");
            spec.caps.jsonSchema = config.kind != QLatin1String("kimi-cli");
            spec.offMachine = config.kind == QLatin1String("openai")
                || config.kind == QLatin1String("anthropic");
            return spec;
        }
    }
    return std::nullopt;
}

ChatProvider *ModelCatalog::createProvider(const QString &modelId, const QString &secret,
                                           QObject *parent) const
{
    for (const ProviderConfig &config : m_providers) {
        if (modelId != config.id && modelId != config.id + QStringLiteral("/") + config.model) {
            continue;
        }
        if (config.kind == QLatin1String("claude-cli")) {
            return new CliChatProvider(CliKind::Claude, config.model, QString(), parent);
        }
        if (config.kind == QLatin1String("codex-cli")) {
            return new CliChatProvider(CliKind::Codex, config.model, QString(), parent);
        }
        if (config.kind == QLatin1String("kimi-cli")) {
            return new CliChatProvider(CliKind::Kimi, config.model, QString(), parent);
        }
        if (config.kind == QLatin1String("openai")) {
            return new OpenAiChatProvider(config.baseUrl, secret, config.model, parent);
        }
        if (config.kind == QLatin1String("anthropic")) {
            return new AnthropicProvider(config.baseUrl, secret, config.model, parent);
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace QindaOffice::Ai
