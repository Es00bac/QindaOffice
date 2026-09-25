// SPDX-License-Identifier: GPL-3.0-or-later
#include "net_policy.h"

#include <QHostAddress>
#include <QUrl>

namespace QindaOffice::Mail {

bool isLoopbackHost(const QString &host)
{
    QString h = host.trimmed().toLower();
    if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']'))) h = h.mid(1, h.size() - 2);
    if (h == QLatin1String("localhost")) return true;
    const QHostAddress address(h);
    return !address.isNull() && address.isLoopback();
}

bool isSecureOrLoopbackUrl(const QUrl &url, QString *why)
{
    if (!url.isValid() || url.host().isEmpty()) {
        if (why) *why = QStringLiteral("“%1” is not a valid address.").arg(url.toString());
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https")) return true;
    if (scheme == QLatin1String("http") && isLoopbackHost(url.host())) return true;
    if (why) {
        *why = QStringLiteral("%1 is refused: only https:// addresses are used (http:// only on this computer).")
                   .arg(url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery));
    }
    return false;
}

} // namespace QindaOffice::Mail
