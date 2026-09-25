// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService banking, reconciliation, reports, dashboard and
// housekeeping (M47): statement import (CSV, OFX, a CSV that needs a
// mapping), the feed (categorize, exclude, rules, matches), reconciling to
// zero, report maps with row kinds/keys/drill-down, fiscal-year date
// presets, KPIs/trends/ratios, PDF/CSV/IIF export, backups, e-mailing an
// invoice through QindaMail and recurring invoices.

#include "books_app_test_support.h"

using namespace BooksAppTest;

namespace {

QString isoOf(const QVariantMap &range, const char *key)
{
    return range.value(QString::fromLatin1(key)).toString();
}

qint64 totalOf(const QVariantMap &report, const QString &key, int column = -1)
{
    for (const QVariant &v : report.value(QStringLiteral("rows")).toList()) {
        const QVariantMap row = v.toMap();
        if (row.value(QStringLiteral("key")).toString() == key) {
            const QVariantList cells = row.value(QStringLiteral("cells")).toList();
            return cells.value(column < 0 ? cells.size() - 1 : column).toMap().value(QStringLiteral("value")).toLongLong();
        }
    }
    return -1;
}

} // namespace

class QindaBooksBankingTest final : public QObject {
    Q_OBJECT

private slots:
    void csvImportFeedRulesAndMatches()
    {
        Company c;
        const qint64 checking = c.account(QStringLiteral("1000"));
        const QString csv = c.dir.filePath(QStringLiteral("march.csv"));
        QVERIFY(writeFile(csv, "Date,Description,Amount\n"
                               "2026-03-02,COFFEE SHOP,-4.50\n"
                               "2026-03-03,ACME CORP PAYMENT,600.00\n"
                               "2026-03-05,OFFICE DEPOT #12,-120.00\n"));
        QVariantMap imported = c.service.importStatement(checking, QUrl::fromLocalFile(csv));
        QVERIFY2(ok(imported), qPrintable(imported.value(QStringLiteral("error")).toString()));
        QCOMPARE(imported.value(QStringLiteral("imported")).toInt(), 3);
        QCOMPARE(imported.value(QStringLiteral("format")).toString(), QStringLiteral("csv"));
        imported = c.service.importStatement(checking, QUrl::fromLocalFile(csv));
        QCOMPARE(imported.value(QStringLiteral("imported")).toInt(), 0);
        QCOMPARE(imported.value(QStringLiteral("duplicates")).toInt(), 3);
        QCOMPARE(find(c.service.bankAccounts(), QStringLiteral("id"), checking).value(QStringLiteral("unreviewed")).toInt(), 3);
        QCOMPARE(find(c.service.bankAccounts(), QStringLiteral("id"), checking).value(QStringLiteral("type")).toString(),
                 QStringLiteral("Bank"));

        QVariantList feed = c.service.feed(checking, QStringLiteral("new"));
        QCOMPARE(feed.size(), 3);
        const QVariantMap coffee = find(feed, QStringLiteral("payee"), QStringLiteral("COFFEE SHOP"));
        QCOMPARE(coffee.value(QStringLiteral("amount")).toLongLong(), -450);
        // The locale's own negative currency form (en_US: accounting parentheses).
        QCOMPARE(coffee.value(QStringLiteral("amountText")).toString(), c.service.formatMoney(-450));
        QVERIFY(coffee.value(QStringLiteral("amountText")).toString().contains(QStringLiteral("4.50")));
        QVERIFY(ok(c.service.categorize(coffee.value(QStringLiteral("id")).toLongLong(),
                                        {{QStringLiteral("accountId"), c.account(QStringLiteral("6300"))}})));
        QCOMPARE(c.service.feed(checking, QStringLiteral("new")).size(), 2);
        QCOMPARE(c.service.feed(checking, QStringLiteral("categorized")).size(), 1);

        const qint64 depot = find(feed, QStringLiteral("payee"), QStringLiteral("OFFICE DEPOT #12")).value(QStringLiteral("id")).toLongLong();
        QVERIFY(ok(c.service.exclude(depot)));
        QCOMPARE(c.service.feed(checking, QStringLiteral("excluded")).size(), 1);
        QVERIFY(ok(c.service.restore(depot)));
        const QVariantMap rule = c.service.saveRule({{QStringLiteral("name"), QStringLiteral("Supplies")},
                                                     {QStringLiteral("payeeContains"), QStringLiteral("office depot")},
                                                     {QStringLiteral("accountId"), c.account(QStringLiteral("6400"))},
                                                     {QStringLiteral("bankAccountId"), checking}});
        QVERIFY2(ok(rule), qPrintable(rule.value(QStringLiteral("error")).toString()));
        QCOMPARE(c.service.rules().first().toMap().value(QStringLiteral("accountName")).toString(),
                 QStringLiteral("Office Supplies and Software"));
        QCOMPARE(c.service.applyRules(checking), 1);
        const QVariantMap suggested = find(c.service.feed(checking, QStringLiteral("new")), QStringLiteral("id"), depot);
        QCOMPARE(suggested.value(QStringLiteral("suggestion")).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("rule"));
        QVERIFY(ok(c.service.acceptSuggestion(depot)));
        // Edit a rule: same shape, replaces it.
        QVariantMap edited = c.service.rules().first().toMap();
        edited.insert(QStringLiteral("payeeContains"), QStringLiteral("depot"));
        QVERIFY(ok(c.service.saveRule(edited)));
        QCOMPARE(c.service.rules().size(), 1);
        QCOMPARE(c.service.rules().first().toMap().value(QStringLiteral("payeeContains")).toString(), QStringLiteral("depot"));

        // The customer's payment recorded in the books → a match suggestion.
        const qint64 acme = c.customer(QStringLiteral("Acme Corp"));
        const qint64 inv = c.invoice(acme, QStringLiteral("600.00"));
        QVERIFY(ok(c.service.receivePayment({{QStringLiteral("customerId"), acme},
                                             {QStringLiteral("date"), QStringLiteral("2026-03-03")},
                                             {QStringLiteral("depositAccountId"), checking},
                                             {QStringLiteral("applications"), QVariantList{QVariantMap{
                                                  {QStringLiteral("invoiceId"), inv}, {QStringLiteral("amount"), QStringLiteral("600.00")}}}}})));
        const QVariantMap payment = c.service.feed(checking, QStringLiteral("new")).first().toMap();
        QCOMPARE(payment.value(QStringLiteral("suggestion")).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("match"));
        QVERIFY(ok(c.service.acceptSuggestion(payment.value(QStringLiteral("id")).toLongLong())));
        QCOMPARE(c.service.feed(checking, QStringLiteral("matched")).size(), 1);
        QCOMPARE(c.service.feed(checking, QStringLiteral("new")).size(), 0);
        QCOMPARE(c.service.feed(checking, QStringLiteral("all")).size(), 3);
    }

