// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SASL XOAUTH2 (Google / Microsoft): the initial client response
// "user=<address>^Aauth=Bearer <token>^A^A", base64-encoded.
//
// AGENT-CONTRACT: the QindaTK SMTP and IMAP providers build this payload
// THEMSELVES from `userName` and the raw access token passed with
// setSecret(token, "xoauth2") (QindaTK src/core/smtp_mail_provider.cpp,
// imap_session.cpp). Hosts therefore hand them the ACCESS TOKEN
// (OAuth2Session::applyAccessToken), never this payload — passing the
// payload would base64 it twice and the server would refuse the login.
// These helpers are for protocols the toolkit does not speak (e.g. a POP3
// AUTH XOAUTH2 before the toolkit's POP3 provider supports it) and for
// tests that pin the wire format both sides must agree on.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace QindaOffice::Mail::XOAuth2 {

// The raw (not encoded) initial response.
[[nodiscard]] QByteArray rawInitialResponse(const QString &userName, const QString &accessToken);
// base64(rawInitialResponse) — what follows "AUTH XOAUTH2 " / "AUTHENTICATE XOAUTH2 ".
[[nodiscard]] QByteArray initialResponse(const QString &userName, const QString &accessToken);
// Decodes the base64 JSON error challenge a server sends when the token is
// refused ({"status":"401","schemes":"bearer","scope":"…"}); empty object
// when the challenge is not one.
[[nodiscard]] QJsonObject decodeErrorChallenge(const QByteArray &base64Challenge);

} // namespace QindaOffice::Mail::XOAuth2
