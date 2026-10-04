// SPDX-License-Identifier: GPL-3.0-or-later
#include "imported_password_backend.h"
#include "synthetic_secret_service.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>

using namespace QindaOffice::Mail;
namespace {
const QString Key = QStringLiteral("synthetic/imap");
const QString Native = QStringLiteral("org.qindaqt.Keyring1");
const QString Standard = QStringLiteral("org.freedesktop.secrets");

class ErrorBackend final : public SecretBackend {
public:
    explicit ErrorBackend(SecretOutcome::Status status) : m_status(status) {}
    void read(const QString &, Callback done) override
    { QTimer::singleShot(0, [done, status = m_status] { done({status, {}, QStringLiteral("primary synthetic error")}); }); }
    void write(const QString &, const QString &, Callback) override { QFAIL("unexpected write"); }
    void remove(const QString &, Callback) override { QFAIL("unexpected remove"); }
    QString name() const override { return QStringLiteral("synthetic"); }
private:
    SecretOutcome::Status m_status;
};

SecretOutcome complete(const std::function<void(SecretBackend::Callback)> &operation)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    auto result = std::make_shared<SecretOutcome>(SecretOutcome{SecretOutcome::Status::Failed, {}, QStringLiteral("test timeout")});
    operation([result, loop = QPointer<QEventLoop>(&loop)](const SecretOutcome &outcome) {
        *result = outcome;
        if (loop) loop->quit();
    });
    timeout.start(2500);
    loop.exec();
    return *result;
}
}

class ImportedPasswordBackendTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_peer.isConnected());
        QVERIFY(m_peer.registerService(Standard));
        QVERIFY(m_peer.registerService(Native));
        m_service = std::make_unique<SyntheticSecretService>(m_peer);
        QVERIFY(m_peer.registerVirtualObject(QStringLiteral("/org/freedesktop/secrets"),
                                             m_service.get(), QDBusConnection::SubPath));
    }
    void init()
    {
        m_service->reset();
        m_primary = std::make_shared<InMemorySecretBackend>();
        m_backend = withImportedPasswords(m_primary, QDBusConnection::sessionBus());
    }
    void cleanup()
    {
        m_backend.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
    void cleanupTestCase()
    {
        m_peer.unregisterService(Native);
        m_peer.unregisterService(Standard);
        m_peer.unregisterObject(QStringLiteral("/org/freedesktop/secrets"));
        m_service.reset();
    }
    void importedReadRecoversMissingLookup()
    {
        bool synchronous = true;
        bool called = false;
        int callbacks = 0;
        SecretOutcome result;
        m_backend->read(Key, [&](const SecretOutcome &outcome) {
            QVERIFY(!synchronous);
            result = outcome;
            called = true;
            ++callbacks;
        });
        synchronous = false;
        QTRY_VERIFY_WITH_TIMEOUT(called, 2500);
        QCOMPARE(result.status, SecretOutcome::Status::Ok);
        QCOMPARE(result.value, QString::fromUtf8("synthetic 密碼 🔒"));
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("Close")), 1);
        QCOMPARE(callbacks, 1);
        QCOMPARE(m_service->calls.value(QStringLiteral("GetSecret")), 1);
        QCOMPARE(m_service->calls.value(QStringLiteral("Unlock")), 0);
    }
    void ordinaryNativeValueWins()
    {
        QCOMPARE(complete([&](auto done) { m_primary->write(Key, QStringLiteral("new native value"), done); }).status,
                 SecretOutcome::Status::Ok);
        const auto result = read();
        QCOMPARE(result.status, SecretOutcome::Status::Ok);
        QCOMPARE(result.value, QStringLiteral("new native value"));
        QVERIFY(m_service->calls.isEmpty());
    }
    void actualQtKeychainSchemaMissRecoversImport()
    {
        m_backend = withImportedPasswords(std::make_shared<KeychainSecretBackend>(), QDBusConnection::sessionBus());
        const auto result = read();
        QCOMPARE(result.status, SecretOutcome::Status::Ok);
        QCOMPARE(result.value, QString::fromUtf8("synthetic 密碼 🔒"));
        using Attributes = QindaOffice::Mail::ImportedPassword::Attributes;
        const Attributes plaintext{{QStringLiteral("user"), Key}, {QStringLiteral("server"), QStringLiteral("qindaoffice-mail")},
                                   {QStringLiteral("type"), QStringLiteral("plaintext")}};
        auto base64 = plaintext;
        base64[QStringLiteral("type")] = QStringLiteral("base64");
        QVERIFY(m_service->queries.contains(plaintext));
        QVERIFY(m_service->queries.contains(base64));
        QVERIFY(m_service->queries.contains(QindaOffice::Mail::ImportedPassword::attributesFor(Key)));
    }
    void actualQtKeychainNativeValueWins()
    {
        m_service->standardCredential = true;
        m_backend = withImportedPasswords(std::make_shared<KeychainSecretBackend>(), QDBusConnection::sessionBus());
        const auto result = read();
        QCOMPARE(result.status, SecretOutcome::Status::Ok);
        QCOMPARE(result.value, QStringLiteral("new native synthetic"));
        QVERIFY(!m_service->queries.contains(QindaOffice::Mail::ImportedPassword::attributesFor(Key)));
    }
    void ordinaryErrorsDoNotFallback_data()
    {
        QTest::addColumn<SecretOutcome::Status>("status");
        QTest::newRow("denied") << SecretOutcome::Status::Denied;
        QTest::newRow("unavailable") << SecretOutcome::Status::Unavailable;
        QTest::newRow("failed") << SecretOutcome::Status::Failed;
    }
    void ordinaryErrorsDoNotFallback()
    {
        QFETCH(SecretOutcome::Status, status);
        m_backend = withImportedPasswords(std::make_shared<ErrorBackend>(status), QDBusConnection::sessionBus());
        const auto result = read();
        QCOMPARE(result.status, status);
        QCOMPARE(result.error, QStringLiteral("primary synthetic error"));
        QVERIFY(m_service->calls.isEmpty());
    }
    void noMatchIsNotFound()
    {
        m_service->matches = 0;
        QCOMPARE(read().status, SecretOutcome::Status::NotFound);
        QCOMPARE(m_service->calls.value(QStringLiteral("GetSecret")), 0);
    }
    void wrongLookupAttribute_data()
    {
        QTest::addColumn<QString>("attribute");
        QTest::newRow("folder") << QStringLiteral("qindaqt.import.kwallet.folder");
        QTest::newRow("key") << QStringLiteral("qindaqt.import.kwallet.key");
        QTest::newRow("type") << QStringLiteral("qindaqt.import.kwallet.type");
    }
    void wrongLookupAttribute()
    {
        QFETCH(QString, attribute);
        m_service->attributes[attribute] = QStringLiteral("wrong");
        QCOMPARE(read().status, SecretOutcome::Status::NotFound);
        QCOMPARE(m_service->calls.value(QStringLiteral("GetSecret")), 0);
    }
    void decryptedAttributesMustMatch()
    {
        m_service->attributes[QStringLiteral("qindaqt.import.kwallet.type")] = QStringLiteral("2");
        m_service->forceDiscovery = true;
        QCOMPARE(read().status, SecretOutcome::Status::Failed);
        QCOMPARE(m_service->calls.value(QStringLiteral("GetSecret")), 0);
    }
    void duplicatesFailClosed()
    {
        m_service->matches = 2;
        QCOMPARE(read().status, SecretOutcome::Status::Failed);
        QCOMPARE(m_service->calls.value(QStringLiteral("GetSecret")), 0);
    }
    void malformedDiscoveryFailsClosed()
    {
        m_service->malformedSearch = true;
        QCOMPARE(read().status, SecretOutcome::Status::Failed);
    }
    void invalidKeyNeverQueriesProvider()
    {
        const auto result = complete([&](auto done) { m_backend->read(QStringLiteral("synthetic/unsupported"), done); });
        QCOMPARE(result.status, SecretOutcome::Status::Failed);
        QVERIFY(m_service->calls.isEmpty());
    }
    void unlockAndPrompt_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<SecretOutcome::Status>("status");
        using U = SyntheticSecretService::Unlock;
        QTest::newRow("immediate") << int(U::Immediate) << SecretOutcome::Status::Ok;
        QTest::newRow("prompt-accept") << int(U::PromptAccept) << SecretOutcome::Status::Ok;
        QTest::newRow("prompt-cancel") << int(U::PromptCancel) << SecretOutcome::Status::Denied;
        QTest::newRow("prompt-malformed") << int(U::PromptMalformed) << SecretOutcome::Status::Failed;
        QTest::newRow("remains-locked") << int(U::RemainLocked) << SecretOutcome::Status::Denied;
    }
    void unlockAndPrompt()
    {
        QFETCH(int, mode);
        QFETCH(SecretOutcome::Status, status);
        m_service->locked = true;
        m_service->unlocking = static_cast<SyntheticSecretService::Unlock>(mode);
        const auto result = read();
        QCOMPARE(result.status, status);
        QCOMPARE(m_service->calls.value(QStringLiteral("Unlock")), 1);
        if (status != SecretOutcome::Status::Ok) QVERIFY(result.value.isEmpty());
    }
    void invalidSecret_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("bad-session") << 0;
        QTest::newRow("unexpected-encryption-parameters") << 1;
        QTest::newRow("wrong-content-type") << 2;
        QTest::newRow("malformed-password") << 3;
    }
    void invalidSecret()
    {
        QFETCH(int, mode);
        if (mode == 0) m_service->wireSession = QStringLiteral("/org/freedesktop/secrets/session/wrong");
        if (mode == 1) m_service->wireParameters = QByteArray(1, 'x');
        if (mode == 2) m_service->contentType = QStringLiteral("text/plain");
        if (mode == 3) m_service->serialized = QByteArray::fromHex("7ffffffe0061");
        const auto result = read();
        QCOMPARE(result.status, SecretOutcome::Status::Failed);
        QVERIFY(result.value.isEmpty());
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("Close")), 1);
    }
    void lockRaceIsDenied()
    {
        m_service->errorOnRead = true;
        QCOMPARE(read().status, SecretOutcome::Status::Denied);
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("Close")), 1);
    }
    void nativeProviderLossIsUnavailable()
    {
        m_service->changeOwnerOnRead = true;
        const auto result = read();
        QVERIFY(m_peer.registerService(Native));
        QCOMPARE(result.status, SecretOutcome::Status::Unavailable);
        QVERIFY(result.value.isEmpty());
    }
    void nativeAliasOwnerMismatchRefusesAccess()
    {
        QVERIFY(m_peer.unregisterService(Native));
        QVERIFY(m_other.registerService(Native));
        const auto result = read();
        QVERIFY(m_other.unregisterService(Native));
        QVERIFY(m_peer.registerService(Native));
        QCOMPARE(result.status, SecretOutcome::Status::Unavailable);
        QVERIFY(m_service->calls.isEmpty());
    }
    void absentNativeNameKeepsOrdinaryMissingAndRemoval()
    {
        QVERIFY(m_peer.unregisterService(Native));
        const auto missing = read();
        const auto removed = complete([&](auto done) { m_backend->remove(Key, done); });
        QVERIFY(m_peer.registerService(Native));
        QCOMPARE(missing.status, SecretOutcome::Status::NotFound);
        QCOMPARE(missing.error, QStringLiteral("No secret is stored for this account."));
        QCOMPARE(removed.status, SecretOutcome::Status::Ok);
        QVERIFY(m_service->calls.isEmpty());
    }
    void providerReplacementClosesOriginalSession()
    {
        m_service->replaceOwnerOnRead = [this] {
            QVERIFY(m_peer.unregisterService(Native));
            QVERIFY(m_peer.unregisterService(Standard));
            QVERIFY(m_other.registerService(Native));
            QVERIFY(m_other.registerService(Standard));
        };
        const auto result = read();
        QVERIFY(m_other.unregisterService(Native));
        QVERIFY(m_other.unregisterService(Standard));
        QVERIFY(m_peer.registerService(Native));
        QVERIFY(m_peer.registerService(Standard));
        QCOMPARE(result.status, SecretOutcome::Status::Unavailable);
        QVERIFY(result.value.isEmpty());
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("Close")), 1);
    }
    void destructionDrainsInFlightSession()
    {
        m_service->delaySessionReply = true;
        bool called = false;
        m_backend->read(Key, [&](const auto &) { called = true; });
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("OpenSession")), 1);
        m_backend.reset();
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("Close")), 1);
        QVERIFY(!called);
        QCOMPARE(m_service->calls.value(QStringLiteral("GetSecret")), 0);
    }
    void destructionDrainsInFlightPrompt()
    {
        m_service->locked = true;
        m_service->unlocking = SyntheticSecretService::Unlock::PromptPending;
        m_service->delayUnlockReply = true;
        bool called = false;
        m_backend->read(Key, [&](const auto &) { called = true; });
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("Unlock")), 1);
        m_backend.reset();
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("Dismiss")), 1);
        QVERIFY(!called);
        QCOMPARE(m_service->calls.value(QStringLiteral("Prompt")), 0);
        QCOMPARE(m_service->calls.value(QStringLiteral("GetSecret")), 0);
    }
    void destroyedBackendDismissesOwnPrompt()
    {
        m_service->locked = true;
        m_service->unlocking = SyntheticSecretService::Unlock::PromptPending;
        bool called = false;
        m_backend->read(Key, [&](const auto &) { called = true; });
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("Prompt")), 1);
        m_backend.reset();
        QTRY_COMPARE(m_service->calls.value(QStringLiteral("Dismiss")), 1);
        QVERIFY(!called);
        QCOMPARE(m_service->calls.value(QStringLiteral("GetSecret")), 0);
    }
    void destructionBeforeReaderStartsHasNoBusCalls()
    {
        bool called = false;
        m_backend->read(Key, [&](const auto &) { called = true; });
        // Primary NotFound runs first and schedules the reader; destruction
        // then precedes that reader's queued start on the next event turn.
        QTimer::singleShot(0, [this] { m_backend.reset(); });
        QTest::qWait(50);
        QVERIFY(!called);
        QVERIFY(m_service->calls.isEmpty());
    }
    void writesUseOnlyPrimary()
    {
        QCOMPARE(complete([&](auto done) { m_backend->write(Key, QStringLiteral("replacement"), done); }).status,
                 SecretOutcome::Status::Ok);
        QVERIFY(m_primary->contains(Key));
        QVERIFY(m_service->calls.isEmpty());
        QCOMPARE(read().value, QStringLiteral("replacement"));
    }
    void removalReportsPreservedImportWithoutUnlock()
    {
        m_service->locked = true;
        complete([&](auto done) { m_primary->write(Key, QStringLiteral("replacement"), done); });
        const auto result = complete([&](auto done) { m_backend->remove(Key, done); });
        QCOMPARE(result.status, SecretOutcome::Status::Failed);
        QVERIFY(!m_primary->contains(Key));
        QVERIFY(result.error.contains(QStringLiteral("preserved")));
        QCOMPARE(m_service->calls.value(QStringLiteral("Unlock")), 0);
        QCOMPARE(m_service->calls.value(QStringLiteral("OpenSession")), 0);
        QCOMPARE(m_service->calls.value(QStringLiteral("GetSecret")), 0);
    }
    void removalOfMissingImportSucceeds()
    {
        m_service->matches = 0;
        QCOMPARE(complete([&](auto done) { m_backend->remove(Key, done); }).status, SecretOutcome::Status::Ok);
    }
    void explicitMemoryModeNeverWrapsKeyring()
    {
        auto backend = defaultSecretBackend();
        QCOMPARE(backend->name(), QStringLiteral("memory"));
        QVERIFY(std::dynamic_pointer_cast<InMemorySecretBackend>(backend));
        QVERIFY(m_service->calls.isEmpty());
    }

private:
    SecretOutcome read() { return complete([&](auto done) { m_backend->read(Key, done); }); }
    QDBusConnection m_peer = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("synthetic-peer"));
    QDBusConnection m_other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("synthetic-other"));
    std::unique_ptr<SyntheticSecretService> m_service;
    std::shared_ptr<InMemorySecretBackend> m_primary;
    std::shared_ptr<SecretBackend> m_backend;
};

int main(int argc, char **argv)
{
    if (qEnvironmentVariable("QQO_MAIL_PRIVATE_SECRET_TEST") != QLatin1String("1")) return 2;
    QTemporaryDir home;
    if (!home.isValid()) return 3;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());
    qputenv("XDG_CACHE_HOME", home.filePath(QStringLiteral("cache")).toUtf8());
    // Keep the real QtKeychain leg on libsecret in this private fixture;
    // never activate a legacy wallet if a test host lacks keyring support.
    qputenv("QTKEYCHAIN_BACKEND", "libsecret");
    qputenv("QQO_MAIL_SECRET_BACKEND", "memory");
    QCoreApplication app(argc, argv);
    ImportedPasswordBackendTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_imported_password_backend.moc"