    void ofxImportAndCsvThatNeedsAMapping()
    {
        Company c;
        const qint64 card = c.account(QStringLiteral("2100"));
        const QString ofx = c.dir.filePath(QStringLiteral("card.ofx"));
        QVERIFY(writeFile(ofx, "OFXHEADER:100\nDATA:OFXSGML\nVERSION:102\n\n<OFX><CREDITCARDMSGSRSV1><CCSTMTTRNRS><CCSTMTRS>"
                               "<CURDEF>USD<BANKTRANLIST>"
                               "<STMTTRN><TRNTYPE>DEBIT<DTPOSTED>20260304<TRNAMT>-42.10<FITID>A1<NAME>HARDWARE STORE</STMTTRN>"
                               "<STMTTRN><TRNTYPE>CREDIT<DTPOSTED>20260320<TRNAMT>100.00<FITID>A2<NAME>PAYMENT THANK YOU</STMTTRN>"
                               "</BANKTRANLIST></CCSTMTRS></CCSTMTTRNRS></CREDITCARDMSGSRSV1></OFX>\n"));
        const QVariantMap imported = c.service.importStatement(card, QUrl::fromLocalFile(ofx));
        QVERIFY2(ok(imported), qPrintable(imported.value(QStringLiteral("error")).toString()));
        QCOMPARE(imported.value(QStringLiteral("format")).toString(), QStringLiteral("ofx"));
        QCOMPARE(imported.value(QStringLiteral("imported")).toInt(), 2);
        QCOMPARE(find(c.service.bankAccounts(), QStringLiteral("id"), card).value(QStringLiteral("type")).toString(),
                 QStringLiteral("CreditCard"));

        // Nothing here looks like a date to the detector: ask for columns.
        const QString odd = c.dir.filePath(QStringLiteral("odd.csv"));
        QVERIFY(writeFile(odd, "Ref,Posted,Thing,Sum\nX1,2026/3/7 noon,Paint,-15.25\nX2,2026/3/9 noon,Brushes,-7.00\n"));
        const QVariantMap needs = c.service.importStatement(c.account(QStringLiteral("1000")), QUrl::fromLocalFile(odd));
        QVERIFY(!ok(needs));
        QVERIFY(needs.value(QStringLiteral("needsMapping")).toBool());
        QCOMPARE(needs.value(QStringLiteral("columns")).toStringList(),
                 (QStringList{QStringLiteral("Ref"), QStringLiteral("Posted"), QStringLiteral("Thing"), QStringLiteral("Sum")}));
        const QString fixed = c.dir.filePath(QStringLiteral("fixed.csv"));
        QVERIFY(writeFile(fixed, "Ref,Posted,Thing,Sum\nX1,07.03.2026,Paint,-15.25\nX2,09.03.2026,Brushes,-7.00\n"));
        const QVariantMap mapped = c.service.importStatement(
            c.account(QStringLiteral("1000")), QUrl::fromLocalFile(fixed),
            {{QStringLiteral("date"), 1}, {QStringLiteral("amount"), 3}, {QStringLiteral("payee"), 2},
             {QStringLiteral("memo"), -1}, {QStringLiteral("dateFormat"), QStringLiteral("dd.MM.yyyy")}});
        QVERIFY2(ok(mapped), qPrintable(mapped.value(QStringLiteral("error")).toString()));
        QCOMPARE(mapped.value(QStringLiteral("imported")).toInt(), 2);
    }

