// SPDX-License-Identifier: GPL-3.0-or-later
// Outlook / Outlook.com / Google CSV contact import (M64). Every fixture is
// a hand-written string with fictional names and example.org addresses.

#include "contact.h"
#include "contact_csv.h"
#include "vcard.h"

#include <QHash>
#include <QTest>

using namespace QindaOffice::Mail;
using namespace Qt::StringLiterals;

namespace {

ContactValue value(const QString &type, const QString &v, bool preferred = false)
{
    return {type, v, preferred};
}

// One RFC 4180 row: fields quoted when they need it.
QByteArray csvLine(const QStringList &fields)
{
    QStringList out;
    for (const QString &field : fields) {
        const bool quote = field.contains(u',') || field.contains(u'"') || field.contains(u'\n');
        out.append(quote ? u"\"%1\""_s.arg(QString(field).replace(u"\""_s, u"\"\""_s)) : field);
    }
    return out.join(u',').toUtf8() + "\r\n";
}

QByteArray table(const QStringList &header, const QList<QHash<QString, QString>> &rows)
{
    QByteArray out = csvLine(header);
    for (const QHash<QString, QString> &row : rows) {
        QStringList fields;
        for (const QString &column : header) fields.append(row.value(column));
        out += csvLine(fields);
    }
    return out;
}

const QString kOutlookCom = u"First Name,Middle Name,Last Name,Title,Suffix,Nickname,Given Yomi,Surname Yomi,"
    "E-mail Address,E-mail 2 Address,E-mail 3 Address,Home Phone,Home Phone 2,Business Phone,Business Phone 2,"
    "Mobile Phone,Car Phone,Other Phone,Primary Phone,Pager,Business Fax,Home Fax,Other Fax,Company Main Phone,"
    "Callback,Radio Phone,Telex,TTY/TDD Phone,IMAddress,Job Title,Department,Company,Office Location,"
    "Manager's Name,Assistant's Name,Assistant's Phone,Company Yomi,Business Street,Business City,Business State,"
    "Business Postal Code,Business Country/Region,Home Street,Home City,Home State,Home Postal Code,"
    "Home Country/Region,Other Street,Other City,Other State,Other Postal Code,Other Country/Region,"
    "Personal Web Page,Spouse,Schools,Hobby,Location,Web Page,Birthday,Anniversary,Notes"_s;

} // namespace

