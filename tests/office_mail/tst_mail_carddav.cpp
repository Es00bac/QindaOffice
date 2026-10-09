// SPDX-License-Identifier: GPL-3.0-or-later
// CardDAV (M76, D-085) against a local fake server (fake_carddav_server.h):
// discovery through /.well-known/carddav, the principal and the home set;
// the initial sync (sync-collection, multiget), incremental syncs both ways
// (server edits, deletions and additions; local edits, new contacts and
// deletions with If-Match / If-None-Match), conflicts resolved server-wins
// with the local copy kept (seen in the pull, and a 412 on the PUT), a
// refused deletion restoring the card, an expired sync token, the ctag
// fallback, a group through the server, and 401 refusals. No real server.

#include "carddav_provider.h"
#include "carddav_xml.h"
#include "contact_store.h"
#include "fake_carddav_server.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Mail;
using namespace QindaOffice::Mail::Test;

namespace {

QByteArray card(const QString &uid, const QString &name, const QString &email, const QString &note = {})
{
    QByteArray out = "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:" + uid.toUtf8() + "\r\nFN:" + name.toUtf8() + "\r\nN:"
        + name.section(QLatin1Char(' '), -1).toUtf8() + ";" + name.section(QLatin1Char(' '), 0, 0).toUtf8()
        + ";;;\r\nEMAIL:" + email.toUtf8() + "\r\n";
    if (!note.isEmpty()) out += "NOTE:" + note.toUtf8() + "\r\n";
    return out + "END:VCARD\r\n";
}

struct Rig {
    QTemporaryDir dir;
    FakeCardDavServer server;
    ContactStore store;
    CardDavProvider provider;

    Rig()
        : store(dir.filePath(QStringLiteral("contacts.json")))
    {
        server.books[QStringLiteral("contacts")].name = QStringLiteral("Contacts");
        provider.setProperty("accountId", QStringLiteral("acct-1"));
        provider.setProperty("url", server.base() + QStringLiteral("/"));
        provider.setProperty("userName", QStringLiteral("ada"));
        provider.setProperty("addressBooks", QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("contacts")},
                                                                      {QStringLiteral("url"), server.bookUrl(QStringLiteral("contacts"))},
                                                                      {QStringLiteral("enabled"), true}}});
        provider.setStore(&store);
        provider.setSecret(QStringLiteral("s3cret"), QStringLiteral("basic"));
    }
    QVariantMap sync()
    {
        QSignalSpy spy(&provider, &CardDavProvider::syncFinished);
        const QString id = QStringLiteral("sync-%1").arg(++serial);
        provider.sync(id);
        if (!spy.wait(15000) && spy.isEmpty()) return {{QStringLiteral("state"), QStringLiteral("timeout")}};
        return spy.first().at(1).toMap();
    }
    QString idOf(const QString &email) const { return store.contactForAddress(email).value(QStringLiteral("id")).toString(); }
    QString bookFile(const QString &email) const
    {
        for (auto it = server.books.value(QStringLiteral("contacts")).cards.cbegin();
             it != server.books.value(QStringLiteral("contacts")).cards.cend(); ++it)
            if (it->vcard.contains(email.toUtf8())) return it.key();
        return {};
    }
    int serial = 0;
};

} // namespace

class TestMailCardDav : public QObject {
    Q_OBJECT

private slots:
    void parsesMultistatus();
    void discovery();
    void initialSyncThenQuiet();
    void incrementalBothWays();
    void conflictSeenInThePull();
    void conflictOnThePut();
    void refusedDeletionComesBack();
    void expiredTokenStartsOver();
    void ctagFallback();
    void groupsTravelAsAppleGroups();
    void unauthorized();
};

void TestMailCardDav::parsesMultistatus()
{
    CardDav::Multistatus ms;
    QString error;
    QVERIFY(CardDav::parseMultistatus(
        "<d:multistatus xmlns:d='DAV:' xmlns:c='urn:ietf:params:xml:ns:carddav'><d:response><d:href>/a/1.vcf</d:href>"
        "<d:propstat><d:prop><d:getetag>\"x\"</d:getetag><c:address-data>BEGIN:VCARD&#13;\nEND:VCARD</c:address-data></d:prop>"
        "<d:status>HTTP/1.1 200 OK</d:status></d:propstat><d:propstat><d:prop><d:displayname/></d:prop>"
        "<d:status>HTTP/1.1 404 Not Found</d:status></d:propstat></d:response><d:response><d:href>/a/2.vcf</d:href>"
        "<d:status>HTTP/1.1 404 Not Found</d:status></d:response><d:sync-token>tok-9</d:sync-token></d:multistatus>",
        &ms, &error));
    QCOMPARE(ms.responses.size(), qsizetype(2));
    QCOMPARE(ms.responses.at(0).etag, QStringLiteral("\"x\""));
    QVERIFY(ms.responses.at(0).addressData.startsWith("BEGIN:VCARD"));
    QCOMPARE(ms.responses.at(1).status, 404);
    QCOMPARE(ms.syncToken, QStringLiteral("tok-9"));
    QVERIFY(!CardDav::parseMultistatus("<!DOCTYPE x [<!ENTITY a 'b'>]><d:multistatus xmlns:d='DAV:'/>", &ms, &error));
    QVERIFY(!CardDav::parseMultistatus("<d:multistatus xmlns:d='DAV:'><d:response>", &ms, &error));
}

