// SPDX-License-Identifier: GPL-3.0-or-later
#include "provider_settings.h"

#include "http_chat_provider.h"
#include "provider_presets.h"
#include "secret_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

namespace QindaOffice::Ai {

ProviderSettings::ProviderSettings(ModelCatalog *catalog, QObject *parent)
    : QObject(parent)
    , m_catalog(catalog)
{
    connect(m_catalog, &ModelCatalog::changed, this, &ProviderSettings::providersChanged);
}

QVariantList ProviderSettings::providers() const
{
    QVariantList list;
    for (const ProviderConfig &config : m_catalog->providers()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), config.id},
            {QStringLiteral("name"), config.displayName.isEmpty() ? config.id : config.displayName},
            {QStringLiteral("kind"), config.kind},
            {QStringLiteral("baseUrl"), config.baseUrl},
            {QStringLiteral("model"), config.model},
            {QStringLiteral("apiKeyRef"), config.apiKeyRef},
            {QStringLiteral("env"), config.env.join(QStringLiteral(", "))},
            {QStringLiteral("offMachine"), ModelCatalog::isOffMachine(config)},
            {QStringLiteral("usesKey"), config.kind == QLatin1String("openai")
                                            || config.kind == QLatin1String("anthropic")},
            {QStringLiteral("body"),
             config.options.contains(QStringLiteral("body"))
                 ? QString::fromUtf8(QJsonDocument(config.options.value(QStringLiteral("body"))
                                                       .toObject())
                                         .toJson(QJsonDocument::Compact))
                 : QString()},
        });
    }
    return list;
}

QVariantList ProviderSettings::presets() const
{
    QVariantList list;
    for (const ProviderPreset &preset : ProviderPresets::all()) {
        QVariantList models;
        for (const PresetModel &model : preset.models) {
            models.append(QVariantMap{{QStringLiteral("id"), model.id},
                                      {QStringLiteral("name"), model.name},
                                      {QStringLiteral("context"), model.context}});
        }
        list.append(QVariantMap{{QStringLiteral("id"), preset.id},
                                {QStringLiteral("name"), preset.name},
                                {QStringLiteral("kind"), preset.kind},
                                {QStringLiteral("baseUrl"), preset.baseUrl},
                                {QStringLiteral("env"), preset.env.join(QStringLiteral(", "))},
                                {QStringLiteral("doc"), preset.doc},
                                {QStringLiteral("hint"), preset.hint},
                                {QStringLiteral("local"), preset.local},
                                {QStringLiteral("models"), models}});
    }
    return list;
}

void ProviderSettings::save()
{
    QString error;
    if (!m_catalog->save(&error)) {
        emit saveFailed(error);
    }
}

QString ProviderSettings::addPreset(const QString &presetId, const QString &model)
{
    const auto preset = ProviderPresets::find(presetId);
    if (!preset.has_value()) {
        return {};
    }
    const QString id = m_catalog->addProvider(preset->toConfig(model.trimmed()));
    save();
    return id;
}

bool ProviderSettings::applyFields(ProviderConfig *config, const QVariantMap &fields,
                                   QString *error) const
{
    const auto text = [&fields](const char *key) {
        return fields.value(QLatin1String(key)).toString().trimmed();
    };
    if (fields.contains(QStringLiteral("name"))) {
        config->displayName = text("name");
    }
    if (fields.contains(QStringLiteral("kind"))) {
        config->kind = text("kind");
    }
    if (fields.contains(QStringLiteral("baseUrl"))) {
        config->baseUrl = text("baseUrl");
        while (config->baseUrl.endsWith(QLatin1Char('/'))) {
            config->baseUrl.chop(1);
        }
    }
    if (fields.contains(QStringLiteral("model"))) {
        config->model = text("model");
    }
    if (fields.contains(QStringLiteral("apiKeyRef"))) {
        const QString ref = ApiKeyResolver::normalizeReference(text("apiKeyRef"));
        if (!ref.isEmpty() && !ApiKeyResolver::isReference(ref)) {
            *error = tr("Only {env:NAME}, {file:PATH} or a file path goes here; paste a key "
                        "into the key field instead, where it is kept in the keyring.");
            return false;
        }
        config->apiKeyRef = ref;
    }
    if (fields.contains(QStringLiteral("body"))) {
        const QString body = text("body");
        if (body.isEmpty()) {
            config->options.remove(QStringLiteral("body"));
        } else {
            const QJsonDocument parsed = QJsonDocument::fromJson(body.toUtf8());
            if (!parsed.isObject()) {
                *error = tr("Extra request fields must be a JSON object.");
                return false;
            }
            config->options.insert(QStringLiteral("body"), parsed.object());
        }
    }
    return true;
}

