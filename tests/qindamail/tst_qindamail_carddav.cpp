// SPDX-License-Identifier: GPL-3.0-or-later
// CardDAV in QindaMail (M76, D-085) against the local fake server: a
// contacts-only account syncs its address book into People through the
// account runtime (keyring secret "carddav"); a 401 is retried once with a
// fresh keyring read and succeeds; a wrong password ends in "needs sign-in";
// an IMAP account's address book uses the account's password; a conflict
// keeps the local copy and says so; and People ▸ Connect Address Book sets
// an account up through the real window (Find address books, test, save),
// after which a new contact can be saved to that address book.

#include "mail_test_support.h"

#include "office_mail/fake_carddav_server.h"
#include "qindamail/src/mail_host.h"

#include "contact_store.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>

using namespace QindaOffice::Apps::Mail;
using QindaOffice::Mail::Test::FakeCardDavServer;
using QindaOffice::Test::findNamed;

namespace {

QByteArray card(const QString &uid, const QString &name, const QString &email, const QString &note = {})
{
    QByteArray out = "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:" + uid.toUtf8() + "\r\nFN:" + name.toUtf8() + "\r\nN:;" + name.toUtf8()
        + ";;;\r\nEMAIL:" + email.toUtf8() + "\r\n";
    if (!note.isEmpty()) out += "NOTE:" + note.toUtf8() + "\r\n";
    return out + "END:VCARD\r\n";
}

QVariantMap cardDavAccount(const FakeCardDavServer &server)
{
    return {{QStringLiteral("kind"), QStringLiteral("carddav")},
            {QStringLiteral("emailAddress"), QStringLiteral("ada@example.org")},
            {QStringLiteral("contacts"), QVariantMap{{QStringLiteral("protocol"), QStringLiteral("carddav")},
                                                     {QStringLiteral("url"), server.base() + QStringLiteral("/")},
                                                     {QStringLiteral("userName"), QStringLiteral("ada")},
                                                     {QStringLiteral("addressBooks"), QVariantList{QVariantMap{
                                                         {QStringLiteral("id"), QStringLiteral("book1")},
                                                         {QStringLiteral("url"), server.bookUrl(QStringLiteral("contacts"))},
                                                         {QStringLiteral("name"), QStringLiteral("Contacts")}}}}}}};
}

QString accountStatus(MailService *service, const QString &id)
{
    return service->accountById(id).value(QStringLiteral("status")).toString();
}

QPair<QString, QString> syncNow(MailService *service, const QString &id)
{
    QSignalSpy done(service, &MailService::operationFinished);
    return MailTest::waitState(done, service->syncAccount(id), 20000);
}

} // namespace

class TestMailCardDavAccounts : public QObject {
    Q_OBJECT

private slots:
    void contactsOnlyAccountSyncs();
    void unauthorizedIsRetriedOnce();
    void wrongPasswordNeedsSignIn();
    void imapAccountLendsItsPassword();
    void conflictIsAnnounced();
    void connectAddressBookThroughTheWindow();
};

void TestMailCardDavAccounts::contactsOnlyAccountSyncs()
{
    MailTest::Env env;
    FakeCardDavServer server;
    server.books[QStringLiteral("contacts")].name = QStringLiteral("Contacts");
    server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                   card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    const QVariantMap added = env.service->addAccount(cardDavAccount(server), QStringLiteral("s3cret"));
    QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(added.value(QStringLiteral("error")).toString()));
    const QString id = added.value(QStringLiteral("id")).toString();
    QTRY_VERIFY(env.secrets->contains(id + QStringLiteral("/carddav")));
    QCOMPARE(syncNow(env.service.get(), id).first, QStringLiteral("accepted"));
    QCOMPARE(env.service->contacts()->contactForAddress(QStringLiteral("grace@example.org"))
                 .value(QStringLiteral("syncAccountId")).toString(), id);
    // A contact made here for that book reaches the server on the next sync.
    QVERIFY(env.service->contacts()->add({{QStringLiteral("displayName"), QStringLiteral("Bob Example")},
                                          {QStringLiteral("emails"), QVariantList{QVariantMap{{QStringLiteral("value"), QStringLiteral("bob@example.org")}}}},
                                          {QStringLiteral("addressBookAccount"), id},
                                          {QStringLiteral("addressBookUrl"), server.bookUrl(QStringLiteral("contacts"))}})
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(syncNow(env.service.get(), id).first, QStringLiteral("accepted"));
    QCOMPARE(server.books.value(QStringLiteral("contacts")).cards.size(), qsizetype(2));
    QCOMPARE(env.service->accountStore()->addressBooks().size(), 1);
    // Removing the account keeps its contacts here, unbound.
    QVERIFY(env.service->removeAccount(id).value(QStringLiteral("ok")).toBool());
    QCOMPARE(env.service->contacts()->count(), 2);
    QVERIFY(env.service->contacts()->contactForAddress(QStringLiteral("grace@example.org"))
                .value(QStringLiteral("syncAccountId")).toString().isEmpty());
}

