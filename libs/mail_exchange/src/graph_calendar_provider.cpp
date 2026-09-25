// SPDX-License-Identifier: GPL-3.0-or-later
#include "graph_calendar_provider.h"

#include "graph_backend.h"

#include <qindatk/protocol_client.h>

#include <QUrl>

namespace QindaOffice::Mail {

GraphCalendarProvider::GraphCalendarProvider(QObject *parent)
    : HostedCalendarProvider(parent, QStringLiteral("microsoft365"))
{
}

QString GraphCalendarProvider::validate() const
{
    const QUrl url(m_baseUrl);
    const bool https = url.scheme() == QLatin1String("https");
    if (!url.isValid() || url.host().isEmpty() || (!https && url.scheme() != QLatin1String("http")))
        return QStringLiteral("The Microsoft Graph base URL must be an http or https URL");
    const Credentials credentials = this->credentials();
    if (credentials.secret.isEmpty()) return QStringLiteral("Sign in first: no access token is set");
    if (credentials.mechanism != QLatin1String("bearer"))
        return QStringLiteral("Microsoft Graph accepts only \"bearer\" (OAuth access token) credentials");
    if (!QindaTK::Protocol::plaintextAuthAllowed(url.host(), https, insecureAllowed()))
        return QStringLiteral("Refusing to send credentials over plain HTTP to %1").arg(url.host());
    return {};
}

HostedCalendarProvider::BackendFactory GraphCalendarProvider::backendFactory(const SyncWindow &window) const
{
    GraphConfig config;
    config.baseUrl = QUrl(m_baseUrl);
    config.calendarId = m_graphCalendarId;
    config.defaultZone = defaultZoneId();
    config.secret = credentials().secret;
    config.timeoutMs = timeout();
    config.allowInsecureAuth = insecureAllowed();
    config.window = window;
    return [config] { return std::make_unique<GraphBackend>(config); };
}

} // namespace QindaOffice::Mail
