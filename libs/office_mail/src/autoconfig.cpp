// SPDX-License-Identifier: GPL-3.0-or-later
#include "autoconfig.h"

#include "bounded_http.h"
#include "client_config_parser.h"
#include "mail_result.h"
#include "provider_presets.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace QindaOffice::Mail {

struct AutoConfig::Pending {
    QString requestId;
    QString emailAddress;
    QString domain;
    int step = 0;                 // index into the network steps
    QVariantList attempts;
    bool anyReached = false;      // at least one step got an HTTP answer
    QPointer<QNetworkReply> reply;
};

namespace {

struct NetworkStep {
    const char *source;
    QString AutoConfig::Endpoints::*url;
};

constexpr NetworkStep kSteps[] = {
    {"autoconfig", &AutoConfig::Endpoints::autoconfig},
    {"well-known", &AutoConfig::Endpoints::wellKnown},
    {"ispdb", &AutoConfig::Endpoints::ispdb},
};
constexpr int kStepCount = int(sizeof(kSteps) / sizeof(kSteps[0]));

QVariantMap attempt(const QString &source, const QString &url, const char *outcome, const QString &error = {})
{
    return {{QStringLiteral("source"), source}, {QStringLiteral("url"), url},
            {QStringLiteral("outcome"), QString::fromLatin1(outcome)}, {QStringLiteral("error"), error}};
}

QVariantList serverList(const QList<ClientConfigServer> &servers)
{
    QVariantList out;
    for (const ClientConfigServer &s : servers) out.append(s.toVariantMap());
    return out;
}

} // namespace

AutoConfig::Endpoints AutoConfig::defaultEndpoints()
{
    return {QStringLiteral("https://autoconfig.%DOMAIN%/mail/config-v1.1.xml?emailaddress=%EMAILADDRESS%"),
            QStringLiteral("https://%DOMAIN%/.well-known/autoconfig/mail/config-v1.1.xml"),
            QStringLiteral("https://autoconfig.thunderbird.net/v1.1/%DOMAIN%")};
}

AutoConfig::AutoConfig(QObject *parent)
    : QObject(parent)
    , m_endpoints(defaultEndpoints())
{
}

AutoConfig::~AutoConfig()
{
    for (const auto &pending : std::as_const(m_pending)) BoundedHttp::cancel(pending->reply);
}

void AutoConfig::setNetworkAccessManager(QNetworkAccessManager *manager)
{
    if (m_ownsManager) delete m_manager;
    m_manager = manager;
    m_ownsManager = false;
}

void AutoConfig::setTimeoutMs(int ms)
{
    ms = qBound(1000, ms, 120000);
    if (ms == m_timeoutMs) return;
    m_timeoutMs = ms;
    emit timeoutMsChanged();
}

QVariantMap AutoConfig::presetFor(const QString &emailAddress) const
{
    const std::optional<ProviderPreset> preset = ProviderPresets::forAddress(emailAddress);
    if (!preset) return {};
    QVariantMap map = preset->toVariantMap();
    map.insert(QStringLiteral("account"), ProviderPresets::accountFor(*preset, emailAddress).toVariantMap());
    return map;
}

void AutoConfig::lookup(const QString &requestId, const QString &emailAddress)
{
    auto pending = std::make_shared<Pending>();
    pending->requestId = requestId;
    pending->emailAddress = emailAddress.trimmed();
    pending->domain = domainOf(pending->emailAddress);

    // The domain goes into host names and paths: accept DNS names only.
    static const QRegularExpression domainPattern(
        QStringLiteral("^(?=.{1,253}$)([a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?\\.)+[a-z]{2,63}$"));
    const QString ace = QString::fromLatin1(QUrl::toAce(pending->domain));
    if (requestId.isEmpty() || m_pending.contains(requestId) || !pending->emailAddress.contains(QLatin1Char('@'))
        || !domainPattern.match(ace).hasMatch()) {
        QVariantMap result = makeResult(requestId, kStateRejected,
                                        requestId.isEmpty() || m_pending.contains(requestId)
                                            ? QStringLiteral("A lookup needs a new, unique request id.")
                                            : QStringLiteral("“%1” is not an e-mail address.").arg(emailAddress));
        QTimer::singleShot(0, this, [this, requestId, result] { emit finished(requestId, result); });
        return;
    }
    pending->domain = ace;
    const bool wasBusy = busy();
    m_pending.insert(requestId, pending);
    if (!wasBusy) emit busyChanged();

    if (m_usePresets) {
        if (const std::optional<ProviderPreset> preset = ProviderPresets::forDomain(pending->domain)) {
            QVariantMap result = makeResult(requestId, kStateAccepted);
            result.insert(QStringLiteral("source"), QStringLiteral("preset"));
            result.insert(QStringLiteral("sourceUrl"), QString());
            result.insert(QStringLiteral("presetId"), preset->id);
            result.insert(QStringLiteral("providerName"), preset->name);
            result.insert(QStringLiteral("auth"), preset->auth);
            result.insert(QStringLiteral("note"), preset->note);
            result.insert(QStringLiteral("account"),
                          ProviderPresets::accountFor(*preset, pending->emailAddress).toVariantMap());
            pending->attempts.append(attempt(QStringLiteral("preset"), {}, "found"));
            QTimer::singleShot(0, this, [this, pending, result] { finish(pending, result); });
            return;
        }
        pending->attempts.append(attempt(QStringLiteral("preset"), {}, "not-found"));
    }
    QTimer::singleShot(0, this, [this, pending] { step(pending); });
}

