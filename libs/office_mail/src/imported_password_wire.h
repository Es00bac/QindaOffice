// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Client-owned standard Secret Service types. Do not include the desktop's
// private keyring headers or infer its persistence layout (D-067).
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QMap>

namespace QindaOffice::Mail::ImportedPassword {
using Attributes = QMap<QString, QString>;
using Paths = QList<QDBusObjectPath>;
struct WireSecret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};
inline QDBusArgument &operator<<(QDBusArgument &argument, const WireSecret &secret)
{
    argument.beginStructure();
    argument << secret.session << secret.parameters << secret.value << secret.contentType;
    argument.endStructure();
    return argument;
}
inline const QDBusArgument &operator>>(const QDBusArgument &argument, WireSecret &secret)
{
    argument.beginStructure();
    argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    argument.endStructure();
    return argument;
}
inline void registerTypes()
{
    qDBusRegisterMetaType<Attributes>();
    qDBusRegisterMetaType<Paths>();
    qDBusRegisterMetaType<WireSecret>();
}
inline Attributes attributesFor(const QString &key)
{
    return {{QStringLiteral("qindaqt.import.kwallet.folder"), QStringLiteral("qindaoffice-mail")},
            {QStringLiteral("qindaqt.import.kwallet.key"), key},
            {QStringLiteral("qindaqt.import.kwallet.type"), QStringLiteral("1")}};
}
} // namespace QindaOffice::Mail::ImportedPassword
Q_DECLARE_METATYPE(QindaOffice::Mail::ImportedPassword::WireSecret)