void TestMailCardDav::discovery()
{
    Rig rig;
    rig.server.books[QStringLiteral("work")].name = QStringLiteral("Work");
    QSignalSpy spy(&rig.provider, &CardDavProvider::discoveryFinished);
    QVERIFY(rig.provider.discover(QStringLiteral("d1")));
    QVERIFY(spy.wait(15000));
    const QVariantMap result = spy.first().at(1).toMap();
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
    const QVariantList books = result.value(QStringLiteral("addressBooks")).toList();
    QCOMPARE(books.size(), 2);
    QCOMPARE(books.at(0).toMap().value(QStringLiteral("url")).toString(), rig.server.bookUrl(QStringLiteral("contacts")));
    QCOMPARE(books.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Contacts"));
    QCOMPARE(books.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Work"));
    QCOMPARE(result.value(QStringLiteral("homeSetUrl")).toString(), rig.server.base() + QStringLiteral("/dav/books/ada/"));
    QStringList paths;
    for (const auto &r : rig.server.requests) paths.append(QString::fromLatin1(r.method) + QLatin1Char(' ') + r.path);
    QVERIFY2(paths.contains(QStringLiteral("PROPFIND /.well-known/carddav")), qPrintable(paths.join(u'|')));
    QVERIFY(paths.contains(QStringLiteral("PROPFIND /dav/principals/ada/")));
    QVERIFY(!result.value(QStringLiteral("transcript")).toStringList().join(u' ').contains(QLatin1String("s3cret")));
}

void TestMailCardDav::initialSyncThenQuiet()
{
    Rig rig;
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("alan.vcf"),
                       card(QStringLiteral("a-1"), QStringLiteral("Alan Turing"), QStringLiteral("alan@example.org")));
    const QVariantMap first = rig.sync();
    QVERIFY2(first.value(QStringLiteral("ok")).toBool(), qPrintable(first.value(QStringLiteral("error")).toString()));
    QCOMPARE(first.value(QStringLiteral("created")).toInt(), 2);
    QCOMPARE(rig.store.count(), 2);
    QCOMPARE(rig.store.contactForAddress(QStringLiteral("grace@example.org")).value(QStringLiteral("syncAccountId")).toString(),
             QStringLiteral("acct-1"));
    QCOMPARE(rig.server.count("REPORT"), 2);   // sync-collection + one multiget
    // Nothing changed anywhere: a quiet sync — no fetch, no write.
    rig.server.requests.clear();
    const QVariantMap second = rig.sync();
    QVERIFY(second.value(QStringLiteral("ok")).toBool());
    QCOMPARE(second.value(QStringLiteral("created")).toInt() + second.value(QStringLiteral("updated")).toInt()
                 + second.value(QStringLiteral("pushed")).toInt(), 0);
    QCOMPARE(rig.server.count("REPORT"), 1);
    QCOMPARE(rig.server.count("PUT"), 0);
}

void TestMailCardDav::incrementalBothWays()
{
    Rig rig;
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("alan.vcf"),
                       card(QStringLiteral("a-1"), QStringLiteral("Alan Turing"), QStringLiteral("alan@example.org")));
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    // Another device: Grace edited, Alan deleted, Ada added.
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org"), QStringLiteral("COBOL")));
    rig.server.removeCard(QStringLiteral("contacts"), QStringLiteral("alan.vcf"));
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("ada.vcf"),
                       card(QStringLiteral("ada-1"), QStringLiteral("Ada Lovelace"), QStringLiteral("ada@example.org")));
    QVariantMap result = rig.sync();
    QCOMPARE(result.value(QStringLiteral("updated")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("deleted")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("created")).toInt(), 1);
    QCOMPARE(rig.store.contactForAddress(QStringLiteral("grace@example.org")).value(QStringLiteral("notes")).toString(), QStringLiteral("COBOL"));
    QVERIFY(rig.store.contactForAddress(QStringLiteral("alan@example.org")).isEmpty());
    // This computer: Ada edited, a new contact for the book, Grace deleted.
    QVERIFY(rig.store.update({{QStringLiteral("id"), rig.idOf(QStringLiteral("ada@example.org"))},
                              {QStringLiteral("organization"), QStringLiteral("Analytical Engines")}}).value(QStringLiteral("ok")).toBool());
    QVERIFY(rig.store.add({{QStringLiteral("displayName"), QStringLiteral("Bob Example")},
                           {QStringLiteral("emails"), QVariantList{QVariantMap{{QStringLiteral("value"), QStringLiteral("bob@example.org")}}}},
                           {QStringLiteral("addressBookAccount"), QStringLiteral("acct-1")},
                           {QStringLiteral("addressBookUrl"), rig.server.bookUrl(QStringLiteral("contacts"))}}).value(QStringLiteral("ok")).toBool());
    QVERIFY(rig.store.remove(rig.idOf(QStringLiteral("grace@example.org"))).value(QStringLiteral("ok")).toBool());
    rig.server.requests.clear();
    result = rig.sync();
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
    QCOMPARE(result.value(QStringLiteral("pushed")).toInt(), 2);
    const auto &cards = rig.server.books.value(QStringLiteral("contacts")).cards;
    QVERIFY(!cards.contains(QStringLiteral("grace.vcf")));
    QVERIFY(cards.value(QStringLiteral("ada.vcf")).vcard.contains("ORG:Analytical Engines"));
    QVERIFY(!rig.bookFile(QStringLiteral("bob@example.org")).isEmpty());
    bool ifMatch = false, ifNoneMatch = false, deleteMatched = false;
    for (const auto &r : rig.server.requests) {
        ifMatch |= r.method == "PUT" && !r.headers.value("if-match").isEmpty();
        ifNoneMatch |= r.method == "PUT" && r.headers.value("if-none-match") == "*";
        deleteMatched |= r.method == "DELETE" && !r.headers.value("if-match").isEmpty();
    }
    QVERIFY(ifMatch && ifNoneMatch && deleteMatched);
    // And quiet again: our own writes come back with our etags.
    rig.server.requests.clear();
    result = rig.sync();
    QCOMPARE(result.value(QStringLiteral("updated")).toInt() + result.value(QStringLiteral("pushed")).toInt(), 0);
}

