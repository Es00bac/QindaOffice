// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Proof Key for Code Exchange (RFC 7636), S256 only, and the random
// `state` value of an authorization request.

#include <QByteArray>

namespace QindaOffice::Mail::Pkce {

// base64url without padding (RFC 7636 Appendix A).
[[nodiscard]] QByteArray base64Url(const QByteArray &bytes);

// 32 random bytes from the system CSPRNG → 43 characters of the unreserved
// set, the RFC's recommended verifier.
[[nodiscard]] QByteArray generateVerifier();

// BASE64URL(SHA256(ASCII(verifier))).
[[nodiscard]] QByteArray challengeS256(const QByteArray &verifier);

// 16 random bytes, base64url: the CSRF `state` of RFC 6749 §10.12.
[[nodiscard]] QByteArray generateState();

} // namespace QindaOffice::Mail::Pkce
