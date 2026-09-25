// SPDX-License-Identifier: GPL-3.0-or-later
// M46: printed forms (invoice, estimate, sales receipt, credit memo,
// statement) through the one PagePainter, to PDF and to a QPrinter.
#include "books_io_fixture.h"

#include "documents.h"
#include "form_renderer.h"
#include "journal.h"
#include "printable_document.h"

#include <QPrinter>
#include <QTemporaryDir>

using namespace QindaOffice::Books;
using namespace BooksIoFixture;

class TestBooksIoForms : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void invoicePdfRenders();
    void longInvoicePaginatesWithRepeatedHeader();
    void otherFormsRender();
    void statementRenders();
    void voidDocumentsAreNotPrinted();
    void samePainterDrivesAPrinter();

private:
    QTemporaryDir m_dir;
    Company m_company;
};

void TestBooksIoForms::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_company = build(m_dir.filePath(QStringLiteral("northwind.qbooks")));
}

void TestBooksIoForms::invoicePdfRenders()
{
    CompanyFile &f = *m_company.file;
    const QString pdf = m_dir.filePath(QStringLiteral("invoice.pdf"));
    QString error;
    QVERIFY2(FormRenderer::writeDocumentPdf(f, m_company.invoice, pdf, &error), qPrintable(error));
    QVERIFY(QFileInfo(pdf).size() > 2000);
    const QStringList pages = pdfPages(pdf);
    QCOMPARE(pages.size(), 1);
    const std::optional<BusinessDocument> doc = Documents(f).load(m_company.invoice);
    QVERIFY(doc);
    const DocumentTotals totals = Documents(f).computeTotals(*doc);
    QVERIFY(totals.tax.isPositive()); // the printed menus are taxable
    const QLocale loc(QLocale::English, QLocale::UnitedStates);
    const QString text = pages.first();
    for (const QString &expected :
         {QStringLiteral("Northwind Creative"), QStringLiteral("INVOICE"), doc->number,
          QStringLiteral("Harbor & Pine Coffee Co."), QStringLiteral("2201 NW Raleigh Street"), totals.total.format(loc),
          (totals.total - usd(150000)).format(loc), QStringLiteral("Balance due"), QStringLiteral("Sales tax (8.25%)"),
          QStringLiteral("Discount (10%)"), QStringLiteral("PAYMENT INSTRUCTIONS"), QStringLiteral("Page 1 of 1")}) {
        QVERIFY2(text.contains(expected), qPrintable(QStringLiteral("missing \"%1\" in:\n%2").arg(expected, text)));
    }
    const QString png = saveGalleryPng(pdf, QStringLiteral("M46-books-invoice.png"));
    qInfo("gallery: %s", qPrintable(png));
}

void TestBooksIoForms::longInvoicePaginatesWithRepeatedHeader()
{
    CompanyFile &f = *m_company.file;
    BusinessDocument inv;
    inv.partyId = m_company.customer;
    inv.date = QDate(2026, 3, 20);
    for (int i = 1; i <= 80; ++i) {
        DocumentLine line;
        line.itemId = m_company.itemIllustration;
        line.description = QStringLiteral("Illustration %1: spot drawing for the seasonal menu").arg(i);
        line.quantity = Decimal::fromInteger(1 + i % 3);
        inv.lines << line;
    }
    const Result created = Sales(f).createInvoice(inv);
    QVERIFY2(created.ok, qPrintable(created.error));
    PrintableDocument printable;
    QVERIFY(buildPrintableDocument(f, created.id, &printable));
    const int expected = FormRenderer::pageCount(printable, QPageSize(QPageSize::Letter));
    QVERIFY(expected > 1);
    const QString pdf = m_dir.filePath(QStringLiteral("long.pdf"));
    QVERIFY(writePdf(pdf, FormRenderer::painter(printable), QPageSize(QPageSize::Letter)));
    const QStringList pages = pdfPages(pdf);
    QCOMPARE(pages.size(), expected);
    for (int p = 0; p < pages.size(); ++p) {
        QVERIFY2(pages[p].contains(QStringLiteral("Page %1 of %2").arg(p + 1).arg(pages.size())), qPrintable(pages[p]));
        if (p > 0) {
            QVERIFY(pages[p].contains(QStringLiteral("continued")));
        }
    }
    // The column header repeats on every page that carries lines, and every
    // line lands exactly once.
    for (int p = 0; p < pages.size() - 1; ++p) {
        QVERIFY2(pages[p].contains(QStringLiteral("DESCRIPTION")), qPrintable(pages[p]));
    }
    const QString all = pages.join(QLatin1Char('\n'));
    for (int i = 1; i <= 80; ++i) {
        QCOMPARE(all.count(QStringLiteral("Illustration %1:").arg(i)), 1);
    }
    QVERIFY(pages.last().contains(QStringLiteral("Balance due")));
    saveGalleryPng(pdf, QStringLiteral("M46-books-invoice-page2.png"), 1);
}

