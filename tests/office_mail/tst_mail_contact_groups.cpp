// SPDX-License-Identifier: GPL-3.0-or-later
// Contact groups (M76, D-085): vCard 4.0 KIND:group / MEMBER and Apple's
// vCard 3.0 X-ADDRESSBOOKSERVER-KIND / -MEMBER in and out, round trips, and
// the address book's group API — members by contact or raw address,
// resolution for the People card, expansion for the composer (nested
// groups, cycles, duplicates, missing members), autocomplete, persistence.

#include "contact.h"
#include "contact_store.h"
#include "vcard.h"

#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace QindaOffice::Mail;
using namespace Qt::StringLiterals;

namespace {

QByteArray crlf(const QList<QByteArray> &lines)
{
    QByteArray out;
    for (const QByteArray &line : lines) out += line + "\r\n";
    return out;
}

const QByteArray kPeople = crlf({"BEGIN:VCARD", "VERSION:4.0", "FN:Grace Hopper", "EMAIL:grace@example.org",
                                 "UID:urn:uuid:11111111-1111-4111-8111-111111111111", "END:VCARD", "BEGIN:VCARD",
                                 "VERSION:4.0", "FN:Alan Turing", "EMAIL:alan@example.org",
                                 "UID:urn:uuid:22222222-2222-4222-8222-222222222222", "END:VCARD"});

} // namespace

class TestMailContactGroups : public QObject {
    Q_OBJECT

private slots:
    void readsVCard40Group();
    void readsAppleGroup30();
    void roundTrips_data();
    void roundTrips();
    void storeGroupsResolveAndExpand();
    void exportAndImportKeepMembers();
    void validation();
};

void TestMailContactGroups::readsVCard40Group()
{
    const VCard::ReadResult read = VCard::read(crlf({
        "BEGIN:VCARD", "VERSION:4.0", "KIND:group", "FN:The Lab", "NOTE:Weekly",
        "MEMBER:urn:uuid:11111111-1111-4111-8111-111111111111",
        "MEMBER;X-CN=\"Lee, Ann\":mailto:ann@example.org", "UID:urn:uuid:99999999-9999-4999-8999-999999999999",
        "END:VCARD"}));
    QVERIFY2(read.warnings.isEmpty(), qPrintable(read.warnings.join(u'\n')));
    QCOMPARE(read.contacts.size(), 1);
    const Contact &g = read.contacts.first();
    QVERIFY(g.isGroup());
    QCOMPARE(g.displayName, u"The Lab"_s);
    QCOMPARE(g.notes, u"Weekly"_s);
    QCOMPARE(g.members.size(), qsizetype(2));
    QCOMPARE(g.members.at(0).uri, u"urn:uuid:11111111-1111-4111-8111-111111111111"_s);
    QVERIFY(!g.members.at(0).isAddress());
    QVERIFY(g.members.at(1).isAddress());
    QCOMPARE(g.members.at(1).address(), u"ann@example.org"_s);
    QCOMPARE(g.members.at(1).name, u"Lee, Ann"_s);
    QVERIFY(g.extra.isEmpty());
}

void TestMailContactGroups::readsAppleGroup30()
{
    const VCard::ReadResult read = VCard::read(crlf({
        "BEGIN:VCARD", "VERSION:3.0", "PRODID:-//Apple Inc.//AddressBook 6.1//EN", "N:Friends", "FN:Friends",
        "X-ADDRESSBOOKSERVER-KIND:group", "X-ADDRESSBOOKSERVER-MEMBER:urn:uuid:11111111-1111-4111-8111-111111111111",
        "X-ADDRESSBOOKSERVER-MEMBER:urn:uuid:22222222-2222-4222-8222-222222222222", "UID:F00D", "END:VCARD"}));
    QCOMPARE(read.contacts.size(), 1);
    const Contact &g = read.contacts.first();
    QVERIFY(g.isGroup());
    QCOMPARE(g.members.size(), qsizetype(2));
    QCOMPARE(g.members.at(1).uri, u"urn:uuid:22222222-2222-4222-8222-222222222222"_s);
}

