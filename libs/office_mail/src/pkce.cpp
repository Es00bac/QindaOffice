// SPDX-License-Identifier: GPL-3.0-or-later
#include "pkce.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace QindaOffice::Mail::Pkce {
namespace {

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    // AGENT-GUARD: QRandomGenerator::system() is the OS CSPRNG; global()
    // would be predictable enough to guess a verifier or state.
    QRandomGenerator *rng = QRandomGenerator::system();
    for (int i = 0; i < count; ++i) bytes[i] = static_cast<char>(rng->bounded(256));
    return bytes;
}

} // namespace

QByteArray base64Url(const QByteArray &bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray generateVerifier()
{
    return base64Url(randomBytes(32));
}

QByteArray challengeS256(const QByteArray &verifier)
{
    return base64Url(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256));
}

QByteArray generateState()
{
    return base64Url(randomBytes(16));
}

} // namespace QindaOffice::Mail::Pkce
