// SPDX-License-Identifier: GPL-3.0-or-later
// EFW2 (SSA Publication 42-007, Tax Year 2026): the W-2 file
// (efw2_export.h). Positions below are the publication's, 1-based and
// inclusive; "Any money field that has no amount to be reported must be
// filled with zeros, not blanks."
#include "efw2_export.h"

#include "company_file.h"
#include "payroll_ledger.h"
#include "payroll_records.h"

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>

namespace QindaOffice::Books {

namespace {

class Record {
public:
    explicit Record(const char *id)
        : m_bytes(512, ' ')
    {
        put(1, 2, QString::fromLatin1(id));
    }
    // Alphanumeric: upper case, left justified, blank filled. E-mail keeps
    // its case (the spec allows it there).
    void put(int start, int end, const QString &text, bool keepCase = false)
    {
        QString value = keepCase ? text : text.toUpper();
        QByteArray ascii;
        for (const QChar c : value.normalized(QString::NormalizationForm_KD)) {
            if (c.unicode() >= 0x20 && c.unicode() < 0x7F) {
                ascii += char(c.unicode());
            }
        }
        const int width = end - start + 1;
        m_bytes.replace(start - 1, width, ascii.left(width).leftJustified(width, ' '));
    }
    // Numeric: right justified, zero filled; money in cents.
    void number(int start, int end, qint64 value)
    {
        const int width = end - start + 1;
        m_bytes.replace(start - 1, width, QByteArray::number(qMax<qint64>(0, value)).rightJustified(width, '0', true));
    }
    void zeros(const QList<std::pair<int, int>> &fields)
    {
        for (const auto &[a, b] : fields) {
            number(a, b, 0);
        }
    }
    [[nodiscard]] QByteArray bytes() const { return m_bytes + "\r\n"; }

private:
    QByteArray m_bytes;
};

struct Address {
    QString delivery; // street or PO box
    QString city;
    QString state;
    QString zip;
    QString zipExtension;
};

// "Street\nCity, ST 80202[-1234]" (the company's settings address).
std::optional<Address> parseAddress(const QString &text)
{
    QStringList lines;
    for (const QString &l : text.split(QLatin1Char('\n'))) {
        if (!l.trimmed().isEmpty()) {
            lines << l.trimmed();
        }
    }
    static const QRegularExpression last(QStringLiteral("^(.+?),?\\s+([A-Za-z]{2})\\s+(\\d{5})(?:-(\\d{4}))?$"));
    const QRegularExpressionMatch m = lines.isEmpty() ? QRegularExpressionMatch() : last.match(lines.last());
    if (lines.size() < 2 || !m.hasMatch()) {
        return std::nullopt;
    }
    return Address{lines.at(lines.size() - 2), m.captured(1), m.captured(2).toUpper(), m.captured(3), m.captured(4)};
}

QString digits(const QString &text)
{
    QString out;
    for (const QChar c : text) {
        if (c.isDigit()) {
            out += c;
        }
    }
    return out;
}

const QStringList kBadSubmitterEin{QStringLiteral("07"), QStringLiteral("08"), QStringLiteral("09"), QStringLiteral("17"),
                                   QStringLiteral("18"), QStringLiteral("19"), QStringLiteral("28"), QStringLiteral("29"),
                                   QStringLiteral("49"), QStringLiteral("69"), QStringLiteral("70"), QStringLiteral("78"),
                                   QStringLiteral("79"), QStringLiteral("89")};

struct Totals {
    qint64 v[12] = {};
};

} // namespace

bool validSignaturePin(const QString &pin)
{
    static const QRegularExpression format(QStringLiteral("^S-(\\d{6})-(\\d)$"));
    const QRegularExpressionMatch m = format.match(pin);
    if (!m.hasMatch()) {
        return false;
    }
    const QString body = m.captured(1);
    int sum = 0;
    for (int i = 0; i < body.size(); ++i) {
        int d = body.at(body.size() - 1 - i).digitValue();
        if (i % 2 == 0) { // the rightmost digit of the body is doubled
            d *= 2;
            d = d > 9 ? d - 9 : d;
        }
        sum += d;
    }
    return (10 - sum % 10) % 10 == m.captured(2).toInt();
}

Efw2File buildEfw2(CompanyFile &file, int year, const Efw2Options &options)
{
    Efw2File out;
    const auto fail = [&out](const QString &why) {
        out.error = why;
        return out;
    };
    PayrollRecords records(file);
    const PayrollSetup setup = records.setup();
    const CompanySettings company = file.settings();
    const QString ein = normalizedTaxId(setup.ein, 9);
    const std::optional<Address> employer = parseAddress(company.address);
    if (options.validate) {
        if (ein.isEmpty() || ein.startsWith(QLatin1String("00")) || kBadSubmitterEin.contains(ein.left(2))) {
            return fail(QStringLiteral("Enter the company's EIN in Payroll Setup (SSA rejects the one on file)."));
        }
        if (setup.bsoUserId.trimmed().size() != 8) {
            return fail(QStringLiteral("Enter the business's 8-character SSA Business Services Online user ID in Payroll Setup."));
        }
        if (setup.w2ContactName.trimmed().isEmpty() || setup.w2ContactEmail.trimmed().isEmpty()
            || digits(setup.w2ContactPhone).isEmpty()) {
            return fail(QStringLiteral("Enter the W-2 contact's name, phone and e-mail in Payroll Setup."));
        }
        if (!setup.signaturePin.isEmpty() && !validSignaturePin(setup.signaturePin)) {
            return fail(QStringLiteral("The employer signature PIN must read S-XXXXXX-Y with a valid check digit."));
        }
        if (!employer) {
            return fail(QStringLiteral("The company address needs a street line and a \"City, ST 12345\" line."));
        }
    }
    const Address a = employer.value_or(Address{});
    const QString name = company.legalName.isEmpty() ? company.displayName() : company.legalName;

    Record ra("RA");
    ra.put(3, 11, ein);
    ra.put(12, 19, setup.bsoUserId.trimmed());
    ra.put(29, 29, QStringLiteral("0"));
    ra.put(36, 37, QStringLiteral("98")); // in-house program
    const auto address = [&](Record &r, int nameStart, int nameEnd, int at) {
        r.put(nameStart, nameEnd, name);
        r.put(at + 22, at + 43, a.delivery);
        r.put(at + 44, at + 65, a.city);
        r.put(at + 66, at + 67, a.state);
        r.put(at + 68, at + 72, a.zip);
        r.put(at + 73, at + 76, a.zipExtension);
    };
    address(ra, 38, 94, 95);   // company: location 95-116, delivery 117-138 …
    address(ra, 217, 273, 274); // submitter: location 274-295, delivery 296-317 …
    ra.put(396, 422, setup.w2ContactName);
    ra.put(423, 437, digits(setup.w2ContactPhone));
    ra.put(446, 485, setup.w2ContactEmail.trimmed(), true);
    ra.put(500, 500, QStringLiteral("L")); // self-prepared

    Record re("RE");
    re.number(3, 6, year);
    re.put(8, 16, ein);
    re.put(26, 26, QStringLiteral("0"));
    re.put(40, 96, name);
    re.put(119, 140, a.delivery);
    re.put(141, 162, a.city);
    re.put(163, 164, a.state);
    re.put(165, 169, a.zip);
    re.put(170, 173, a.zipExtension);
    re.put(174, 174, QStringLiteral("N")); // kind of employer: none apply
    re.put(219, 219, QStringLiteral("R")); // regular (Form 941)
    re.put(221, 221, QStringLiteral("0"));
    re.put(222, 248, setup.w2ContactName);
    re.put(249, 263, digits(setup.w2ContactPhone));
    re.put(279, 318, setup.w2ContactEmail.trimmed(), true);
    re.put(319, 328, setup.signaturePin.trimmed());

    QByteArray body = ra.bytes() + re.bytes();
    QMap<qint64, QList<PaycheckRecord>> byEmployee;
    for (const PaycheckRecord &r : PayrollLedger(file).paychecks(QDate(year, 1, 1), QDate(year, 12, 31))) {
        byEmployee[r.employeeId] << r;
    }
    if (byEmployee.isEmpty()) {
        return fail(QStringLiteral("No paychecks in %1: there are no W-2s to file.").arg(year));
    }
    Totals t;
    qint64 simpleTotal = 0;
    int roCount = 0;
    for (auto it = byEmployee.cbegin(); it != byEmployee.cend(); ++it) {
        const EmployeePayroll e = records.employee(it.key());
        const QString ssn = normalizedTaxId(e.ssn, 9);
        if (options.validate && (ssn.isEmpty() || ssn.startsWith(QLatin1Char('9')) || ssn.startsWith(QLatin1String("666")))) {
            return fail(QStringLiteral("%1's Social Security number is missing or one SSA does not accept.").arg(e.fullName()));
        }
        qint64 v[8] = {};
        qint64 k401 = 0;
        qint64 simple = 0;
        for (const PaycheckRecord &r : it.value()) {
            v[0] += r.taxes.federalWages.minor();
            v[1] += r.taxes.federalIncomeTax.minor();
            v[2] += r.taxes.socialSecurityWages.minor();
            v[3] += r.taxes.socialSecurity.minor();
            v[4] += r.taxes.medicareWages.minor();
            v[5] += (r.taxes.medicare + r.taxes.additionalMedicare).minor();
            (r.retirementKind == PayDeductions::Retirement::SimpleIra ? simple : k401) += r.retirement.minor();
        }
        Record rw("RW");
        rw.put(3, 11, ssn.isEmpty() ? QStringLiteral("000000000") : ssn);
        rw.put(12, 26, e.firstName);
        rw.put(27, 41, e.middleInitial);
        rw.put(42, 61, e.lastName);
        rw.put(62, 65, e.suffix);
        rw.put(88, 109, e.street);
        rw.put(110, 131, e.city);
        rw.put(132, 133, e.state);
        rw.put(134, 138, digits(e.zip).left(5));
        rw.put(139, 142, digits(e.zip).mid(5, 4));
        const int moneyAt[6][2] = {{188, 198}, {199, 209}, {210, 220}, {221, 231}, {232, 242}, {243, 253}};
        for (int i = 0; i < 6; ++i) {
            rw.number(moneyAt[i][0], moneyAt[i][1], v[i]);
            t.v[i] += v[i];
        }
        rw.zeros({{254, 264}, {276, 286}, {298, 308}, {309, 319}, {320, 330}, {331, 341}, {353, 363}, {364, 374},
                  {375, 385}, {386, 396}, {408, 418}, {419, 429}, {430, 440}, {441, 451}, {452, 462}, {463, 473},
                  {474, 484}});
        rw.number(287, 297, k401);
        t.v[6] += k401;
        rw.put(486, 486, QStringLiteral("0"));
        rw.put(488, 488, (k401 + simple) > 0 ? QStringLiteral("1") : QStringLiteral("0"));
        rw.put(489, 489, QStringLiteral("0"));
        body += rw.bytes();
        if (simple > 0) {
            Record ro("RO");
            ro.zeros({{12, 22}, {23, 33}, {34, 44}, {56, 66}, {67, 77}, {78, 88}, {89, 99}, {111, 121}, {122, 132},
                      {133, 143}, {144, 154}, {155, 165}, {166, 176}, {177, 187}, {188, 198}, {199, 209}, {210, 220},
                      {221, 231}, {232, 242}, {243, 253}, {275, 285}, {286, 296}, {297, 307}, {308, 318}, {319, 329},
                      {330, 340}, {341, 351}, {363, 373}, {374, 384}});
            ro.number(45, 55, simple);
            body += ro.bytes();
            simpleTotal += simple;
            ++roCount;
        }
        ++out.employees;
    }
    Record rt("RT");
    rt.number(3, 9, out.employees);
    const int totalAt[6][2] = {{10, 24}, {25, 39}, {40, 54}, {55, 69}, {70, 84}, {85, 99}};
    for (int i = 0; i < 6; ++i) {
        rt.number(totalAt[i][0], totalAt[i][1], t.v[i]);
    }
    rt.zeros({{100, 114}, {130, 144}, {160, 174}, {175, 189}, {190, 204}, {205, 219}, {235, 249}, {250, 264}, {265, 279},
              {280, 294}, {295, 309}, {310, 324}, {325, 339}, {340, 354}, {355, 369}, {370, 384}, {385, 399}, {400, 414}});
    rt.number(145, 159, t.v[6]);
    body += rt.bytes();
    if (roCount > 0) {
        Record ru("RU");
        ru.number(3, 9, roCount);
        ru.zeros({{10, 24}, {25, 39}, {40, 54}});
        ru.number(55, 69, simpleTotal);
        body += ru.bytes();
        out.warnings << QStringLiteral("SIMPLE deferrals are reported on RO/RU records; only the code S fields are filled.");
    }
    Record rf("RF");
    rf.number(8, 16, out.employees);
    body += rf.bytes();
    out.bytes = body;
    out.ok = true;
    out.warnings << QStringLiteral("Check the file with SSA's AccuWage Online before uploading it through Business Services Online.");
    return out;
}

bool writeEfw2(CompanyFile &file, int year, const QString &path, QString *error)
{
    const Efw2File built = buildEfw2(file, year);
    if (!built.ok) {
        if (error != nullptr) {
            *error = built.error;
        }
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(built.bytes) != built.bytes.size() || !f.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("could not write %1").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Books