void TestMailCardDavAccounts::unauthorizedIsRetriedOnce()
{
    MailTest::Env env;
    FakeCardDavServer server;
    server.books[QStringLiteral("contacts")].name = QStringLiteral("Contacts");
    server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                   card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    const QString id = env.service->addAccount(cardDavAccount(server), QStringLiteral("s3cret")).value(QStringLiteral("id")).toString();
    QTRY_VERIFY(env.secrets->contains(id + QStringLiteral("/carddav")));
    server.rejectNext = 1;   // the first answer is a 401; the runtime re-reads the keyring and retries
    QCOMPARE(syncNow(env.service.get(), id).first, QStringLiteral("accepted"));
    QCOMPARE(server.rejectNext, 0);
    QCOMPARE(env.service->contacts()->count(), 1);
    QCOMPARE(accountStatus(env.service.get(), id), QStringLiteral("idle"));
    QVERIFY(server.count("PROPFIND") >= 2);   // the refused PROPFIND and the retried one
}

void TestMailCardDavAccounts::wrongPasswordNeedsSignIn()
{
    MailTest::Env env;
    FakeCardDavServer server;
    server.books[QStringLiteral("contacts")].name = QStringLiteral("Contacts");
    const QString id = env.service->addAccount(cardDavAccount(server), QStringLiteral("wrong")).value(QStringLiteral("id")).toString();
    QTRY_VERIFY(env.secrets->contains(id + QStringLiteral("/carddav")));
    QCOMPARE(syncNow(env.service.get(), id).first, QStringLiteral("rejected"));
    QCOMPARE(accountStatus(env.service.get(), id), QStringLiteral("needsSignIn"));
    QVERIFY(!env.service->accountById(id).value(QStringLiteral("lastError")).toString().contains(QLatin1String("wrong")));
}

