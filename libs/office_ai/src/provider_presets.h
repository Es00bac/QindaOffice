// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Ready-made provider rows (D-057), the way OpenCode knows providers: a
// table distilled from the models.dev catalog by
// tools/gen_provider_presets.py and compiled in as
// :/qindaoffice/ai/provider_presets.json. Each preset says which of the
// two wire protocols the provider speaks ("openai" chat/completions or
// "anthropic" messages), its base URL, the environment variables that
// conventionally hold its key, and a handful of current models; a live
// GET {base}/models fills in the rest where the provider supports it.

#include "model_catalog.h"

namespace QindaOffice::Ai {

struct PresetModel {
    QString id;
    QString name;
    int context = 0;
    int output = 0;
    bool reasoning = false;
};

struct ProviderPreset {
    QString id;
    QString name;
    QString kind;
    QString baseUrl;
    QStringList env;
    QString doc;
    QString hint;
    bool local = false;
    QList<PresetModel> models;

    // A catalog row for `model` (the first preset model when empty).
    [[nodiscard]] ProviderConfig toConfig(const QString &model = {}) const;
};

class ProviderPresets {
public:
    [[nodiscard]] static const QList<ProviderPreset> &all();
    [[nodiscard]] static std::optional<ProviderPreset> find(const QString &id);
};

} // namespace QindaOffice::Ai