    void reconcileToZeroAndFinish()
    {
        Company c;
        const qint64 checking = c.account(QStringLiteral("1000"));
        QVERIFY(ok(c.service.transfer({{QStringLiteral("fromAccountId"), c.account(QStringLiteral("1010"))},
                                       {QStringLiteral("toAccountId"), checking},
                                       {QStringLiteral("amount"), QStringLiteral("1000.00")},
                                       {QStringLiteral("date"), QStringLiteral("2026-03-01")}})));
        QVERIFY(ok(c.service.saveDocument({{QStringLiteral("kind"), QStringLiteral("check")},
                                           {QStringLiteral("paymentAccountId"), checking},
                                           {QStringLiteral("reference"), QStringLiteral("1001")},
                                           {QStringLiteral("date"), QStringLiteral("2026-03-10")},
                                           {QStringLiteral("lines"), QVariantList{QVariantMap{
                                                {QStringLiteral("accountId"), c.account(QStringLiteral("6900"))},
                                                {QStringLiteral("amount"), QStringLiteral("200.00")}}}}})));
        const QVariantMap started = c.service.startReconciliation(checking, QStringLiteral("2026-03-31"), QStringLiteral("800.00"));
        QVERIFY2(ok(started), qPrintable(started.value(QStringLiteral("error")).toString()));
        const qint64 session = started.value(QStringLiteral("sessionId")).toLongLong();
        QVERIFY(!ok(c.service.startReconciliation(checking, QStringLiteral("2026-03-31"), QStringLiteral("800.00"))));
        QCOMPARE(c.service.reconciliationInProgress(checking).value(QStringLiteral("sessionId")).toLongLong(), session);
        const QVariantList candidates = c.service.reconcileCandidates(session);
        QCOMPARE(candidates.size(), 2);
        QVariantMap state = c.service.reconcileState(session);
        QVERIFY(!state.value(QStringLiteral("canFinish")).toBool());
        QCOMPARE(state.value(QStringLiteral("differenceText")).toString(), QStringLiteral("$800.00"));
        QVERIFY(!ok(c.service.finishReconciliation(session)));
        QVariantList ids;
        for (const QVariant &v : candidates) {
            ids << v.toMap().value(QStringLiteral("lineId"));
        }
        QVERIFY(ok(c.service.markCleared(session, ids, true)));
        state = c.service.reconcileState(session);
        QCOMPARE(state.value(QStringLiteral("clearedBalanceText")).toString(), QStringLiteral("$800.00"));
        QCOMPARE(state.value(QStringLiteral("endingBalanceText")).toString(), QStringLiteral("$800.00"));
        QCOMPARE(state.value(QStringLiteral("difference")).toLongLong(), 0);
        QVERIFY(state.value(QStringLiteral("canFinish")).toBool());
        QVERIFY(ok(c.service.finishReconciliation(session)));
        const QVariantList reg = c.service.accountRegister(checking, QString(), QString());
        QCOMPARE(reg.size(), 2);
        QVERIFY(reg.first().toMap().value(QStringLiteral("reconciled")).toBool());
        QCOMPARE(reg.first().toMap().value(QStringLiteral("depositText")).toString(), QStringLiteral("$1,000.00"));
        QCOMPARE(reg.last().toMap().value(QStringLiteral("paymentText")).toString(), QStringLiteral("$200.00"));
        QCOMPARE(reg.last().toMap().value(QStringLiteral("balanceText")).toString(), QStringLiteral("$800.00"));
        QCOMPARE(find(c.service.bankAccounts(), QStringLiteral("id"), checking).value(QStringLiteral("lastReconciled")).toString(),
                 QStringLiteral("2026-03-31"));
        QVERIFY(ok(c.service.runReport(QStringLiteral("reconciliation"), {{QStringLiteral("accountId"), checking}})));
        QVERIFY(ok(c.service.undoLastReconciliation(checking)));
        QVERIFY(!c.service.accountRegister(checking, QString(), QString()).first().toMap().value(QStringLiteral("reconciled")).toBool());
    }