void AutoConfig::cancel(const QString &requestId)
{
    const std::shared_ptr<Pending> pending = m_pending.value(requestId);
    if (!pending) return;
    BoundedHttp::cancel(pending->reply);
    pending->attempts.append(attempt(QString(), QString(), "cancelled"));
    QVariantMap result = makeResult(requestId, kStateRejected, QStringLiteral("Cancelled."));
    result.insert(QStringLiteral("cancelled"), true);
    finish(pending, result);
}

void AutoConfig::step(const std::shared_ptr<Pending> &pending)
{
    if (m_pending.value(pending->requestId) != pending) return; // cancelled meanwhile
    if (pending->step >= kStepCount) {
        finish(pending, notFound(pending));
        return;
    }
    const NetworkStep &s = kSteps[pending->step++];
    const QString source = QString::fromLatin1(s.source);
    QString text = m_endpoints.*(s.url);
    text.replace(QLatin1String("%DOMAIN%"), pending->domain);
    text.replace(QLatin1String("%EMAILADDRESS%"), QString::fromLatin1(QUrl::toPercentEncoding(pending->emailAddress)));
    const QUrl url(text, QUrl::StrictMode);

    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
        m_ownsManager = true;
    }
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/xml, text/xml;q=0.9, */*;q=0.1");
    HttpLimits limits;
    limits.timeoutMs = m_timeoutMs;
    limits.maxBytes = ClientConfigParser::kMaxBytes;
    pending->reply = BoundedHttp::send(
        m_manager, request, "GET", {}, limits, this, [this, pending, source, text](const HttpOutcome &outcome) {
            pending->reply = nullptr;
            if (m_pending.value(pending->requestId) != pending) return;
            using K = HttpOutcome::Kind;
            switch (outcome.kind) {
            case K::Ok:
                pending->anyReached = true;
                handleDocument(pending, source, text, outcome.body);
                return;
            case K::HttpError:
            case K::TooLarge:
                pending->anyReached = true;
                pending->attempts.append(attempt(source, text, outcome.status == 404 || outcome.status == 410
                                                     ? "not-found" : "invalid", outcome.error));
                break;
            case K::Refused:
                pending->anyReached = true; // our policy, not the network
                pending->attempts.append(attempt(source, text, "refused", outcome.error));
                break;
            case K::Network:
            case K::Timeout:
                pending->attempts.append(attempt(source, text, "offline", outcome.error));
                break;
            case K::Aborted:
                return; // cancel() already finished the lookup
            }
            step(pending);
        });
}

void AutoConfig::handleDocument(const std::shared_ptr<Pending> &pending, const QString &source, const QString &url,
                                const QByteArray &body)
{
    const ClientConfig config = ClientConfigParser::parse(body, pending->emailAddress);
    bool usable = false;
    const MailAccount account = config.ok ? config.toAccount(pending->emailAddress, &usable) : MailAccount();
    if (!config.ok || !usable) {
        const QString why = !config.ok ? config.error
                                       : QStringLiteral("No encrypted IMAP/POP3 and SMTP server pair was listed.");
        pending->attempts.append(attempt(source, url, "invalid", why));
        step(pending);
        return;
    }
    pending->attempts.append(attempt(source, url, "found"));
    QVariantMap result = makeResult(pending->requestId, kStateAccepted);
    result.insert(QStringLiteral("source"), source);
    result.insert(QStringLiteral("sourceUrl"), url);
    result.insert(QStringLiteral("providerName"), config.displayName);
    result.insert(QStringLiteral("account"), account.toVariantMap());
    result.insert(QStringLiteral("incoming"), serverList(config.incoming));
    result.insert(QStringLiteral("outgoing"), serverList(config.outgoing));
    result.insert(QStringLiteral("documentationUrl"), config.documentationUrl);
    finish(pending, result);
}

QVariantMap AutoConfig::notFound(const std::shared_ptr<Pending> &pending) const
{
    if (!pending->anyReached) {
        return makeResult(pending->requestId, kStateOffline,
                          QStringLiteral("No configuration server could be reached for %1; check the "
                                         "connection or enter the server settings.")
                              .arg(pending->domain),
                          true);
    }
    return makeResult(pending->requestId, kStateRejected,
                      QStringLiteral("No settings were found for %1; enter the server settings.").arg(pending->domain));
}

void AutoConfig::finish(const std::shared_ptr<Pending> &pending, const QVariantMap &base)
{
    if (m_pending.value(pending->requestId) != pending) return;
    m_pending.remove(pending->requestId);
    QVariantMap result = base;
    result.insert(QStringLiteral("emailAddress"), pending->emailAddress);
    result.insert(QStringLiteral("domain"), pending->domain);
    if (!result.contains(QStringLiteral("source"))) result.insert(QStringLiteral("source"), QString());
    result.insert(QStringLiteral("attempts"), pending->attempts);
    if (m_pending.isEmpty()) emit busyChanged();
    emit finished(pending->requestId, result);
}

} // namespace QindaOffice::Mail
