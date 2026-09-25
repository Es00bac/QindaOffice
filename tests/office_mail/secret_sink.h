// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Stands in for a QindaTK provider in the tests: the same write-only
// setSecret(secret, mechanism) invokable MailSecrets::applySecret and
// OAuth2Session::applyAccessToken call.

#include <QObject>
#include <QString>

namespace QindaOffice::Mail::Test {

class SecretSink : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism)
    {
        received = secret;
        receivedMechanism = mechanism;
    }
    QString received;
    QString receivedMechanism;
};

} // namespace QindaOffice::Mail::Test
