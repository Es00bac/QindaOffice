// SPDX-License-Identifier: GPL-3.0-or-later
// ContactStore (M64): persistence, photos, bounds, import merge, search,
// autocomplete and export. Everything lives in QTemporaryDirs, and main()
// points every XDG directory into one before Qt starts, so nothing of the
// owner's is ever read or written. Fixtures are fictional.

#include "contact_store.h"
#include "vcard.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace QindaOffice::Mail;
using namespace Qt::StringLiterals;

namespace {

QByteArray fakePng(int size, int seed = 1)
{
    QByteArray bytes("\x89PNG\r\n\x1A\n\x00\x00\x00\x0DIHDR", 16);
    for (int i = 0; bytes.size() < size; ++i) bytes.append(char(i * 13 + seed));
    return bytes;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QVariantMap email(const QString &address, const QString &type = {}, bool preferred = false)
{
    return {{u"value"_s, address}, {u"type"_s, type}, {u"preferred"_s, preferred}};
}

QVariantMap phone(const QString &number)
{
    return {{u"value"_s, number}};
}

QStringList column(const QVariantList &maps, const char *key = "displayName")
{
    QStringList out;
    for (const QVariant &map : maps) out.append(map.toMap().value(QString::fromLatin1(key)).toString());
    return out;
}

QString dump(const Contact &c)
{
    return QString::fromUtf8(QJsonDocument(toJson(c)).toJson());
}

} // namespace

class TestMailContactStore : public QObject {
    Q_OBJECT

public:
    explicit TestMailContactStore(const QString &xdgRoot) : m_xdgRoot(xdgRoot) {}

private:
    QString m_xdgRoot;

private slots:
    void defaultPathStaysInsideTheTestXdgHome()
    {
        const QString path = ContactStore::defaultPath();
        QVERIFY2(path.startsWith(m_xdgRoot), qPrintable(path));
        QVERIFY(path.endsWith(u"/qindaoffice/mail/contacts.json"_s));
        ContactStore store;
        QCOMPARE(store.path(), path);
        QVERIFY(store.add({{u"displayName"_s, u"Default Path"_s}}).value(u"ok"_s).toBool());
        QVERIFY(QFile::exists(path));
    }

    void addUpdateRemovePersist()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"book/contacts.json"_s);
        ContactStore store(path);
        QSignalSpy changed(&store, &ContactStore::contactsChanged);
        QSignalSpy added(&store, &ContactStore::contactAdded);
        const QVariantMap result = store.add({
            {u"givenName"_s, u"Ada"_s}, {u"familyName"_s, u"Lovelace"_s},
            {u"emails"_s, QVariantList{email(u"ada@example.org"_s, u"Work"_s, true)}},
            {u"phones"_s, QVariantList{phone(u"+1 555 0100"_s)}},
            {u"addresses"_s, QVariantList{QVariantMap{{u"type"_s, u"home"_s}, {u"street"_s, u"1 Engine Way"_s}}}},
            {u"categories"_s, u"Friends, Work"_s}, {u"birthday"_s, u"18151210"_s},
        });
        QVERIFY2(result.value(u"ok"_s).toBool(), qPrintable(result.value(u"error"_s).toString()));
        const QString id = result.value(u"id"_s).toString();
        QVERIFY(!id.isEmpty());
        QCOMPARE(added.count(), 1);
        QCOMPARE(changed.count(), 1);
        const QJsonObject root = QJsonDocument::fromJson(readFile(path)).object();
        QCOMPARE(root.value(u"schema"_s).toString(), u"qindaoffice.mail.contacts/1"_s);
        QCOMPARE(root.value(u"contacts"_s).toArray().size(), 1);

        ContactStore reopened(path);
        const QVariantMap map = reopened.contactById(id);
        QCOMPARE(map.value(u"displayName"_s).toString(), u"Ada Lovelace"_s);
        QCOMPARE(map.value(u"formattedName"_s).toString(), QString());
        QCOMPARE(map.value(u"birthday"_s).toString(), u"1815-12-10"_s);
        QCOMPARE(map.value(u"categories"_s).toStringList(), QStringList({u"Friends"_s, u"Work"_s}));
        QCOMPARE(map.value(u"emails"_s).toList().first().toMap().value(u"type"_s).toString(), u"work"_s);
        QCOMPARE(map.value(u"primaryEmail"_s).toString(), u"ada@example.org"_s);
        QCOMPARE(map.value(u"photoUrl"_s).toString(), QString());

