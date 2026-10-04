// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "imported_password_wire.h"
#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QHash>
#include <functional>

// Synthetic, private-bus standard Secret Service peer. Never opens files or
// invokes a keyring/PAM/backend. All values are test literals (D-067).
class SyntheticSecretService final : public QDBusVirtualObject {
    Q_OBJECT
public:
    explicit SyntheticSecretService(QDBusConnection bus);
    void reset();
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;

    QindaOffice::Mail::ImportedPassword::Attributes attributes;
    QByteArray serialized;
    QString contentType;
    QString wireSession;
    QByteArray wireParameters;
    int matches = 1;
    bool locked = false;
    bool forceDiscovery = false;
    bool malformedSearch = false;
    bool errorOnRead = false;
    bool changeOwnerOnRead = false;
    bool delaySessionReply = false;
    bool delayUnlockReply = false;
    bool standardCredential = false;
    std::function<void()> replaceOwnerOnRead;
    enum class Unlock { Immediate, PromptAccept, PromptCancel, PromptMalformed, PromptPending, RemainLocked };
    Unlock unlocking = Unlock::Immediate;
    QHash<QString, int> calls;
    QList<QindaOffice::Mail::ImportedPassword::Attributes> queries;

private:
    QDBusConnection m_bus;
};
