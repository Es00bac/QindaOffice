// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private state of OAuth2Session shared by oauth2_session.cpp (authorize)
// and oauth2_tokens.cpp (token endpoint). Not part of the library's API.

#include "oauth2_session.h"

#include <QByteArray>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace QindaOffice::Mail {

struct OAuth2Session::AuthFlow {
    QString requestId;
    QByteArray verifier;        // PKCE; never leaves this process except in the code redemption
    QByteArray state;
    QUrl url;                   // the authorization request
    QUrl redirectUri;
    LoopbackReceiver *receiver = nullptr;   // child of the session, deleteLater'd
    QTimer *timer = nullptr;                // child of the session
    QPointer<QNetworkReply> exchange;       // the code redemption, while running
};

// One token-endpoint answer, classified.
struct OAuth2Session::TokenGrant {
    bool ok = false;
    const char *state = "rejected";
    QString error;
    bool retryable = false;
    bool invalidGrant = false;  // the refresh token (or code) is dead
    bool aborted = false;
    QString accessToken;
    QString refreshToken;
    QString scope;
    qint64 expiresIn = 0;
};

} // namespace QindaOffice::Mail