void TestMailContactGroups::roundTrips_data()
{
    QTest::addColumn<int>("version");
    QTest::newRow("4.0") << 4;
    QTest::newRow("3.0") << 3;
}

void TestMailContactGroups::roundTrips()
{
    QFETCH(int, version);
    Contact g;
    g.kind = u"group"_s;
    g.displayName = u"Book club"_s;
    g.uid = u"club-1"_s;
    g.members = {ContactMember::forCard(u"11111111-1111-4111-8111-111111111111"_s),
                 ContactMember::forAddress(u"ann@example.org"_s, u"Ann; Lee"_s)};
    normalizeContact(g);
    const QByteArray bytes = VCard::write({g}, version == 4 ? VCard::Version::V40 : VCard::Version::V30);
    QVERIFY(bytes.contains(version == 4 ? "KIND:group" : "X-ADDRESSBOOKSERVER-KIND:group"));
    QVERIFY(bytes.contains(version == 4 ? "MEMBER:urn:uuid:11111111" : "X-ADDRESSBOOKSERVER-MEMBER:urn:uuid:11111111"));
    const VCard::ReadResult read = VCard::read(bytes);
    QCOMPARE(read.contacts.size(), 1);
    Contact back = read.contacts.first();
    normalizeContact(back);
    QCOMPARE(back.kind, g.kind);
    QCOMPARE(back.members, g.members);
    QCOMPARE(back.displayName, g.displayName);
}

void TestMailContactGroups::storeGroupsResolveAndExpand()
{
    QTemporaryDir dir;
    ContactStore store(dir.filePath(u"contacts.json"_s));
    QVERIFY(store.importData(kPeople, u"people.vcf"_s).value(u"ok"_s).toBool());
    const QString grace = store.contactForAddress(u"grace@example.org"_s).value(u"id"_s).toString();
    const QString alan = store.contactForAddress(u"alan@example.org"_s).value(u"id"_s).toString();
    QVariantMap added = store.add({{u"kind"_s, u"group"_s}, {u"displayName"_s, u"Team"_s},
                                   {u"members"_s, QVariantList{QVariantMap{{u"contactId"_s, grace}},
                                                               QVariantMap{{u"address"_s, u"x@example.org"_s}, {u"name"_s, u"Xu"_s}},
                                                               u"Grace H <GRACE@example.org>"_s}}});
    QVERIFY2(added.value(u"ok"_s).toBool(), qPrintable(added.value(u"error"_s).toString()));
    const QString team = added.value(u"id"_s).toString();
    const QVariantMap card = store.contactById(team);
    QVERIFY(card.value(u"isGroup"_s).toBool());
    const QVariantList members = card.value(u"members"_s).toList();
    QCOMPARE(members.size(), 3);
    QCOMPARE(members.at(0).toMap().value(u"kind"_s).toString(), u"contact"_s);
    QCOMPARE(members.at(0).toMap().value(u"contactId"_s).toString(), grace);
    QCOMPARE(members.at(0).toMap().value(u"address"_s).toString(), u"grace@example.org"_s);
    QCOMPARE(members.at(1).toMap().value(u"kind"_s).toString(), u"address"_s);
    QCOMPARE(members.at(1).toMap().value(u"displayName"_s).toString(), u"Xu"_s);
    // Grace twice (by card and by address): one recipient.
    QVariantMap expanded = store.expandGroup(team);
    QCOMPARE(expanded.value(u"recipients"_s).toStringList(),
             (QStringList{u"Grace Hopper <grace@example.org>"_s, u"Xu <x@example.org>"_s}));

    // A group inside a group, a cycle back, and a member not in the book.
    const QString outer = store.add({{u"kind"_s, u"group"_s}, {u"displayName"_s, u"Everyone"_s},
                                     {u"members"_s, QVariantList{QVariantMap{{u"contactId"_s, team}},
                                                                 QVariantMap{{u"contactId"_s, alan}},
                                                                 QVariantMap{{u"uri"_s, u"urn:uuid:404"_s}}}}})
                              .value(u"id"_s).toString();
    QVERIFY(!outer.isEmpty());
    QVariantList cycle = store.contactById(team).value(u"members"_s).toList();
    cycle.append(QVariantMap{{u"contactId"_s, outer}});
    QVERIFY(store.update({{u"id"_s, team}, {u"members"_s, cycle}}).value(u"ok"_s).toBool());
    expanded = store.expandGroup(outer);
    QCOMPARE(expanded.value(u"recipients"_s).toStringList(),
             (QStringList{u"Grace Hopper <grace@example.org>"_s, u"Xu <x@example.org>"_s, u"Alan Turing <alan@example.org>"_s}));
    QCOMPARE(expanded.value(u"missing"_s).toInt(), 1);
    QVERIFY(!store.expandGroup(grace).value(u"ok"_s).toBool());
    QCOMPARE(store.contactForMember(u"urn:uuid:22222222-2222-4222-8222-222222222222"_s).value(u"id"_s).toString(), alan);

    // Autocomplete offers the group by name; the store reloads it.
    const QVariantList offered = store.complete(u"tea"_s);
    QVERIFY(std::any_of(offered.cbegin(), offered.cend(), [&](const QVariant &v) {
        return v.toMap().value(u"group"_s).toBool() && v.toMap().value(u"contactId"_s).toString() == team;
    }));
    ContactStore reopened(dir.filePath(u"contacts.json"_s));
    QCOMPARE(reopened.expandGroup(outer).value(u"count"_s).toInt(), 3);
}

