// SPDX-License-Identifier: GPL-3.0-or-later
// vCard 2.1 / 3.0 / 4.0 reading and writing for the address book (M64).
// Every fixture is a hand-written string with fictional names and
// example.org addresses.

#include "contact.h"
#include "vcard.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStringEncoder>
#include <QTest>

using namespace QindaOffice::Mail;
using namespace Qt::StringLiterals;

namespace {

QByteArray crlf(std::initializer_list<const char *> lines)
{
    QByteArray out;
    for (const char *line : lines) {
        out += line;
        out += "\r\n";
    }
    return out;
}

QByteArray fakeJpeg(int size)
{
    QByteArray bytes("\xFF\xD8\xFF\xE0\x00\x10JFIF\x00", 11);
    for (int i = 0; bytes.size() < size; ++i) bytes.append(char(i * 7 + 3));
    return bytes;
}

QByteArray fakePng(int size)
{
    QByteArray bytes("\x89PNG\r\n\x1A\n\x00\x00\x00\x0DIHDR", 16);
    for (int i = 0; bytes.size() < size; ++i) bytes.append(char(i * 13 + 1));
    return bytes;
}

// Outlook's 2.1 base64 block: indented lines, ended by an empty line.
QByteArray outlookBase64(const QByteArray &bytes)
{
    const QByteArray b64 = bytes.toBase64();
    QByteArray out;
    for (qsizetype i = 0; i < b64.size(); i += 72) out += "  " + b64.mid(i, 72) + "\r\n";
    return out + "\r\n";
}

// 3.0 folding of an ASCII line (CRLF + one space).
QByteArray folded(const QByteArray &line)
{
    QByteArray out = line.left(75);
    for (qsizetype i = 75; i < line.size(); i += 74) out += "\r\n " + line.mid(i, 74);
    return out + "\r\n";
}

QString dump(const Contact &c)
{
    QJsonObject o = toJson(c);
    o.insert(u"~photo"_s, QString::fromLatin1(c.photo.toBase64()));
    o.insert(u"~photoMimeType"_s, c.photoMimeType);
    return QString::fromUtf8(QJsonDocument(o).toJson());
}

ContactValue value(const QString &type, const QString &v, bool preferred = false)
{
    return {type, v, preferred};
}

Contact fullContact()
{
    Contact c;
    c.uid = u"urn:uuid:7d1c2f3a-0000-4000-8000-0000000000aa"_s;
    c.displayName = u"Dr. Ægir Þórsson, Jr."_s;
    c.prefix = u"Dr."_s;
    c.givenName = u"Ægir"_s;
    c.middleName = u"Jón"_s;
    c.familyName = u"Þórsson"_s;
    c.suffix = u"Jr."_s;
    c.nickname = u"Gir; the \\ Brave, Bold"_s;
    c.emails = {value(u"work"_s, u"aegir@example.org"_s, true), value(u"home"_s, u"gir@example.org"_s),
                value(u"Gaming Club"_s, u"club@example.org"_s)};
    c.phones = {value(u"cell"_s, u"+354 555 0101"_s, true), value(u"work,fax"_s, u"+354 555 0102"_s),
                value(QString(), u"+354 555 0103,,12"_s)};
    c.urls = {value(u"work"_s, u"https://example.org/~aegir?a=1,b=2;c"_s), value(u"homepage"_s, u"https://example.org/blog"_s, true)};
    ContactAddress home{u"home"_s, u"PO 7"_s, u"Flat 2"_s, u"Laugavegur 1\nBack entrance"_s, u"Reykjavík"_s,
                        u"Höfuðborgarsvæðið"_s, u"101"_s, u"Iceland"_s, u"Laugavegur 1\nReykjavík; Iceland, \"Earth\""_s, true};
    ContactAddress summer{u"Summer house"_s, {}, {}, u"Sumarhús 3"_s, u"Þingvellir"_s, {}, u"801"_s, u"Iceland"_s, {}, false};
    c.addresses = {home, summer};
    c.organization = u"Example Hf.; Ltd"_s;
    c.department = u"R&D, Engines"_s;
    c.title = u"Chief, Engines"_s;
    c.birthday = u"1980-02-29"_s;
    c.anniversary = u"--06-17"_s;
    c.notes = u"Multi-line note:\nline 2, with comma; semicolon \\ backslash\n\n李小龍 🙂 end\n"_s;
    c.categories = {u"Friends"_s, u"Work, Iceland"_s, u"Ferðir"_s};
    c.photo = fakePng(3000);
    c.photoMimeType = u"image/png"_s;
    c.photoUrl = u"https://example.org/photos/aegir.jpg"_s;
    c.revision = u"2026-10-08T12:00:00Z"_s;
    c.extra = {{QString(), u"X-SOCIALPROFILE"_s, {{u"TYPE"_s, u"mastodon"_s}}, u"https://example.org/@aegir"_s},
               {u"item7"_s, u"X-ABDATE"_s, {}, u"2001-01-01"_s},
               {u"item7"_s, u"X-ABLabel"_s, {}, u"_$!<Anniversary>!$_"_s},
               {QString(), u"X-CUSTOM"_s, {{u"X-PARAM"_s, u"a,b:c"_s}}, u"one\\;two\\,three\\nfour"_s},
               {QString(), u"GEO"_s, {}, u"64.1466;-21.9426"_s},
               {QString(), u"LOGO"_s, {{u"ENCODING"_s, u"b"_s}, {u"TYPE"_s, u"PNG"_s}}, QString::fromLatin1(fakePng(60).toBase64())}};
    return c;
}

} // namespace

