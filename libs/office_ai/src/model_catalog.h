// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Model catalog (plan section 5.3): providers.json under the suite config
// dir, seeded with Claude CLI, Codex CLI, Kimi CLI, GLM, Moonshot and a
// local llama.cpp endpoint. Secrets are referenced by key id and resolved
// through SecretStore at connection time, never stored here.

#include "chat_types.h"

#include <QJsonObject>

namespace QindaOffice::Ai {

struct ProviderConfig {
    QString id;
    QString displayName;
    QString kind; // "claude-cli" | "codex-cli" | "kimi-cli" | "openai" | "anthropic"
    QString baseUrl;
    QString model;
    QString secretKey; // key id in SecretStore; empty for CLI providers
    QJsonObject extra;
};

class ModelCatalog : public QObject {
    Q_OBJECT
public:
    explicit ModelCatalog(QString path, QObject *parent = nullptr);

    [[nodiscard]] QList<ProviderConfig> providers() const { return m_providers; }
    [[nodiscard]] QStringList modelIds() const;
    [[nodiscard]] std::optional<ModelSpec> specFor(const QString &modelId) const;

    // Builds a ready provider for the model id; the secret is resolved
    // synchronously from `secret` when the kind needs one (empty -> caller
    // cancelled). Returns nullptr when unavailable.
    [[nodiscard]] ChatProvider *createProvider(const QString &modelId, const QString &secret,
                                               QObject *parent) const;

    [[nodiscard]] bool load(QString *error = nullptr);
    [[nodiscard]] bool save(QString *error = nullptr) const;
    [[nodiscard]] QString path() const { return m_path; }

    // Seed document written when none exists (plan section 5.3).
    [[nodiscard]] static QByteArray seedDocument();

private:
    QString m_path;
    QList<ProviderConfig> m_providers;
};

} // namespace QindaOffice::Ai