    void reportMapsKeysAndDrillDown()
    {
        Company c;
        const qint64 acme = c.customer(QStringLiteral("Acme"));
        c.invoice(acme, QStringLiteral("1000.00"), QStringLiteral("2026-01-15"));
        c.bill(c.vendor(QStringLiteral("Landlord")), QStringLiteral("400.00"), QStringLiteral("6500"), QStringLiteral("2026-02-01"));
        const QVariantList catalog = c.service.reportCatalog();
        QVERIFY(catalog.size() >= 20);
        const QVariantMap pnlEntry = find(catalog, QStringLiteral("id"), QStringLiteral("profit_and_loss"));
        QCOMPARE(pnlEntry.value(QStringLiteral("options")).toStringList(),
                 (QStringList{QStringLiteral("from"), QStringLiteral("to"), QStringLiteral("basis"),
                              QStringLiteral("comparison"), QStringLiteral("columns")}));
        QCOMPARE(find(catalog, QStringLiteral("id"), QStringLiteral("balance_sheet")).value(QStringLiteral("options")).toStringList().first(),
                 QStringLiteral("asOf"));

        const QVariantMap pnl = c.service.runReport(QStringLiteral("profit_and_loss"),
                                                    {{QStringLiteral("from"), QStringLiteral("2026-01-01")},
                                                     {QStringLiteral("to"), QStringLiteral("2026-03-31")}});
        QVERIFY(ok(pnl));
        QCOMPARE(pnl.value(QStringLiteral("title")).toString(), QStringLiteral("Profit and Loss"));
        QCOMPARE(pnl.value(QStringLiteral("company")).toString(), QStringLiteral("Test Books LLC"));
        QCOMPARE(totalOf(pnl, QStringLiteral("total:net_income")), 60000);
        QSet<QString> kinds;
        QVariantMap drill;
        for (const QVariant &v : pnl.value(QStringLiteral("rows")).toList()) {
            const QVariantMap row = v.toMap();
            kinds.insert(row.value(QStringLiteral("kind")).toString());
            if (row.value(QStringLiteral("key")).toString() == QStringLiteral("account:%1").arg(c.account(QStringLiteral("4100")))) {
                drill = row.value(QStringLiteral("drill")).toMap();
                QCOMPARE(row.value(QStringLiteral("cells")).toList().last().toMap().value(QStringLiteral("text")).toString(),
                         QStringLiteral("1,000.00"));
            }
        }
        QVERIFY(kinds.contains(QStringLiteral("section")) && kinds.contains(QStringLiteral("account"))
                && kinds.contains(QStringLiteral("total")));
        QCOMPARE(drill.value(QStringLiteral("accountId")).toLongLong(), c.account(QStringLiteral("4100")));
        const QVariantList txns = c.service.drillDown(drill);
        QCOMPARE(txns.size(), 1);
        QCOMPARE(txns.first().toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("invoice"));
        QCOMPARE(txns.first().toMap().value(QStringLiteral("amountText")).toString(), QStringLiteral("$1,000.00"));
        QVERIFY(txns.first().toMap().value(QStringLiteral("documentId")).toLongLong() != 0);

        const QVariantMap bs = c.service.runReport(QStringLiteral("balance_sheet"), {{QStringLiteral("asOf"), QStringLiteral("2026-03-31")}});
        QVERIFY(totalOf(bs, QStringLiteral("total:assets")) > 0);
        QCOMPARE(totalOf(bs, QStringLiteral("total:assets")), totalOf(bs, QStringLiteral("total:liabilities_and_equity")));
        const QVariantMap cash = c.service.runReport(QStringLiteral("profit_and_loss"),
                                                     {{QStringLiteral("from"), QStringLiteral("2026-01-01")},
                                                      {QStringLiteral("to"), QStringLiteral("2026-03-31")},
                                                      {QStringLiteral("basis"), QStringLiteral("cash")}});
        QCOMPARE(totalOf(cash, QStringLiteral("total:net_income")), 0); // nothing paid yet
        const QVariantMap months = c.service.runReport(QStringLiteral("profit_and_loss"),
                                                       {{QStringLiteral("from"), QStringLiteral("2026-01-01")},
                                                        {QStringLiteral("to"), QStringLiteral("2026-03-31")},
                                                        {QStringLiteral("columns"), QStringLiteral("months")}});
        QCOMPARE(months.value(QStringLiteral("columns")).toList().size(), 5); // label, Jan, Feb, Mar, total

        const QVariantMap aging = c.service.runReport(QStringLiteral("ar_aging_summary"), {{QStringLiteral("asOf"), QStringLiteral("2026-03-31")}});
        QVariantMap acmeRow;
        for (const QVariant &v : aging.value(QStringLiteral("rows")).toList()) {
            if (v.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("party:%1").arg(acme)) {
                acmeRow = v.toMap();
            }
        }
        QVERIFY(!acmeRow.isEmpty());
        QVERIFY(!c.service.drillDown(acmeRow.value(QStringLiteral("drill")).toMap()).isEmpty());
        QVERIFY(!ok(c.service.runReport(QStringLiteral("no_such_report"), {})));
        QVERIFY(!ok(c.service.runReport(QStringLiteral("profit_and_loss"), {{QStringLiteral("from"), QStringLiteral("2026-04-01")},
                                                                           {QStringLiteral("to"), QStringLiteral("2026-03-01")}})));
    }