class TestMailContacts : public QObject {
    Q_OBJECT

private slots:
    void reads21Outlook()
    {
        const QByteArray jpeg = fakeJpeg(400);
        QByteArray data = crlf({
            "BEGIN:VCARD", "VERSION:2.1", "N;LANGUAGE=en-us:Lovelace;Ada;Augusta;Countess", "FN:Ada Lovelace",
            "ORG:Analytical Engines Ltd;Research", "TITLE:Lead Programmer",
            "NOTE;ENCODING=QUOTED-PRINTABLE;CHARSET=windows-1252:Caf=E9 meeting at 10=0D=0A=",
            "Bring the =93notes=94.",
            "TEL;WORK;VOICE:+1 555 0100", "TEL;HOME;VOICE:+1 555 0101", "TEL;CELL;VOICE:+1 555 0102",
            "TEL;WORK;FAX:+1 555 0103", "ADR;WORK;PREF:;;1 Engine Way;London;;EC1A 1AA;United Kingdom",
            "LABEL;WORK;PREF;ENCODING=QUOTED-PRINTABLE:1 Engine Way=0D=0ALondon EC1A 1AA=0D=0AUnited Kingdom",
            "URL;WORK:https://example.org/ada", "EMAIL;PREF;INTERNET:ada@example.org",
            "EMAIL;INTERNET:ada.home@example.org", "X-MS-OL-DEFAULT-POSTAL-ADDRESS:2", "BDAY:19751210",
            "X-MS-ANNIVERSARY:20010915", "PHOTO;TYPE=JPEG;ENCODING=BASE64:"});
        data += outlookBase64(jpeg);
        data += crlf({"REV:20261008T120000Z", "END:VCARD"});
        const VCard::ReadResult read = VCard::read(data);
        QVERIFY2(read.warnings.isEmpty(), qPrintable(read.warnings.join(u'\n')));
        QCOMPARE(read.skipped, 0);
        QCOMPARE(read.contacts.size(), 1);
        const Contact &c = read.contacts.first();
        QCOMPARE(c.displayName, u"Ada Lovelace"_s);
        QCOMPARE(QStringList({c.familyName, c.givenName, c.middleName, c.prefix, c.suffix}),
                 QStringList({u"Lovelace"_s, u"Ada"_s, u"Augusta"_s, u"Countess"_s, QString()}));
        QCOMPARE(c.organization, u"Analytical Engines Ltd"_s);
        QCOMPARE(c.department, u"Research"_s);
        QCOMPARE(c.notes, u"Café meeting at 10\nBring the “notes”."_s);
        QCOMPARE(c.phones, QList<ContactValue>({value(u"work"_s, u"+1 555 0100"_s), value(u"home"_s, u"+1 555 0101"_s),
                                                value(u"cell"_s, u"+1 555 0102"_s), value(u"work,fax"_s, u"+1 555 0103"_s)}));
        QCOMPARE(c.emails, QList<ContactValue>({value({}, u"ada@example.org"_s, true), value({}, u"ada.home@example.org"_s)}));
        QCOMPARE(c.urls, QList<ContactValue>({value(u"work"_s, u"https://example.org/ada"_s)}));
        QCOMPARE(c.addresses.size(), 1);
        const ContactAddress &a = c.addresses.first();
        QCOMPARE(a.type, u"work"_s);
        QVERIFY(a.preferred);
        QCOMPARE(a.street, u"1 Engine Way"_s);
        QCOMPARE(a.postalCode, u"EC1A 1AA"_s);
        QCOMPARE(a.label, u"1 Engine Way\nLondon EC1A 1AA\nUnited Kingdom"_s);
        QCOMPARE(c.birthday, u"1975-12-10"_s);
        QCOMPARE(c.anniversary, u"2001-09-15"_s);
        QCOMPARE(c.revision, u"20261008T120000Z"_s);
        QCOMPARE(c.photo, jpeg);
        QCOMPARE(c.photoMimeType, u"image/jpeg"_s);
        QCOMPARE(c.extra.size(), 1);
        QCOMPARE(c.extra.first().name, u"X-MS-OL-DEFAULT-POSTAL-ADDRESS"_s);
        QCOMPARE(c.extra.first().value, u"2"_s);
    }

