// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Where an API key comes from (D-057, extending D-052's keyring rule).
// First hit wins:
//
//   1. the suite's keyring entry (QtKeychain, service "qindaoffice")
//   2. a REFERENCE stored in settings — "{env:NAME}" or "{file:PATH}",
//      OpenCode's substitution syntax (PATH may start with ~/) — so a key
//      file the user already keeps (llama-server's --api-key-file copy in
//      ~/.config/qinda-llm/api-key) is read at request time and never
//      copied into a settings file
//   3. the provider's conventional environment variables (models.dev
//      `env`: ANTHROPIC_API_KEY, MOONSHOT_API_KEY, …)
//   4. OpenCode's own credential store, ~/.local/share/opencode/auth.json
//      ({"<provider>": {"type": "api", "key": …}}), so a provider already
//      connected with OpenCode's /connect just works
//   5. a Secret Service item by attributes (secret-tool lookup …) — how
//      the ElevenLabs key Gabbee stores is reused
//
// AGENT-GUARD: a resolved key is handed to the caller and never written
// anywhere; `origin` is a human label ("keyring", "file", "OpenCode") for
// the settings UI, never the key.

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace QindaOffice::Ai {

struct KeySources {
    QString keyringKey;      // 1: key id under service "qindaoffice"
    QString reference;       // 2: "{env:X}" / "{file:path}"
    QStringList envVars;     // 3
    QString openCodeId;      // 4: provider id in OpenCode's auth.json
    QStringList secretAttributes; // 5: attribute/value pairs for secret-tool
};

class ApiKeyResolver {
public:
    using Callback = std::function<void(const QString &key, const QString &origin)>;

    // Resolves asynchronously (the keyring answers on the session bus);
    // `context` bounds the callback's lifetime. Empty key = none found.
    static void resolve(const KeySources &sources, QObject *context, const Callback &callback);

    // The synchronous steps 2-5, exposed for the tests.
    [[nodiscard]] static QString fromReference(const QString &reference);
    [[nodiscard]] static QString fromEnvironment(const QStringList &names, QString *which = nullptr);
    [[nodiscard]] static QString fromOpenCode(const QString &providerId);
    [[nodiscard]] static QString fromSecretService(const QStringList &attributes);
    // True for "{env:…}" / "{file:…}"; anything else is not a reference
    // (a literal key pasted there is refused by the settings UI).
    [[nodiscard]] static bool isReference(const QString &text);
    // What the settings field stores: a bare path ("~/…" or "/…", no
    // spaces: a key never looks like that) becomes "{file:…}"; anything
    // else is returned trimmed and unchanged.
    [[nodiscard]] static QString normalizeReference(const QString &text);
    [[nodiscard]] static QString expandHome(const QString &path);
};

} // namespace QindaOffice::Ai
