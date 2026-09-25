// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Two-way sync of one Exchange calendar folder over Exchange Web Services
// (SOAP 1.1, on-premises Exchange 2010 SP1 and later) with
// LocalProductivityStore. Same API as GraphCalendarProvider; see
// HostedCalendarProvider for the shared contract and docs/mail.md.
//
// Configure: url (https://<host>/EWS/Exchange.asmx), userName, accountId,
// calendarId, store; setSecret(password, "basic") or setSecret(token,
// "bearer"). serverVersion is sent as RequestServerVersion; impersonate
// (an SMTP address) adds ExchangeImpersonation; folderId selects a calendar
// folder other than the distinguished "calendar" (see listCalendars).

#include "hosted_calendar_provider.h"

#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Mail {

class EwsCalendarProvider : public HostedCalendarProvider {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString url MEMBER m_url NOTIFY configChanged)
    Q_PROPERTY(QString userName MEMBER m_userName NOTIFY configChanged)
    Q_PROPERTY(QString serverVersion MEMBER m_serverVersion NOTIFY configChanged)
    Q_PROPERTY(QString impersonate MEMBER m_impersonate NOTIFY configChanged)
    Q_PROPERTY(QString folderId MEMBER m_folderId NOTIFY configChanged)
public:
    explicit EwsCalendarProvider(QObject *parent = nullptr);

protected:
    QString idPrefix() const override { return QStringLiteral("ews"); }
    QString versionField() const override { return QStringLiteral("remoteChangeKey"); }
    QString validate() const override;
    BackendFactory backendFactory(const SyncWindow &window) const override;

private:
    QString m_url, m_userName, m_serverVersion = QStringLiteral("Exchange2013_SP1"), m_impersonate, m_folderId;
};

} // namespace QindaOffice::Mail