        QSignalSpy updated(&reopened, &ContactStore::contactUpdated);
        QVERIFY(reopened.update({{u"id"_s, id}, {u"title"_s, u"Countess"_s}}).value(u"ok"_s).toBool());
        QCOMPARE(updated.count(), 1);
        const QVariantMap after = reopened.contactById(id);
        QCOMPARE(after.value(u"title"_s).toString(), u"Countess"_s);
        QCOMPARE(after.value(u"emails"_s).toList().size(), 1); // keys not named are kept
        QCOMPARE(after.value(u"phones"_s).toList().size(), 1);
        // The map QML was given goes back through update() unchanged.
        QVERIFY(reopened.update(after).value(u"ok"_s).toBool());
        QVariantMap again = reopened.contactById(id);
        QVariantMap expected = after;
        again.remove(u"formattedName"_s);
        expected.remove(u"formattedName"_s);
        QCOMPARE(again, expected);
        store.reload();
        QCOMPARE(store.contactById(id).value(u"title"_s).toString(), u"Countess"_s);

        QSignalSpy removed(&reopened, &ContactStore::contactRemoved);
        QVERIFY(reopened.remove(id).value(u"ok"_s).toBool());
        QCOMPARE(removed.count(), 1);
        QCOMPARE(reopened.count(), 0);
        QCOMPARE(QJsonDocument::fromJson(readFile(path)).object().value(u"contacts"_s).toArray().size(), 0);
        QVERIFY(!reopened.remove(id).value(u"ok"_s).toBool());
    }

    void validation()
    {
        QTemporaryDir dir;
        ContactStore store(dir.filePath(u"contacts.json"_s));
        QVariantMap result = store.add({});
        QVERIFY(!result.value(u"ok"_s).toBool());
        QVERIFY(result.value(u"error"_s).toString().contains(u"needs a name"_s));
        QCOMPARE(store.lastError(), result.value(u"error"_s).toString());
        result = store.add({{u"displayName"_s, u"X"_s}, {u"emails"_s, QVariantList{email(u"not an address"_s)}}});
        QVERIFY(result.value(u"error"_s).toString().startsWith(u"emails[0]"_s));
        result = store.add({{u"displayName"_s, u"X"_s}, {u"birthday"_s, u"31/31/2020"_s}});
        QVERIFY(result.value(u"error"_s).toString().startsWith(u"birthday"_s));
        QVERIFY(!store.add({{u"displayName"_s, u"X"_s}, {u"id"_s, u"../escape"_s}}).value(u"ok"_s).toBool());
        QVERIFY(!store.update({{u"id"_s, u"missing"_s}, {u"title"_s, u"x"_s}}).value(u"ok"_s).toBool());
        QCOMPARE(store.count(), 0);
        QVERIFY(!QFile::exists(dir.filePath(u"contacts.json"_s))); // nothing was ever written
    }

    void unknownFieldsArePreserved()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"contacts.json"_s);
        const QJsonObject root{
            {u"schema"_s, u"qindaoffice.mail.contacts/1"_s},
            {u"futureRoot"_s, QJsonArray{1, 2}},
            {u"contacts"_s, QJsonArray{
                QJsonObject{{u"id"_s, u"c1"_s}, {u"displayName"_s, u"Known"_s}, {u"futureField"_s, u"keep me"_s}},
                QJsonObject{{u"displayName"_s, u"No Id"_s}},
                QJsonObject{{u"id"_s, u"c1"_s}, {u"displayName"_s, u"Duplicate Id"_s}},
                QJsonObject{{u"id"_s, u"c4"_s}, {u"displayName"_s, u"Escaping Photo"_s}, {u"photoFile"_s, u"../contacts.json"_s}},
                u"not an object"_s,
            }},
        };
        QVERIFY(writeFile(path, QJsonDocument(root).toJson()));
        ContactStore store(path);
        QCOMPARE(store.count(), 4);
        QVERIFY(!store.readOnly());
        QVERIFY(store.contactById(u"c4"_s).value(u"photoUrl"_s).toString().isEmpty());
        QVERIFY(store.update({{u"id"_s, u"c1"_s}, {u"title"_s, u"Engineer"_s}}).value(u"ok"_s).toBool());
        const QJsonObject saved = QJsonDocument::fromJson(readFile(path)).object();
        QCOMPARE(saved.value(u"futureRoot"_s).toArray(), (QJsonArray{1, 2}));
        const QJsonArray contacts = saved.value(u"contacts"_s).toArray();
        QCOMPARE(contacts.size(), 4);
        QCOMPARE(contacts.at(0).toObject().value(u"futureField"_s).toString(), u"keep me"_s);
        QCOMPARE(contacts.at(0).toObject().value(u"title"_s).toString(), u"Engineer"_s);
        QSet<QString> ids;
        for (const QJsonValue &value : contacts) ids.insert(value.toObject().value(u"id"_s).toString());
        QCOMPARE(ids.size(), 4); // the id-less and the duplicate entry got fresh ids
    }

    void photosAreFilesAndAreCleanedUp()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath(u"picture.png"_s);
        const QByteArray png = fakePng(2048);
        QVERIFY(writeFile(picture, png));
        ContactStore store(dir.filePath(u"contacts.json"_s));
        QVariantMap result = store.add({{u"displayName"_s, u"With Photo"_s}, {u"photoFile"_s, QUrl::fromLocalFile(picture).toString()}});
        QVERIFY(result.value(u"ok"_s).toBool());
        QVERIFY(result.value(u"warnings"_s).toStringList().isEmpty());
        const QString first = result.value(u"id"_s).toString();
        const QString stored = store.photoDirectory() + u'/'
            + QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex()) + u".png"_s;
        QCOMPARE(readFile(stored), png);
        const QVariantMap map = store.contactById(first);
        QCOMPARE(map.value(u"photoUrl"_s).toString(), QUrl::fromLocalFile(stored).toString());
        QVERIFY(map.value(u"hasPhoto"_s).toBool());
        QCOMPARE(map.value(u"photoMimeType"_s).toString(), u"image/png"_s);
        QCOMPARE(store.photoBytes(first), png);
        // The same picture (here as a plain path) is stored once.
        const QString second = store.add({{u"displayName"_s, u"Same Photo"_s}, {u"photoFile"_s, picture}}).value(u"id"_s).toString();
        QCOMPARE(QDir(store.photoDirectory()).entryList(QDir::Files).size(), 1);
        QVERIFY(store.remove(first).value(u"ok"_s).toBool());
        QVERIFY(QFile::exists(stored)); // still referenced
        QVERIFY(store.update({{u"id"_s, second}, {u"photoRemove"_s, true}}).value(u"ok"_s).toBool());
        QVERIFY(!QFile::exists(stored)); // no longer referenced
        QVERIFY(!store.contactById(second).value(u"hasPhoto"_s).toBool());
        // Over 1 MiB, not a picture, or not local: refused with a warning,
        // the contact kept without it, nothing fetched.
        const QString big = dir.filePath(u"big.png"_s);
        QVERIFY(writeFile(big, fakePng(ContactLimits::kMaxPhotoBytes + 1)));
        result = store.add({{u"displayName"_s, u"Big Photo"_s}, {u"photoFile"_s, big}});
        QVERIFY(result.value(u"ok"_s).toBool());
        QVERIFY(result.value(u"warnings"_s).toStringList().join(u'\n').contains(u"larger than 1 MiB"_s));
        QVERIFY(!store.contactById(result.value(u"id"_s).toString()).value(u"hasPhoto"_s).toBool());
        const QString text = dir.filePath(u"notes.txt"_s);
        QVERIFY(writeFile(text, "just text"));
        QVERIFY(!store.add({{u"displayName"_s, u"Text"_s}, {u"photoFile"_s, text}}).value(u"warnings"_s).toStringList().isEmpty());
        QVERIFY(!store.add({{u"displayName"_s, u"Remote"_s}, {u"photoFile"_s, u"https://example.org/a.png"_s}})
                     .value(u"warnings"_s).toStringList().isEmpty());
        QVERIFY(QDir(store.photoDirectory()).entryList(QDir::Files).isEmpty());
    }

    void foreignSchemaIsReadOnly()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"contacts.json"_s);
        const QByteArray original = QJsonDocument(QJsonObject{
            {u"schema"_s, u"qindaoffice.mail.contacts/2"_s},
            {u"contacts"_s, QJsonArray{QJsonObject{{u"id"_s, u"c1"_s}, {u"displayName"_s, u"From The Future"_s}}}},
        }).toJson();
        QVERIFY(writeFile(path, original));
        ContactStore store(path);
        QCOMPARE(store.count(), 1);
        QVERIFY(store.readOnly());
        QVERIFY(store.lastError().contains(u"read-only"_s));
        QVERIFY(!store.add({{u"displayName"_s, u"New"_s}}).value(u"ok"_s).toBool());
        QVERIFY(!store.update({{u"id"_s, u"c1"_s}, {u"title"_s, u"x"_s}}).value(u"ok"_s).toBool());
        QVERIFY(!store.remove(u"c1"_s).value(u"ok"_s).toBool());
        QVERIFY(!store.importData("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:X\r\nEND:VCARD\r\n", u"x.vcf"_s).value(u"ok"_s).toBool());
        QCOMPARE(readFile(path), original);
    }

    void corruptFileIsMovedAside()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"contacts.json"_s);
        QVERIFY(writeFile(path, "{not json"));
        ContactStore store(path);
        QCOMPARE(store.count(), 0);
        QVERIFY(!store.readOnly());
        QVERIFY(store.lastError().contains(u"moved to"_s));
        QCOMPARE(readFile(path + u".corrupt"_s), QByteArray("{not json"));
        QVERIFY(!QFile::exists(path));
        QVERIFY(store.add({{u"displayName"_s, u"Fresh Start"_s}}).value(u"ok"_s).toBool());
        QVERIFY(QFile::exists(path));
        QCOMPARE(readFile(path + u".corrupt"_s), QByteArray("{not json"));
    }

    void boundsRefuseTheTwentyThousandAndFirst()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"contacts.json"_s);
        auto writeBook = [&path](int count) {
            QByteArray json = "{\"schema\": \"qindaoffice.mail.contacts/1\", \"contacts\": [";
            for (int i = 0; i < count; ++i) {
                if (i > 0) json += ',';
                json += "{\"id\": \"c" + QByteArray::number(i) + "\", \"displayName\": \"Person " + QByteArray::number(i) + "\"}";
            }
            return writeFile(path, json + "]}");
        };
        QVERIFY(writeBook(ContactLimits::kMaxContacts));
        ContactStore store(path);
        QCOMPARE(store.count(), ContactLimits::kMaxContacts);
        QVERIFY(!store.readOnly());
        QVariantMap result = store.add({{u"displayName"_s, u"One Too Many"_s}});
        QVERIFY(!result.value(u"ok"_s).toBool());
        QVERIFY(result.value(u"error"_s).toString().contains(u"full"_s));
        result = store.importData("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:New Person\r\nEND:VCARD\r\n", u"new.vcf"_s);
        QVERIFY(!result.value(u"ok"_s).toBool());
        QVERIFY(result.value(u"error"_s).toString().contains(u"Nothing was imported"_s));
        // A card whose UID is a store id (what an export writes) updates in place.
        result = store.importData("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Person Five Renamed\r\nUID:c5\r\nEND:VCARD\r\n", u"u.vcf"_s);
        QVERIFY2(result.value(u"ok"_s).toBool(), qPrintable(result.value(u"error"_s).toString()));
        QCOMPARE(result.value(u"updated"_s).toInt(), 1);
        QCOMPARE(store.contactById(u"c5"_s).value(u"displayName"_s).toString(), u"Person Five Renamed"_s);
        QCOMPARE(store.count(), ContactLimits::kMaxContacts);
        // More than the bound on disk: read-only, so writing back loses nothing.
        QVERIFY(writeBook(ContactLimits::kMaxContacts + 1));
        store.reload();
        QVERIFY(store.readOnly());
        QCOMPARE(store.count(), ContactLimits::kMaxContacts);
    }

    void importMergeRules()
    {
        QTemporaryDir dir;
        ContactStore store(dir.filePath(u"contacts.json"_s));
        const QString ada = store.add({{u"displayName"_s, u"Ada Lovelace"_s}, {u"emails"_s, QVariantList{email(u"ada@example.org"_s)}},
                                       {u"phones"_s, QVariantList{phone(u"+1 555 0100"_s)}},
                                       {u"categories"_s, QStringList{u"Friends"_s}}}).value(u"id"_s).toString();
        const QByteArray cards =
            "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:A. Lovelace\r\nEMAIL:ADA@example.org\r\nTEL:+1 (555) 0100\r\n"
            "TEL;TYPE=cell:+1 555 0111\r\nTITLE:Countess\r\nCATEGORIES:Work,friends\r\nNOTE:From the import\r\nEND:VCARD\r\n"
            "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Grace Hopper\r\nUID:u-2\r\nEMAIL:grace@example.org\r\nEND:VCARD\r\n"
            "BEGIN:VCARD\r\nVERSION:3.0\r\nTEL:+1 555 0199\r\nEND:VCARD\r\n";
        QSignalSpy changed(&store, &ContactStore::contactsChanged);
        QVariantMap result = store.importData(cards, u"people.vcf"_s);
        QVERIFY2(result.value(u"ok"_s).toBool(), qPrintable(result.value(u"error"_s).toString()));
        QCOMPARE(result.value(u"added"_s).toInt(), 1);
        QCOMPARE(result.value(u"updated"_s).toInt(), 1);
        QCOMPARE(result.value(u"skipped"_s).toInt(), 1);
        QVERIFY(result.value(u"warnings"_s).toStringList().join(u'\n').contains(u"has no name"_s));
        QCOMPARE(changed.count(), 1); // one signal per import
        const QVariantMap merged = store.contactById(ada);
        QCOMPARE(merged.value(u"displayName"_s).toString(), u"Ada Lovelace"_s); // kept
        QCOMPARE(merged.value(u"title"_s).toString(), u"Countess"_s);           // filled
        QCOMPARE(merged.value(u"notes"_s).toString(), u"From the import"_s);
        QCOMPARE(column(merged.value(u"phones"_s).toList(), "value"), QStringList({u"+1 555 0100"_s, u"+1 555 0111"_s}));
        QCOMPARE(column(merged.value(u"emails"_s).toList(), "value"), QStringList({u"ada@example.org"_s}));
        QCOMPARE(merged.value(u"categories"_s).toStringList(), QStringList({u"Friends"_s, u"Work"_s}));
        // The same UID again: replaced in place.
        result = store.importData("BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Grace B. Hopper\r\nUID:u-2\r\n"
                                  "EMAIL:grace@example.org\r\nEND:VCARD\r\n", u"again.vcf"_s);
        QCOMPARE(result.value(u"updated"_s).toInt(), 1);
        QCOMPARE(result.value(u"added"_s).toInt(), 0);
        QCOMPARE(store.count(), 2);
        QCOMPARE(column(store.search(u"grace"_s)), QStringList({u"Grace B. Hopper"_s}));
    }

    void searchAndCompleteRanking()
    {
        QTemporaryDir dir;
        ContactStore store(dir.filePath(u"contacts.json"_s));
        const QList<QVariantMap> people = {
            {{u"displayName"_s, u"Ada Lovelace"_s}, {u"nickname"_s, u"Countess"_s}, {u"organization"_s, u"Analytical Engines"_s},
             {u"emails"_s, QVariantList{email(u"ada@example.org"_s), email(u"countess@example.org"_s, u"work"_s, true)}}},
            {{u"displayName"_s, u"Adam Smith"_s}, {u"emails"_s, QVariantList{email(u"adam@example.com"_s)}}},
            {{u"displayName"_s, u"Grace Hopper"_s}, {u"organization"_s, u"US Navy"_s},
             {u"emails"_s, QVariantList{email(u"grace@example.org"_s)}}, {u"phones"_s, QVariantList{phone(u"+1 555 0199"_s)}}},
            {{u"displayName"_s, u"Linus Phone"_s}, {u"phones"_s, QVariantList{phone(u"+1 555 0177"_s)}}},
            {{u"givenName"_s, u"Zoë"_s}, {u"familyName"_s, u"Ådahl"_s}, {u"emails"_s, QVariantList{email(u"zoe@example.org"_s)}}},
            {{u"displayName"_s, u"Lovelace, Ada (Home)"_s}, {u"emails"_s, QVariantList{email(u"ada.home@example.org"_s)}}},
            {{u"displayName"_s, u"Item 10"_s}}, {{u"displayName"_s, u"Item 9"_s}}, {{u"displayName"_s, u"émile Zola"_s}},
        };
        for (const QVariantMap &person : people) QVERIFY(store.add(person).value(u"ok"_s).toBool());
        QCOMPARE(column(store.contacts()),
                 QStringList({u"Ada Lovelace"_s, u"Adam Smith"_s, u"émile Zola"_s, u"Grace Hopper"_s, u"Item 9"_s,
                              u"Item 10"_s, u"Linus Phone"_s, u"Lovelace, Ada (Home)"_s, u"Zoë Ådahl"_s}));
        QCOMPARE(column(store.search(u"ada"_s)),
                 QStringList({u"Ada Lovelace"_s, u"Adam Smith"_s, u"Lovelace, Ada (Home)"_s, u"Zoë Ådahl"_s}));
        QCOMPARE(column(store.search(u"ada lov"_s)), QStringList({u"Ada Lovelace"_s, u"Lovelace, Ada (Home)"_s}));
        QCOMPARE(column(store.search(u"navy"_s)), QStringList({u"Grace Hopper"_s}));
        QCOMPARE(column(store.search(u"0199"_s)), QStringList({u"Grace Hopper"_s}));
        QCOMPARE(column(store.search(u"ZOE"_s)), QStringList({u"Zoë Ådahl"_s}));
        QCOMPARE(column(store.search(u"countess"_s)), QStringList({u"Ada Lovelace"_s}));
        QCOMPARE(store.search(QString()).size(), 9);
        QCOMPARE(store.search(QString(), 3).size(), 3);

        const QVariantList ada = store.complete(u"ada"_s);
        QCOMPARE(column(ada, "value"), QStringList({u"ada@example.org"_s, u"countess@example.org"_s, u"adam@example.com"_s,
                                                    u"ada.home@example.org"_s, u"zoe@example.org"_s}));
        QCOMPARE(column(ada, "label"), QStringList({u"Ada Lovelace <ada@example.org>"_s, u"Ada Lovelace <countess@example.org>"_s,
                                                    u"Adam Smith <adam@example.com>"_s,
                                                    u"\"Lovelace, Ada (Home)\" <ada.home@example.org>"_s,
                                                    u"Zoë Ådahl <zoe@example.org>"_s}));
        QCOMPARE(ada.at(1).toMap().value(u"type"_s).toString(), u"work"_s);
        QCOMPARE(ada.at(0).toMap().value(u"name"_s).toString(), u"Ada Lovelace"_s);
        QVERIFY(!ada.at(0).toMap().value(u"contactId"_s).toString().isEmpty());
        // A contact's preferred address first when both match alike.
        QCOMPARE(column(store.complete(u"count"_s), "value"), QStringList({u"countess@example.org"_s, u"ada@example.org"_s}));
        QCOMPARE(column(store.complete(u"grace@example.org"_s), "label"), QStringList({u"Grace Hopper <grace@example.org>"_s}));
        QVERIFY(store.complete(QString()).isEmpty());
        QVERIFY(store.complete(u"linus"_s).isEmpty()); // no address to offer
        QCOMPARE(store.complete(u"a"_s, 2).size(), 2);
    }

    void addSenderFindsOrCreates()
    {
        QTemporaryDir dir;
        ContactStore store(dir.filePath(u"contacts.json"_s));
        QVariantMap result = store.addSender(u"Hopper, Grace"_s, u"grace@example.org"_s);
        QVERIFY(result.value(u"ok"_s).toBool());
        QVERIFY(!result.value(u"existed"_s).toBool());
        const QVariantMap grace = store.contactById(result.value(u"id"_s).toString());
        QCOMPARE(grace.value(u"givenName"_s).toString(), u"Grace"_s);
        QCOMPARE(grace.value(u"familyName"_s).toString(), u"Hopper"_s);
        QCOMPARE(grace.value(u"displayName"_s).toString(), u"Grace Hopper"_s);
        result = store.addSender(u"Someone Else"_s, u"Grace Hopper <GRACE@example.org>"_s);
        QVERIFY(result.value(u"existed"_s).toBool());
        QCOMPARE(result.value(u"id"_s), grace.value(u"id"_s));
        QCOMPARE(store.contactForAddress(u"Grace@Example.org"_s).value(u"id"_s), grace.value(u"id"_s));
        QVERIFY(store.contactForAddress(u"nobody@example.org"_s).isEmpty());
        QVERIFY(!store.addSender(u"x"_s, u"not an address"_s).value(u"ok"_s).toBool());
        result = store.addSender(QString(), u"bare@example.org"_s);
        QCOMPARE(store.contactById(result.value(u"id"_s).toString()).value(u"displayName"_s).toString(), u"bare@example.org"_s);
        // Without a name, the address's own display name is used.
        result = store.addSender(QString(), u"Dorothy Vaughan <dorothy@example.org>"_s);
        const QVariantMap dorothy = store.contactById(result.value(u"id"_s).toString());
        QCOMPARE(dorothy.value(u"givenName"_s).toString(), u"Dorothy"_s);
        QCOMPARE(dorothy.value(u"familyName"_s).toString(), u"Vaughan"_s);
        QCOMPARE(store.count(), 3);
    }

    void exportThenImportIsEqual()
    {
        QTemporaryDir dir;
        const QString picture = dir.filePath(u"p.png"_s);
        QVERIFY(writeFile(picture, fakePng(4000, 7)));
        ContactStore store(dir.filePath(u"a/contacts.json"_s));
        const QVariantMap full{
            {u"displayName"_s, u"Dr. Ægir Þórsson"_s}, {u"prefix"_s, u"Dr."_s}, {u"givenName"_s, u"Ægir"_s},
            {u"familyName"_s, u"Þórsson"_s}, {u"nickname"_s, u"Gir"_s}, {u"organization"_s, u"Example Hf."_s},
            {u"department"_s, u"R&D"_s}, {u"title"_s, u"Chief"_s},
            {u"emails"_s, QVariantList{email(u"aegir@example.org"_s, u"work"_s, true), email(u"club@example.org"_s, u"Gaming Club"_s)}},
            {u"phones"_s, QVariantList{QVariantMap{{u"type"_s, u"cell"_s}, {u"value"_s, u"+354 555 0101"_s}}}},
            {u"urls"_s, QVariantList{QVariantMap{{u"type"_s, u"homepage"_s}, {u"value"_s, u"https://example.org/blog"_s}}}},
            {u"addresses"_s, QVariantList{QVariantMap{{u"type"_s, u"home"_s}, {u"street"_s, u"Laugavegur 1\nBack"_s},
                                                      {u"locality"_s, u"Reykjavík"_s}, {u"country"_s, u"Iceland"_s},
                                                      {u"label"_s, u"Laugavegur 1\nReykjavík"_s}, {u"preferred"_s, true}}}},
            {u"birthday"_s, u"--02-29"_s}, {u"anniversary"_s, u"2001-06-17"_s},
            {u"notes"_s, u"Two\nlines; with, punctuation \\ and 李小龍 🙂"_s},
            {u"categories"_s, QStringList{u"Friends"_s, u"Ferðir"_s}},
            {u"photoRemoteUri"_s, u"https://example.org/remote.jpg"_s},
            {u"extra"_s, QVariantList{QVariantMap{{u"name"_s, u"X-SOCIALPROFILE"_s},
                                                  {u"params"_s, QVariantList{QVariantMap{{u"name"_s, u"TYPE"_s}, {u"value"_s, u"mastodon"_s}}}},
                                                  {u"value"_s, u"https://example.org/@aegir"_s}}}},
            {u"photoFile"_s, QUrl::fromLocalFile(picture)},
        };
        QStringList ids;
        for (const QVariantMap &map : {full, QVariantMap{{u"displayName"_s, u"Second Person"_s}, {u"uid"_s, u"urn:uuid:second"_s},
                                                         {u"emails"_s, QVariantList{email(u"second@example.org"_s)}}},
                                       QVariantMap{{u"displayName"_s, u"Third, With Comma"_s}, {u"organization"_s, u"Org; Inc"_s}}}) {
            const QVariantMap result = store.add(map);
            QVERIFY2(result.value(u"ok"_s).toBool(), qPrintable(result.value(u"error"_s).toString()));
            ids.append(result.value(u"id"_s).toString());
        }
        QVERIFY(!store.photoBytes(ids.first()).isEmpty());
        for (const QString &version : {u"3.0"_s, u"4.0"_s}) {
            const QString file = dir.filePath(u"export-%1.vcf"_s.arg(version));
            QVariantMap result = store.exportVcf(QUrl::fromLocalFile(file), {}, version);
            QVERIFY2(result.value(u"ok"_s).toBool(), qPrintable(result.value(u"error"_s).toString()));
            QCOMPARE(result.value(u"count"_s).toInt(), 3);
            ContactStore other(dir.filePath(u"b-%1/contacts.json"_s.arg(version)));
            result = other.importFile(QUrl::fromLocalFile(file));
            QVERIFY2(result.value(u"ok"_s).toBool(), qPrintable(result.value(u"error"_s).toString()));
            QCOMPARE(result.value(u"added"_s).toInt(), 3);
            for (const Contact &original : store.list()) {
                const QString uid = original.uid.isEmpty() ? original.id : original.uid; // what the export wrote
                std::optional<Contact> imported;
                for (const Contact &c : other.list()) {
                    if (c.uid == uid) imported = c;
                }
                QVERIFY2(imported.has_value(), qPrintable(uid));
                Contact copy = *imported;
                copy.id = original.id;
                copy.uid = original.uid;
                QCOMPARE(dump(copy), dump(original));
                QCOMPARE(other.photoBytes(imported->id), store.photoBytes(original.id));
            }
        }
        QCOMPARE(store.exportVcf(QUrl::fromLocalFile(dir.filePath(u"one.vcf"_s)), {ids.at(1)}, u"3.0"_s).value(u"count"_s).toInt(), 1);
        QVERIFY(!store.exportVcf(QUrl::fromLocalFile(dir.filePath(u"x.vcf"_s)), {}, u"5.0"_s).value(u"ok"_s).toBool());
        QVERIFY(!store.exportVcf(QUrl::fromLocalFile(store.path()), {}, u"3.0"_s).value(u"ok"_s).toBool());
        QVERIFY(!store.exportVcf(QUrl(u"https://example.org/x.vcf"_s), {}, u"3.0"_s).value(u"ok"_s).toBool());
    }

    void importFormatsAndErrors()
    {
        QTemporaryDir dir;
        ContactStore store(dir.filePath(u"contacts.json"_s));
        QVariantMap result = store.importData("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Sniffed Card\r\nEND:VCARD\r\n", u"download.txt"_s);
        QCOMPARE(result.value(u"added"_s).toInt(), 1);
        result = store.importData("First Name,E-mail Address\r\nCsv,csv@example.org\r\n", u"export"_s);
        QCOMPARE(result.value(u"added"_s).toInt(), 1);
        QVERIFY(!store.importData("hello world", u"notes.txt"_s).value(u"ok"_s).toBool());
        result = store.importData("no cards in here", u"empty.vcf"_s);
        QVERIFY(!result.value(u"ok"_s).toBool());
        QVERIFY2(result.value(u"error"_s).toString().contains(u"No vCard"_s), qPrintable(result.value(u"error"_s).toString()));
        const QString csv = dir.filePath(u"people.csv"_s);
        QVERIFY(writeFile(csv, "First Name,Last Name\r\nFile,Import\r\n"));
        result = store.importFile(QUrl::fromLocalFile(csv));
        QCOMPARE(result.value(u"added"_s).toInt(), 1);
        QVERIFY(!store.importFile(QUrl::fromLocalFile(dir.filePath(u"missing.vcf"_s))).value(u"ok"_s).toBool());
        QVERIFY(!store.importFile(QUrl(u"https://example.org/people.vcf"_s)).value(u"ok"_s).toBool());
        QCOMPARE(store.count(), 3);
        QCOMPARE(column(store.search(u"file import"_s)), QStringList({u"File Import"_s}));
    }
};

int main(int argc, char *argv[])
{
    // AGENT-GUARD: before Qt starts, so QStandardPaths can never resolve the
    // owner's real data, config, state or cache directories.
    QTemporaryDir xdg;
    if (!xdg.isValid()) return 1;
    for (const char *name : {"XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) {
        qputenv(name, QFile::encodeName(xdg.filePath(QString::fromLatin1(name))));
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); // deterministic collation
    QCoreApplication app(argc, argv);
    TestMailContactStore test(xdg.path());
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mail_contact_store.moc"
