// SPDX-License-Identifier: GPL-3.0-or-later
#include "windows_zones.h"

#include <QTimeZone>

namespace QindaOffice::Mail::Zones {
namespace {
bool isUtc(const QString &name)
{
    static const QStringList names{QStringLiteral("UTC"), QStringLiteral("Etc/UTC"), QStringLiteral("GMT"),
        QStringLiteral("Etc/GMT"), QStringLiteral("Coordinated Universal Time"), QStringLiteral("tzone://Microsoft/Utc")};
    return names.contains(name, Qt::CaseInsensitive);
}
} // namespace

QString toWindows(const QString &iana)
{
    if (isUtc(iana)) return QStringLiteral("UTC");
    return QString::fromUtf8(QTimeZone::ianaIdToWindowsId(iana.toUtf8()));
}

QString toIana(const QString &name, const QStringList &preferred)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return {};
    if (isUtc(trimmed)) {
        for (const QString &candidate : preferred)
            if (isUtc(candidate)) return candidate;
        return QStringLiteral("UTC");
    }
    // An IANA id is taken as it is; Windows names are not valid QTimeZone ids.
    if (QTimeZone(trimmed.toUtf8()).isValid()) return trimmed;
    const QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(trimmed.toUtf8());
    if (iana.isEmpty()) return {};
    for (const QString &candidate : preferred)
        if (!candidate.isEmpty() && toWindows(candidate) == trimmed) return candidate;
    return QString::fromUtf8(iana);
}

} // namespace QindaOffice::Mail::Zones
