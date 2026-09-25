// SPDX-License-Identifier: GPL-3.0-or-later
#include "oauth_client_registry.h"

#include "oauth2_profiles.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace QindaOffice::Mail {
namespace {

struct FileEntry {
    bool exists = false;
    bool ok = true;
    QString error;
    QJsonObject entry;  // the provider's object, if any
};

FileEntry readEntry(const QString &path, const QString &provider)
{
    FileEntry result;
    if (path.isEmpty()) return result;
    QFile file(path);
    if (!file.exists()) return result;
    result.exists = true;
    if (!file.open(QIODevice::ReadOnly)) {
        result.ok = false;
        result.error = QStringLiteral("Cannot read %1: %2").arg(path, file.errorString());
        return result;
    }
    if (file.size() > 64 * 1024) {
        result.ok = false;
        result.error = QStringLiteral("%1 is larger than 64 KiB.").arg(path);
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.ok = false;
        result.error = QStringLiteral("%1 is not a valid OAuth client file (%2).")
                           .arg(path, parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                                                   : QStringLiteral("not an object"));
        return result;
    }
    const QString schema = doc.object().value(QLatin1String("schema")).toString();
    if (!schema.isEmpty() && schema != QLatin1String("qindaoffice.mail.oauth-clients/1")) {
        result.ok = false;
        result.error = QStringLiteral("%1 has an unknown schema (%2).").arg(path, schema);
        return result;
    }
    result.entry = doc.object().value(provider).toObject();
    return result;
}

QString text(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString().trimmed();
}

} // namespace

OAuthClientRegistry::OAuthClientRegistry()
    : m_userPath(defaultUserPath())
    , m_systemPath(defaultSystemPath())
{
}

QString OAuthClientRegistry::defaultUserPath()
{
    // Read-only lookup: no directory is created for it.
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/qindaoffice/mail/oauth-clients.json");
}

OAuthClientRegistry::Resolution OAuthClientRegistry::resolve(const QString &provider, const QString &accountClientId,
                                                             const QString &accountTenant) const
{
    Resolution resolution;
    const std::optional<OAuthProfile> profile = OAuthProfile::forProvider(provider);
    if (!profile) {
        resolution.error = QStringLiteral("“%1” is not an OAuth provider QindaMail knows (google, microsoft).")
                               .arg(provider);
        return resolution;
    }
    const FileEntry user = readEntry(m_userPath, provider);
    const FileEntry system = readEntry(m_systemPath, provider);

    if (!accountClientId.trimmed().isEmpty()) {
        OAuthClient client;
        client.provider = provider;
        client.clientId = accountClientId.trimmed();
        client.tenant = accountTenant.trimmed();
        client.source = QStringLiteral("account");
        for (const FileEntry *file : {&user, &system}) {
            if (file->ok && text(file->entry, "clientId") == client.clientId && client.clientSecret.isEmpty()) {
                client.clientSecret = text(file->entry, "clientSecret");
                if (client.tenant.isEmpty()) client.tenant = text(file->entry, "tenant");
            }
        }
        resolution.ok = true;
        resolution.client = client;
        return resolution;
    }

    const std::pair<const FileEntry *, const char *> files[] = {{&user, "user"}, {&system, "system"}};
    for (const auto &[file, source] : files) {
        if (!file->ok) {
            resolution.error = file->error;
            return resolution;
        }
        const QString clientId = text(file->entry, "clientId");
        if (clientId.isEmpty()) continue;
        OAuthClient client;
        client.provider = provider;
        client.clientId = clientId;
        client.clientSecret = text(file->entry, "clientSecret");
        client.tenant = !accountTenant.trimmed().isEmpty() ? accountTenant.trimmed() : text(file->entry, "tenant");
        client.source = QString::fromLatin1(source);
        resolution.ok = true;
        resolution.client = client;
        return resolution;
    }

    // AGENT-CONTRACT: the account dialog shows this text verbatim (app lane);
    // keep it actionable and keep the Gmail app-password hint.
    resolution.error = QStringLiteral("No OAuth client ID is configured for %1 sign-in. QindaOffice does not ship "
                                      "one: register a desktop OAuth client with %1 and put its client ID in %2 "
                                      "(or ask your distribution to provide %3).")
                           .arg(profile->name, m_userPath, m_systemPath);
    if (provider == QLatin1String("google")) {
        resolution.error += QStringLiteral(" For Gmail mail alone, an app password works instead.");
    }
    return resolution;
}

} // namespace QindaOffice::Mail