    void reads30()
    {
        const QByteArray png = fakePng(500);
        QByteArray data = crlf({
            "BEGIN:VCARD", "VERSION:3.0", "PRODID:-//Example//Contacts 1.0//EN",
            "N:Hopper;Grace;Brewster Murray;Rear Admiral;", "FN:Grace Hopper",
            "NICKNAME:Amazing Grace,Grandma COBOL", "ORG:US Navy;Computing\\, Programming;Compilers",
            "EMAIL;type=INTERNET;type=WORK;type=pref:grace@example.org",
            "item1.EMAIL;type=INTERNET:grace.school@example.org", "item1.X-ABLabel:School",
            "item2.TEL:+1 555 0199", "item2.X-ABLabel:_$!<Mobile>!$_", "TEL;TYPE=work,voice:+1 555 0198",
            "TEL;TYPE=\"home,fax\":+1 555 0197", "ADR;TYPE=HOME:;Apt 4;12 Bug Street;Arlington;VA;22201;USA",
            "LABEL;TYPE=HOME:12 Bug Street\\nArlington\\, VA 22201",
            "NOTE:Line one\\nLine two\\, with comma\\; and semicolon \\\\ backslash and a long line that i",
            "\ts folded with a tab and continues here", "  and then with a space.",
            "CATEGORIES:Navy,Computing\\,History", "BDAY;value=date:1906-12-09"});
        data += folded("PHOTO;ENCODING=b;TYPE=PNG:" + png.toBase64());
        data += crlf({"X-SOCIALPROFILE;type=twitter:https://example.org/grace",
                      "IMPP;X-SERVICE-TYPE=Jabber;type=HOME:xmpp:grace@example.org",
                      "UID:5f0c3b8e-0000-4000-8000-000000000001", "END:VCARD"});
        const VCard::ReadResult read = VCard::read(data);
        QVERIFY2(read.warnings.isEmpty(), qPrintable(read.warnings.join(u'\n')));
        QCOMPARE(read.contacts.size(), 1);
        const Contact &c = read.contacts.first();
        QCOMPARE(c.middleName, u"Brewster Murray"_s);
        QCOMPARE(c.prefix, u"Rear Admiral"_s);
        QCOMPARE(c.nickname, u"Amazing Grace, Grandma COBOL"_s);
        QCOMPARE(c.organization, u"US Navy"_s);
        QCOMPARE(c.department, u"Computing, Programming, Compilers"_s);
        QCOMPARE(c.emails, QList<ContactValue>({value(u"work"_s, u"grace@example.org"_s, true),
                                                value(u"School"_s, u"grace.school@example.org"_s)}));
        QCOMPARE(c.phones, QList<ContactValue>({value(u"cell"_s, u"+1 555 0199"_s), value(u"work"_s, u"+1 555 0198"_s),
                                                value(u"home,fax"_s, u"+1 555 0197"_s)}));
        QCOMPARE(c.addresses.size(), 1);
        QCOMPARE(c.addresses.first().extended, u"Apt 4"_s);
        QCOMPARE(c.addresses.first().region, u"VA"_s);
        QCOMPARE(c.addresses.first().label, u"12 Bug Street\nArlington, VA 22201"_s);
        QCOMPARE(c.notes, u"Line one\nLine two, with comma; and semicolon \\ backslash and a long line that "
                          "is folded with a tab and continues here and then with a space."_s);
        QCOMPARE(c.categories, QStringList({u"Navy"_s, u"Computing,History"_s}));
        QCOMPARE(c.birthday, u"1906-12-09"_s);
        QCOMPARE(c.photo, png);
        QCOMPARE(c.photoMimeType, u"image/png"_s);
        QCOMPARE(c.uid, u"5f0c3b8e-0000-4000-8000-000000000001"_s);
        QCOMPARE(c.extra.size(), 2); // both Apple labels were consumed, PRODID dropped
        QCOMPARE(c.extra.at(0).params, (QList<QPair<QString, QString>>{{u"type"_s, u"twitter"_s}}));
        QCOMPARE(c.extra.at(1).name, u"IMPP"_s);
        QCOMPARE(c.extra.at(1).value, u"xmpp:grace@example.org"_s);
        QCOMPARE(c.extra.at(1).params.size(), 2);
    }