class TestMailContactCsv : public QObject {
    Q_OBJECT

private slots:
    void outlookDesktop()
    {
        const QByteArray csv =
            "\"Title\",\"First Name\",\"Middle Name\",\"Last Name\",\"Suffix\",\"Company\",\"Department\",\"Job Title\","
            "\"Business Street\",\"Business Street 2\",\"Business City\",\"Business Postal Code\",\"Business Country/Region\","
            "\"Business Fax\",\"Business Phone\",\"Mobile Phone\",\"Primary Phone\",\"Anniversary\",\"Birthday\",\"Categories\","
            "\"E-mail Address\",\"E-mail Type\",\"E-mail Display Name\",\"E-mail 2 Address\",\"E-mail 2 Type\","
            "\"E-mail 2 Display Name\",\"Notes\",\"Spouse\",\"Web Page\",\"Gender\",\"Priority\",\"Private\",\"User 1\"\r\n"
            "\"Ms.\",\"Ada\",\"\",\"Lovelace\",\"\",\"Analytical Engines, Ltd\",\"Research\",\"Programmer\","
            "\"1 Engine Way\",\"Floor 2\",\"London\",\"EC1A 1AA\",\"United Kingdom\","
            "\"+44 20 5550 0103\",\"+44 20 5550 0100\",\"+44 7700 900100\",\"+44 7700 900100\",\"0/0/00\",\"12/10/1815\","
            "\"Friends;Work\",\"ada@example.org\",\"SMTP\",\"Ada Lovelace (ada@example.org)\","
            "\"/o=ExchangeLabs/ou=Exchange Administrative Group/cn=Recipients/cn=ada2\",\"EX\",\"Ada at Work\","
            "\"Line one\r\nLine two with \"\"quotes\"\", and commas\",\"William\",\"https://example.org/ada\","
            "\"Female\",\"Normal\",\"False\",\"\"\r\n"
            "\"\",\"Grace\",\"\",\"Hopper\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\","
            "\"0/0/00\",\"0/0/00\",\"\","
            "\"/o=ExchangeLabs/ou=Exchange Administrative Group/cn=Recipients/cn=grace\",\"EX\","
            "\"Grace Hopper (grace@example.org)\",\"\",\"\",\"\",\"\",\"\",\"\",\"Unspecified\",\"Normal\",\"False\",\"\"\r\n"
            ",,,,,,,,,,,,,,+1 555 0100,,,,,,,,,,,,,,,,,,\r\n"
            ",,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,\r\n"
            "\r\n";
        const ContactCsv::ReadResult read = ContactCsv::read(csv);
        QCOMPARE(read.contacts.size(), 2);
        QCOMPARE(read.skipped, 2); // the phone-only row and the row of empty fields; not the blank line
        const Contact &ada = read.contacts.at(0);
        QCOMPARE(ada.prefix, u"Ms."_s); // Outlook's "Title" is the name prefix
        QCOMPARE(ada.givenName, u"Ada"_s);
        QCOMPARE(ada.familyName, u"Lovelace"_s);
        QCOMPARE(ada.organization, u"Analytical Engines, Ltd"_s);
        QCOMPARE(ada.department, u"Research"_s);
        QCOMPARE(ada.title, u"Programmer"_s);
        QCOMPARE(ada.effectiveDisplayName(), u"Ada Lovelace"_s);
        QCOMPARE(ada.addresses.size(), 1);
        QCOMPARE(ada.addresses.first().type, u"work"_s);
        QCOMPARE(ada.addresses.first().street, u"1 Engine Way\nFloor 2"_s);
        QCOMPARE(ada.addresses.first().country, u"United Kingdom"_s);
        QCOMPARE(ada.phones, QList<ContactValue>({value(u"cell"_s, u"+44 7700 900100"_s, true),
                                                  value(u"work"_s, u"+44 20 5550 0100"_s),
                                                  value(u"work,fax"_s, u"+44 20 5550 0103"_s)}));
        QCOMPARE(ada.birthday, u"1815-12-10"_s);
        QVERIFY(ada.anniversary.isEmpty());
        QCOMPARE(ada.categories, QStringList({u"Friends"_s, u"Work"_s}));
        QCOMPARE(ada.emails, QList<ContactValue>({value({}, u"ada@example.org"_s)}));
        QCOMPARE(ada.notes, u"Line one\nLine two with \"quotes\", and commas"_s);
        QCOMPARE(ada.urls, QList<ContactValue>({value({}, u"https://example.org/ada"_s)}));
        QCOMPARE(ada.extra.size(), 2);
        QCOMPARE(ada.extra.at(0).name, u"X-SPOUSE"_s);
        QCOMPARE(ada.extra.at(0).value, u"William"_s);
        QCOMPARE(ada.extra.at(1).name, u"X-GENDER"_s);
        const Contact &grace = read.contacts.at(1);
        QCOMPARE(grace.emails, QList<ContactValue>({value({}, u"grace@example.org"_s)}));
        QVERIFY(grace.birthday.isEmpty());
        QVERIFY(grace.extra.isEmpty()); // Outlook's defaults are not kept
        const QString warnings = read.warnings.join(u'\n');
        QVERIFY2(warnings.contains(u"1 Exchange-only address"_s), qPrintable(warnings));
        QVERIFY(warnings.contains(u"Row 4 has no name"_s));
        QVERIFY(warnings.contains(u"Row 5 has no name"_s));
        // CSV extras survive the vCard round trip.
        const VCard::ReadResult again = VCard::read(VCard::write(read.contacts));
        QCOMPARE(again.contacts.at(0).extra, ada.extra);
    }

