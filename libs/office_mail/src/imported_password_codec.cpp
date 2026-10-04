// SPDX-License-Identifier: GPL-3.0-or-later
#include "imported_password_codec.h"

#include <QDataStream>
#include <QIODevice>
#include <QRegularExpression>

namespace QindaOffice::Mail::ImportedPassword {

bool decode(const QByteArray &serialized, QString &password)
{
    password.clear();
    if (serialized.size() < 4 || serialized.size() > 1024 * 1024) return false;
    // AGENT-CONTRACT: KWallet::Entry::setValue(QString)/password() use the
    // default QDataStream QString codec. Import preserves those bytes (D-067).
    // Its ordinary bounded UTF-16 framing is identical to Qt_6_0; extended
    // size encodings introduced later are outside our one-MiB contract.
    // Inspect its length before extraction: a corrupt length must not cause
    // an unbounded allocation. Extended lengths cannot fit this input bound.
    QDataStream stream(serialized);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::BigEndian);
    quint32 byteLength = 0;
    stream >> byteLength;
    if (byteLength == 0 || byteLength != quint32(serialized.size() - 4) || (byteLength % 2) != 0) return false;
    stream.device()->seek(0);
    QString decoded;
    stream >> decoded;
    if (stream.status() != QDataStream::Ok || !stream.atEnd() || decoded.isNull()) return false;
    // Reject malformed UTF-16 instead of silently replacing invalid units.
    for (qsizetype i = 0; i < decoded.size(); ++i) {
        if (decoded.at(i).isNull()) return false;
        if (decoded.at(i).isHighSurrogate()) {
            if (++i == decoded.size() || !decoded.at(i).isLowSurrogate()) return false;
        } else if (decoded.at(i).isLowSurrogate()) {
            return false;
        }
    }
    password = std::move(decoded);
    return true;
}

bool validKey(const QString &key)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^[A-Za-z0-9._-]{1,128}/(?:imap|smtp|pop3|caldav|ews|ics|oauth-refresh-google|oauth-refresh-microsoft)$"));
    return pattern.match(key).hasMatch();
}

} // namespace QindaOffice::Mail::ImportedPassword
