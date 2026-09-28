// SPDX-License-Identifier: GPL-3.0-or-later
// ModelCatalog's OpenCode import (D-057): the providers a user already set
// up for OpenCode become catalog rows. Two sources:
//   - opencode.json / opencode.jsonc "provider" entries with an
//     options.baseURL (custom providers such as a llama-server on another
//     machine); "{env:…}"/"{file:…}" apiKey values are kept as references
//     (a relative {file:} path is resolved against the config's directory,
//     as OpenCode does), a literal apiKey is never copied;
//   - auth.json API keys for providers we have a preset for (the key
//     itself stays in OpenCode's file; the row's openCodeId points at it).

#include "model_catalog.h"
#include "provider_presets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace QindaOffice::Ai {

namespace {

// JSONC → JSON: drops // and /* */ comments outside strings and trailing
// commas before } or ].
QByteArray stripJsonc(const QByteArray &in)
{
    QByteArray out;
    out.reserve(in.size());
    bool inString = false;
    for (qsizetype i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (inString) {
            out.append(c);
            if (c == '\\' && i + 1 < in.size()) {
                out.append(in.at(++i));
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            out.append(c);
        } else if (c == '/' && i + 1 < in.size() && in.at(i + 1) == '/') {
            while (i < in.size() && in.at(i) != '\n') {
                ++i;
            }
            out.append('\n');
        } else if (c == '/' && i + 1 < in.size() && in.at(i + 1) == '*') {
            i += 2;
            while (i + 1 < in.size() && !(in.at(i) == '*' && in.at(i + 1) == '/')) {
                ++i;
            }
            ++i;
        } else if (c == ',') {
            qsizetype j = i + 1;
            while (j < in.size() && QChar::isSpace(uchar(in.at(j)))) {
                ++j;
            }
            if (j < in.size() && (in.at(j) == '}' || in.at(j) == ']')) {
                continue; // trailing comma
            }
            out.append(c);
        } else {
            out.append(c);
        }
    }
    return out;
}

QString referenceFor(const QString &apiKey, const QString &configDir)
{
    if (!ApiKeyResolver::isReference(apiKey)) {
        return {};
    }
    if (apiKey.startsWith(QLatin1String("{file:"))) {
        QString path = apiKey.mid(6).chopped(1).trimmed();
        if (!path.startsWith(QLatin1Char('/')) && !path.startsWith(QLatin1Char('~'))) {
            path = QDir(configDir).absoluteFilePath(path);
        }
        return QStringLiteral("{file:%1}").arg(path);
    }
    return apiKey;
}

QString slug(const QString &text)
{
    QString out;
    for (const QChar c : text.toLower()) {
        out.append(c.isLetterOrNumber() ? c : QLatin1Char('-'));
    }
    return out;
}

} // namespace

QStringList ModelCatalog::defaultOpenCodeConfigs()
{
    const QString configHome = qEnvironmentVariableIsSet("XDG_CONFIG_HOME")
        ? qEnvironmentVariable("XDG_CONFIG_HOME")
        : QDir::homePath() + QStringLiteral("/.config");
    QStringList paths{configHome + QStringLiteral("/opencode/opencode.json"),
                      configHome + QStringLiteral("/opencode/opencode.jsonc")};
    const QString explicitConfig = qEnvironmentVariable("OPENCODE_CONFIG");
    if (!explicitConfig.isEmpty()) {
        paths.append(explicitConfig);
    }
    // Per-tool OpenCode configs the user keeps next to a service's own
    // settings (e.g. ~/.config/qinda-llm/opencode.json for the llama-server
    // on qinda): any ~/.config/*/opencode.json.
    const QDir config(configHome);
    for (const QString &dir : config.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString candidate = config.filePath(dir + QStringLiteral("/opencode.json"));
        if (dir != QLatin1String("opencode") && QFileInfo::exists(candidate)) {
            paths.append(candidate);
        }
    }
    return paths;
}

QList<ProviderConfig> ModelCatalog::openCodeProviders(const QStringList &configPaths)
{
    QList<ProviderConfig> rows;
    for (const QString &path : configPaths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonObject providers = QJsonDocument::fromJson(stripJsonc(file.readAll()))
                                          .object()
                                          .value(QStringLiteral("provider"))
                                          .toObject();
        const QString configDir = QFileInfo(path).absolutePath();
        for (auto it = providers.begin(); it != providers.end(); ++it) {
            const QJsonObject provider = it.value().toObject();
            const QJsonObject options = provider.value(QStringLiteral("options")).toObject();
            const QString baseUrl = options.value(QStringLiteral("baseURL")).toString();
            if (baseUrl.isEmpty()) {
                continue; // an override of a built-in provider: no endpoint to import
            }
            const bool anthropic = provider.value(QStringLiteral("npm")).toString()
                == QLatin1String("@ai-sdk/anthropic");
            const QString name = provider.value(QStringLiteral("name")).toString(it.key());
            const QJsonObject models = provider.value(QStringLiteral("models")).toObject();
            for (auto m = models.begin(); m != models.end(); ++m) {
                ProviderConfig row;
                row.id = models.size() == 1 ? slug(it.key()) : slug(it.key() + QLatin1Char('-') + m.key());
                row.kind = anthropic ? QStringLiteral("anthropic") : QStringLiteral("openai");
                row.baseUrl = anthropic && baseUrl.endsWith(QLatin1String("/v1")) ? baseUrl.chopped(3)
                                                                                 : baseUrl;
                row.model = m.key();
                const QString modelName = m.value().toObject().value(QStringLiteral("name")).toString(m.key());
                row.displayName = QStringLiteral("%1 — %2").arg(name, modelName);
                row.apiKeyRef = referenceFor(options.value(QStringLiteral("apiKey")).toString(), configDir);
                for (const QJsonValue &env : provider.value(QStringLiteral("env")).toArray()) {
                    row.env.append(env.toString());
                }
                row.openCodeId = it.key();
                rows.append(row);
            }
        }
    }
    // Providers connected with OpenCode's /connect.
    const QString dataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME")
        ? qEnvironmentVariable("XDG_DATA_HOME")
        : QDir::homePath() + QStringLiteral("/.local/share");
    QFile auth(dataHome + QStringLiteral("/opencode/auth.json"));
    if (auth.open(QIODevice::ReadOnly)) {
        const QJsonObject entries = QJsonDocument::fromJson(auth.readAll()).object();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (it.value().toObject().value(QStringLiteral("type")).toString() != QLatin1String("api")) {
                continue;
            }
            if (const auto preset = ProviderPresets::find(it.key())) {
                ProviderConfig row = preset->toConfig();
                row.secretKey.clear(); // the key lives in OpenCode's auth.json
                rows.append(row);
            }
        }
    }
    return rows;
}

} // namespace QindaOffice::Ai