    void rangePresetsFollowTheFiscalYear()
    {
        Company c;
        const auto check = [&c](const char *name, const char *from, const char *to) {
            const QVariantMap r = c.service.rangePreset(QString::fromLatin1(name));
            QCOMPARE(isoOf(r, "from"), QString::fromLatin1(from));
            QCOMPARE(isoOf(r, "to"), QString::fromLatin1(to));
            QVERIFY(!r.value(QStringLiteral("label")).toString().isEmpty());
        };
        check("thisMonth", "2026-03-01", "2026-03-31");
        check("lastMonth", "2026-02-01", "2026-02-28");
        check("thisQuarter", "2026-01-01", "2026-03-31");
        check("lastQuarter", "2025-10-01", "2025-12-31");
        check("thisYear", "2026-01-01", "2026-12-31");
        check("lastYear", "2025-01-01", "2025-12-31");
        check("yearToDate", "2026-01-01", "2026-03-31");
        check("last12Months", "2025-04-01", "2026-03-31");
        // A fiscal year starting in July.
        QVERIFY(ok(c.service.saveSettings({{QStringLiteral("fiscalYearStartMonth"), 7}})));
        check("thisYear", "2025-07-01", "2026-06-30");
        check("lastYear", "2024-07-01", "2025-06-30");
        check("yearToDate", "2025-07-01", "2026-03-31");
        check("thisQuarter", "2026-01-01", "2026-03-31"); // Q3 of FY2026
        check("lastQuarter", "2025-10-01", "2025-12-31");
        c.service.setToday(QDate(2026, 7, 15));
        check("thisQuarter", "2026-07-01", "2026-09-30");
        check("lastQuarter", "2026-04-01", "2026-06-30");
        check("yearToDate", "2026-07-01", "2026-07-15");
        check("lastYear", "2025-07-01", "2026-06-30");
        QVERIFY(c.service.rangePreset(QStringLiteral("bogus")).value(QStringLiteral("from")).toString().isEmpty());
    }