    void encodings()
    {
        const QByteArray ansi = "First Name,Last Name,E-mail Address,Notes\r\n"
                                "Jos\xE9,M\xFC" "ller,jose@example.org,\x93Quoted\x94 \x80 50\r\n";
        ContactCsv::ReadResult read = ContactCsv::read(ansi);
        QCOMPARE(read.contacts.size(), 1);
        QCOMPARE(read.contacts.first().givenName, u"José"_s);
        QCOMPARE(read.contacts.first().familyName, u"Müller"_s);
        QCOMPARE(read.contacts.first().notes, u"“Quoted” € 50"_s);
        read = ContactCsv::read("\xEF\xBB\xBF" "First Name,Last Name\r\nZo\xC3\xAB,\xC3\x85" "dahl\r\n");
        QCOMPARE(read.contacts.size(), 1);
        QCOMPARE(read.contacts.first().givenName, u"Zoë"_s);
        QCOMPARE(read.contacts.first().familyName, u"Ådahl"_s);
        // European Outlook: the list separator is ';'.
        read = ContactCsv::read("First Name;Last Name;E-mail Address;Categories\nAda;Lovelace;ada@example.org;\"Friends;Work\"\n");
        QCOMPARE(read.contacts.size(), 1);
        QCOMPARE(read.contacts.first().categories, QStringList({u"Friends"_s, u"Work"_s}));
    }

    void outlookCom()
    {
        const QStringList header = kOutlookCom.split(u',');
        const QByteArray csv = table(header, {{
            {u"First Name"_s, u"Grace"_s}, {u"Last Name"_s, u"Hopper"_s}, {u"Nickname"_s, u"Amazing Grace"_s},
            {u"E-mail Address"_s, u"grace@example.org"_s}, {u"E-mail 2 Address"_s, u"g.hopper@example.org"_s},
            {u"Mobile Phone"_s, u"+1 555 0199"_s}, {u"Job Title"_s, u"Rear Admiral"_s}, {u"Company"_s, u"US Navy"_s},
            {u"Manager's Name"_s, u"Ada L."_s}, {u"Business City"_s, u"Arlington"_s}, {u"Business State"_s, u"VA"_s},
            {u"Business Postal Code"_s, u"22201"_s}, {u"Business Country/Region"_s, u"USA"_s},
            {u"Web Page"_s, u"https://example.org/grace"_s}, {u"Birthday"_s, u"12/9/1906"_s},
            {u"IMAddress"_s, u"grace@chat.example.org"_s}, {u"Notes"_s, u"multi\nline"_s},
        }});
        const ContactCsv::ReadResult read = ContactCsv::read(csv);
        QCOMPARE(read.contacts.size(), 1);
        const Contact &c = read.contacts.first();
        QCOMPARE(c.nickname, u"Amazing Grace"_s);
        QCOMPARE(c.emails, QList<ContactValue>({value({}, u"grace@example.org"_s), value({}, u"g.hopper@example.org"_s)}));
        QCOMPARE(c.phones, QList<ContactValue>({value(u"cell"_s, u"+1 555 0199"_s)}));
        QCOMPARE(c.title, u"Rear Admiral"_s);
        QCOMPARE(c.organization, u"US Navy"_s);
        QCOMPARE(c.addresses.size(), 1);
        QCOMPARE(c.addresses.first().locality, u"Arlington"_s);
        QCOMPARE(c.addresses.first().postalCode, u"22201"_s);
        QCOMPARE(c.birthday, u"1906-12-09"_s);
        QCOMPARE(c.notes, u"multi\nline"_s);
        QCOMPARE(c.extra.size(), 2);
        QCOMPARE(c.extra.at(0).name, u"X-IMADDRESS"_s);
        QCOMPARE(c.extra.at(1).name, u"X-MANAGER"_s);
    }

