// SPDX-License-Identifier: GPL-3.0-or-later
// Payroll on paper and in files (M71, D-081): a paycheck prints on voucher
// stock with its pay stub on both stubs (text layer read back: the
// employee, this check and the year, net pay, the SSN masked), the Form
// 941 worksheet prints through the report painter, and the EFW2 W-2 file
// has SSA's 512-byte records with every value where the specification
// puts it. Fake identity numbers: SSN 900-00-0001, EIN 00-0000000.
#include "books_io_fixture.h"

#include "check_print.h"
#include "efw2_export.h"
#include "employees.h"
#include "payroll_items.h"
#include "payroll_ledger.h"
#include "payroll_records.h"
#include "payroll_reports.h"
#include "report_output.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Books;
using namespace BooksIoFixture;

namespace {

struct PayrollFile {
    QTemporaryDir dir;
    std::unique_ptr<CompanyFile> file;
    qint64 bank = 0;
    qint64 jane = 0;
    qint64 paycheck = 0;

    PayrollFile()
    {
        CompanySettings s = settings();
        s.address = QStringLiteral("418 Alder Street\nDenver, CO 80202");
        QString error;
        file = CompanyFile::create(dir.filePath(QStringLiteral("payroll.qbooks")), s, QStringLiteral("general"), &error);
        if (!file) {
            qFatal("payroll company: %s", qPrintable(error));
        }
        bank = ChartOfAccounts(*file).find(QStringLiteral("1000"));
        PayrollRecords records(*file);
        PayrollSetup setup = records.setup();
        setup.entity = EntityTax::SCorporation;
        setup.ein = QStringLiteral("00-0000000");
        setup.payrollBankAccountId = bank;
        setup.optCity = QStringLiteral("denver");
        setup.colorado = true;
        setup.bsoUserId = QStringLiteral("QBOOKS01");
        setup.w2ContactName = QStringLiteral("Jane Doe");
        setup.w2ContactPhone = QStringLiteral("(303) 555-0100");
        setup.w2ContactEmail = QStringLiteral("jane@example.test");
        records.saveSetup(setup);
        records.ensurePayrollAccounts();
        Employee e;
        e.name = QStringLiteral("Jane Doe");
        jane = Employees(*file).create(e).id;
        EmployeePayroll p = records.employee(jane);
        p.ssn = QStringLiteral("900-00-0001");
        p.street = QStringLiteral("1 Elm Street");
        p.city = QStringLiteral("Denver");
        p.state = QStringLiteral("CO");
        p.zip = QStringLiteral("80202");
        p.payRate = Decimal(78000, 0);
        p.ownerEmployee = true;
        records.saveEmployee(p);
        PaycheckRequest r;
        r.employeeId = jane;
        r.payDate = QDate(2026, 10, 9);
        r.periodStart = QDate(2026, 9, 26);
        r.periodEnd = QDate(2026, 10, 9);
        r.checkNumber = QStringLiteral("2001");
        paycheck = PayrollLedger(*file).createPaycheck(r).id;
    }
};

QByteArray field(const QByteArray &record, int start, int end)
{
    return record.mid(start - 1, end - start + 1);
}

} // namespace