    void reads40()
    {
        const QByteArray jpeg = fakeJpeg(300);
        const QByteArray data = crlf({
            "BEGIN:VCARD", "VERSION:4.0", "FN:Katherine Johnson", "N:Johnson;Katherine;;;",
            "EMAIL;TYPE=work;PREF=1:katherine@example.org", "EMAIL;TYPE=home;PREF=2:kj@example.org",
            "TEL;VALUE=uri;TYPE=\"voice,cell\":tel:+1-555-0150",
            "ADR;TYPE=work;LABEL=\"NASA Langley^nHampton, VA\":;;1 Langley Blvd;Hampton;VA;23681;USA",
            "BDAY:--0826", "ANNIVERSARY:19391125", "PHOTO;MEDIATYPE=image/png:https://example.org/photos/kj.png",
            "KIND:individual", "GENDER:F", "CLIENTPIDMAP:1;urn:uuid:3df403f4-5924-4bb7-b077-3c711d9eb34b",
            "UID:urn:uuid:4fbe8971-0bc3-424c-9c26-36c3e1eff6b1"})
            + "PHOTO:data:image/jpeg;base64," + jpeg.toBase64() + "\r\nEND:VCARD\r\n";
        const VCard::ReadResult read = VCard::read(data);
        QVERIFY2(read.warnings.isEmpty(), qPrintable(read.warnings.join(u'\n')));
        QCOMPARE(read.contacts.size(), 1);
        const Contact &c = read.contacts.first();
        QCOMPARE(c.emails, QList<ContactValue>({value(u"work"_s, u"katherine@example.org"_s, true),
                                                value(u"home"_s, u"kj@example.org"_s)}));
        QCOMPARE(c.phones, QList<ContactValue>({value(u"cell"_s, u"+1-555-0150"_s)}));
        QCOMPARE(c.addresses.first().label, u"NASA Langley\nHampton, VA"_s);
        QCOMPARE(c.birthday, u"--08-26"_s);
        QCOMPARE(c.anniversary, u"1939-11-25"_s);
        QCOMPARE(c.photoUrl, u"https://example.org/photos/kj.png"_s); // kept as text, never fetched
        QCOMPARE(c.photo, jpeg);
        QCOMPARE(c.photoMimeType, u"image/jpeg"_s);
        QCOMPARE(c.uid, u"urn:uuid:4fbe8971-0bc3-424c-9c26-36c3e1eff6b1"_s);
        QCOMPARE(c.extra.size(), 3);
        QCOMPARE(c.extra.at(2).value, u"1;urn:uuid:3df403f4-5924-4bb7-b077-3c711d9eb34b"_s);
    }

