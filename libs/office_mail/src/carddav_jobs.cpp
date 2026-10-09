// SPDX-License-Identifier: GPL-3.0-or-later
// CardDAV: the worker-thread dialogue and address-book discovery
// (carddav_jobs.h). The sync dialogue is carddav_sync.cpp.

#include "carddav_dialogue_p.h"

#include <qindatk/caldav_discovery.h>

namespace QindaOffice::Mail::CardDav {

using QindaTK::Protocol::Failure;

namespace Detail {

bool Dialogue::request(const QByteArray &method, const QUrl &url, const QMap<QByteArray, QByteArray> &headers,
                       const QByteArray &body, QindaTK::Protocol::HttpResponse *response)
{
    namespace Protocol = QindaTK::Protocol;
    *response = {};
    if (failed()) return false;
    const bool https = url.scheme() == QLatin1String("https");
    if (!url.isValid() || url.host().isEmpty() || (!https && url.scheme() != QLatin1String("http"))) {
        fail(Failure::Rejected, QStringLiteral("An address book needs an https address (%1)").arg(url.toDisplayString()));
        return false;
    }
    const bool credentials = !m_credentials.authorization.isEmpty();
    if (credentials && !Protocol::plaintextAuthAllowed(url.host(), https, m_credentials.allowInsecureAuth)) {
        fail(Failure::Rejected, QStringLiteral("Refusing to send the password over plain HTTP to %1").arg(url.host()));
        return false;
    }
    if (credentials && !m_credentials.site.isEmpty() && !QindaTK::CalDavDiscovery::sameSite(m_credentials.site, url.host())) {
        fail(Failure::Rejected, QStringLiteral("The server sent QindaMail to another site (%1); the password was not sent")
                                    .arg(url.host()));
        return false;
    }
    if (++m_requests > kMaxRequests) {
        fail(Failure::Rejected, QStringLiteral("The address book needs more than %1 requests").arg(kMaxRequests));
        return false;
    }
    if (m_requests == 1) {
        for (const QString &secret : m_credentials.secrets) m_channel.addSecret(secret);
    }
    Protocol::Endpoint endpoint;
    endpoint.host = url.host();
    endpoint.port = quint16(url.port(https ? 443 : 80));
    endpoint.security = https ? QStringLiteral("tls") : QStringLiteral("none");
    endpoint.timeoutMs = m_credentials.timeoutMs;
    endpoint.allowInsecureAuth = m_credentials.allowInsecureAuth;
    QMap<QByteArray, QByteArray> all = headers;
    if (credentials) all.insert("Authorization", m_credentials.authorization);
    all.insert("User-Agent", "QindaMail-CardDAV/1");
    QByteArray target = url.path(QUrl::FullyEncoded).toUtf8();
    if (target.isEmpty()) target = "/";
    if (url.hasQuery()) target += '?' + url.query(QUrl::FullyEncoded).toUtf8();
    if (!Protocol::httpRequest(m_channel, endpoint, method, target, all, body, response, kMaxBody)) return false;
    // AGENT-NOTE (RFC 6578 §3.2): an expired sync token is a 403 with the
    // DAV:valid-sync-token precondition — the sync starts over, it is not a
    // sign-in problem.
    const bool expiredToken = response->status == 403 && response->body.contains("valid-sync-token");
    if ((response->status == 401 || response->status == 403) && !expiredToken) {
        m_authRejected = true;
        fail(Failure::Rejected, QStringLiteral("The address book server refused the credentials (HTTP %1)").arg(response->status));
        return false;
    }
    if (response->status == 429 || response->status >= 500) {
        fail(Failure::Offline, QStringLiteral("The address book server is busy (HTTP %1)").arg(response->status));
        return false;
    }
    return true;
}

bool Dialogue::multistatus(const QByteArray &method, const QUrl &url, int depth, const QByteArray &body,
                           Multistatus *out, int *status)
{
    *out = Multistatus();
    QMap<QByteArray, QByteArray> headers{{"Content-Type", "application/xml; charset=utf-8"}};
    if (depth >= 0) headers.insert("Depth", QByteArray::number(depth));
    QindaTK::Protocol::HttpResponse response;
    if (!request(method, url, headers, body, &response)) return false;
    *status = response.status;
    if (response.status != 207) return true;
    QString error;
    if (!parseMultistatus(response.body, out, &error)) {
        fail(Failure::Rejected, error);
        return false;
    }
    return true;
}

void Dialogue::fail(Failure failure, const QString &error)
{
    m_channel.fail(failure, error);
}

void Dialogue::finish(Outcome *out) const
{
    namespace Protocol = QindaTK::Protocol;
    const Failure failure = m_channel.failure();
    out->state = Protocol::failureState(failure);
    out->error = failure == Failure::None ? QString() : Protocol::redact(m_channel.error(), m_credentials.secrets);
    out->retryable = failure == Failure::Offline;
    out->authRejected = m_authRejected;
    out->requests = m_requests;
    out->transcript.clear();
    for (const QString &line : m_channel.transcript()) out->transcript.append(Protocol::redact(line, m_credentials.secrets));
}

} // namespace Detail

QString Detail::pathKey(const QUrl &url)
{
    return url.adjusted(QUrl::StripTrailingSlash | QUrl::NormalizePathSegments).path(QUrl::FullyEncoded);
}

DavResponse Detail::selfOf(const Multistatus &ms, const QUrl &url)
{
    for (const DavResponse &response : ms.responses) {
        if (pathKey(url.resolved(QUrl(response.href))) == pathKey(url)) return response;
    }
    return ms.responses.isEmpty() ? DavResponse() : ms.responses.first();
}

namespace {

using Detail::pathKey;
using Detail::selfOf;

// PROPFIND following redirects (discovery only: the well-known URI answers
// with one, and servers move their context path).
bool propfindFollow(Detail::Dialogue &d, QUrl *url, int depth, const QByteArray &body, Multistatus *out, int *status)
{
    for (int hop = 0; hop <= 5; ++hop) {
        QindaTK::Protocol::HttpResponse response;
        const QMap<QByteArray, QByteArray> headers{{"Content-Type", "application/xml; charset=utf-8"},
                                                   {"Depth", QByteArray::number(depth)}};
        if (!d.request("PROPFIND", *url, headers, body, &response)) return false;
        *status = response.status;
        if (response.status >= 300 && response.status < 400 && response.headers.contains("location")) {
            *url = url->resolved(QUrl(QString::fromUtf8(response.headers.value("location"))));
            continue;
        }
        *out = Multistatus();
        QString error;
        if (response.status == 207 && !parseMultistatus(response.body, out, &error)) {
            d.fail(Failure::Rejected, error);
            return false;
        }
        return true;
    }
    d.fail(Failure::Rejected, QStringLiteral("The address book server redirects too often"));
    return false;
}

} // namespace

QString hrefKey(const QUrl &base, const QString &href)
{
    return base.resolved(QUrl(href)).path(QUrl::FullyEncoded);
}

Discovery discover(const QUrl &start, const Credentials &credentials)
{
    Discovery out;
    Detail::Dialogue d(credentials);
    QUrl url = start;
    Multistatus ms;
    int status = 0;
    const auto done = [&d, &out] {
        d.finish(&out);
        if (out.state != QLatin1String("accepted")) out.books.clear();
        return out;
    };
    if (!propfindFollow(d, &url, 0, propfindDiscovery(), &ms, &status)) return done();
    DavResponse self = selfOf(ms, url);
    const auto useful = [](int code, const DavResponse &r) {
        return code == 207 && (!r.principal.isEmpty() || !r.homeSet.isEmpty() || r.resourceTypes.contains(QLatin1String("addressbook")));
    };
    if (!useful(status, self) && url.path() != QLatin1String("/.well-known/carddav")) {
        // RFC 6764 §5: the well-known URI on the same origin names the context path.
        QUrl known = start;
        known.setPath(QStringLiteral("/.well-known/carddav"));
        known.setQuery(QString());
        Multistatus viaKnown;
        int knownStatus = 0;
        if (!propfindFollow(d, &known, 0, propfindDiscovery(), &viaKnown, &knownStatus)) return done();
        if (useful(knownStatus, selfOf(viaKnown, known)) || (status != 207 && knownStatus == 207)) {
            url = known;
            ms = viaKnown;
            status = knownStatus;
            self = selfOf(ms, url);
        }
    }
    if (status != 207) {
        d.fail(Failure::Rejected, QStringLiteral("No CardDAV address book service answered at %1 (HTTP %2)")
                                      .arg(start.toDisplayString()).arg(status));
        return done();
    }
    if (self.resourceTypes.contains(QLatin1String("addressbook"))) {
        out.books.append({url.toString(), self.displayName.isEmpty() ? pathKey(url).section(QLatin1Char('/'), -1) : self.displayName});
        return done();
    }
    QUrl principal = self.principal.isEmpty() ? QUrl() : url.resolved(QUrl(self.principal));
    QUrl home = self.homeSet.isEmpty() ? QUrl() : url.resolved(QUrl(self.homeSet));
    if (home.isEmpty() && principal.isValid() && pathKey(principal) != pathKey(url)) {
        Multistatus props;
        int code = 0;
        if (!propfindFollow(d, &principal, 0, propfindDiscovery(), &props, &code)) return done();
        const DavResponse p = selfOf(props, principal);
        if (code == 207 && !p.homeSet.isEmpty()) home = principal.resolved(QUrl(p.homeSet));
    }
    if (home.isEmpty() && self.resourceTypes.contains(QLatin1String("collection"))) home = url;
    if (home.isEmpty()) {
        d.fail(Failure::Rejected, QStringLiteral("The server did not name an address book home"));
        return done();
    }
    Multistatus members;
    if (!propfindFollow(d, &home, 1, propfindBooks(), &members, &status)) return done();
    if (status != 207) {
        d.fail(Failure::Rejected, QStringLiteral("Listing the address books failed (HTTP %1)").arg(status));
        return done();
    }
    for (const DavResponse &member : std::as_const(members.responses)) {
        QUrl book = home.resolved(QUrl(member.href));
        if (pathKey(book) == pathKey(home) || !member.resourceTypes.contains(QLatin1String("addressbook"))) continue;
        if (!book.path().endsWith(QLatin1Char('/'))) book.setPath(book.path() + QLatin1Char('/'));
        if (out.books.size() >= 500) break;
        out.books.append({book.toString(),
                          member.displayName.isEmpty() ? pathKey(book).section(QLatin1Char('/'), -1) : member.displayName});
    }
    out.principalUrl = principal.isValid() ? principal.toString() : QString();
    out.homeSetUrl = home.toString();
    return done();
}

} // namespace QindaOffice::Mail::CardDav
