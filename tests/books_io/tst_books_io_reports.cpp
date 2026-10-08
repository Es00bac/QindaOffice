// SPDX-License-Identifier: GPL-3.0-or-later
// M46: ReportTable → PDF (the same PagePainter contract as the forms) and
// → CSV, with the CSV read back to the numbers the report computed. M63:
// → Excel, read back with QXlsx: money, numbers, percentages and dates are
// numeric cells with their formats, never text.
#include "books_io_fixture.h"

#include "csv_util.h"
#include "report_output.h"
#include "report_xlsx.h"
#include "reports.h"

#include <QTemporaryDir>

#include <xlsxcell.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>

using namespace QindaOffice::Books;
using namespace BooksIoFixture;

class TestBooksIoReports : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void profitAndLossPdf();
    void reportCsvRoundTrip_data();
    void reportCsvRoundTrip();
    void wideReportsGoLandscape();
    void longReportsPaginate();
    void csvQuoting();
    void reportsToExcelAsNumbers();

private:
    QTemporaryDir m_dir;
    Company m_company;
};

void TestBooksIoReports::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_company = build(m_dir.filePath(QStringLiteral("northwind.qbooks")));
}

void TestBooksIoReports::profitAndLossPdf()
{
    CompanyFile &f = *m_company.file;
    PnlOptions options;
    options.from = QDate(2026, 3, 1);
    options.to = QDate(2026, 3, 31);
    options.basis = Basis::Accrual;
    const ReportTable table = Reports(f).profitAndLoss(options);
    const QString pdf = m_dir.filePath(QStringLiteral("pnl.pdf"));
    QString error;
    QVERIFY2(ReportPdf::writeReportPdf(f, table, pdf, &error), qPrintable(error));
    const QStringList pages = pdfPages(pdf);
    QCOMPARE(pages.size(), 1);
    const QString text = pages.first();
    const QString net = Money(table.money(QStringLiteral("total:net_income"), QStringLiteral("total")), QStringLiteral("USD"))
                            .format(QLocale(QLocale::English, QLocale::UnitedStates), false);
    for (const QString &expected : {QStringLiteral("Profit and Loss"), QStringLiteral("Northwind Creative"),
                                    QStringLiteral("Net Income"), QStringLiteral("Project Fees"), net,
                                    QStringLiteral("Page 1 of 1")}) {
        QVERIFY2(text.contains(expected), qPrintable(QStringLiteral("missing \"%1\" in:\n%2").arg(expected, text)));
    }
    qInfo("gallery: %s", qPrintable(saveGalleryPng(pdf, QStringLiteral("M46-books-profit-and-loss.png"))));
    const QString bs = m_dir.filePath(QStringLiteral("bs.pdf"));
    QVERIFY(ReportPdf::writeReportPdf(f, Reports(f).balanceSheet({QDate(2026, 3, 31), Basis::Accrual, Comparison::None}),
                                      bs, &error));
    saveGalleryPng(bs, QStringLiteral("M46-books-balance-sheet.png"));
}

void TestBooksIoReports::reportCsvRoundTrip_data()
{
    QTest::addColumn<QString>("report");
    QTest::newRow("profit_and_loss") << QStringLiteral("pnl");
    QTest::newRow("balance_sheet") << QStringLiteral("bs");
    QTest::newRow("trial_balance") << QStringLiteral("tb");
    QTest::newRow("ar_aging_detail") << QStringLiteral("aging");
}

void TestBooksIoReports::reportCsvRoundTrip()
{
    QFETCH(QString, report);
    CompanyFile &f = *m_company.file;
    Reports reports(f);
    ReportTable table;
    if (report == QLatin1String("pnl")) {
        table = reports.profitAndLoss({QDate(2026, 3, 1), QDate(2026, 3, 31), Basis::Accrual, Comparison::None,
                                       PnlColumns::Total});
    } else if (report == QLatin1String("bs")) {
        table = reports.balanceSheet({QDate(2026, 3, 31), Basis::Accrual, Comparison::None});
    } else if (report == QLatin1String("tb")) {
        table = reports.trialBalance(QDate(2026, 3, 31), Basis::Accrual);
    } else {
        table = reports.arAgingDetail(QDate(2026, 5, 15));
    }
    QVERIFY(!table.rows.isEmpty());
    const QByteArray csv = reportToCsv(table);
    QVERIFY(csv.endsWith("\r\n"));
    const QList<QStringList> rows = Csv::parse(QString::fromUtf8(csv), QLatin1Char(','));
    QCOMPARE(rows.size(), table.rows.size() + 1);
    for (int c = 0; c < table.columns.size(); ++c) {
        QCOMPARE(rows.first().value(c), table.columns[c].title.isEmpty() ? table.columns[c].id : table.columns[c].title);
    }
    // Every money cell comes back as the exact minor-unit value.
    int checked = 0;
    for (int r = 0; r < table.rows.size(); ++r) {
        for (int c = 0; c < table.columns.size(); ++c) {
            const ReportCell &cell = table.rows[r].cells.value(c);
            const QString text = rows[r + 1].value(c);
            if (cell.empty) {
                QVERIFY(text.isEmpty());
                continue;
            }
            if (cell.type == ColumnType::Money) {
                const std::optional<Money> back = Money::parse(text, QStringLiteral("USD"));
                QVERIFY2(back.has_value(), qPrintable(text));
                QCOMPARE(back->minor(), cell.value);
                ++checked;
            } else if (cell.type == ColumnType::Text) {
                QCOMPARE(text, cell.text);
            }
        }
    }
    QVERIFY(checked > 0);
    if (report == QLatin1String("tb")) {
        // Debits = credits survives the trip.
        QCOMPARE(rows.last().value(1), rows.last().value(2));
    }
}