QString ProviderSettings::addCustom(const QVariantMap &fields)
{
    ProviderConfig config;
    config.id = fields.value(QStringLiteral("id"), QStringLiteral("custom")).toString();
    config.kind = QStringLiteral("openai");
    QString error;
    if (!applyFields(&config, fields, &error)) {
        emit saveFailed(error);
        return {};
    }
    config.secretKey = config.id + QStringLiteral("-api-key");
    const QString id = m_catalog->addProvider(config);
    save();
    return id;
}

bool ProviderSettings::update(const QString &id, const QVariantMap &fields)
{
    auto config = m_catalog->configFor(id);
    if (!config.has_value()) {
        return false;
    }
    QString error;
    if (!applyFields(&*config, fields, &error)) {
        emit saveFailed(error);
        return false;
    }
    m_catalog->updateProvider(*config);
    save();
    return true;
}

void ProviderSettings::remove(const QString &id)
{
    if (m_catalog->removeProvider(id)) {
        save();
    }
}

void ProviderSettings::setApiKey(const QString &id, const QString &key)
{
    auto config = m_catalog->configFor(id);
    if (!config.has_value() || key.trimmed().isEmpty()) {
        return;
    }
    if (config->secretKey.isEmpty()) {
        config->secretKey = config->id + QStringLiteral("-api-key");
        m_catalog->updateProvider(*config);
        save();
    }
    // AGENT-GUARD: the store outlives the asynchronous keyring job (its
    // parent); deleting it right away cancelled the write.
    auto *store = new SecretStore(this);
    QPointer<ProviderSettings> self(this);
    store->store(config->secretKey, key.trimmed(), [self, store, id](bool ok) {
        store->deleteLater();
        if (self == nullptr) {
            return;
        }
        if (!ok) {
            emit self->saveFailed(tr("The keyring refused to store the key."));
        }
        self->checkKey(id);
    });
}

void ProviderSettings::checkKey(const QString &id)
{
    const auto config = m_catalog->configFor(id);
    if (!config.has_value()) {
        return;
    }
    QPointer<ProviderSettings> self(this);
    ApiKeyResolver::resolve(ModelCatalog::keySources(*config), this,
                            [self, id](const QString &key, const QString &origin) {
                                if (self != nullptr) {
                                    emit self->keyStatus(id, key.isEmpty() ? QString() : origin);
                                }
                            });
}

void ProviderSettings::test(const QString &id)
{
    const auto config = m_catalog->configFor(id);
    if (!config.has_value()) {
        return;
    }
    QPointer<ProviderSettings> self(this);
    const ProviderConfig row = *config;
    ApiKeyResolver::resolve(ModelCatalog::keySources(row), this,
                            [self, row](const QString &key, const QString &) {
        if (self == nullptr) {
            return;
        }
        if (row.kind == QLatin1String("openai")) {
            auto *provider = new OpenAiChatProvider(row.baseUrl, key, row.model, self);
            provider->fetchModels([self, row, provider](const QStringList &models,
                                                         const QString &error) {
                provider->deleteLater();
                if (self == nullptr) {
                    return;
                }
                if (!error.isEmpty()) {
                    emit self->testResult(row.id, false, tr("%1 answered %2").arg(row.baseUrl, error),
                                          {});
                    return;
                }
                const bool listed = models.contains(row.model);
                emit self->testResult(row.id, true,
                                      listed ? tr("Connected; %1 is available.").arg(row.model)
                                             : tr("Connected; %n model(s) listed.", "",
                                                  int(models.size())),
                                      models);
            });
            return;
        }
        ChatProvider *provider = self->m_catalog->createProvider(row.id, key, self);
        if (provider == nullptr) {
            emit self->testResult(row.id, false, tr("This kind of provider cannot be tested."), {});
            return;
        }
        ChatRequest request;
        request.maxTokens = 16;
        request.messages.append({QStringLiteral("user"), QStringLiteral("Reply with: ok"), {}});
        ChatRun *run = provider->start(request);
        connect(run, &ChatRun::finished, self, [self, row, run, provider](const ChatResult &) {
            run->deleteLater();
            provider->deleteLater();
            emit self->testResult(row.id, true, tr("Connected; %1 answered.").arg(row.model), {});
        });
        connect(run, &ChatRun::failed, self, [self, row, run, provider](const ChatError &error) {
            run->deleteLater();
            provider->deleteLater();
            emit self->testResult(row.id, false, error.message, {});
        });
    });
}

int ProviderSettings::importOpenCode()
{
    int added = 0;
    for (const ProviderConfig &row :
         ModelCatalog::openCodeProviders(ModelCatalog::defaultOpenCodeConfigs())) {
        bool known = false;
        for (const ProviderConfig &existing : m_catalog->providers()) {
            known = known || (existing.baseUrl == row.baseUrl && existing.model == row.model);
        }
        if (!known) {
            m_catalog->addProvider(row);
            ++added;
        }
    }
    if (added > 0) {
        save();
    }
    return added;
}

} // namespace QindaOffice::Ai
