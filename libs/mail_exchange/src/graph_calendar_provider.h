// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Two-way sync of one Microsoft 365 calendar over Microsoft Graph v1.0 with
// LocalProductivityStore. Same API as EwsCalendarProvider; see
// HostedCalendarProvider for the shared contract and docs/mail.md.
//
// Configure: accountId, calendarId, store, graphCalendarId (empty = the
// mailbox's default calendar; ids come from listCalendars), and
// setSecret(accessToken, "bearer") - the host's OAuth flow (office_mail)
// owns sign-in and refresh and retries once after `authRejected`.
// baseUrl is injectable for tests and national clouds.

#include "hosted_calendar_provider.h"

#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Mail {

class GraphCalendarProvider : public HostedCalendarProvider {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString baseUrl MEMBER m_baseUrl NOTIFY configChanged)
    Q_PROPERTY(QString graphCalendarId MEMBER m_graphCalendarId NOTIFY configChanged)
public:
    explicit GraphCalendarProvider(QObject *parent = nullptr);

protected:
    QString idPrefix() const override { return QStringLiteral("graph"); }
    QString versionField() const override { return QStringLiteral("remoteEtag"); }
    QString validate() const override;
    BackendFactory backendFactory(const SyncWindow &window) const override;

private:
    QString m_baseUrl = QStringLiteral("https://graph.microsoft.com/v1.0"), m_graphCalendarId;
};

} // namespace QindaOffice::Mail