void TestBooksIoReports::wideReportsGoLandscape()
{
    CompanyFile &f = *m_company.file;
    const ReportTable months = Reports(f).profitAndLoss(
        {QDate(2026, 1, 1), QDate(2026, 12, 31), Basis::Accrual, Comparison::None, PnlColumns::Months});
    const ReportStyle style = ReportPdf::styleFor(f);
    QVERIFY(ReportPdf::prefersLandscape(months, QPageSize(QPageSize::Letter), style));
    const ReportTable simple = Reports(f).profitAndLoss({QDate(2026, 1, 1), QDate(2026, 12, 31), Basis::Accrual,
                                                         Comparison::None, PnlColumns::Total});
    QVERIFY(!ReportPdf::prefersLandscape(simple, QPageSize(QPageSize::Letter), style));
    const QString pdf = m_dir.filePath(QStringLiteral("months.pdf"));
    QVERIFY(ReportPdf::writeReportPdf(f, months, pdf));
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(pdf);
    QVERIFY(doc);
    const QSizeF size = doc->page(0)->pageSizeF();
    QVERIFY(size.width() > size.height());
    QVERIFY(doc->page(0)->text(QRectF()).contains(QStringLiteral("Dec 2026")));
}

void TestBooksIoReports::longReportsPaginate()
{
    CompanyFile &f = *m_company.file;
    // Enough postings to spill the journal over several pages.
    const qint64 checking = m_company.checking;
    const qint64 software = ChartOfAccounts(f).find(QStringLiteral("Software and Subscriptions"));
    for (int i = 0; i < 60; ++i) {
        BusinessDocument e;
        e.kind = DocKind::Expense;
        e.date = QDate(2026, 4, 1).addDays(i % 28);
        e.accountId = checking;
        DocumentLine l;
        l.accountId = software;
        l.description = QStringLiteral("Plugin licence %1").arg(i + 1);
        l.amount = usd(1000 + i);
        e.lines = {l};
        QVERIFY(Purchases(f).createExpense(e).ok);
    }
    const ReportTable journal = Reports(f).journal(QDate(2026, 4, 1), QDate(2026, 4, 30));
    const QString pdf = m_dir.filePath(QStringLiteral("journal.pdf"));
    QVERIFY(ReportPdf::writeReportPdf(f, journal, pdf));
    const QStringList pages = pdfPages(pdf);
    QVERIFY(pages.size() > 1);
    for (int p = 1; p < pages.size(); ++p) {
        QVERIFY(pages[p].contains(QStringLiteral("Journal (continued)")));
        QVERIFY(pages[p].contains(QStringLiteral("Debit"))); // header repeated
    }
    const QString all = pages.join(QLatin1Char('\n'));
    // Every expense line lands on exactly one page (the memo is on the
    // category line; the bank line carries the document memo, empty here).
    QCOMPARE(all.count(QStringLiteral("Plugin licence")), 60);
}

void TestBooksIoReports::csvQuoting()
{
    const QStringList row{QStringLiteral("plain"), QStringLiteral("a,b"), QStringLiteral("say \"hi\""),
                          QStringLiteral("two\nlines"), QStringLiteral(" padded "), QString()};
    const QByteArray doc = Csv::document({row, row});
    QCOMPARE(doc.left(doc.indexOf("\r\n")),
             QByteArray("plain,\"a,b\",\"say \"\"hi\"\"\",\"two\nlines\",\" padded \","));
    const QList<QStringList> back = Csv::parse(QString::fromUtf8(doc), QLatin1Char(','));
    QCOMPARE(back.size(), 2);
    QCOMPARE(back.first(), row);
    QCOMPARE(Csv::detectDelimiter(QStringLiteral("a;b;c\n1,5;2;3\n4;5,5;6\n")), QLatin1Char(';'));
    QCOMPARE(Csv::detectDelimiter(QStringLiteral("a\tb\tc\n1\t2\t3\n")), QLatin1Char('\t'));
}

