// SPDX-License-Identifier: GPL-3.0-or-later
#include "synthetic_secret_service.h"

#include <QDBusMessage>
#include <QDBusVariant>
#include <QDataStream>
#include <QIODevice>
#include <QTimer>

using namespace QindaOffice::Mail::ImportedPassword;
namespace {
const QString Root = QStringLiteral("/org/freedesktop/secrets");
const QString ItemPath = Root + QStringLiteral("/collection/synthetic/item1");
const QString StandardItemPath = Root + QStringLiteral("/collection/synthetic/standard");
const QString SessionPath = Root + QStringLiteral("/session/synthetic");
const QString PromptPath = Root + QStringLiteral("/prompt/synthetic");
QVariant path(const QString &value) { return QVariant::fromValue(QDBusObjectPath(value)); }
using SecretMap = QMap<QDBusObjectPath, WireSecret>;
Attributes standardAttributes()
{
    return {{QStringLiteral("user"), QStringLiteral("synthetic/imap")},
            {QStringLiteral("server"), QStringLiteral("qindaoffice-mail")},
            {QStringLiteral("type"), QStringLiteral("plaintext")}};
}
}

SyntheticSecretService::SyntheticSecretService(QDBusConnection bus) : m_bus(std::move(bus))
{
    registerTypes();
    qDBusRegisterMetaType<SecretMap>();
    reset();
}

void SyntheticSecretService::reset()
{
    attributes = attributesFor(QStringLiteral("synthetic/imap"));
    serialized.clear();
    QDataStream stream(&serialized, QIODevice::WriteOnly);
    stream << QString::fromUtf8("synthetic 密碼 🔒");
    contentType = QStringLiteral("application/vnd.kde.kwallet-entry");
    wireSession = SessionPath;
    wireParameters.clear();
    matches = 1;
    locked = forceDiscovery = malformedSearch = errorOnRead = changeOwnerOnRead = false;
    delaySessionReply = delayUnlockReply = standardCredential = false;
    replaceOwnerOnRead = {};
    unlocking = Unlock::Immediate;
    calls.clear();
    queries.clear();
}

bool SyntheticSecretService::handleMessage(const QDBusMessage &message, const QDBusConnection &connection)
{
    ++calls[message.member()];
    const auto reply = [&](const QVariantList &args = {}) { connection.send(message.createReply(args)); };
    const auto error = [&](const QString &name) { connection.send(message.createErrorReply(name, QStringLiteral("synthetic failure"))); };
    const auto args = message.arguments();
    if (message.member() == QLatin1String("SearchItems")) {
        if (malformedSearch) { reply({QStringLiteral("invalid")}); return true; }
        const auto query = qdbus_cast<Attributes>(args.at(0));
        queries.append(query);
        Paths paths;
        if (standardCredential && query == standardAttributes()) {
            paths.append(QDBusObjectPath(StandardItemPath));
        } else if (forceDiscovery || query == attributes) {
            for (int i = 0; i < matches; ++i)
                paths.append(QDBusObjectPath(i == 0 ? ItemPath : Root + QStringLiteral("/collection/synthetic/item2")));
        }
        reply({QVariant::fromValue(locked ? Paths{} : paths), QVariant::fromValue(locked ? paths : Paths{})});
    } else if (message.member() == QLatin1String("Unlock")) {
        if (unlocking == Unlock::Immediate) locked = false;
        const bool prompt = unlocking == Unlock::PromptAccept || unlocking == Unlock::PromptCancel
            || unlocking == Unlock::PromptMalformed || unlocking == Unlock::PromptPending;
        const auto response = message.createReply({QVariant::fromValue(locked ? Paths{} : Paths{QDBusObjectPath(ItemPath)}),
                                                   path(prompt ? PromptPath : QStringLiteral("/"))});
        if (delayUnlockReply) QTimer::singleShot(100, this, [connection, response] { connection.send(response); });
        else connection.send(response);
    } else if (message.member() == QLatin1String("Prompt")) {
        reply();
        if (unlocking != Unlock::PromptPending) {
            QTimer::singleShot(0, this, [this, client = message.service()] {
                const bool dismissed = unlocking == Unlock::PromptCancel;
                if (!dismissed) locked = false;
                auto signal = QDBusMessage::createTargetedSignal(client, PromptPath,
                    QStringLiteral("org.freedesktop.Secret.Prompt"), QStringLiteral("Completed"));
                const QVariant result = unlocking == Unlock::PromptMalformed
                    ? QVariant(QStringLiteral("invalid")) : QVariant::fromValue(Paths{QDBusObjectPath(ItemPath)});
                signal.setArguments({dismissed, QVariant::fromValue(QDBusVariant(result))});
                m_bus.send(signal);
            });
        }
    } else if (message.member() == QLatin1String("GetAll")) {
        if (message.path() == StandardItemPath)
            reply({QVariantMap{{QStringLiteral("Locked"), false}, {QStringLiteral("Attributes"), QVariant::fromValue(standardAttributes())},
                              {QStringLiteral("Label"), QStringLiteral("synthetic")},
                              {QStringLiteral("Created"), QVariant::fromValue(quint64(1))},
                              {QStringLiteral("Modified"), QVariant::fromValue(quint64(1))}}});
        else reply({QVariantMap{{QStringLiteral("Collections"), QVariant::fromValue(Paths{})}}});
    } else if (message.member() == QLatin1String("Get")) {
        const bool getLocked = args.at(1).toString() == QLatin1String("Locked");
        const QVariant value = getLocked ? QVariant(locked) : QVariant::fromValue(attributes);
        reply({QVariant::fromValue(QDBusVariant(value))});
    } else if (message.member() == QLatin1String("OpenSession")) {
        if (args.at(0).toString() != QLatin1String("plain")) {
            error(QStringLiteral("org.freedesktop.DBus.Error.NotSupported"));
        } else {
            const auto response = message.createReply({QVariant::fromValue(QDBusVariant(QString())), path(SessionPath)});
            if (delaySessionReply) QTimer::singleShot(100, this, [connection, response] { connection.send(response); });
            else connection.send(response);
        }
    } else if (message.member() == QLatin1String("GetSecrets")) {
        const auto requested = qdbus_cast<Paths>(args.at(0));
        SecretMap secrets;
        if (requested.contains(QDBusObjectPath(StandardItemPath)))
            secrets.insert(QDBusObjectPath(StandardItemPath), {qdbus_cast<QDBusObjectPath>(args.at(1)), {},
                QByteArray("new native synthetic"), QStringLiteral("text/plain")});
        reply({QVariant::fromValue(secrets)});
    } else if (message.member() == QLatin1String("GetSecret")) {
        if (changeOwnerOnRead) m_bus.unregisterService(QStringLiteral("org.qindaqt.Keyring1"));
        if (replaceOwnerOnRead) replaceOwnerOnRead();
        if (locked || errorOnRead) {
            error(QStringLiteral("org.freedesktop.Secret.Error.IsLocked"));
        } else {
            reply({QVariant::fromValue(WireSecret{QDBusObjectPath(wireSession), wireParameters, serialized, contentType})});
        }
    } else if (message.member() == QLatin1String("Close") || message.member() == QLatin1String("Dismiss")) {
        reply();
    } else {
        error(QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"));
    }
    return true;
}