class TstBooksIoPayroll : public QObject {
    Q_OBJECT
private slots:
    void paycheckPrintsWithItsStub()
    {
        PayrollFile c;
        QVERIFY(c.paycheck != 0);
        QString why;
        QVERIFY2(isPrintableCheck(*c.file, c.paycheck, &why), qPrintable(why));
        PrintableCheck check;
        QVERIFY(buildPrintableCheck(*c.file, c.paycheck, &check));
        QCOMPARE(check.payee, QStringLiteral("Jane Doe"));
        QCOMPARE(check.address, QStringList({QStringLiteral("Jane Doe"), QStringLiteral("1 Elm Street"),
                                             QStringLiteral("Denver, CO 80202")}));
        QVERIFY(check.payStub.has_value());
        QCOMPARE(check.amount, QStringLiteral("**2,308.17"));
        QCOMPARE(check.amountWords, QStringLiteral("Two thousand three hundred eight and 17/100"));
        QCOMPARE(check.payStub->net.current, QStringLiteral("2,308.17"));
        QCOMPARE(check.payStub->maskedSsn, QStringLiteral("•••-••-0001"));
        const QString pdf = c.dir.filePath(QStringLiteral("paycheck.pdf"));
        QVERIFY2(CheckRenderer::writeChecksPdf(*c.file, {c.paycheck}, pdf, &why), qPrintable(why));
        const QStringList pages = pdfPages(pdf);
        QCOMPARE(pages.size(), 1);
        const QString text = pages.first();
        for (const char *expected : {"Jane Doe", "Net pay", "Federal income tax", "320.38", "Social security", "186.00",
                                     "Colorado income tax", "123.00", "Colorado FAMLI", "13.20", "Local OPT", "5.75",
                                     "2,308.17", "THIS CHECK", "YEAR TO DATE", "2001"}) {
            QVERIFY2(text.contains(QString::fromUtf8(expected)), expected);
        }
        QVERIFY(text.contains(QStringLiteral("0001")));
        QVERIFY(!text.contains(QStringLiteral("900-00-0001")));
        QVERIFY(!text.contains(QStringLiteral("900000001")));
        // Both stubs print the pay stub.
        QCOMPARE(text.count(QStringLiteral("Net pay")), 2);
        if (qEnvironmentVariableIsSet("QQO_GRAB_DIR")) {
            std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(pdf);
            doc->setRenderHint(Poppler::Document::Antialiasing);
            doc->setRenderHint(Poppler::Document::TextAntialiasing);
            doc->page(0)->renderToImage(110, 110).save(qEnvironmentVariable("QQO_GRAB_DIR")
                                                       + QStringLiteral("/qindabooks_paycheck_stub.png"));
        }
    }

    // Outside Colorado, with the company's own employee item: the stub
    // prints it (1 % of 3,000 = 30.00, the year 30.00); no Colorado tax is
    // taken from this check.
    void customItemOnTheStub()
    {
        PayrollFile c;
        PayrollRecords records(*c.file);
        PayrollSetup s = records.setup();
        s.colorado = false;
        s.stateCode = QStringLiteral("TX");
        s.optCity.clear();
        QVERIFY(records.saveSetup(s).ok);
        PayrollItem sdi;
        sdi.name = QStringLiteral("Example state disability");
        sdi.side = Payroll::CustomItem::Side::Employee;
        sdi.rate = *Decimal::parse(QStringLiteral("0.01"));
        sdi.agency = QStringLiteral("Example disability fund");
        QVERIFY(PayrollItems(*c.file).saveItem(sdi).ok);
        PaycheckRequest r;
        r.employeeId = c.jane;
        r.payDate = QDate(2026, 10, 23);
        r.periodStart = QDate(2026, 10, 10);
        r.periodEnd = QDate(2026, 10, 23);
        r.checkNumber = QStringLiteral("2002");
        const qint64 doc = PayrollLedger(*c.file).createPaycheck(r).id;
        QVERIFY(doc != 0);
        PrintableCheck check;
        QVERIFY(buildPrintableCheck(*c.file, doc, &check));
        bool found = false;
        for (const PayStubLine &l : check.payStub->taxes) {
            if (l.label == QStringLiteral("Example state disability")) {
                found = true;
                QCOMPARE(l.current, QStringLiteral("30.00"));
                QCOMPARE(l.yearToDate, QStringLiteral("30.00"));
            }
            // The year's first check was Colorado's: its line keeps the
            // year to date, this check takes none.
            if (l.label.startsWith(QStringLiteral("Colorado")) || l.label == QStringLiteral("Local OPT")) {
                QCOMPARE(l.current, QStringLiteral("0.00"));
            }
        }
        QVERIFY(found);
        QString why;
        const QString pdf = c.dir.filePath(QStringLiteral("custom.pdf"));
        QVERIFY2(CheckRenderer::writeChecksPdf(*c.file, {doc}, pdf, &why), qPrintable(why));
        const QString text = pdfPages(pdf).first();
        QVERIFY(text.contains(QStringLiteral("Example state disability")));
        QVERIFY(text.contains(QStringLiteral("30.00")));
    }

