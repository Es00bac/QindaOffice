// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Finds the server settings for an e-mail address, in this order:
//   1. the built-in presets (no network),
//   2. https://autoconfig.<domain>/mail/config-v1.1.xml?emailaddress=<address>
//   3. https://<domain>/.well-known/autoconfig/mail/config-v1.1.xml
//   4. the Mozilla ISPDB, https://autoconfig.thunderbird.net/v1.1/<domain>
// (Thunderbird's order minus its http:// and MX-record steps, which this
// lane leaves out: https only, D-043.) The first source that yields a usable
// IMAP/POP3 + SMTP pair wins. Only the provider's own servers (step 2) see
// the full address; the ISPDB sees the domain only.
//
// Each lookup ends in exactly one finished(requestId, result) (mail_result.h
// contract). Result keys beyond the contract:
//   emailAddress, domain,
//   source       preset | autoconfig | well-known | ispdb | "" (none),
//   sourceUrl    the document that answered ("" for a preset),
//   presetId, providerName, auth, note   (preset hits; auth/note as in
//                                          provider_presets.h),
//   account      a candidate MailAccount map (no id; AccountStore::add
//                assigns one),
//   incoming, outgoing   every server the source listed (maps with type,
//                host, port, security, userName, authentication,
//                authMechanism), for the dialog's "other choices",
//   attempts     [{source, url, outcome: found | not-found | invalid |
//                offline | refused | cancelled, error}] in the order tried.
// Nothing found: state rejected (or offline + retryable when every network
// step failed to connect).

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QNetworkAccessManager;

namespace QindaOffice::Mail {

class AutoConfig : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // Per network step (connect + complete answer).
    Q_PROPERTY(int timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY timeoutMsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    // URL templates; %DOMAIN% becomes the (ACE) domain and %EMAILADDRESS%
    // the percent-encoded address. Tests point them at a loopback server.
    struct Endpoints {
        QString autoconfig;
        QString wellKnown;
        QString ispdb;
    };
    [[nodiscard]] static Endpoints defaultEndpoints();

    explicit AutoConfig(QObject *parent = nullptr);
    ~AutoConfig() override;

    void setEndpoints(const Endpoints &endpoints) { m_endpoints = endpoints; }
    [[nodiscard]] Endpoints endpoints() const { return m_endpoints; }
    // Tests of the network order switch the presets off.
    void setUsePresets(bool use) { m_usePresets = use; }
    // Optional shared manager (not owned); by default AutoConfig owns one.
    void setNetworkAccessManager(QNetworkAccessManager *manager);

    [[nodiscard]] int timeoutMs() const { return m_timeoutMs; }
    void setTimeoutMs(int ms);
    [[nodiscard]] bool busy() const { return !m_pending.isEmpty(); }

    Q_INVOKABLE void lookup(const QString &requestId, const QString &emailAddress);
    // The pending lookup ends with state rejected and outcome cancelled.
    Q_INVOKABLE void cancel(const QString &requestId);
    // Synchronous preset match ({} when the domain has none): the preset map
    // plus `account` (the candidate).
    Q_INVOKABLE QVariantMap presetFor(const QString &emailAddress) const;

signals:
    void finished(const QString &requestId, const QVariantMap &result);
    void timeoutMsChanged();
    void busyChanged();

private:
    struct Pending;
    void step(const std::shared_ptr<Pending> &pending);
    void handleDocument(const std::shared_ptr<Pending> &pending, const QString &source, const QString &url,
                        const QByteArray &body);
    void finish(const std::shared_ptr<Pending> &pending, const QVariantMap &result);
    [[nodiscard]] QVariantMap notFound(const std::shared_ptr<Pending> &pending) const;

    Endpoints m_endpoints;
    bool m_usePresets = true;
    int m_timeoutMs = 10000;
    QNetworkAccessManager *m_manager = nullptr;
    bool m_ownsManager = false;
    QHash<QString, std::shared_ptr<Pending>> m_pending;
};

} // namespace QindaOffice::Mail