    void manyCardsWithAMalformedOne()
    {
        const QByteArray data = crlf({
            "BEGIN:VCARD", "VERSION:3.0", "FN:First Person", "EMAIL:first@example.org", "END:VCARD",
            "BEGIN:VCARD", "VERSION:3.0", "this line has no colon", "NOTE:only a note", "END:VCARD",
            "BEGIN:VCARD", "FN:No Version", "END:VCARD",
            "BEGIN:VCARD", "VERSION:3.0", "FN:Third Person",
            "BEGIN:VCARD", "VERSION:3.0", "FN:Last Person", "TEL:+1 555 0100"});
        const VCard::ReadResult read = VCard::read(data);
        QCOMPARE(read.skipped, 1);
        QStringList names;
        for (const Contact &c : read.contacts) names.append(c.displayName);
        QCOMPARE(names, QStringList({u"First Person"_s, u"No Version"_s, u"Third Person"_s, u"Last Person"_s}));
        const QString warnings = read.warnings.join(u'\n');
        QVERIFY2(warnings.contains(u"not a vCard property"_s), qPrintable(warnings));
        QVERIFY(warnings.contains(u"Card 2 (line 6) has no name"_s));
        QVERIFY(warnings.contains(u"Card 3 (line 11) has no VERSION"_s));
        QVERIFY(warnings.contains(u"Card 4 (line 14) has no END:VCARD"_s));
        QVERIFY(warnings.contains(u"Card 5 (line 17) has no END:VCARD"_s));
    }

    void embeddedAgentCardIsDropped()
    {
        const QByteArray data = crlf({
            "BEGIN:VCARD", "VERSION:2.1", "FN:Boss Person", "AGENT:", "BEGIN:VCARD", "VERSION:2.1",
            "FN:Assistant Person", "END:VCARD", "EMAIL:boss@example.org", "END:VCARD",
            "BEGIN:VCARD", "VERSION:2.1", "FN:Next Person", "END:VCARD"});
        const VCard::ReadResult read = VCard::read(data);
        QCOMPARE(read.contacts.size(), 2);
        QCOMPARE(read.contacts.at(0).displayName, u"Boss Person"_s);
        QCOMPARE(read.contacts.at(0).primaryEmail(), u"boss@example.org"_s);
        QCOMPARE(read.contacts.at(0).extra.size(), 1); // the (empty) AGENT itself
        QCOMPARE(read.contacts.at(1).displayName, u"Next Person"_s);
        QVERIFY(read.warnings.join(u'\n').contains(u"embedded vCard (AGENT) was dropped"_s));
    }

    void unknownPropertiesSurviveWriteAndRead()
    {
        const QByteArray data = crlf({
            "BEGIN:VCARD", "VERSION:2.1", "FN:Extra Person",
            "X-FOO;ENCODING=QUOTED-PRINTABLE;CHARSET=UTF-8:a=0D=0Ab=C3=A9",
            "item3.X-BAR;TYPE=HOME;X-LIST=\"x,y\":p\\;q", "END:VCARD"});
        const VCard::ReadResult first = VCard::read(data);
        QCOMPARE(first.contacts.size(), 1);
        const QList<VCardProperty> extra = first.contacts.first().extra;
        QCOMPARE(extra.size(), 2);
        QCOMPARE(extra.at(0).value, u"a\\nbé"_s);
        QVERIFY(extra.at(0).params.isEmpty()); // QUOTED-PRINTABLE/CHARSET described bytes that are decoded now
        QCOMPARE(extra.at(1).group, u"item3"_s);
        QCOMPARE(extra.at(1).params, (QList<QPair<QString, QString>>{{u"TYPE"_s, u"HOME"_s}, {u"X-LIST"_s, u"x,y"_s}}));
        for (VCard::Version version : {VCard::Version::V30, VCard::Version::V40}) {
            const QByteArray written = VCard::write(first.contacts, version);
            QVERIFY(!written.contains("QUOTED-PRINTABLE"));
            QVERIFY(!written.contains("CHARSET"));
            QVERIFY(written.contains("item3.X-BAR;TYPE=HOME;X-LIST=\"x,y\":p\\;q\r\n"));
            const VCard::ReadResult again = VCard::read(written);
            QCOMPARE(again.contacts.size(), 1);
            QCOMPARE(again.contacts.first().extra, extra);
        }
    }

    void roundTrip_data()
    {
        QTest::addColumn<int>("version");
        QTest::newRow("3.0") << int(VCard::Version::V30);
        QTest::newRow("4.0") << int(VCard::Version::V40);
    }