    void google()
    {
        const QStringList header = QStringList{
            u"Name"_s, u"Given Name"_s, u"Additional Name"_s, u"Family Name"_s, u"Yomi Name"_s, u"Birthday"_s,
            u"Notes"_s, u"Group Membership"_s, u"E-mail 1 - Type"_s, u"E-mail 1 - Value"_s, u"E-mail 2 - Type"_s,
            u"E-mail 2 - Value"_s, u"Phone 1 - Type"_s, u"Phone 1 - Value"_s, u"Address 1 - Type"_s,
            u"Address 1 - Formatted"_s, u"Address 1 - Street"_s, u"Address 1 - City"_s, u"Address 1 - PO Box"_s,
            u"Address 1 - Region"_s, u"Address 1 - Postal Code"_s, u"Address 1 - Country"_s,
            u"Address 1 - Extended Address"_s, u"Organization 1 - Type"_s, u"Organization 1 - Name"_s,
            u"Organization 1 - Title"_s, u"Website 1 - Type"_s, u"Website 1 - Value"_s, u"Event 1 - Type"_s,
            u"Event 1 - Value"_s};
        const QByteArray csv = table(header, {{
            {u"Name"_s, u"Katherine Johnson"_s}, {u"Given Name"_s, u"Katherine"_s}, {u"Family Name"_s, u"Johnson"_s},
            {u"Birthday"_s, u"1918-08-26"_s}, {u"Group Membership"_s, u"* myContacts ::: NASA"_s},
            {u"E-mail 1 - Type"_s, u"* Work"_s}, {u"E-mail 1 - Value"_s, u"katherine@example.org ::: kj.work@example.org"_s},
            {u"E-mail 2 - Type"_s, u"Home"_s}, {u"E-mail 2 - Value"_s, u"kj@example.org"_s},
            {u"Phone 1 - Type"_s, u"Mobile"_s}, {u"Phone 1 - Value"_s, u"+1 555 0150"_s},
            {u"Address 1 - Type"_s, u"Work"_s}, {u"Address 1 - Formatted"_s, u"1 Langley Blvd\nHampton, VA"_s},
            {u"Address 1 - Street"_s, u"1 Langley Blvd"_s}, {u"Address 1 - City"_s, u"Hampton"_s},
            {u"Address 1 - Region"_s, u"VA"_s}, {u"Address 1 - Postal Code"_s, u"23681"_s},
            {u"Address 1 - Country"_s, u"USA"_s}, {u"Organization 1 - Type"_s, u"Work"_s},
            {u"Organization 1 - Name"_s, u"NASA"_s}, {u"Organization 1 - Title"_s, u"Mathematician"_s},
            {u"Website 1 - Type"_s, u"Profile"_s}, {u"Website 1 - Value"_s, u"https://example.org/kj"_s},
            {u"Event 1 - Type"_s, u"Anniversary"_s}, {u"Event 1 - Value"_s, u"1939-11-25"_s},
        }});
        ContactCsv::ReadResult read = ContactCsv::read(csv);
        QCOMPARE(read.contacts.size(), 1);
        const Contact c = read.contacts.first();
        QCOMPARE(c.displayName, u"Katherine Johnson"_s);
        QCOMPARE(c.emails, QList<ContactValue>({value(u"work"_s, u"katherine@example.org"_s, true),
                                                value(u"work"_s, u"kj.work@example.org"_s), value(u"home"_s, u"kj@example.org"_s)}));
        QCOMPARE(c.phones, QList<ContactValue>({value(u"cell"_s, u"+1 555 0150"_s)}));
        QCOMPARE(c.addresses.size(), 1);
        QCOMPARE(c.addresses.first().type, u"work"_s);
        QCOMPARE(c.addresses.first().label, u"1 Langley Blvd\nHampton, VA"_s);
        QCOMPARE(c.addresses.first().postalCode, u"23681"_s);
        QCOMPARE(c.organization, u"NASA"_s);
        QCOMPARE(c.title, u"Mathematician"_s);
        QCOMPARE(c.urls, QList<ContactValue>({value(u"profile"_s, u"https://example.org/kj"_s)}));
        QCOMPARE(c.birthday, u"1918-08-26"_s);
        QCOMPARE(c.anniversary, u"1939-11-25"_s);
        QCOMPARE(c.categories, QStringList({u"NASA"_s}));
        QVERIFY(c.extra.isEmpty()); // an empty Yomi Name is not kept

        // The newer Google layout: First Name / Labels / "E-mail 1 - Label".
        read = ContactCsv::read("First Name,Last Name,Labels,E-mail 1 - Label,E-mail 1 - Value,Phone 1 - Label,"
                                "Phone 1 - Value,Organization Name,Organization Title\r\n"
                                "Dorothy,Vaughan,* myContacts ::: Friends,Other,dorothy@example.org,Work Fax,"
                                "+1 555 0160,NACA,Supervisor\r\n");
        QCOMPARE(read.contacts.size(), 1);
        QCOMPARE(read.contacts.first().emails, QList<ContactValue>({value(u"other"_s, u"dorothy@example.org"_s)}));
        QCOMPARE(read.contacts.first().phones, QList<ContactValue>({value(u"work,fax"_s, u"+1 555 0160"_s)}));
        QCOMPARE(read.contacts.first().categories, QStringList({u"Friends"_s}));
        QCOMPARE(read.contacts.first().organization, u"NACA"_s);
    }

