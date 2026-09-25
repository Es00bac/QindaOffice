// SPDX-License-Identifier: GPL-3.0-or-later
#include "ews_calendar_provider.h"

#include "ews_backend.h"

#include <qindatk/protocol_client.h>

#include <QUrl>

namespace QindaOffice::Mail {

EwsCalendarProvider::EwsCalendarProvider(QObject *parent) : HostedCalendarProvider(parent) {}

QString EwsCalendarProvider::validate() const
{
    const QUrl url(m_url);
    const bool https = url.scheme() == QLatin1String("https");
    if (!url.isValid() || url.host().isEmpty() || (!https && url.scheme() != QLatin1String("http")))
        return QStringLiteral("The EWS URL must be an http or https URL such as https://mail.example.com/EWS/Exchange.asmx");
    if (m_serverVersion.startsWith(QLatin1String("Exchange2007")))
        return QStringLiteral("Exchange 2007 is not supported (time zones need Exchange 2010 or later)");
    const Credentials credentials = this->credentials();
    if (credentials.secret.isEmpty()) return QStringLiteral("Sign in first: no password or token is set");
    if (credentials.mechanism != QLatin1String("basic") && credentials.mechanism != QLatin1String("bearer"))
        return QStringLiteral("EWS accepts \"basic\" (password) or \"bearer\" (OAuth token) credentials");
    if (credentials.mechanism == QLatin1String("basic") && m_userName.isEmpty())
        return QStringLiteral("Password sign-in needs a user name");
    if (!QindaTK::Protocol::plaintextAuthAllowed(url.host(), https, insecureAllowed()))
        return QStringLiteral("Refusing to send credentials over plain HTTP to %1").arg(url.host());
    return {};
}

HostedCalendarProvider::BackendFactory EwsCalendarProvider::backendFactory(const SyncWindow &window) const
{
    EwsConfig config;
    config.url = QUrl(m_url);
    config.userName = m_userName;
    config.serverVersion = m_serverVersion;
    config.impersonate = m_impersonate;
    config.folderId = m_folderId;
    config.defaultZone = defaultZoneId();
    config.secret = credentials().secret;
    config.mechanism = credentials().mechanism;
    config.timeoutMs = timeout();
    config.allowInsecureAuth = insecureAllowed();
    config.window = window;
    return [config] { return std::make_unique<EwsBackend>(config); };
}

} // namespace QindaOffice::Mail