void TestBooksIoForms::otherFormsRender()
{
    CompanyFile &f = *m_company.file;
    Sales sales(f);
    BusinessDocument doc;
    doc.partyId = m_company.customer;
    doc.date = QDate(2026, 3, 22);
    DocumentLine line;
    line.itemId = m_company.itemBrand;
    doc.lines = {line};
    const Result estimate = sales.saveEstimate(doc);
    const Result receipt = sales.createSalesReceipt(doc);
    const Result memo = sales.createCreditMemo(doc);
    QVERIFY(estimate.ok && receipt.ok && memo.ok);
    const QList<QPair<qint64, QString>> forms{{estimate.id, QStringLiteral("ESTIMATE")},
                                              {receipt.id, QStringLiteral("SALES RECEIPT")},
                                              {memo.id, QStringLiteral("CREDIT MEMO")}};
    for (const auto &[id, title] : forms) {
        const QString pdf = m_dir.filePath(title + QStringLiteral(".pdf"));
        QString error;
        QVERIFY2(FormRenderer::writeDocumentPdf(f, id, pdf, &error), qPrintable(error));
        const QString text = pdfPages(pdf).join(QLatin1Char('\n'));
        QVERIFY2(text.contains(title), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("Brand identity system")));
        QVERIFY(!text.contains(QStringLiteral("PAYMENT INSTRUCTIONS"))); // invoices and statements only
    }
    // A bill has no customer-facing form.
    PrintableDocument unused;
    QVERIFY(!buildPrintableDocument(f, m_company.payment, &unused));
}

void TestBooksIoForms::statementRenders()
{
    CompanyFile &f = *m_company.file;
    const QString pdf = m_dir.filePath(QStringLiteral("statement.pdf"));
    QString error;
    QVERIFY2(FormRenderer::writeStatementPdf(f, m_company.customer, QDate(2026, 3, 1), QDate(2026, 3, 31), pdf, &error),
             qPrintable(error));
    const QString text = pdfPages(pdf).join(QLatin1Char('\n'));
    const QLocale loc(QLocale::English, QLocale::UnitedStates);
    const Money due = Sales(f).customerBalance(m_company.customer, QDate(2026, 3, 31));
    for (const QString &expected : {QStringLiteral("STATEMENT"), QStringLiteral("Balance forward"),
                                    QStringLiteral("Amount due"), QStringLiteral("CURRENT"), QStringLiteral("OVER 90 DAYS"),
                                    due.format(loc)}) {
        QVERIFY2(text.contains(expected), qPrintable(QStringLiteral("missing \"%1\" in:\n%2").arg(expected, text)));
    }
    saveGalleryPng(pdf, QStringLiteral("M46-books-statement.png"));
    PrintableDocument bad;
    QVERIFY(!buildPrintableStatement(f, m_company.vendor, QDate(2026, 3, 1), QDate(2026, 3, 31), &bad));
}

void TestBooksIoForms::voidDocumentsAreNotPrinted()
{
    CompanyFile &f = *m_company.file;
    BusinessDocument doc;
    doc.partyId = m_company.customer;
    doc.date = QDate(2026, 3, 25);
    DocumentLine line;
    line.itemId = m_company.itemIllustration;
    doc.lines = {line};
    const Result created = Sales(f).createInvoice(doc);
    QVERIFY(Ledger(f).voidDocument(created.id, QStringLiteral("duplicate")).ok);
    PrintableDocument printable;
    QString error;
    QVERIFY(!buildPrintableDocument(f, created.id, &printable, &error));
    QVERIFY(error.contains(QStringLiteral("void")));
}

void TestBooksIoForms::samePainterDrivesAPrinter()
{
    // The bridge the app uses for Print / Print Preview: a QPrinter is a
    // QPagedPaintDevice, and the same painter yields the same pages.
    PrintableDocument printable;
    QVERIFY(buildPrintableDocument(*m_company.file, m_company.invoice, &printable));
    const auto callback = asPrintCallback<QPrinter>(FormRenderer::painter(printable));
    const QString pdf = m_dir.filePath(QStringLiteral("printer.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(pdf);
    printer.setPageSize(QPageSize(QPageSize::Letter));
    QPainter painter(&printer);
    callback(&printer, &painter);
    painter.end();
    const QStringList pages = pdfPages(pdf);
    QCOMPARE(pages.size(), 1);
    QVERIFY(pages.first().contains(QStringLiteral("Balance due")));
    QVERIFY(pages.first().contains(QStringLiteral("Harbor & Pine Coffee Co.")));
}

QTEST_MAIN(TestBooksIoForms)
#include "tst_books_io_forms.moc"
