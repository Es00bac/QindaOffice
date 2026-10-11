// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic QuickBooks-shaped EMP/TIMEACT imports: preview, one-batch
// refusal, duplicate counts and the engine's actual time records.
#include "company_file.h"
#include "employees.h"
#include "iif_import.h"
#include "iif_reader.h"
#include "lists.h"
#include "time_tracking.h"

#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace QindaOffice::Books;

namespace {

QByteArray iif(const char *text)
{
    QByteArray bytes(text);
    bytes.replace('|', '\t');
    return bytes;
}

std::unique_ptr<CompanyFile> blank(const QString &path)
{
    CompanySettings settings;
    settings.legalName = QStringLiteral("Harbor Design LLC");
    QString error;
    auto file = CompanyFile::create(path, settings, QStringLiteral("general"), &error);
    if (!file) qFatal("fixture company: %s", qPrintable(error));
    return file;
}

const char *const kActivities = R"IIF(!ACCNT|NAME|ACCNTTYPE
ACCNT|Consulting Income|INC
!CUST|NAME
CUST|Harbor Clinic
!EMP|NAME|ADDR1|ADDR2|ADDR3|PHONE1|EMAIL|HIDDEN
EMP|Ada Lee|Ada Lee|15 Maple St|Denver CO|555-0111|ada@example.test|N
!INVITEM|NAME|INVITEMTYPE|DESC|ACCNT|PRICE
INVITEM|Consulting|SERV|Hourly advice|Consulting Income|90.00
!TIMEACCT|DATE|JOB|EMP|ITEM|PITEM|DURATION|PROJ|NOTE|XFERTOPAYROLL|BILLINGSTATUS
TIMEACT|10/04/2026|Harbor Clinic|Ada Lee|Consulting||1:30|Planning|Review|N|1
TIMEACT|10/04/2026|Harbor Clinic|Ada Lee|Consulting||1:30|Planning|Review|N|1
TIMEACT|10/05/2026||Ada Lee|||0:45||Internal|N|0
)IIF";

} // namespace

class TestBooksIoIifTime final : public QObject {
    Q_OBJECT
private slots:
    void proTimerHeaderAndCountedRows()
    {
        QTemporaryDir dir;
        auto file = blank(dir.filePath(QStringLiteral("time.qbooks")));
        const auto parsed = IifReader::parse(iif(kActivities));
        QVERIFY(parsed.problems.isEmpty());
        QCOMPARE(parsed.lists.size(), 7);
        const auto preview = IifImport::run(*file, iif(kActivities));
        QVERIFY2(preview.ok, qPrintable(preview.error));
        QCOMPARE(preview.employees.added, 1);
        QCOMPARE(preview.timeActivities.added, 3);
        QCOMPARE(Employees(*file).employees(true).size(), 0);
        QCOMPARE(TimeTracking(*file).entries().size(), 0);

        IifImportOptions options; options.commit = true;
        const auto imported = IifImport::run(*file, iif(kActivities), options);
        QVERIFY2(imported.committed, qPrintable(imported.error));
        QCOMPARE(imported.employees.added, 1);
        QCOMPARE(imported.timeActivities.added, 3);
        const auto employee = Employees(*file).employees().first();
        QCOMPARE(employee.name, QStringLiteral("Ada Lee"));
        QCOMPARE(employee.address, QStringLiteral("15 Maple St\nDenver CO"));
        const auto entries = TimeTracking(*file).entries();
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(0).minutes, 90);
        QCOMPARE(entries.at(1).minutes, 90);
        QVERIFY(entries.at(0).billable && entries.at(1).billable);
        QCOMPARE(entries.at(0).customerId, Lists(*file).findParty(PartyKind::Customer, QStringLiteral("Harbor Clinic")));
        QVERIFY(!entries.at(2).billable);
        QCOMPARE(entries.at(2).minutes, 45);

        const auto again = IifImport::run(*file, iif(kActivities), options);
        QVERIFY2(again.committed, qPrintable(again.error));
        QCOMPARE(again.employees.existing, 1);
        QCOMPARE(again.timeActivities.existing, 3);
        QCOMPARE(again.timeActivities.added, 0);
        QCOMPARE(TimeTracking(*file).entries().size(), 3);
    }

    void billedAndAmbiguousTimeStopTheBatch()
    {
        QTemporaryDir dir;
        auto file = blank(dir.filePath(QStringLiteral("refused.qbooks")));
        const QByteArray source = iif(R"IIF(!EMP|NAME
EMP|Ada Lee
!TIMEACT|DATE|JOB|EMP|ITEM|DURATION|BILLINGSTATUS
TIMEACT|10/04/2026|Harbor Clinic|Ada Lee|Consulting|1:30|2
TIMEACT|10/05/2026||Ada Lee||0:30|9
)IIF");
        IifImportOptions strict; strict.commit = true;
        const auto report = IifImport::run(*file, source, strict);
        QVERIFY(!report.ok);
        QVERIFY(!report.committed);
        QCOMPARE(report.refused.size(), 2);
        QCOMPARE(report.refused.at(0).firstLine, 4);
        QVERIFY(report.refused.at(0).reason.contains(QStringLiteral("invoice link")));
        QCOMPARE(Employees(*file).employees(true).size(), 0);
        QCOMPARE(TimeTracking(*file).entries().size(), 0);

        IifImportOptions skip; skip.commit = true; skip.skipRefused = true;
        const auto partial = IifImport::run(*file, source, skip);
        QVERIFY2(partial.committed, qPrintable(partial.error));
        QCOMPARE(partial.refused.size(), 2);
        QCOMPARE(partial.employees.added, 1);
        QCOMPARE(partial.timeActivities.added, 0);
        QCOMPARE(Employees(*file).employees(true).size(), 1);
        QCOMPARE(TimeTracking(*file).entries().size(), 0);
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    TestBooksIoIifTime test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_books_io_iif_time.moc"
