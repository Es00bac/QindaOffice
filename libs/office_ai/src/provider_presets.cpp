// SPDX-License-Identifier: GPL-3.0-or-later
#include "provider_presets.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace QindaOffice::Ai {

namespace {

QList<ProviderPreset> loadPresets()
{
    QList<ProviderPreset> presets;
    QFile file(QStringLiteral(":/qindaoffice/ai/provider_presets.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return presets;
    }
    const QJsonArray array =
        QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("presets")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ProviderPreset preset;
        preset.id = object.value(QStringLiteral("id")).toString();
        preset.name = object.value(QStringLiteral("name")).toString();
        preset.kind = object.value(QStringLiteral("kind")).toString();
        preset.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
        for (const QJsonValue &env : object.value(QStringLiteral("env")).toArray()) {
            preset.env.append(env.toString());
        }
        preset.doc = object.value(QStringLiteral("doc")).toString();
        preset.hint = object.value(QStringLiteral("hint")).toString();
        preset.local = object.value(QStringLiteral("local")).toBool();
        for (const QJsonValue &m : object.value(QStringLiteral("models")).toArray()) {
            const QJsonObject model = m.toObject();
            preset.models.append({model.value(QStringLiteral("id")).toString(),
                                  model.value(QStringLiteral("name")).toString(),
                                  model.value(QStringLiteral("context")).toInt(),
                                  model.value(QStringLiteral("output")).toInt(),
                                  model.value(QStringLiteral("reasoning")).toBool()});
        }
        presets.append(preset);
    }
    return presets;
}

} // namespace

ProviderConfig ProviderPreset::toConfig(const QString &model) const
{
    ProviderConfig config;
    config.id = id;
    config.kind = kind;
    config.baseUrl = baseUrl;
    config.model = model.isEmpty() && !models.isEmpty() ? models.first().id : model;
    QString modelName = config.model;
    for (const PresetModel &m : models) {
        if (m.id == config.model) {
            modelName = m.name;
        }
    }
    config.displayName = modelName.isEmpty() ? name : QStringLiteral("%1 — %2").arg(name, modelName);
    config.env = env;
    config.openCodeId = id;
    if (!local) {
        config.secretKey = id + QStringLiteral("-api-key");
    }
    if (id == QLatin1String("openai")) {
        // AGENT-NOTE: OpenAI's current (reasoning) models reject
        // max_tokens and a non-default temperature on chat/completions.
        config.options.insert(QStringLiteral("maxTokensField"),
                              QStringLiteral("max_completion_tokens"));
        config.options.insert(QStringLiteral("sendTemperature"), false);
    }
    if (id == QLatin1String("openrouter")) {
        config.options.insert(QStringLiteral("headers"),
                              QJsonObject{{QStringLiteral("X-Title"), QStringLiteral("QindaOffice")}});
    }
    return config;
}

const QList<ProviderPreset> &ProviderPresets::all()
{
    static const QList<ProviderPreset> presets = loadPresets();
    return presets;
}

std::optional<ProviderPreset> ProviderPresets::find(const QString &id)
{
    for (const ProviderPreset &preset : all()) {
        if (preset.id == id) {
            return preset;
        }
    }
    return std::nullopt;
}

} // namespace QindaOffice::Ai
