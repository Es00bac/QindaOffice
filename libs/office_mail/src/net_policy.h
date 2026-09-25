// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Transport policy shared by account validation, autoconfig and OAuth:
// credentials and configuration travel over TLS, or to a loopback endpoint
// (local bridges such as Proton Mail Bridge, and the tests). Mirrors the
// toolkit's rule for its providers (QindaTK D-187).

#include <QString>

class QUrl;

namespace QindaOffice::Mail {

// 127.0.0.0/8, ::1 and "localhost". Names are not resolved: a DNS name
// that happens to resolve to loopback is NOT loopback here.
[[nodiscard]] bool isLoopbackHost(const QString &host);

// https:// anywhere, http:// only to a loopback host. `why` receives a
// sentence for the user when the URL is refused.
[[nodiscard]] bool isSecureOrLoopbackUrl(const QUrl &url, QString *why = nullptr);

} // namespace QindaOffice::Mail