void TestMailCardDav::conflictSeenInThePull()
{
    Rig rig;
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    QVERIFY(rig.store.update({{QStringLiteral("id"), rig.idOf(QStringLiteral("grace@example.org"))},
                              {QStringLiteral("notes"), QStringLiteral("edited here")}}).value(QStringLiteral("ok")).toBool());
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org"), QStringLiteral("edited there")));
    rig.server.requests.clear();
    const QVariantMap result = rig.sync();
    QCOMPARE(result.value(QStringLiteral("conflicts")).toInt(), 1);
    QCOMPARE(rig.server.count("PUT"), 0);   // the server wins: nothing overwritten there
    QCOMPARE(rig.store.count(), 2);
    int copies = 0;
    for (const QVariant &value : rig.store.contacts()) {
        const QVariantMap c = value.toMap();
        if (c.value(QStringLiteral("categories")).toStringList().contains(QLatin1String(ContactStore::kConflictCategory))) {
            ++copies;
            QCOMPARE(c.value(QStringLiteral("notes")).toString(), QStringLiteral("edited here"));
            QVERIFY(c.value(QStringLiteral("syncAccountId")).toString().isEmpty());
        } else {
            QCOMPARE(c.value(QStringLiteral("notes")).toString(), QStringLiteral("edited there"));
        }
    }
    QCOMPARE(copies, 1);
}

void TestMailCardDav::conflictOnThePut()
{
    Rig rig;
    rig.server.syncCollection = false;
    rig.server.frozenCtag = true;   // the pull cannot see the change: the PUT meets it
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    QVERIFY(rig.store.update({{QStringLiteral("id"), rig.idOf(QStringLiteral("grace@example.org"))},
                              {QStringLiteral("notes"), QStringLiteral("mine")}}).value(QStringLiteral("ok")).toBool());
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org"), QStringLiteral("theirs")));
    const QVariantMap result = rig.sync();
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
    QCOMPARE(result.value(QStringLiteral("conflicts")).toInt(), 1);
    QVERIFY(rig.server.books.value(QStringLiteral("contacts")).cards.value(QStringLiteral("grace.vcf")).vcard.contains("NOTE:theirs"));
    QCOMPARE(rig.store.contactForAddress(QStringLiteral("grace@example.org")).value(QStringLiteral("syncAccountId")).toString(),
             QStringLiteral("acct-1"));
    QStringList notes;
    for (const QVariant &value : rig.store.contacts()) notes.append(value.toMap().value(QStringLiteral("notes")).toString());
    notes.sort();
    QCOMPARE(notes, (QStringList{QStringLiteral("mine"), QStringLiteral("theirs")}));
}

