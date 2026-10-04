// SPDX-License-Identifier: GPL-3.0-or-later
#include "imported_password_reader.h"
#include "imported_password_codec.h"
#include "imported_password_wire.h"

#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>

namespace QindaOffice::Mail::ImportedPassword {
namespace {
const QString Root = QStringLiteral("/org/freedesktop/secrets");
const QString Service = QStringLiteral("org.freedesktop.Secret.Service");
const QString Item = QStringLiteral("org.freedesktop.Secret.Item");
const QString Prompt = QStringLiteral("org.freedesktop.Secret.Prompt");
const QString StandardName = QStringLiteral("org.freedesktop.secrets");
const QString NativeName = QStringLiteral("org.qindaqt.Keyring1");
const QString BusName = QStringLiteral("org.freedesktop.DBus");

bool ownedPath(const QString &path)
{
    return path.size() <= 1024 && path.startsWith(Root + QLatin1Char('/'));
}

QVariant variant(const QVariant &value)
{
    return qdbus_cast<QDBusVariant>(value).variant();
}
} // namespace

Reader::Reader(QDBusConnection bus, QString key, bool presenceOnly,
               SecretBackend::Callback done, QObject *parent)
    : QObject(parent), m_bus(std::move(bus)), m_key(std::move(key)), m_done(std::move(done)),
      m_presenceOnly(presenceOnly)
{
    registerTypes();
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        fail(SecretOutcome::Status::Denied, QStringLiteral("Imported credential access timed out."));
    });
}

Reader::~Reader() { cleanup(); }

void Reader::cancel()
{
    setParent(nullptr);
    m_done = {};
    if (!m_finished) finish({SecretOutcome::Status::Failed, {}, {}});
}

void Reader::start()
{
    // Even invalid keys complete later, consistently with SecretBackend.
    QTimer::singleShot(0, this, [this] {
        if (m_finished) return;
        if (!validKey(m_key))
            return fail(SecretOutcome::Status::Failed, QStringLiteral("Invalid imported credential key."));
        if (!m_bus.isConnected())
            return fail(SecretOutcome::Status::Unavailable, QStringLiteral("The native keyring is unavailable."));
        m_deadline.start(70000); // Native prompt expires after 60 seconds.
        pinProvider([this] { search(); });
    });
}

void Reader::call(const QString &destination, const QString &path, const QString &interface,
                  const QString &method, const QVariantList &arguments, const QString &signature, Reply done)
{
    auto message = QDBusMessage::createMethodCall(destination, path, interface, method);
    message.setAutoStartService(false);
    message.setArguments(arguments);
    // AGENT-GUARD: no QDBusInterface (synchronous introspection), activation,
    // KWallet endpoint or provider-private persistence API in this adapter.
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 2000), this);
    ++m_pendingCalls;
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, method, arguments, signature, done](QDBusPendingCallWatcher *) {
        const QDBusMessage reply = watcher->reply();
        watcher->deleteLater();
        --m_pendingCalls;
        if (m_finished) {
            // Cancellation may race a resource-creating reply. Drain it and
            // close only that resource on the originally pinned unique owner.
            if (reply.type() == QDBusMessage::ReplyMessage && reply.signature() == signature) {
                if (method == QLatin1String("OpenSession"))
                    m_session = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1)).path();
                if (method == QLatin1String("Unlock"))
                    m_prompt = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1)).path();
                cleanup();
            }
            if (m_pendingCalls == 0) deleteLater();
            return;
        }
        if (reply.type() == QDBusMessage::ErrorMessage) {
            const QString name = reply.errorName(); // Never expose provider text or a credential key.
            if (m_owner.isEmpty() && method == QLatin1String("GetNameOwner") && arguments == QVariantList{NativeName}
                && name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner"))
                return fail(SecretOutcome::Status::NotFound, {}); // Bridge is inapplicable on other desktops.
            if (name == QLatin1String("org.freedesktop.Secret.Error.IsLocked")
                || name == QLatin1String("org.freedesktop.DBus.Error.AccessDenied"))
                return fail(SecretOutcome::Status::Denied, QStringLiteral("The imported credential is locked or access was denied."));
            const bool unavailable = name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")
                || name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner")
                || name == QLatin1String("org.freedesktop.DBus.Error.NoReply")
                || name == QLatin1String("org.freedesktop.DBus.Error.Disconnected");
            return fail(unavailable ? SecretOutcome::Status::Unavailable : SecretOutcome::Status::Failed,
                        QStringLiteral("The native keyring could not complete imported credential access."));
        }
        // AGENT-NOTE: Qt returns an empty service() for method replies. This
        // is ordinary same-user Secret Service assurance: unique destination,
        // alias equality, owner-change watch and final re-pin (D-067), not raw
        // reply-sender or executable attestation.
        if (reply.type() != QDBusMessage::ReplyMessage || reply.signature() != signature)
            return fail(SecretOutcome::Status::Failed, QStringLiteral("The native keyring returned an invalid response."));
        done(reply);
    });
}