    void dashboardNumbersAgreeWithTheReports()
    {
        Company c;
        const qint64 acme = c.customer(QStringLiteral("Acme"));
        c.invoice(acme, QStringLiteral("1000.00"), QStringLiteral("2026-01-15"));
        c.invoice(acme, QStringLiteral("500.00"), QStringLiteral("2026-03-20"));
        c.bill(c.vendor(QStringLiteral("Landlord")), QStringLiteral("400.00"), QStringLiteral("6500"), QStringLiteral("2026-03-02"));
        const QVariantMap k = c.service.kpis(QString());
        QVERIFY(ok(k));
        QCOMPARE(k.value(QStringLiteral("incomeMtd")).toLongLong(), 50000);
        QCOMPARE(k.value(QStringLiteral("expensesMtd")).toLongLong(), 40000);
        QCOMPARE(k.value(QStringLiteral("netMtd")).toLongLong(), 10000);
        QCOMPARE(k.value(QStringLiteral("incomeYtd")).toLongLong(), 150000);
        QCOMPARE(k.value(QStringLiteral("netYtdText")).toString(), QStringLiteral("$1,100.00"));
        QCOMPARE(k.value(QStringLiteral("openInvoices")).toLongLong(), 150000);
        QCOMPARE(k.value(QStringLiteral("openInvoiceCount")).toInt(), 2);
        QCOMPARE(k.value(QStringLiteral("overdueInvoices")).toInt(), 1); // the January one
        QCOMPARE(k.value(QStringLiteral("overdueAmount")).toLongLong(), 100000);
        QCOMPARE(k.value(QStringLiteral("unpaidBills")).toLongLong(), 40000);
        QCOMPARE(k.value(QStringLiteral("cash")).toLongLong(), 0);
        const QVariantMap pnl = c.service.runReport(QStringLiteral("profit_and_loss"),
                                                    {{QStringLiteral("from"), QStringLiteral("2026-01-01")},
                                                     {QStringLiteral("to"), QStringLiteral("2026-03-31")}});
        QCOMPARE(totalOf(pnl, QStringLiteral("total:net_income")), k.value(QStringLiteral("netYtd")).toLongLong());

        const QVariantList trends = c.service.monthlyTrends(3);
        QCOMPARE(trends.size(), 3);
        const QVariantMap march = trends.last().toMap();
        QCOMPARE(march.value(QStringLiteral("month")).toString(), QStringLiteral("2026-03"));
        QCOMPARE(march.value(QStringLiteral("income")).toLongLong(), 50000);
        QCOMPARE(march.value(QStringLiteral("incomeText")).toString(), QStringLiteral("$500.00"));
        QCOMPARE(march.value(QStringLiteral("incomeChart")).typeId(), int(QMetaType::Double));
        QCOMPARE(march.value(QStringLiteral("incomeChart")).toDouble(), 500.0);
        const QVariantList mix = c.service.expenseBreakdown(QStringLiteral("2026-01-01"), QStringLiteral("2026-03-31"));
        QCOMPARE(mix.size(), 1);
        QCOMPARE(mix.first().toMap().value(QStringLiteral("shareText")).toString(), QStringLiteral("100%"));
        const QVariantList ratios = c.service.ratios(QString());
        QCOMPARE(ratios.size(), 7);
        const QVariantMap margin = find(ratios, QStringLiteral("id"), QStringLiteral("net_margin"));
        QCOMPARE(margin.value(QStringLiteral("valueText")).toString(), QStringLiteral("73.3%"));
        QVERIFY(margin.value(QStringLiteral("good")).toBool());
        QVERIFY(!margin.value(QStringLiteral("definition")).toString().isEmpty());
    }

