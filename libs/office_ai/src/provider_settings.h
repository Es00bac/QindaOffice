// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ProviderSettings (D-057): the QML face of a ModelCatalog for
// qml/ProviderSettingsDialog.qml — the configured rows, the compiled-in
// presets (provider_presets.h), adding/editing/removing rows, storing a
// key in the keyring, testing a row, and importing from OpenCode.
//
// AGENT-GUARD (D-052): a key typed into the dialog goes straight to the
// keyring (SecretStore) and is never kept in this object, in the catalog
// file or in a signal; testing reports only WHERE a key was found.

#include "model_catalog.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Ai {

class ProviderSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application over its model catalog.")
    // [{id, name, kind, baseUrl, model, apiKeyRef, env, offMachine, body}]
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    // [{id, name, kind, baseUrl, env, doc, hint, local, models: [{id, name, context}]}]
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
public:
    explicit ProviderSettings(ModelCatalog *catalog, QObject *parent = nullptr);

    [[nodiscard]] QVariantList providers() const;
    [[nodiscard]] QVariantList presets() const;

    Q_INVOKABLE QString addPreset(const QString &presetId, const QString &model);
    // `fields`: name, kind, baseUrl, model, apiKeyRef, body (JSON text).
    Q_INVOKABLE QString addCustom(const QVariantMap &fields);
    Q_INVOKABLE bool update(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE void remove(const QString &id);
    // Stores `key` in the keyring for the row (never echoed back).
    Q_INVOKABLE void setApiKey(const QString &id, const QString &key);
    // Answers keyStatus(id, origin) — "" when no key is found anywhere.
    Q_INVOKABLE void checkKey(const QString &id);
    // Resolves the key and lists the endpoint's models (OpenAI-shaped
    // rows) or sends a one-word prompt (the rest); answers testResult.
    Q_INVOKABLE void test(const QString &id);
    // Adds the rows OpenCode knows that the catalog lacks; returns how many.
    Q_INVOKABLE int importOpenCode();

signals:
    void providersChanged();
    void keyStatus(const QString &id, const QString &origin);
    void testResult(const QString &id, bool ok, const QString &message, const QStringList &models);
    void saveFailed(const QString &message);

private:
    bool applyFields(ProviderConfig *config, const QVariantMap &fields, QString *error) const;
    void save();

    ModelCatalog *m_catalog;
};

} // namespace QindaOffice::Ai
