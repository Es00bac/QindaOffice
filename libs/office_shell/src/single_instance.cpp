// SPDX-License-Identifier: GPL-3.0-or-later
#include "single_instance.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariant>

namespace QindaOffice::Shell {

SingleInstance::SingleInstance(QString serviceName, QString objectPath, QObject *parent)
    : QObject(parent)
    , m_serviceName(std::move(serviceName))
    , m_objectPath(std::move(objectPath))
    , m_interfaceName(QStringLiteral("org.qindaqt.App"))
{
}

bool SingleInstance::acquireOrHandOff(const QStringList &paths, bool *handedOff)
{
    *handedOff = false;
    // AGENT-NOTE: isConnected() must be the first call on the session bus:
    // it brings Qt's connection up synchronously and gives a clean
    // standalone fallback when no bus exists. Degraded environments (bus
    // present but denying name ownership) are handled by the failed
    // hand-off path below - never by hanging.
    if (!QDBusConnection::sessionBus().isConnected()) {
        // No session bus: run standalone, always primary (degraded, not
        // broken — the desktop integration rule applies here too).
        m_acquired = true;
        return true;
    }
    auto *bus = QDBusConnection::sessionBus().interface();
    if (bus == nullptr) {
        m_acquired = true;
        return true;
    }
    // AGENT-GUARD: with default flags registerService yields
    // ServiceRegistered when we own the name, and ServiceNotRegistered when
    // someone else already does (ServiceQueued only appears with explicit
    // queue flags). Anything but ServiceRegistered means "another instance
    // is primary" (or this bus denies ownership - a degraded environment).
    const QDBusConnectionInterface::RegisterServiceReply reply =
        bus->registerService(m_serviceName);
    if (reply == QDBusConnectionInterface::ServiceRegistered) {
        m_acquired = true;
        return true;
    }
    *handedOff = invokeOpenPaths(paths);
    return false;
}

bool SingleInstance::invokeOpenPaths(const QStringList &paths) const
{
    // AGENT-GUARD: never use QDBusInterface here - its constructor performs
    // a blocking introspection call that can park the thread forever on a
    // degraded bus. A raw method call with an explicit timeout fails fast.
    QDBusMessage message = QDBusMessage::createMethodCall(
        m_serviceName, m_objectPath, m_interfaceName, QStringLiteral("OpenPaths"));
    message.setArguments({QVariant::fromValue(paths)});
    const QDBusMessage reply =
        QDBusConnection::sessionBus().call(message, QDBus::Block, 2000 /* ms */);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning().noquote() << "running instance not reachable:"
                             << reply.errorMessage();
        return false;
    }
    return true;
}

bool SingleInstance::publish()
{
    if (!m_acquired) {
        return false;
    }
    auto connection = QDBusConnection::sessionBus();
    if (!connection.registerObject(m_objectPath, this,
                                   QDBusConnection::ExportAllInvokables)) {
        qWarning().noquote() << "could not publish" << m_objectPath;
        return false;
    }
    return true;
}

bool SingleInstance::OpenPaths(const QStringList &paths)
{
    emit openPathsRequested(paths);
    return true;
}

} // namespace QindaOffice::Shell
