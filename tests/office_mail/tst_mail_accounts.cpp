// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts, presets, client registry and secrets (M43, docs/mail.md).

#include "account_store.h"
#include "mail_secrets.h"
#include "oauth_client_registry.h"
#include "provider_presets.h"
#include "secret_sink.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace QindaOffice::Mail;
using QindaOffice::Mail::Test::SecretSink;

class TestMailAccounts : public QObject {
    Q_OBJECT

private:
    static QVariantMap imapAccount()
    {
        MailAccount a;
        a.kind = QStringLiteral("imap");
        a.emailAddress = QStringLiteral("ada@example.org");
        a.incoming.protocol = QStringLiteral("imap");
        a.incoming.host = QStringLiteral("imap.example.org");
        a.incoming.security = QStringLiteral("tls");
        a.outgoing.host = QStringLiteral("smtp.example.org");
        a.outgoing.security = QStringLiteral("starttls");
        return a.toVariantMap();
    }

    static QVariantMap waitResult(QSignalSpy &spy, const QString &requestId)
    {
        for (int round = 0; round < 50; ++round) {
            for (const QList<QVariant> &args : std::as_const(spy)) {
                if (args.at(0).toString() == requestId) return args.at(1).toMap();
            }
            spy.wait(100);
        }
        return {};
    }

private slots:
    void jsonRoundTripKeepsUnknownFields()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("accounts.json"));
        // A file from a "newer build" of the same schema: unknown keys at
        // every level must survive load → update → save.
        QJsonObject account = MailAccount::fromVariantMap(imapAccount()).toJson();
        account.insert(QStringLiteral("id"), QStringLiteral("acc-1"));
        account.insert(QStringLiteral("displayName"), QStringLiteral("Ada"));
        account.insert(QStringLiteral("futureTop"), QStringLiteral("keep me"));
        QJsonObject incoming = account.value(QStringLiteral("incoming")).toObject();
        incoming.insert(QStringLiteral("futureIncoming"), 42);
        account.insert(QStringLiteral("incoming"), incoming);
        QJsonObject calendar = account.value(QStringLiteral("calendar")).toObject();
        calendar.insert(QStringLiteral("calendars"),
                        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("c1")},
                                               {QStringLiteral("futureEntry"), true}}});
        account.insert(QStringLiteral("calendar"), calendar);
        const QJsonObject root{{QStringLiteral("schema"), QStringLiteral("qindaoffice.mail.accounts/1")},
                               {QStringLiteral("futureRoot"), QJsonArray{1, 2}},
                               {QStringLiteral("accounts"), QJsonArray{account}}};
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(root).toJson());
        out.close();

        AccountStore store(path);
        QCOMPARE(store.count(), 1);
        QVariantMap map = store.accountById(QStringLiteral("acc-1"));
        QCOMPARE(map.value(QStringLiteral("futureTop")).toString(), QStringLiteral("keep me"));
        map.insert(QStringLiteral("displayName"), QStringLiteral("Ada Lovelace"));
        const QVariantMap updated = store.update(map);
        QVERIFY2(updated.value(QStringLiteral("ok")).toBool(), qPrintable(updated.value(QStringLiteral("error")).toString()));

        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QJsonObject saved = QJsonDocument::fromJson(in.readAll()).object();
        QCOMPARE(saved.value(QStringLiteral("schema")).toString(), QStringLiteral("qindaoffice.mail.accounts/1"));
        QCOMPARE(saved.value(QStringLiteral("futureRoot")).toArray().size(), 2);
        const QJsonObject savedAccount = saved.value(QStringLiteral("accounts")).toArray().at(0).toObject();
        QCOMPARE(savedAccount.value(QStringLiteral("displayName")).toString(), QStringLiteral("Ada Lovelace"));
        QCOMPARE(savedAccount.value(QStringLiteral("futureTop")).toString(), QStringLiteral("keep me"));
        QCOMPARE(savedAccount.value(QStringLiteral("incoming")).toObject().value(QStringLiteral("futureIncoming")).toInt(), 42);
        QVERIFY(savedAccount.value(QStringLiteral("calendar")).toObject().value(QStringLiteral("calendars"))
                    .toArray().at(0).toObject().value(QStringLiteral("futureEntry")).toBool());

        // And the typed fields themselves round-trip.
        const MailAccount reread = MailAccount::fromJson(savedAccount);
        QCOMPARE(reread.toJson(), savedAccount);
    }

    void addGeneratesIdAndDefaults()
    {
        QTemporaryDir dir;
        AccountStore store(dir.filePath(QStringLiteral("mail/accounts.json")));
        QSignalSpy added(&store, &AccountStore::accountAdded);
        const QVariantMap result = store.add(imapAccount());
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        const QString id = result.value(QStringLiteral("id")).toString();
        QVERIFY(!id.isEmpty());
        QCOMPARE(added.count(), 1);
        const std::optional<MailAccount> a = store.find(id);
        QVERIFY(a);
        QCOMPARE(a->displayName, QStringLiteral("ada@example.org"));
        QCOMPARE(a->incoming.port, 993);
        QCOMPARE(a->outgoing.port, 587);
        QCOMPARE(a->incoming.userName, QStringLiteral("ada@example.org"));
        QCOMPARE(a->outgoing.userName, QStringLiteral("ada@example.org"));
        // A second store on the same file sees it.
        AccountStore again(dir.filePath(QStringLiteral("mail/accounts.json")));
        QCOMPARE(again.accounts().size(), 1);
    }

    void validation_data()
    {
        QTest::addColumn<QVariantMap>("patch");
        QTest::addColumn<QString>("expected");
        const auto with = [](const QString &path, const QVariant &value) {
            QVariantMap m = imapAccount();
            const QStringList parts = path.split(QLatin1Char('.'));
            if (parts.size() == 1) {
                m.insert(parts.at(0), value);
            } else {
                QVariantMap inner = m.value(parts.at(0)).toMap();
                inner.insert(parts.at(1), value);
                m.insert(parts.at(0), inner);
            }
            return m;
        };
        QTest::newRow("kind") << with(QStringLiteral("kind"), QStringLiteral("carrier-pigeon")) << QStringLiteral("kind:");
        QTest::newRow("address") << with(QStringLiteral("emailAddress"), QStringLiteral("not an address"))
                                 << QStringLiteral("emailAddress:");
        QTest::newRow("host") << with(QStringLiteral("incoming.host"), QString()) << QStringLiteral("incoming.host:");
        QTest::newRow("port") << with(QStringLiteral("incoming.port"), 70000) << QStringLiteral("incoming.port:");
        QTest::newRow("clear text") << with(QStringLiteral("outgoing.security"), QStringLiteral("none"))
                                    << QStringLiteral("outgoing.security:");
        QTest::newRow("xoauth2 without provider") << with(QStringLiteral("incoming.authMechanism"), QStringLiteral("xoauth2"))
                                                  << QStringLiteral("incoming.authMechanism:");
        QTest::newRow("pop3 calendar") << [&] {
            QVariantMap m = with(QStringLiteral("kind"), QStringLiteral("pop3"));
            QVariantMap in = m.value(QStringLiteral("incoming")).toMap();
            in.insert(QStringLiteral("protocol"), QStringLiteral("pop3"));
            m.insert(QStringLiteral("incoming"), in);
            m.insert(QStringLiteral("calendar"), QVariantMap{{QStringLiteral("protocol"), QStringLiteral("caldav")},
                                                            {QStringLiteral("url"), QStringLiteral("https://dav.example.org/")}});
            return m;
        }() << QStringLiteral("calendar.protocol:");
        QTest::newRow("http calendar") << with(QStringLiteral("calendar"),
                                               QVariantMap{{QStringLiteral("protocol"), QStringLiteral("caldav")},
                                                           {QStringLiteral("url"), QStringLiteral("http://dav.example.org/")}})
                                       << QStringLiteral("calendar.url:");
        QTest::newRow("m365 password") << [&] {
            QVariantMap m = with(QStringLiteral("kind"), QStringLiteral("microsoft365"));
            m.insert(QStringLiteral("oauth"), QVariantMap{{QStringLiteral("provider"), QStringLiteral("microsoft")}});
            return m;
        }() << QStringLiteral("incoming.authMechanism:");
        QTest::newRow("sync interval") << with(QStringLiteral("syncIntervalMinutes"), 5000)
                                       << QStringLiteral("syncIntervalMinutes:");
        QTest::newRow("secret key") << with(QStringLiteral("incoming.password"), QStringLiteral("hunter2"))
                                    << QStringLiteral("incoming.password:");
        QTest::newRow("secret key top") << with(QStringLiteral("refresh_token"), QStringLiteral("1//x"))
                                        << QStringLiteral("refresh_token:");
    }

    void validation()
    {
        QFETCH(QVariantMap, patch);
        QFETCH(QString, expected);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("accounts.json"));
        AccountStore store(path);
        const QStringList problems = store.validate(patch);
        bool found = false;
        for (const QString &p : problems) found = found || p.startsWith(expected);
        QVERIFY2(found, qPrintable(problems.join(QLatin1Char('\n'))));
        const QVariantMap result = store.add(patch);
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(!result.value(QStringLiteral("errors")).toStringList().isEmpty());
        QVERIFY(!QFile::exists(path)); // nothing written for an invalid account
        QCOMPARE(store.count(), 0);
    }

    void noSecretOnDisk()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("accounts.json"));
        auto backend = std::make_shared<InMemorySecretBackend>();
        MailSecrets secrets(backend);
        AccountStore store(path);
        store.setSecrets(&secrets);
        const QString id = store.add(imapAccount()).value(QStringLiteral("id")).toString();
        QVERIFY(!id.isEmpty());
        const QString password = QStringLiteral("pw-Tr0ub4dor&3-unique");
        QSignalSpy done(&secrets, &MailSecrets::finished);
        secrets.write(QStringLiteral("w1"), id, QStringLiteral("imap"), password);
        QVERIFY(waitResult(done, QStringLiteral("w1")).value(QStringLiteral("ok")).toBool());
        // Touch the file again after the secret exists.
        QVariantMap map = store.accountById(id);
        map.insert(QStringLiteral("signature"), QStringLiteral("-- \nAda"));
        QVERIFY(store.update(map).value(QStringLiteral("ok")).toBool());

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(!bytes.contains(password.toUtf8()));
        QVERIFY(!bytes.toLower().contains("password"));
        QVERIFY(backend->contains(MailSecrets::keyFor(id, QStringLiteral("imap"))));
    }

    void removeWipesSecrets()
    {
        QTemporaryDir dir;
        auto backend = std::make_shared<InMemorySecretBackend>();
        MailSecrets secrets(backend);
        AccountStore store(dir.filePath(QStringLiteral("accounts.json")));
        store.setSecrets(&secrets);
        const QString a = store.add(imapAccount()).value(QStringLiteral("id")).toString();
        const QString b = store.add(imapAccount()).value(QStringLiteral("id")).toString();
        QSignalSpy done(&secrets, &MailSecrets::finished);
        const QStringList services = {QStringLiteral("imap"), QStringLiteral("smtp"), QStringLiteral("caldav"),
                                      QStringLiteral("oauth-refresh-google"), QStringLiteral("oauth-refresh-microsoft")};
        for (const QString &service : services) {
            secrets.write(QStringLiteral("a-") + service, a, service, QStringLiteral("s-") + service);
            QVERIFY(waitResult(done, QStringLiteral("a-") + service).value(QStringLiteral("ok")).toBool());
        }
        secrets.write(QStringLiteral("b-imap"), b, QStringLiteral("imap"), QStringLiteral("other"));
        QVERIFY(waitResult(done, QStringLiteral("b-imap")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(backend->keys().size(), 6);

        QSignalSpy wiped(&store, &AccountStore::secretsRemoved);
        const QVariantMap removed = store.remove(a);
        QVERIFY(removed.value(QStringLiteral("ok")).toBool());
        QCOMPARE(store.count(), 1);
        QVERIFY(wiped.wait(2000));
        QCOMPARE(wiped.at(0).at(0).toString(), a);
        const QVariantMap result = wiped.at(0).at(1).toMap();
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("removed")).toInt(), int(MailSecrets::services().size()));
        QCOMPARE(backend->keys(), QStringList{MailSecrets::keyFor(b, QStringLiteral("imap"))});
    }

    void corruptFileIsMovedAside()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("accounts.json"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("{ not json");
        out.close();
        AccountStore store(path);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("mail accounts:.*moved to")));
        QCOMPARE(store.count(), 0);
        QVERIFY(QFile::exists(path + QStringLiteral(".corrupt")));
        QVERIFY(!store.readOnly());
        QVERIFY(store.add(imapAccount()).value(QStringLiteral("ok")).toBool());
    }

    void newerSchemaIsReadOnly()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("accounts.json"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(R"({"schema":"qindaoffice.mail.accounts/2","accounts":[{"id":"x","kind":"imap"}]})");
        out.close();
        AccountStore store(path);
        QCOMPARE(store.count(), 1);
        QVERIFY(store.readOnly());
        QVERIFY(!store.add(imapAccount()).value(QStringLiteral("ok")).toBool());
        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QVERIFY(in.readAll().contains("accounts/2")); // untouched
    }

    void presets_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("presetId");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("imapHost");
        QTest::addColumn<int>("smtpPort");
        QTest::addColumn<QString>("smtpSecurity");
        QTest::addColumn<QString>("auth");
        QTest::newRow("gmail") << "a@gmail.com" << "gmail" << "gmail" << "imap.gmail.com" << 465 << "tls" << "oauth-preferred";
        QTest::newRow("googlemail") << "a@GoogleMail.com" << "gmail" << "gmail" << "imap.gmail.com" << 465 << "tls"
                                    << "oauth-preferred";
        QTest::newRow("hotmail") << "a@hotmail.com" << "outlook" << "microsoft365" << "outlook.office365.com" << 587
                                 << "starttls" << "oauth-required";
        QTest::newRow("msn") << "a@msn.com" << "outlook" << "microsoft365" << "outlook.office365.com" << 587
                             << "starttls" << "oauth-required";
        QTest::newRow("yahoo") << "a@yahoo.com" << "yahoo" << "imap" << "imap.mail.yahoo.com" << 465 << "tls" << "app-password";
        QTest::newRow("icloud") << "a@me.com" << "icloud" << "imap" << "imap.mail.me.com" << 587 << "starttls" << "app-password";
        QTest::newRow("fastmail") << "a@fastmail.com" << "fastmail" << "imap" << "imap.fastmail.com" << 465 << "tls"
                                  << "app-password";
        QTest::newRow("aol") << "a@aol.com" << "aol" << "imap" << "imap.aol.com" << 465 << "tls" << "app-password";
        QTest::newRow("zoho") << "a@zoho.com" << "zoho" << "imap" << "imap.zoho.com" << 465 << "tls" << "password";
        QTest::newRow("gmx.de") << "a@gmx.de" << "gmx-net" << "imap" << "imap.gmx.net" << 587 << "starttls" << "password";
        QTest::newRow("gmx.com") << "a@gmx.com" << "gmx-com" << "imap" << "imap.gmx.com" << 587 << "starttls" << "password";
        QTest::newRow("mailbox.org") << "a@mailbox.org" << "mailbox-org" << "imap" << "imap.mailbox.org" << 465 << "tls"
                                     << "password";
        QTest::newRow("proton") << "a@proton.me" << "proton" << "imap" << "127.0.0.1" << 1025 << "starttls" << "bridge";
        QTest::newRow("unknown") << "a@example.org" << "" << "" << "" << 0 << "" << "";
    }

    void presets()
    {
        QFETCH(QString, address);
        QFETCH(QString, presetId);
        QFETCH(QString, kind);
        QFETCH(QString, imapHost);
        QFETCH(int, smtpPort);
        QFETCH(QString, smtpSecurity);
        QFETCH(QString, auth);
        const std::optional<ProviderPreset> preset = ProviderPresets::forAddress(address);
        if (presetId.isEmpty()) {
            QVERIFY(!preset);
            return;
        }
        QVERIFY(preset);
        QCOMPARE(preset->id, presetId);
        QCOMPARE(preset->kind, kind);
        QCOMPARE(preset->incoming.host, imapHost);
        QCOMPARE(preset->outgoing.port, smtpPort);
        QCOMPARE(preset->outgoing.security, smtpSecurity);
        QCOMPARE(preset->auth, auth);
        QVERIFY(!preset->note.isEmpty());
    }

    void everyPresetMakesAValidAccount()
    {
        for (const ProviderPreset &preset : ProviderPresets::all()) {
            const QString domain = preset.domains.isEmpty() ? QStringLiteral("contoso.com") : preset.domains.first();
            MailAccount a = AccountStore::normalized(ProviderPresets::accountFor(preset, QStringLiteral("user@") + domain));
            a.id = QStringLiteral("preset-") + preset.id;
            QVERIFY2(a.validate().isEmpty(), qPrintable(preset.id + QStringLiteral(": ") + a.validate().join(QLatin1Char(';'))));
        }
        const MailAccount icloud = ProviderPresets::accountFor(*ProviderPresets::forId(QStringLiteral("icloud")),
                                                               QStringLiteral("a@icloud.com"));
        QCOMPARE(icloud.calendar.url, QStringLiteral("https://caldav.icloud.com/"));
        const MailAccount fastmail = ProviderPresets::accountFor(*ProviderPresets::forId(QStringLiteral("fastmail")),
                                                                 QStringLiteral("a@fastmail.com"));
        QCOMPARE(fastmail.calendar.url, QStringLiteral("https://caldav.fastmail.com/dav/calendars/user/a@fastmail.com/"));
        QVERIFY(ProviderPresets::forId(QStringLiteral("proton"))->note.contains(QStringLiteral("Bridge")));
    }

    void draftFor()
    {
        AccountStore store(QStringLiteral("/nonexistent/accounts.json"));
        const QVariantMap gmail = store.draftFor(QStringLiteral("imap"), QStringLiteral("a@gmail.com"));
        QCOMPARE(gmail.value(QStringLiteral("kind")).toString(), QStringLiteral("gmail"));
        // Google Workspace on its own domain: manual "gmail" kind.
        const QVariantMap workspace = store.draftFor(QStringLiteral("gmail"), QStringLiteral("a@corp.example"));
        QCOMPARE(workspace.value(QStringLiteral("incoming")).toMap().value(QStringLiteral("host")).toString(),
                 QStringLiteral("imap.gmail.com"));
        const QVariantMap pop = store.draftFor(QStringLiteral("pop3"), QStringLiteral("a@example.org"));
        QCOMPARE(pop.value(QStringLiteral("incoming")).toMap().value(QStringLiteral("protocol")).toString(),
                 QStringLiteral("pop3"));
    }

    void clientRegistryOrder()
    {
        QTemporaryDir dir;
        const QString user = dir.filePath(QStringLiteral("user.json"));
        const QString system = dir.filePath(QStringLiteral("system.json"));
        OAuthClientRegistry registry;
        registry.setUserPath(user);
        registry.setSystemPath(system);

        // Nothing anywhere: a clear error, with the app-password hint for Google.
        OAuthClientRegistry::Resolution r = registry.resolve(QStringLiteral("google"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("No OAuth client ID")));
        QVERIFY(r.error.contains(QStringLiteral("app password")));
        QVERIFY(r.error.contains(user));
        QVERIFY(!registry.resolve(QStringLiteral("microsoft")).error.contains(QStringLiteral("app password")));

        const auto write = [](const QString &path, const QByteArray &json) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(json);
        };
        write(system, R"({"google":{"clientId":"sys-g","clientSecret":"sys-secret"},"microsoft":{"clientId":"sys-m","tenant":"organizations"}})");
        r = registry.resolve(QStringLiteral("google"));
        QVERIFY(r.ok);
        QCOMPARE(r.client.clientId, QStringLiteral("sys-g"));
        QCOMPARE(r.client.clientSecret, QStringLiteral("sys-secret"));
        QCOMPARE(r.client.source, QStringLiteral("system"));
        QCOMPARE(registry.resolve(QStringLiteral("microsoft")).client.tenant, QStringLiteral("organizations"));

        write(user, R"({"schema":"qindaoffice.mail.oauth-clients/1","google":{"clientId":"user-g","clientSecret":"user-secret"}})");
        r = registry.resolve(QStringLiteral("google"));
        QCOMPARE(r.client.clientId, QStringLiteral("user-g"));
        QCOMPARE(r.client.source, QStringLiteral("user"));
        // The user file has no microsoft entry: the system one still answers.
        QCOMPARE(registry.resolve(QStringLiteral("microsoft")).client.clientId, QStringLiteral("sys-m"));

        // The account's own client ID wins, borrowing a file's secret for the same ID.
        r = registry.resolve(QStringLiteral("google"), QStringLiteral("sys-g"));
        QCOMPARE(r.client.source, QStringLiteral("account"));
        QCOMPARE(r.client.clientSecret, QStringLiteral("sys-secret"));
        r = registry.resolve(QStringLiteral("google"), QStringLiteral("own-id"));
        QCOMPARE(r.client.clientId, QStringLiteral("own-id"));
        QVERIFY(r.client.clientSecret.isEmpty());

        // A broken user file is reported, not skipped.
        write(user, "{ nope");
        r = registry.resolve(QStringLiteral("google"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(user));
        QVERIFY(!registry.resolve(QStringLiteral("yahoo")).ok);
    }

    void secretsInMemory()
    {
        auto backend = std::make_shared<InMemorySecretBackend>();
        MailSecrets secrets(backend);
        QCOMPARE(secrets.backendName(), QStringLiteral("memory"));
        QSignalSpy done(&secrets, &MailSecrets::finished);

        secrets.check(QStringLiteral("c0"), QStringLiteral("acc"), QStringLiteral("smtp"));
        QVariantMap r = waitResult(done, QStringLiteral("c0"));
        QVERIFY(r.value(QStringLiteral("ok")).toBool());
        QVERIFY(!r.value(QStringLiteral("exists")).toBool());

        secrets.write(QStringLiteral("w"), QStringLiteral("acc"), QStringLiteral("smtp"), QStringLiteral("s3cret"));
        QVERIFY(waitResult(done, QStringLiteral("w")).value(QStringLiteral("ok")).toBool());
        QVERIFY(backend->contains(QStringLiteral("acc/smtp")));

        secrets.check(QStringLiteral("c1"), QStringLiteral("acc"), QStringLiteral("smtp"));
        r = waitResult(done, QStringLiteral("c1"));
        QVERIFY(r.value(QStringLiteral("exists")).toBool());
        for (const QVariant &v : std::as_const(r)) QVERIFY(v.toString() != QStringLiteral("s3cret"));

        SecretSink sink;
        secrets.applySecret(QStringLiteral("a"), QStringLiteral("acc"), QStringLiteral("smtp"), &sink,
                            QStringLiteral("plain"));
        r = waitResult(done, QStringLiteral("a"));
        QVERIFY(r.value(QStringLiteral("applied")).toBool());
        QCOMPARE(sink.received, QStringLiteral("s3cret"));
        QCOMPARE(sink.receivedMechanism, QStringLiteral("plain"));
        QVERIFY(!QJsonDocument(QJsonObject::fromVariantMap(r)).toJson().contains("s3cret"));

        // Unknown service and bad account ids are refused.
        secrets.write(QStringLiteral("bad"), QStringLiteral("acc"), QStringLiteral("ftp"), QStringLiteral("x"));
        r = waitResult(done, QStringLiteral("bad"));
        QVERIFY(!r.value(QStringLiteral("ok")).toBool());
        QCOMPARE(r.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        secrets.write(QStringLiteral("bad2"), QStringLiteral("a/b"), QStringLiteral("imap"), QStringLiteral("x"));
        QVERIFY(!waitResult(done, QStringLiteral("bad2")).value(QStringLiteral("ok")).toBool());

        // No secret service: an honest error, and nothing stored anywhere.
        backend->setUnavailable(true);
        secrets.write(QStringLiteral("u"), QStringLiteral("acc"), QStringLiteral("imap"), QStringLiteral("x"));
        r = waitResult(done, QStringLiteral("u"));
        QVERIFY(!r.value(QStringLiteral("ok")).toBool());
        QVERIFY(r.value(QStringLiteral("unavailable")).toBool());
        QVERIFY(r.value(QStringLiteral("error")).toString().contains(QStringLiteral("keyring")));
        QVERIFY(!backend->contains(QStringLiteral("acc/imap")));
    }

    void qmlTypesRegistered()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import QindaOffice.Mail
            QtObject {
                id: root
                property MailSecrets keyring: MailSecrets {}
                property AccountStore store: AccountStore { path: "/nonexistent/qqo/accounts.json"; secrets: root.keyring }
                property AutoConfig autoConfig: AutoConfig {}
                property OAuth2Session session: OAuth2Session { secrets: root.keyring }
                property int kinds: store.kinds.length
                property int presets: store.presets.length
                property string backend: keyring.backendName
                property bool wired: store.secrets === keyring && session.secrets === keyring
            })", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QCOMPARE(root->property("kinds").toInt(), 8);   // M76: carddav
        QCOMPARE(root->property("presets").toInt(), int(ProviderPresets::all().size()));
        QVERIFY(root->property("wired").toBool());
        QCOMPARE(root->property("backend").toString(), QStringLiteral("memory")); // QQO_MAIL_SECRET_BACKEND
    }
};

QTEST_GUILESS_MAIN(TestMailAccounts)
#include "tst_mail_accounts.moc"