    void roundTrip()
    {
        QFETCH(int, version);
        Contact plain;
        plain.emails = {value({}, u"only@example.org"_s)};
        const QList<Contact> contacts = {fullContact(), plain};
        const QByteArray written = VCard::write(contacts, VCard::Version(version));
        const VCard::ReadResult read = VCard::read(written);
        QVERIFY2(read.warnings.isEmpty(), qPrintable(read.warnings.join(u'\n')));
        QCOMPARE(read.contacts.size(), 2);
        QCOMPARE(dump(read.contacts.at(0)), dump(contacts.at(0)));
        QVERIFY(read.contacts.at(0) == contacts.at(0));
        // FN is required: a contact without one gains its effective name.
        QCOMPARE(read.contacts.at(1).displayName, u"only@example.org"_s);
        QCOMPARE(read.contacts.at(1).emails, plain.emails);
    }

    void writesTheExpectedLines()
    {
        Contact c = fullContact();
        c.extra.clear();
        const QByteArray v3 = VCard::write({c}, VCard::Version::V30);
        const QByteArray v4 = VCard::write({c}, VCard::Version::V40);
        const QByteArray unfolded3 = QByteArray(v3).replace("\r\n ", "");
        const QByteArray unfolded4 = QByteArray(v4).replace("\r\n ", "");
        QVERIFY(v3.startsWith("BEGIN:VCARD\r\nVERSION:3.0\r\nPRODID:-//QindaQt//QindaMail//EN\r\n"));
        QVERIFY(v4.startsWith("BEGIN:VCARD\r\nVERSION:4.0\r\nPRODID:-//QindaQt//QindaMail//EN\r\n"));
        QVERIFY(unfolded3.contains("\r\nEMAIL;TYPE=internet,work,pref:aegir@example.org\r\n"));
        QVERIFY(unfolded4.contains("\r\nEMAIL;TYPE=work;PREF=1:aegir@example.org\r\n"));
        QVERIFY(unfolded3.contains("\r\nitem1.EMAIL;TYPE=internet:club@example.org\r\nitem1.X-ABLabel:Gaming Club\r\n"));
        QVERIFY(unfolded3.contains("\r\nPHOTO;ENCODING=b;TYPE=PNG:iVBORw0K"));
        QVERIFY(unfolded4.contains("\r\nPHOTO:data:image/png;base64,iVBORw0K"));
        QVERIFY(unfolded3.contains("\r\nPHOTO;VALUE=uri:https://example.org/photos/aegir.jpg\r\n"));
        QVERIFY(unfolded3.contains("\r\nBDAY:1980-02-29\r\nX-ANNIVERSARY:--06-17\r\n"));
        QVERIFY(unfolded4.contains("\r\nBDAY:19800229\r\nANNIVERSARY:--0617\r\n"));
        QVERIFY(unfolded3.contains("\r\nFN:Dr. Ægir Þórsson\\, Jr.\r\n"));
        QVERIFY(unfolded3.contains("\r\nORG:Example Hf.\\; Ltd;R&D\\, Engines\r\n"));
        QVERIFY(unfolded3.contains("\r\nLABEL;TYPE=home:Laugavegur 1\\nReykjavík\\; Iceland\\, \"Earth\"\r\n"));
        QVERIFY(unfolded4.contains("LABEL=\"Laugavegur 1^nReykjavík; Iceland, ^'Earth^'\""));
        Contact bare;
        bare.emails = {value({}, u"bare@example.org"_s)};
        const QByteArray minimal = VCard::write({bare}, VCard::Version::V30);
        QVERIFY(minimal.contains("\r\nFN:bare@example.org\r\nN:;;;;\r\n"));
        QVERIFY(!VCard::write({bare}, VCard::Version::V40).contains("\r\nN:"));
    }

