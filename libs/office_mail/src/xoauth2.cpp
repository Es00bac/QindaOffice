// SPDX-License-Identifier: GPL-3.0-or-later
#include "xoauth2.h"

#include <QJsonDocument>

namespace QindaOffice::Mail::XOAuth2 {

QByteArray rawInitialResponse(const QString &userName, const QString &accessToken)
{
    return "user=" + userName.toUtf8() + "\x01" "auth=Bearer " + accessToken.toUtf8() + "\x01\x01";
}

QByteArray initialResponse(const QString &userName, const QString &accessToken)
{
    return rawInitialResponse(userName, accessToken).toBase64();
}

QJsonObject decodeErrorChallenge(const QByteArray &base64Challenge)
{
    const auto decoded = QByteArray::fromBase64Encoding(base64Challenge.trimmed(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) return {};
    return QJsonDocument::fromJson(*decoded).object();
}

} // namespace QindaOffice::Mail::XOAuth2