    void exportsBackupsAndEmail()
    {
        Company c;
        const qint64 acme = c.customer(QStringLiteral("Acme Corp"), QStringLiteral("ap@acme.test"));
        const qint64 inv = c.invoice(acme, QStringLiteral("1000.00"));
        const auto size = [](const QString &path) { return QFileInfo(path).size(); };
        const QString pdf = c.dir.filePath(QStringLiteral("pnl.pdf"));
        QVERIFY(ok(c.service.exportReport(QStringLiteral("profit_and_loss"), {}, QUrl::fromLocalFile(pdf), QStringLiteral("pdf"))));
        QVERIFY(size(pdf) > 1000);
        const QString csv = c.dir.filePath(QStringLiteral("pnl.csv"));
        QVERIFY(ok(c.service.exportReport(QStringLiteral("profit_and_loss"), {}, QUrl::fromLocalFile(csv), QStringLiteral("csv"))));
        QFile csvFile(csv);
        QVERIFY(csvFile.open(QIODevice::ReadOnly));
        QVERIFY(csvFile.readAll().contains("1000.00"));
        const QString form = c.dir.filePath(QStringLiteral("inv.pdf"));
        QVERIFY(ok(c.service.exportDocumentPdf(inv, QUrl::fromLocalFile(form))));
        QVERIFY(size(form) > 1000);
        const QString iif = c.dir.filePath(QStringLiteral("books.iif"));
        QVERIFY(ok(c.service.exportIif(QUrl::fromLocalFile(iif))));
        QVERIFY(size(iif) > 0);
        const QString journal = c.dir.filePath(QStringLiteral("journal.csv"));
        QVERIFY(ok(c.service.exportJournalCsv(QUrl::fromLocalFile(journal))));
        QVERIFY(size(journal) > 0);
        const QString customers = c.dir.filePath(QStringLiteral("customers.csv"));
        QVERIFY(ok(c.service.exportListCsv(QStringLiteral("customers"), QUrl::fromLocalFile(customers))));
        Company other;
        const QVariantMap imported = other.service.importListCsv(QStringLiteral("customers"), QUrl::fromLocalFile(customers));
        QVERIFY2(ok(imported), qPrintable(imported.value(QStringLiteral("error")).toString()));
        QCOMPARE(imported.value(QStringLiteral("added")).toInt(), 1);
        QVERIFY(!ok(other.service.importListCsv(QStringLiteral("planets"), QUrl::fromLocalFile(customers))));

        const QVariantMap backup = c.service.backupNow();
        QVERIFY2(ok(backup), qPrintable(backup.value(QStringLiteral("error")).toString()));
        QVERIFY(QFileInfo::exists(backup.value(QStringLiteral("path")).toString()));
        const QString copy = c.dir.filePath(QStringLiteral("copy.qbooks"));
        QVERIFY(ok(c.service.backupTo(QUrl::fromLocalFile(copy))));
        QVERIFY(!ok(c.service.backupTo(QUrl::fromLocalFile(copy)))); // never over a file

        QString program;
        QStringList arguments;
        c.service.setLauncher([&](const QString &p, const QStringList &a) {
            program = p;
            arguments = a;
            return true;
        });
        QSignalSpy notified(&c.service, &BooksService::notify);
        const QVariantMap mail = c.service.emailDocument(inv);
        QVERIFY2(ok(mail), qPrintable(mail.value(QStringLiteral("error")).toString()));
        QCOMPARE(program, QStringLiteral("qindamail"));
        // M48: the PDF travels as `--attach <path>` before the mailto: URL.
        QCOMPARE(arguments.size(), 3);
        QCOMPARE(arguments.at(0), QStringLiteral("--attach"));
        QCOMPARE(arguments.at(1), mail.value(QStringLiteral("path")).toString());
        QVERIFY(arguments.at(2).startsWith(QStringLiteral("mailto:ap@acme.test?subject=")));
        QVERIFY(arguments.at(2).contains(QStringLiteral("Invoice%20INV-1001%20from%20Test%20Books%20LLC")));
        QCOMPARE(mail.value(QStringLiteral("subject")).toString(), QStringLiteral("Invoice INV-1001 from Test Books LLC"));
        QVERIFY(mail.value(QStringLiteral("attached")).toBool());
        QVERIFY(size(mail.value(QStringLiteral("path")).toString()) > 1000);
        QCOMPARE(notified.count(), 1); // "INV-1001.pdf is attached"
    }