void TestMailCardDav::refusedDeletionComesBack()
{
    Rig rig;
    rig.server.syncCollection = false;
    rig.server.frozenCtag = true;
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    QVERIFY(rig.store.remove(rig.idOf(QStringLiteral("grace@example.org"))).value(QStringLiteral("ok")).toBool());
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org"), QStringLiteral("changed there")));
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    QCOMPARE(rig.store.contactForAddress(QStringLiteral("grace@example.org")).value(QStringLiteral("notes")).toString(),
             QStringLiteral("changed there"));
    QVERIFY(rig.server.books.value(QStringLiteral("contacts")).cards.contains(QStringLiteral("grace.vcf")));
}

void TestMailCardDav::expiredTokenStartsOver()
{
    Rig rig;
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("alan.vcf"),
                       card(QStringLiteral("a-1"), QStringLiteral("Alan Turing"), QStringLiteral("alan@example.org")));
    rig.server.oldestToken = 1000;   // every token we hold is refused (403 valid-sync-token)
    const QVariantMap result = rig.sync();
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
    QCOMPARE(rig.store.count(), 2);
    QVERIFY(result.value(QStringLiteral("transcript")).toStringList().join(u'\n').count(QLatin1String("REPORT")) >= 3);
}

void TestMailCardDav::ctagFallback()
{
    Rig rig;
    rig.server.syncCollection = false;
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    QCOMPARE(rig.store.count(), 1);
    rig.server.requests.clear();
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    QCOMPARE(rig.server.requests.size(), qsizetype(1));   // the ctag did not move: one PROPFIND
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("alan.vcf"),
                       card(QStringLiteral("a-1"), QStringLiteral("Alan Turing"), QStringLiteral("alan@example.org")));
    QCOMPARE(rig.sync().value(QStringLiteral("created")).toInt(), 1);
}

void TestMailCardDav::groupsTravelAsAppleGroups()
{
    Rig rig;
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("grace.vcf"),
                       card(QStringLiteral("g-1"), QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")));
    rig.server.putCard(QStringLiteral("contacts"), QStringLiteral("team.vcf"),
                       "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:team-1\r\nFN:Team\r\nN:Team;;;;\r\nX-ADDRESSBOOKSERVER-KIND:group\r\n"
                       "X-ADDRESSBOOKSERVER-MEMBER:urn:uuid:g-1\r\nEND:VCARD\r\n");
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    QString team;
    for (const QVariant &value : rig.store.contacts())
        if (value.toMap().value(QStringLiteral("isGroup")).toBool()) team = value.toMap().value(QStringLiteral("id")).toString();
    QCOMPARE(rig.store.expandGroup(team).value(QStringLiteral("recipients")).toStringList(),
             QStringList{QStringLiteral("Grace Hopper <grace@example.org>")});
    QVERIFY(rig.store.update({{QStringLiteral("id"), team},
                              {QStringLiteral("members"), QVariantList{QVariantMap{{QStringLiteral("uri"), QStringLiteral("urn:uuid:g-1")}},
                                                                       QStringLiteral("zed@example.org")}}}).value(QStringLiteral("ok")).toBool());
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    const QByteArray sent = rig.server.books.value(QStringLiteral("contacts")).cards.value(QStringLiteral("team.vcf")).vcard;
    QVERIFY2(sent.contains("X-ADDRESSBOOKSERVER-KIND:group"), sent.constData());
    QVERIFY(sent.contains("X-ADDRESSBOOKSERVER-MEMBER:mailto:zed@example.org"));
}

void TestMailCardDav::unauthorized()
{
    Rig rig;
    rig.provider.setSecret(QStringLiteral("wrong-password"), QStringLiteral("basic"));
    QVariantMap result = rig.sync();
    QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
    QVERIFY(result.value(QStringLiteral("authRejected")).toBool());
    QVERIFY(result.value(QStringLiteral("error")).toString().contains(QLatin1String("HTTP 401")));
    QVERIFY(!result.value(QStringLiteral("error")).toString().contains(QLatin1String("wrong-password")));
    // The right password; the server still refuses one request, then accepts.
    rig.provider.setSecret(QStringLiteral("s3cret"), QStringLiteral("basic"));
    rig.server.rejectNext = 1;
    QVERIFY(rig.sync().value(QStringLiteral("authRejected")).toBool());
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    // A bearer token is sent as one.
    rig.server.bearer = QStringLiteral("tok-123");
    rig.provider.setSecret(QStringLiteral("tok-123"), QStringLiteral("bearer"));
    rig.server.requests.clear();
    QVERIFY(rig.sync().value(QStringLiteral("ok")).toBool());
    QCOMPARE(rig.server.requests.first().headers.value("authorization"), QByteArray("Bearer tok-123"));
}

QTEST_GUILESS_MAIN(TestMailCardDav)

#include "tst_mail_carddav.moc"
