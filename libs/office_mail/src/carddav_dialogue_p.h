// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the CardDAV jobs (carddav_jobs.cpp, carddav_sync.cpp): one
// worker-thread HTTP dialogue with the credential rules of carddav_jobs.h.

#include "carddav_jobs.h"
#include "carddav_xml.h"

#include <qindatk/protocol_client.h>

#include <QMap>

namespace QindaOffice::Mail::CardDav::Detail {

inline constexpr int kMaxRequests = 20000;
inline constexpr qint64 kMaxBody = 16LL * 1024 * 1024;

// A collection's path without its trailing slash (how resources compare).
[[nodiscard]] QString pathKey(const QUrl &url);
// The response describing `url` itself (else the first).
[[nodiscard]] DavResponse selfOf(const Multistatus &ms, const QUrl &url);

class Dialogue {
public:
    explicit Dialogue(const Credentials &credentials) : m_credentials(credentials) {}

    // One request; false when the dialogue failed as a whole (no connection,
    // a refused destination, 401/403, 429/5xx, too many requests). Any other
    // status is the caller's to read.
    bool request(const QByteArray &method, const QUrl &url, const QMap<QByteArray, QByteArray> &headers,
                 const QByteArray &body, QindaTK::Protocol::HttpResponse *response);
    // A PROPFIND/REPORT answered 207 and parsed; `status` set either way.
    bool multistatus(const QByteArray &method, const QUrl &url, int depth, const QByteArray &body, Multistatus *out,
                     int *status);
    void fail(QindaTK::Protocol::Failure failure, const QString &error);
    [[nodiscard]] bool failed() const { return m_channel.failure() != QindaTK::Protocol::Failure::None; }
    // Fills the common result fields (state, redacted error, transcript).
    void finish(Outcome *out) const;

private:
    const Credentials &m_credentials;
    QindaTK::Protocol::Channel m_channel;
    bool m_authRejected = false;
    int m_requests = 0;
};

} // namespace QindaOffice::Mail::CardDav::Detail