    void form941PrintsAsAReport()
    {
        PayrollFile c;
        const ReportTable f = PayrollReports(*c.file).form941(2026, 4);
        const QString pdf = c.dir.filePath(QStringLiteral("941.pdf"));
        QString error;
        QVERIFY2(ReportPdf::writeReportPdf(*c.file, f, pdf, &error), qPrintable(error));
        const QString text = pdfPages(pdf).join(QLatin1Char('\n'));
        for (const char *expected : {"Form 941 worksheet", "Federal income tax withheld", "Total taxes after adjustments",
                                     "779.38", "Current quarter's adjustment for fractions of cents"}) {
            QVERIFY2(text.contains(QString::fromUtf8(expected)), expected);
        }
        QVERIFY(!text.contains(QStringLiteral("00-0000000"))); // the EIN masked
    }

    void signaturePins()
    {
        QVERIFY(validSignaturePin(QStringLiteral("S-123456-6")));
        QVERIFY(!validSignaturePin(QStringLiteral("S-123456-4")));
        QVERIFY(!validSignaturePin(QStringLiteral("s-123456-6")));
        QVERIFY(!validSignaturePin(QStringLiteral("S 123456 6")));
    }

    // EFW2 refuses the fake numbers (SSA rejects an EIN starting 00 and an
    // SSN starting 9), so the layout is checked with validation off.
    void efw2Records()
    {
        PayrollFile c;
        const Efw2File refused = buildEfw2(*c.file, 2026);
        QVERIFY(!refused.ok);
        QVERIFY(refused.error.contains(QStringLiteral("EIN")));
        QVERIFY(!refused.error.contains(QStringLiteral("000000000")));
        Efw2Options layout;
        layout.validate = false;
        const Efw2File f = buildEfw2(*c.file, 2026, layout);
        QVERIFY2(f.ok, qPrintable(f.error));
        QCOMPARE(f.employees, 1);
        const QList<QByteArray> records = f.bytes.split('\n');
        QCOMPARE(records.size(), 6); // RA RE RW RT RF and the empty tail after the last CR/LF
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(records.at(i).size(), 513); // 512 + the CR
            QVERIFY(records.at(i).endsWith('\r'));
        }
        const QByteArray ra = records.at(0);
        const QByteArray re = records.at(1);
        const QByteArray rw = records.at(2);
        const QByteArray rt = records.at(3);
        const QByteArray rf = records.at(4);
        QCOMPARE(field(ra, 1, 2), QByteArray("RA"));
        QCOMPARE(field(ra, 12, 19), QByteArray("QBOOKS01"));
        QCOMPARE(field(ra, 36, 37), QByteArray("98"));
        QCOMPARE(field(ra, 446, 462), QByteArray("jane@example.test"));
        QCOMPARE(field(ra, 500, 500), QByteArray("L"));
        QCOMPARE(field(re, 3, 6), QByteArray("2026"));
        QVERIFY2(field(re, 8, 16) == QByteArray("000000000"), "RE carries the EIN"); // compared, never printed
        QCOMPARE(field(re, 119, 134), QByteArray("418 ALDER STREET"));
        QCOMPARE(field(re, 141, 146), QByteArray("DENVER"));
        QCOMPARE(field(re, 163, 169), QByteArray("CO80202"));
        QCOMPARE(field(re, 174, 174), QByteArray("N"));
        QCOMPARE(field(re, 219, 219), QByteArray("R"));
        QVERIFY2(field(rw, 3, 11) == QByteArray("900000001"), "RW carries the SSN");
        QCOMPARE(field(rw, 12, 15), QByteArray("JANE"));
        QCOMPARE(field(rw, 42, 44), QByteArray("DOE"));
        QCOMPARE(field(rw, 188, 198), QByteArray("00000300000")); // box 1
        QCOMPARE(field(rw, 199, 209), QByteArray("00000032038")); // box 2
        QCOMPARE(field(rw, 221, 231), QByteArray("00000018600")); // box 4
        QCOMPARE(field(rw, 243, 253), QByteArray("00000004350")); // box 6
        QCOMPARE(field(rw, 463, 473), QByteArray("00000000000")); // money fields are zeros, never blank
        QCOMPARE(field(rw, 488, 488), QByteArray("0"));
        QCOMPARE(field(rt, 3, 9), QByteArray("0000001"));
        QCOMPARE(field(rt, 10, 24), QByteArray("000000000300000"));
        QCOMPARE(field(rf, 8, 16), QByteArray("000000001"));
    }
};

QTEST_MAIN(TstBooksIoPayroll)
#include "tst_books_io_payroll.moc"
