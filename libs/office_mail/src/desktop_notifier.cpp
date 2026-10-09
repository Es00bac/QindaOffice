// SPDX-License-Identifier: GPL-3.0-or-later
// Desktop alerts over org.freedesktop.Notifications (desktop_notifier.h).
#include "desktop_notifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;

namespace {

DesktopNotifier::Sink &sink()
{
    static DesktopNotifier::Sink instance;
    return instance;
}

} // namespace

void DesktopNotifier::setSink(Sink replacement)
{
    sink() = std::move(replacement);
}

bool DesktopNotifier::notify(const QString &title, const QString &body)
{
    if (sink()) {
        sink()(title, body);
        return true;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    QDBusMessage call = QDBusMessage::createMethodCall(u"org.freedesktop.Notifications"_s,
                                                       u"/org/freedesktop/Notifications"_s,
                                                       u"org.freedesktop.Notifications"_s, u"Notify"_s);
    const QVariantMap hints{{u"desktop-entry"_s, u"org.qindaqt.QindaMail"_s},
                            {u"category"_s, u"email.arrived"_s},
                            {u"suppress-sound"_s, true}};
    call << u"QindaMail"_s << uint(0) << u"org.qindaqt.QindaMail"_s << title.left(200) << body.left(1000)
         << QStringList() << hints << int(-1);
    // AGENT-NOTE: asyncCall, not call(): a slow or missing notification
    // daemon must never block the window that runs the rules.
    static_cast<void>(bus.asyncCall(call));
    return true;
}

} // namespace QindaOffice::Mail
