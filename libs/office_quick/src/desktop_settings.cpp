// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_settings.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <QVariantMap>

namespace QindaOffice::Quick {

namespace {
constexpr auto kService = "org.qindaqt.Settings1";
constexpr auto kPath = "/org/qindaqt/Settings1";
constexpr auto kInterface = "org.qindaqt.Settings1";
} // namespace

QVariantMap DesktopSettings::values(const QStringList &keys)
{
    // AGENT-GUARD: no QDBusInterface — its constructor performs a blocking
    // introspection round-trip that parks the GUI thread up to the 25 s
    // timeout on a degraded bus (the exact trap single_instance.cpp's guard
    // documents). A raw, method-only message degrades visibly and fast.
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QString::fromLatin1(kInterface), QStringLiteral("GetSnapshot"));
    message.setArguments({QVariant(keys)});
    // Bounded wait: callWithArgumentList honours the timeout instead of the
    // 25 s default.
    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(message, 1500);
    pending.waitForFinished();
    if (!pending.isValid() || pending.isError()) {
        return {};
    }
    const QDBusMessage reply = pending.reply();
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {}; // degraded run: defaults apply, never block
    }
    return reply.arguments().first().toMap()
        .value(QStringLiteral("values"))
        .toMap();
}

bool DesktopSettings::reducedMotion()
{
    const QVariantMap snapshot = values({QStringLiteral("accessibility.reducedMotion")});
    return snapshot.value(QStringLiteral("accessibility.reducedMotion")).toBool();
}

} // namespace QindaOffice::Quick