void TestMailCardDavAccounts::imapAccountLendsItsPassword()
{
    MailTest::Env env;
    FakeCardDavServer server;
    server.books[QStringLiteral("contacts")].name = QStringLiteral("Contacts");
    server.putCard(QStringLiteral("contacts"), QStringLiteral("alan.vcf"),
                   card(QStringLiteral("a-1"), QStringLiteral("Alan Turing"), QStringLiteral("alan@example.org")));
    QVariantMap account = cardDavAccount(server);
    account.insert(QStringLiteral("kind"), QStringLiteral("imap"));
    // Mail servers that are not there (a closed loopback port): IMAP is
    // offline, the address book is not.
    QTcpServer closed;
    QVERIFY(closed.listen(QHostAddress::LocalHost, 0));
    const quint16 port = closed.serverPort();
    closed.close();
    const QVariantMap mailServer{{QStringLiteral("host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), port},
                                 {QStringLiteral("security"), QStringLiteral("none")}, {QStringLiteral("userName"), QStringLiteral("ada")}};
    QVariantMap incoming = mailServer;
    incoming.insert(QStringLiteral("protocol"), QStringLiteral("imap"));
    account.insert(QStringLiteral("incoming"), incoming);
    account.insert(QStringLiteral("outgoing"), mailServer);
    const QVariantMap added = env.service->addAccount(account, QString());   // no password at all yet
    QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(added.value(QStringLiteral("error")).toString()));
    const QString id = added.value(QStringLiteral("id")).toString();
    // Only the mail password is stored: the address book borrows it.
    QSignalSpy written(env.service->secrets(), &QindaOffice::Mail::MailSecrets::finished);
    env.service->secrets()->write(QStringLiteral("w1"), id, QStringLiteral("imap"), QStringLiteral("s3cret"));
    QVERIFY(MailTest::waitResult(written, QStringLiteral("w1")).value(QStringLiteral("ok")).toBool());
    QVERIFY(!env.secrets->contains(id + QStringLiteral("/carddav")));
    QCOMPARE(syncNow(env.service.get(), id).first, QStringLiteral("offline"));   // IMAP; the address book synced
    QCOMPARE(env.service->contacts()->count(), 1);
    QCOMPARE(env.service->contacts()->contactForAddress(QStringLiteral("alan@example.org")).value(QStringLiteral("syncAccountId")).toString(), id);
}

void TestMailCardDavAccounts::conflictIsAnnounced()
{
    MailTest::Env env;
    FakeCardDavServer server;
    server.books[QStringLiteral("contacts")].name = QStringLiteral("Contacts");
    server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                   card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    const QString id = env.service->addAccount(cardDavAccount(server), QStringLiteral("s3cret")).value(QStringLiteral("id")).toString();
    QTRY_VERIFY(env.secrets->contains(id + QStringLiteral("/carddav")));
    QCOMPARE(syncNow(env.service.get(), id).first, QStringLiteral("accepted"));
    const QString grace = env.service->contacts()->contactForAddress(QStringLiteral("grace@example.org")).value(QStringLiteral("id")).toString();
    QVERIFY(env.service->contacts()->update({{QStringLiteral("id"), grace}, {QStringLiteral("notes"), QStringLiteral("here")}})
                .value(QStringLiteral("ok")).toBool());
    server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                   card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org"), QStringLiteral("there")));
    QSignalSpy notices(env.service.get(), &MailService::notify);
    QCOMPARE(syncNow(env.service.get(), id).first, QStringLiteral("accepted"));
    bool announced = false;
    for (const QList<QVariant> &args : std::as_const(notices))
        announced |= args.at(0).toString() == QLatin1String("Contacts changed in two places") && args.at(1).toString().contains(QLatin1String("Grace Hopper"));
    QVERIFY(announced);
    QCOMPARE(env.service->contacts()->count(), 2);
}

void TestMailCardDavAccounts::connectAddressBookThroughTheWindow()
{
    MailTest::Env env;
    FakeCardDavServer server;
    server.books[QStringLiteral("contacts")].name = QStringLiteral("Family");
    server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                   card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    MailHost host(env.service.get());
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{QStringLiteral("host"), QVariant::fromValue(&host)}}));
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    host.attachWindow(window);
    window->resize(1280, 800);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(host.actions()->trigger(QStringLiteral("mailViewPeopleAction")));
    QVERIFY(host.actions()->trigger(QStringLiteral("contactAddressBookAction")));
    QObject *dialog = object->findChild<QObject *>(QStringLiteral("accountDialog"));
    QVERIFY(dialog != nullptr);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QCOMPARE(dialog->property("kind").toString(), QStringLiteral("carddav"));
    QObject *section = object->findChild<QObject *>(QStringLiteral("addressBookSection"));
    QVERIFY(section != nullptr);
    const auto field = [&](const char *name) { return object->findChild<QObject *>(QString::fromLatin1(name)); };
    field("accountEmail")->setProperty("text", QStringLiteral("ada@example.org"));
    field("accountPassword")->setProperty("text", QStringLiteral("s3cret"));
    field("addressBookUrl")->setProperty("text", server.base() + QStringLiteral("/"));
    field("addressBookUser")->setProperty("text", QStringLiteral("ada"));
    // Find address books borrows the account's password field.
    QVERIFY(QMetaObject::invokeMethod(section, "discover"));
    QTRY_COMPARE(section->property("found").toList().size(), 1);
    QTest::qWait(150);
    // The section is open on a carddav account: server, user, the book found.
    QVERIFY(!section->property("collapsed").toBool());
    QQuickItem *book = findNamed(window->contentItem(), QStringLiteral("addressBook_0"));
    QVERIFY(book != nullptr && book->isVisible());
    QCOMPARE(book->property("text").toString(), QStringLiteral("Family"));
    QindaOffice::Test::saveGrab(window, QStringLiteral("qindamail_carddav_setup_1280"));
    window->resize(520, 800);
    QTest::qWait(250);
    QindaOffice::Test::saveGrab(window, QStringLiteral("qindamail_carddav_setup_520"));
    window->resize(1280, 800);
    // Test, then save: the account carries the book; a sync fills People.
    QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
    QTRY_COMPARE(dialog->property("step").toString(), QStringLiteral("testing"));
    QTRY_VERIFY(dialog->property("testResult").toMap().value(QStringLiteral("state")).toString() == QLatin1String("accepted"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
    QTRY_COMPARE(env.service->accountStore()->count(), 1);
    const QVariantMap account = env.service->accountStore()->accounts().first().toMap();
    QCOMPARE(account.value(QStringLiteral("contacts")).toMap().value(QStringLiteral("addressBooks")).toList().size(), 1);
    QVERIFY(host.actions()->trigger(QStringLiteral("mailSyncAction")));
    QTRY_COMPARE(env.service->contacts()->count(), 1);

    // A new contact saved to that address book reaches the server.
    QVERIFY(host.actions()->trigger(QStringLiteral("contactNewAction")));
    QQuickItem *saveTo = nullptr;
    QTRY_VERIFY((saveTo = findNamed(window->contentItem(), QStringLiteral("contactAddressBook"))) != nullptr && saveTo->isVisible());
    QCOMPARE(saveTo->property("count").toInt(), 2);
    saveTo->setProperty("currentIndex", 1);
    QVERIFY(saveTo->property("displayText").toString().contains(QLatin1String("Family")));
    window->resize(520, 800);
    QTest::qWait(250);
    QindaOffice::Test::saveGrab(window, QStringLiteral("qindamail_contact_save_to_520"));
    window->resize(1280, 800);
    findNamed(window->contentItem(), QStringLiteral("contactGivenName"))->setProperty("text", QStringLiteral("Bob"));
    QObject *editor = findNamed(window->contentItem(), QStringLiteral("contactEditor"));
    QVariant saved;
    QVERIFY(QMetaObject::invokeMethod(editor, "save", Q_RETURN_ARG(QVariant, saved)));
    QVERIFY(saved.toBool());
    QTRY_VERIFY(findNamed(window->contentItem(), QStringLiteral("contactSyncedWith"))->isVisible());
    QVERIFY(host.actions()->trigger(QStringLiteral("mailSyncAction")));
    QTRY_COMPARE(server.books.value(QStringLiteral("contacts")).cards.size(), qsizetype(2));
}

MAIL_TEST_MAIN(TestMailCardDavAccounts)

#include "tst_qindamail_carddav.moc"