void TestMailContactGroups::exportAndImportKeepMembers()
{
    QTemporaryDir dir;
    ContactStore store(dir.filePath(u"a.json"_s));
    QVERIFY(store.importData(kPeople, u"people.vcf"_s).value(u"ok"_s).toBool());
    const QString alan = store.contactForAddress(u"alan@example.org"_s).value(u"id"_s).toString();
    QVERIFY(store.add({{u"kind"_s, u"group"_s}, {u"displayName"_s, u"Pair"_s},
                       {u"members"_s, QVariantList{QVariantMap{{u"contactId"_s, alan}}, u"zed@example.org"_s}}})
                .value(u"ok"_s).toBool());
    for (const QString &version : {u"4.0"_s, u"3.0"_s}) {
        const QString file = dir.filePath(u"out-"_s + version + u".vcf"_s);
        QVERIFY(store.exportVcf(QUrl::fromLocalFile(file), {}, version).value(u"ok"_s).toBool());
        ContactStore fresh(dir.filePath(u"fresh-"_s + version + u".json"_s));
        QVERIFY(fresh.importFile(QUrl::fromLocalFile(file)).value(u"ok"_s).toBool());
        const QVariantList all = fresh.contacts();
        const auto pair = std::find_if(all.cbegin(), all.cend(), [](const QVariant &v) {
            return v.toMap().value(u"isGroup"_s).toBool();
        });
        QVERIFY(pair != all.cend());
        QCOMPARE(fresh.expandGroup(pair->toMap().value(u"id"_s).toString()).value(u"recipients"_s).toStringList(),
                 (QStringList{u"Alan Turing <alan@example.org>"_s, u"zed@example.org"_s}));
    }
}

void TestMailContactGroups::validation()
{
    Contact g;
    g.kind = u"group"_s;
    g.members = {ContactMember::forAddress(u"a@example.org"_s)};
    normalizeContact(g);
    QVERIFY(!validateContact(g).isEmpty());   // no name
    g.displayName = u"G"_s;
    QVERIFY(validateContact(g).isEmpty());
    g.members.append({u"urn:uuid:bad\nline"_s, {}});
    QVERIFY(!validateContact(g).isEmpty());
    g.members.removeLast();
    g.members.append(ContactMember::forAddress(u"not an address"_s));
    QVERIFY(!validateContact(g).isEmpty());
    // An individual keeps no members; "individual" is the default kind.
    Contact person;
    person.displayName = u"P"_s;
    person.kind = u"Individual"_s;
    person.members = {ContactMember::forAddress(u"a@example.org"_s)};
    normalizeContact(person);
    QVERIFY(person.kind.isEmpty() && person.members.isEmpty());
}

QTEST_GUILESS_MAIN(TestMailContactGroups)

#include "tst_mail_contact_groups.moc"
