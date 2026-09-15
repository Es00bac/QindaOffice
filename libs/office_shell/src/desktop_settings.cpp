// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_settings.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QVariantMap>

namespace QindaOffice::Shell {

namespace {
constexpr auto kService = "org.qindaqt.Settings1";
constexpr auto kPath = "/org/qindaqt/Settings1";
constexpr auto kInterface = "org.qindaqt.Settings1";
} // namespace

QVariantMap DesktopSettings::values(const QStringList &keys)
{
    QDBusInterface iface(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                         QString::fromLatin1(kInterface));
    if (!iface.isValid()) {
        return {};
    }
    QDBusReply<QVariantMap> reply = iface.call(QStringLiteral("GetSnapshot"), keys);
    if (!reply.isValid()) {
        return {};
    }
    return reply.value().value(QStringLiteral("values")).toMap();
}

bool DesktopSettings::reducedMotion()
{
    const QVariantMap snapshot = values({QStringLiteral("accessibility.reducedMotion")});
    return snapshot.value(QStringLiteral("accessibility.reducedMotion")).toBool();
}

} // namespace QindaOffice::Shell