    void recurringInvoicesGenerateOnce()
    {
        Company c;
        const qint64 acme = c.customer(QStringLiteral("Acme"));
        const QVariantMap saved = c.service.saveRecurring(
            {{QStringLiteral("name"), QStringLiteral("Retainer")},
             {QStringLiteral("frequency"), QStringLiteral("monthly")},
             {QStringLiteral("start"), QStringLiteral("2026-01-31")},
             {QStringLiteral("document"), QVariantMap{{QStringLiteral("partyId"), acme},
                                                      {QStringLiteral("lines"), QVariantList{QVariantMap{
                                                           {QStringLiteral("description"), QStringLiteral("Retainer")},
                                                           {QStringLiteral("accountId"), c.account(QStringLiteral("4100"))},
                                                           {QStringLiteral("amount"), QStringLiteral("250.00")}}}}}}});
        QVERIFY2(ok(saved), qPrintable(saved.value(QStringLiteral("error")).toString()));
        QCOMPARE(c.service.dueRecurringCount(), 3); // Jan 31, Feb 28, Mar 31
        QCOMPARE(c.service.recurringTemplates().first().toMap().value(QStringLiteral("nextDate")).toString(),
                 QStringLiteral("2026-01-31"));
        QCOMPARE(c.service.generateDueRecurring(), 3);
        QCOMPARE(c.service.generateDueRecurring(), 0);
        QCOMPARE(c.service.dueRecurringCount(), 0);
        QCOMPARE(c.service.documents({{QStringLiteral("kind"), QStringLiteral("invoice")}}).size(), 3);
        QVERIFY(ok(c.service.saveRecurring({{QStringLiteral("id"), idOf(saved)}, {QStringLiteral("active"), false}})));
        QVERIFY(!c.service.recurringTemplates().first().toMap().value(QStringLiteral("active")).toBool());
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksBankingTest)

#include "tst_qindabooks_banking.moc"