    void foldsAt75OctetsNeverInsideAUtf8Sequence()
    {
        Contact c;
        c.displayName = u"Folding Test"_s;
        for (int i = 0; i < 40; ++i) c.notes += u"ÆÞ李🙂x"_s;
        c.photo = fakeJpeg(5000);
        const QByteArray written = VCard::write({c}, VCard::Version::V30);
        const QList<QByteArray> lines = written.split('\n');
        int continuations = 0;
        for (QByteArray line : lines) {
            if (line.endsWith('\r')) line.chop(1);
            QVERIFY2(line.size() <= 75, line.constData());
            QVERIFY2(line.isValidUtf8(), line.toHex().constData());
            if (line.startsWith(' ')) ++continuations;
        }
        QVERIFY(continuations > 20);
        const VCard::ReadResult read = VCard::read(written);
        QCOMPARE(read.contacts.size(), 1);
        QCOMPARE(read.contacts.first().notes, c.notes);
        QCOMPARE(read.contacts.first().photo, c.photo);
    }

    void hostileInput()
    {
        // A line over 2 MiB is dropped; the card survives.
        QByteArray huge = crlf({"BEGIN:VCARD", "VERSION:3.0", "FN:Huge Line"});
        huge += "NOTE:" + QByteArray(3 * 1024 * 1024, 'a') + "\r\nEND:VCARD\r\n";
        VCard::ReadResult read = VCard::read(huge);
        QCOMPARE(read.contacts.size(), 1);
        QVERIFY(read.contacts.first().notes.isEmpty());
        QVERIFY(read.warnings.join(u'\n').contains(u"longer than 2 MiB"_s));

        // Binary garbage: no crash, nothing read.
        QByteArray garbage(1024 * 1024, Qt::Uninitialized);
        QRandomGenerator generator(64);
        for (char &ch : garbage) ch = char(generator.bounded(256));
        read = VCard::read(garbage);
        QVERIFY(read.contacts.isEmpty());
        read = VCard::read(QByteArray("BEGIN:VCARD\r\n") + garbage);
        QVERIFY(read.contacts.isEmpty());

        // Warnings are bounded.
        QByteArray empties;
        for (int i = 0; i < 500; ++i) empties += "BEGIN:VCARD\r\nEND:VCARD\r\n";
        read = VCard::read(empties);
        QCOMPARE(read.skipped, 500);
        QCOMPARE(read.warnings.size(), ContactLimits::kMaxWarnings + 1);

        // A photo over 1 MiB is dropped, the contact kept; 64 values per kind.
        QByteArray big = crlf({"BEGIN:VCARD", "VERSION:3.0", "FN:Big Photo"});
        big += folded("PHOTO;ENCODING=b;TYPE=JPEG:" + fakeJpeg(ContactLimits::kMaxPhotoBytes + 10).toBase64());
        for (int i = 0; i < 70; ++i) big += "EMAIL:p" + QByteArray::number(i) + "@example.org\r\n";
        big += "END:VCARD\r\n";
        read = VCard::read(big);
        QCOMPARE(read.contacts.size(), 1);
        QVERIFY(read.contacts.first().photo.isEmpty());
        QCOMPARE(read.contacts.first().emails.size(), ContactLimits::kMaxMultiValues);
        QVERIFY(read.warnings.join(u'\n').contains(u"larger than 1 MiB"_s));

        // 20 000 cards per file.
        QByteArray many;
        for (int i = 0; i <= ContactLimits::kMaxContacts; ++i) many += "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:N\r\nEND:VCARD\r\n";
        read = VCard::read(many);
        QCOMPARE(read.contacts.size(), ContactLimits::kMaxContacts);
        QCOMPARE(read.skipped, 1);
    }

    void honoursUtf16Boms()
    {
        const QString text = u"BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Zoë Ådahl\r\nEMAIL:zoe@example.org\r\nEND:VCARD\r\n"_s;
        for (QStringConverter::Encoding encoding : {QStringConverter::Utf16LE, QStringConverter::Utf16BE}) {
            QStringEncoder encoder(encoding, QStringEncoder::Flag::WriteBom);
            const QByteArray bytes = encoder.encode(text);
            const VCard::ReadResult read = VCard::read(bytes);
            QCOMPARE(read.contacts.size(), 1);
            QCOMPARE(read.contacts.first().displayName, u"Zoë Ådahl"_s);
        }
        const VCard::ReadResult bom = VCard::read("\xEF\xBB\xBF" + text.toUtf8());
        QCOMPARE(bom.contacts.size(), 1);
        QCOMPARE(bom.contacts.first().displayName, u"Zoë Ådahl"_s);
    }
};

QTEST_GUILESS_MAIN(TestMailContacts)
#include "tst_mail_contacts.moc"
