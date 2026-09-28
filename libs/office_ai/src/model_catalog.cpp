// SPDX-License-Identifier: GPL-3.0-or-later
#include "model_catalog.h"

#include "anthropic_provider.h"
#include "cli_provider.h"
#include "http_chat_provider.h"

#include <QDebug>
#include <QUrl>
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
        m_providers.append(ProviderConfig::fromJson(value.toObject()));
    }
    return true;
}

bool ModelCatalog::save(QString *error) const
{
    QJsonArray providers;
    for (const ProviderConfig &config : m_providers) {
        providers.append(config.toJson());
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
            spec.offMachine = isOffMachine(config);
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
            auto *provider = new OpenAiChatProvider(config.baseUrl, secret, config.model, parent);
            OpenAiOptions options;
            const QJsonObject o = config.options;
            if (o.contains(QStringLiteral("maxTokensField"))) {
                options.maxTokensField = o.value(QStringLiteral("maxTokensField")).toString();
            }
            options.sendTemperature = o.value(QStringLiteral("sendTemperature")).toBool(true);
            options.extraBody = o.value(QStringLiteral("body")).toObject();
            const QJsonObject headers = o.value(QStringLiteral("headers")).toObject();
            for (auto it = headers.begin(); it != headers.end(); ++it) {
                options.headers.insert(it.key().toUtf8(), it.value().toString().toUtf8());
            }
            provider->setOptions(options);
            return provider;
        }
        if (config.kind == QLatin1String("anthropic")) {
            return new AnthropicProvider(config.baseUrl, secret, config.model, parent);
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace QindaOffice::Ai

namespace QindaOffice::Ai {

QJsonObject ProviderConfig::toJson() const
{
    QJsonObject object{
        {QStringLiteral("id"), id},
        {QStringLiteral("displayName"), displayName},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("baseUrl"), baseUrl},
        {QStringLiteral("model"), model},
        {QStringLiteral("secretKey"), secretKey},
        {QStringLiteral("extra"), extra},
    };
    // The D-057 fields are written only when set, so a file written by
    // this build still reads cleanly in an older one.
    if (!apiKeyRef.isEmpty()) {
        object.insert(QStringLiteral("apiKey"), apiKeyRef);
    }
    if (!env.isEmpty()) {
        object.insert(QStringLiteral("env"), QJsonArray::fromStringList(env));
    }
    if (!openCodeId.isEmpty()) {
        object.insert(QStringLiteral("openCodeId"), openCodeId);
    }
    if (!options.isEmpty()) {
        object.insert(QStringLiteral("options"), options);
    }
    return object;
}

ProviderConfig ProviderConfig::fromJson(const QJsonObject &object)
{
    ProviderConfig config;
    config.id = object.value(QStringLiteral("id")).toString();
    config.displayName = object.value(QStringLiteral("displayName")).toString();
    config.kind = object.value(QStringLiteral("kind")).toString();
    config.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
    config.model = object.value(QStringLiteral("model")).toString();
    config.secretKey = object.value(QStringLiteral("secretKey")).toString();
    config.extra = object.value(QStringLiteral("extra")).toObject();
    // AGENT-GUARD (D-052): only a reference is ever honoured here; a
    // literal key typed into the file is ignored rather than sent.
    const QString ref = object.value(QStringLiteral("apiKey")).toString();
    config.apiKeyRef = ApiKeyResolver::isReference(ref) ? ref : QString();
    for (const QJsonValue &value : object.value(QStringLiteral("env")).toArray()) {
        config.env.append(value.toString());
    }
    config.openCodeId = object.value(QStringLiteral("openCodeId")).toString();
    config.options = object.value(QStringLiteral("options")).toObject();
    return config;
}

KeySources ModelCatalog::keySources(const ProviderConfig &config)
{
    KeySources sources;
    sources.keyringKey = config.secretKey;
    sources.reference = config.apiKeyRef;
    sources.envVars = config.env;
    sources.openCodeId = config.openCodeId;
    return sources;
}

bool ModelCatalog::isOffMachine(const ProviderConfig &config)
{
    if (config.kind != QLatin1String("openai") && config.kind != QLatin1String("anthropic")) {
        return false; // the CLI providers are the user's own subscription tools
    }
    const QString host = QUrl(config.baseUrl).host();
    return host != QLatin1String("localhost") && host != QLatin1String("127.0.0.1")
        && host != QLatin1String("::1");
}

QString ModelCatalog::addProvider(ProviderConfig config)
{
    // Rows of one provider share its keyring id (the preset's
    // "<provider>-api-key"), so a second model needs no second key.
    const QString base = config.id.isEmpty() ? QStringLiteral("provider") : config.id;
    QString id = base;
    for (int n = 2; configFor(id).has_value(); ++n) {
        id = QStringLiteral("%1-%2").arg(base).arg(n);
    }
    config.id = id;
    m_providers.append(config);
    emit changed();
    return id;
}

bool ModelCatalog::updateProvider(const ProviderConfig &config)
{
    for (ProviderConfig &existing : m_providers) {
        if (existing.id == config.id) {
            existing = config;
            emit changed();
            return true;
        }
    }
    return false;
}

bool ModelCatalog::removeProvider(const QString &id)
{
    if (m_providers.removeIf([&id](const ProviderConfig &c) { return c.id == id; }) == 0) {
        return false;
    }
    emit changed();
    return true;
}

} // namespace QindaOffice::Ai