void Reader::pinProvider(std::function<void()> done)
{
    call(BusName, QStringLiteral("/org/freedesktop/DBus"), BusName, QStringLiteral("GetNameOwner"),
         {NativeName}, QStringLiteral("s"), [this, done](const QDBusMessage &reply) {
        const QString owner = reply.arguments().at(0).toString();
        if (!owner.startsWith(QLatin1Char(':')) || (!m_owner.isEmpty() && owner != m_owner))
            return fail(SecretOutcome::Status::Unavailable, QStringLiteral("The native keyring provider changed."));
        call(BusName, QStringLiteral("/org/freedesktop/DBus"), BusName, QStringLiteral("GetNameOwner"),
             {StandardName}, QStringLiteral("s"), [this, owner, done](const QDBusMessage &standardReply) {
            if (standardReply.arguments().at(0).toString() != owner)
                return fail(SecretOutcome::Status::Unavailable, QStringLiteral("The imported credential requires the native keyring."));
            if (m_owner.isEmpty()) {
                m_owner = owner;
                auto *watcher = new QDBusServiceWatcher(StandardName, m_bus,
                    QDBusServiceWatcher::WatchForOwnerChange, this);
                watcher->addWatchedService(NativeName);
                connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
                        [this](const QString &, const QString &, const QString &newOwner) {
                    if (!m_finished && newOwner != m_owner)
                        fail(SecretOutcome::Status::Unavailable, QStringLiteral("The native keyring provider changed."));
                });
            }
            done();
        });
    });
}

void Reader::search()
{
    call(m_owner, Root, Service, QStringLiteral("SearchItems"), {QVariant::fromValue(attributesFor(m_key))},
         QStringLiteral("aoao"), [this](const QDBusMessage &reply) {
        const auto unlocked = qdbus_cast<Paths>(reply.arguments().at(0));
        const auto locked = qdbus_cast<Paths>(reply.arguments().at(1));
        if (unlocked.isEmpty() && locked.isEmpty())
            return fail(SecretOutcome::Status::NotFound, QStringLiteral("No imported credential is stored for this account."));
        if (unlocked.size() + locked.size() != 1)
            return fail(SecretOutcome::Status::Failed, QStringLiteral("Multiple imported credentials match; resolve them in Passwords and Keys."));
        m_item = (unlocked.isEmpty() ? locked.first() : unlocked.first()).path();
        if (!ownedPath(m_item))
            return fail(SecretOutcome::Status::Failed, QStringLiteral("The imported credential path is invalid."));
        if (m_presenceOnly) return finish({SecretOutcome::Status::Ok, {}, {}});
        if (!locked.isEmpty()) return unlock();
        checkItem();
    });
}

void Reader::unlock()
{
    call(m_owner, Root, Service, QStringLiteral("Unlock"),
         {QVariant::fromValue(Paths{QDBusObjectPath(m_item)})}, QStringLiteral("aoo"),
         [this](const QDBusMessage &reply) {
        const QString prompt = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1)).path();
        if (prompt == QLatin1String("/")) return checkItem();
        if (!ownedPath(prompt))
            return fail(SecretOutcome::Status::Failed, QStringLiteral("The native keyring prompt is invalid."));
        m_prompt = prompt;
        if (!m_bus.connect(m_owner, m_prompt, Prompt, QStringLiteral("Completed"),
                           this, SLOT(promptCompleted(bool,QDBusVariant))))
            return fail(SecretOutcome::Status::Failed, QStringLiteral("The native keyring prompt could not be observed."));
        call(m_owner, m_prompt, Prompt, QStringLiteral("Prompt"), {QString()}, {}, [](const QDBusMessage &) {});
    });
}

void Reader::promptCompleted(bool dismissed, const QDBusVariant &result)
{
    if (m_finished || m_prompt.isEmpty()) return;
    m_bus.disconnect(m_owner, m_prompt, Prompt, QStringLiteral("Completed"),
                     this, SLOT(promptCompleted(bool,QDBusVariant)));
    m_prompt.clear();
    if (dismissed)
        return fail(SecretOutcome::Status::Denied, QStringLiteral("Imported credential access was cancelled or refused."));
    const QVariant value = result.variant();
    if (value.metaType() != QMetaType::fromType<Paths>()
        && (value.metaType() != QMetaType::fromType<QDBusArgument>()
            || value.value<QDBusArgument>().currentSignature() != QLatin1String("ao")))
        return fail(SecretOutcome::Status::Failed, QStringLiteral("The native keyring prompt returned an invalid result."));
    // Prompt results may name collections. The actual item's state and
    // decrypted attributes, rather than the result list, authorize the read.
    checkItem();
}

