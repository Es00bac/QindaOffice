// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Model catalog (plan section 5.3): providers.json under the suite config
// dir, seeded with Claude CLI, Codex CLI, Kimi CLI, GLM, Moonshot and a
// local llama.cpp endpoint. Secrets are referenced by key id and resolved
// through ApiKeyResolver at connection time, never stored here (D-052).
//
// D-057 borrowed from OpenCode: a row may carry an `apiKey` REFERENCE
// ("{env:NAME}" / "{file:PATH}", never a literal key), the provider's
// conventional `env` variable names, and wire `options` (see
// OpenAiOptions). Rows are added from the compiled-in presets
// (provider_presets.h, distilled from models.dev) or imported from the
// user's opencode.json.

#include "api_key_resolver.h"
#include "chat_types.h"

#include <QJsonObject>
#include <QStringList>

namespace QindaOffice::Ai {

struct ProviderConfig {
    QString id;
    QString displayName;
    QString kind; // "claude-cli" | "codex-cli" | "kimi-cli" | "openai" | "anthropic"
    QString baseUrl;
    QString model;
    QString secretKey; // key id in SecretStore; empty for CLI providers
    QJsonObject extra;
    QString apiKeyRef;   // "{env:X}" / "{file:path}" (ApiKeyResolver)
    QStringList env;     // conventional key variables, e.g. ANTHROPIC_API_KEY
    QString openCodeId;  // provider id in OpenCode's auth.json
    // {"maxTokensField", "sendTemperature", "body": {...}, "headers": {...}}
    QJsonObject options;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static ProviderConfig fromJson(const QJsonObject &object);
};

class ModelCatalog : public QObject {
    Q_OBJECT
public:
    explicit ModelCatalog(QString path, QObject *parent = nullptr);

    [[nodiscard]] QList<ProviderConfig> providers() const { return m_providers; }
    [[nodiscard]] QStringList modelIds() const;
    [[nodiscard]] std::optional<ModelSpec> specFor(const QString &modelId) const;
    // AGENT-CONTRACT: the only supported way to map a model id back to its
    // provider row. modelIds() hands out "<id>/<model>", so a caller that
    // compares against ProviderConfig::id alone never matches and silently
    // ends up with a default-constructed config (empty secretKey -> the API
    // key is never fetched -> the request goes out unauthenticated).
    [[nodiscard]] std::optional<ProviderConfig> configFor(const QString &modelId) const;

    // Builds a ready provider for the model id; the secret is resolved
    // synchronously from `secret` when the kind needs one (empty -> caller
    // cancelled). Returns nullptr when unavailable.
    [[nodiscard]] ChatProvider *createProvider(const QString &modelId, const QString &secret,
                                               QObject *parent) const;

    // Where this row's key comes from (keyring id, reference, env, …).
    [[nodiscard]] static KeySources keySources(const ProviderConfig &config);
    // True when requests to this row leave the machine (the badge).
    [[nodiscard]] static bool isOffMachine(const ProviderConfig &config);

    // Editing (the provider settings dialog). addProvider makes the id
    // unique and returns it; nothing is saved until save().
    QString addProvider(ProviderConfig config);
    bool updateProvider(const ProviderConfig &config);
    bool removeProvider(const QString &id);

    // Rows for the providers the user configured in OpenCode:
    // opencode.json(c) custom providers (baseURL + models) and every
    // auth.json API key whose provider is one of our presets.
    [[nodiscard]] static QList<ProviderConfig> openCodeProviders(const QStringList &configPaths);
    [[nodiscard]] static QStringList defaultOpenCodeConfigs();

    [[nodiscard]] bool load(QString *error = nullptr);
    [[nodiscard]] bool save(QString *error = nullptr) const;
    [[nodiscard]] QString path() const { return m_path; }

    // Seed document written when none exists (plan section 5.3).
    [[nodiscard]] static QByteArray seedDocument();

signals:
    // Rows were added, edited or removed (pickers rebuild their lists).
    void changed();

private:
    QString m_path;
    QList<ProviderConfig> m_providers;
};

} // namespace QindaOffice::Ai