    void dates()
    {
        const QByteArray csv = "First Name,Birthday\r\nA,4/15/75\r\nB,15.4.1975\r\nC,15/04/1975\r\n"
                               "D,1/1/4501\r\nE,1975-04-15\r\nF,--04-15\r\nG,2/30/1975\r\nH,4/15/1975 0:00:00\r\n";
        const ContactCsv::ReadResult read = ContactCsv::read(csv);
        QStringList birthdays;
        for (const Contact &c : read.contacts) birthdays.append(c.birthday);
        QCOMPARE(birthdays, QStringList({u"1975-04-15"_s, u"1975-04-15"_s, u"1975-04-15"_s, QString(),
                                         u"1975-04-15"_s, u"--04-15"_s, QString(), u"1975-04-15"_s}));
    }

    void hostileInput()
    {
        QVERIFY(ContactCsv::read(QByteArray()).contacts.isEmpty());
        ContactCsv::ReadResult read = ContactCsv::read("Colour,Shape\r\nred,round\r\n");
        QVERIFY(read.contacts.isEmpty());
        QVERIFY(read.warnings.join(u'\n').contains(u"names no contact columns"_s));
        // An unterminated quote runs to the end; nothing crashes.
        read = ContactCsv::read("First Name,Notes\r\nAda,\"never closed\r\nGrace,x\r\n");
        QCOMPARE(read.contacts.size(), 1);
        QCOMPARE(read.contacts.first().notes, u"never closed\nGrace,x"_s);
        // More than 20 000 rows.
        QByteArray many = "First Name\r\n";
        for (int i = 0; i <= ContactLimits::kMaxContacts; ++i) many += "N\r\n";
        read = ContactCsv::read(many);
        QCOMPARE(read.contacts.size(), ContactLimits::kMaxContacts);
        QCOMPARE(read.skipped, 1);
        // Files over 64 MiB are refused by both readers.
        const QByteArray huge(ContactLimits::kMaxImportFileBytes + 1, 'a');
        QVERIFY(ContactCsv::read(huge).warnings.join(u'\n').contains(u"64 MiB"_s));
        QVERIFY(VCard::read(huge).warnings.join(u'\n').contains(u"64 MiB"_s));
    }
};

QTEST_GUILESS_MAIN(TestMailContactCsv)
#include "tst_mail_contact_csv.moc"