void Reader::checkItem()
{
    call(m_owner, m_item, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"),
         {Item, QStringLiteral("Locked")}, QStringLiteral("v"), [this](const QDBusMessage &reply) {
        const QVariant locked = variant(reply.arguments().at(0));
        if (locked.metaType() != QMetaType::fromType<bool>())
            return fail(SecretOutcome::Status::Failed, QStringLiteral("The imported credential lock state is invalid."));
        if (locked.toBool())
            return fail(SecretOutcome::Status::Denied, QStringLiteral("The imported credential remains locked."));
        call(m_owner, m_item, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"),
             {Item, QStringLiteral("Attributes")}, QStringLiteral("v"), [this](const QDBusMessage &attributesReply) {
            const QVariant attributes = variant(attributesReply.arguments().at(0));
            if (qdbus_cast<Attributes>(attributes) != attributesFor(m_key))
                return fail(SecretOutcome::Status::Failed, QStringLiteral("The imported credential does not match this account."));
            // Locked search indexes are discovery only. Recheck uniqueness
            // after unlocking, against authenticated native item attributes.
            call(m_owner, Root, Service, QStringLiteral("SearchItems"), {QVariant::fromValue(attributesFor(m_key))},
                 QStringLiteral("aoao"), [this](const QDBusMessage &searchReply) {
                const auto unlocked = qdbus_cast<Paths>(searchReply.arguments().at(0));
                const auto locked = qdbus_cast<Paths>(searchReply.arguments().at(1));
                if (unlocked != Paths{QDBusObjectPath(m_item)} || !locked.isEmpty())
                    return fail(SecretOutcome::Status::Failed, QStringLiteral("The imported credential match changed."));
                openSession();
            });
        });
    });
}

void Reader::openSession()
{
    call(m_owner, Root, Service, QStringLiteral("OpenSession"),
         {QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString()))}, QStringLiteral("vo"),
         [this](const QDBusMessage &reply) {
        const QVariant output = variant(reply.arguments().at(0));
        m_session = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1)).path();
        if (output.metaType() != QMetaType::fromType<QString>() || !output.toString().isEmpty() || !ownedPath(m_session))
            return fail(SecretOutcome::Status::Failed, QStringLiteral("The native keyring session is invalid."));
        readSecret();
    });
}

void Reader::readSecret()
{
    call(m_owner, m_item, Item, QStringLiteral("GetSecret"), {QVariant::fromValue(QDBusObjectPath(m_session))},
         QStringLiteral("(oayays)"), [this](const QDBusMessage &reply) {
        auto wire = qdbus_cast<WireSecret>(reply.arguments().at(0));
        QString password;
        const bool valid = wire.session.path() == m_session && wire.parameters.isEmpty()
            && wire.contentType == QLatin1String("application/vnd.kde.kwallet-entry") && decode(wire.value, password);
        wire.value.fill(0);
        if (!valid)
            return fail(SecretOutcome::Status::Failed, QStringLiteral("The imported Password record is malformed or has an unsupported type."));
        pinProvider([this, password] { finish({SecretOutcome::Status::Ok, password, {}}); });
    });
}

void Reader::fail(SecretOutcome::Status status, const QString &error) { finish({status, {}, error}); }

void Reader::finish(SecretOutcome outcome)
{
    if (m_finished) return;
    m_finished = true;
    m_deadline.stop();
    cleanup();
    auto done = std::move(m_done);
    if (m_pendingCalls == 0) deleteLater();
    if (done) done(outcome);
}

void Reader::cleanup()
{
    if (m_owner.isEmpty()) return;
    const auto close = [this](const QString &path, const QString &interface, const QString &method) {
        if (!ownedPath(path)) return;
        auto message = QDBusMessage::createMethodCall(m_owner, path, interface, method);
        message.setAutoStartService(false);
        m_bus.asyncCall(message, 2000);
    };
    if (!m_prompt.isEmpty()) {
        m_bus.disconnect(m_owner, m_prompt, Prompt, QStringLiteral("Completed"),
                         this, SLOT(promptCompleted(bool,QDBusVariant)));
        close(m_prompt, Prompt, QStringLiteral("Dismiss"));
        m_prompt.clear();
    }
    close(m_session, QStringLiteral("org.freedesktop.Secret.Session"), QStringLiteral("Close"));
    m_session.clear();
}
} // namespace QindaOffice::Mail::ImportedPassword