namespace {

// The 1-based row whose column A (indentation trimmed) is `label`; 0 if none.
int rowOf(QXlsx::Document &doc, const QString &label)
{
    for (int row = 1; row <= doc.dimension().lastRow(); ++row) {
        if (doc.read(row, 1).toString().trimmed() == label) {
            return row;
        }
    }
    return 0;
}

} // namespace

void TestBooksIoReports::reportsToExcelAsNumbers()
{
    CompanyFile &f = *m_company.file;
    PnlOptions options;
    options.from = QDate(2026, 3, 1);
    options.to = QDate(2026, 3, 31);
    options.basis = Basis::Accrual;
    const ReportTable pnl = Reports(f).profitAndLoss(options);
    const QString path = m_dir.filePath(QStringLiteral("pnl.xlsx"));
    QString error;
    QVERIFY2(ReportXlsx::write(pnl, path, &error), qPrintable(error));
    QXlsx::Document doc(path);
    QVERIFY(doc.isLoadPackage());
    QCOMPARE(doc.read(1, 1).toString(), QStringLiteral("Northwind Creative"));
    QCOMPARE(doc.read(2, 1).toString(), QStringLiteral("Profit and Loss"));
    QVERIFY(doc.cellAt(5, 1)->format().fontBold()); // the header row
    const int net = rowOf(doc, QStringLiteral("Net Income"));
    QVERIFY(net > 5);
    const std::shared_ptr<QXlsx::Cell> total = doc.cellAt(net, 2);
    QVERIFY(total);
    QCOMPARE(total->value().typeId(), QMetaType::Double);
    QCOMPARE(qRound64(total->value().toDouble() * 100), pnl.money(QStringLiteral("total:net_income"), QStringLiteral("total")));
    QCOMPARE(total->format().numberFormat(), ReportXlsx::moneyFormat(QStringLiteral("USD")));
    QCOMPARE(ReportXlsx::moneyFormat(QStringLiteral("USD")), QStringLiteral("\"$\"#,##0.00;-\"$\"#,##0.00"));
    QVERIFY(total->format().fontBold());
    const int fees = rowOf(doc, QStringLiteral("4000 Project Fees"));
    QVERIFY(fees > 0);
    QCOMPARE(doc.cellAt(fees, 2)->value().typeId(), QMetaType::Double);
    QVERIFY(doc.read(fees, 1).toString().startsWith(QLatin1Char(' '))); // indented under Income

    // A percentage share and a listing with dates.
    const ReportTable sales = Reports(f).salesByCustomer(QDate(2026, 1, 1), QDate(2026, 3, 31), Basis::Accrual);
    const QString salesPath = m_dir.filePath(QStringLiteral("sales.xlsx"));
    QVERIFY(ReportXlsx::write(sales, salesPath, &error));
    QXlsx::Document salesDoc(salesPath);
    const int shareColumn = sales.columnIndex(QStringLiteral("share")) + 1;
    QVERIFY(shareColumn > 0);
    const int customer = rowOf(salesDoc, QStringLiteral("Harbor & Pine Coffee Co."));
    QVERIFY(customer > 0);
    const std::shared_ptr<QXlsx::Cell> share = salesDoc.cellAt(customer, shareColumn);
    QCOMPARE(share->value().typeId(), QMetaType::Double);
    QCOMPARE(share->value().toDouble(), 1.0); // the only customer: 100.0%
    QCOMPARE(share->format().numberFormat(), QStringLiteral("0.0%"));
    const ReportTable ledger = Reports(f).generalLedger(QDate(2026, 3, 1), QDate(2026, 3, 31));
    const QString ledgerPath = m_dir.filePath(QStringLiteral("ledger.xlsx"));
    QVERIFY(ReportXlsx::write(ledger, ledgerPath, &error));
    QXlsx::Document ledgerDoc(ledgerPath);
    const int dateColumn = ledger.columnIndex(QStringLiteral("date")) + 1;
    QVERIFY(dateColumn > 0);
    bool sawDate = false;
    for (int row = 6; row <= ledgerDoc.dimension().lastRow() && !sawDate; ++row) {
        const std::shared_ptr<QXlsx::Cell> c = ledgerDoc.cellAt(row, dateColumn);
        if (c && c->format().numberFormat() == QStringLiteral("yyyy-mm-dd")) {
            sawDate = c->readValue().toDate() == QDate(2026, 3, 2) || c->readValue().toDate().isValid();
        }
    }
    QVERIFY(sawDate);
}

QTEST_MAIN(TestBooksIoReports)
#include "tst_books_io_reports.moc"
