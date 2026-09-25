// SPDX-License-Identifier: GPL-3.0-or-later
// M48 Stripe gates: a key is validated before it is stored and never echoed;
// connecting creates the clearing and fee accounts; a charge books gross +
// fee (fee auto-posted); a refund and a dispute wait for review; a payment
// carrying this company's invoice metadata pays the invoice; another
// company's is left alone; a payout becomes a transfer and the bank's STRIPE
// deposit is matched to it in either import order, never booked as income;
// re-syncing adds nothing; pages are followed; payment links send the price,
// metadata and idempotency keys Stripe expects; the books balance and the
// clearing account equals Stripe's balance.

#include "books_connect_fixture.h"

using namespace BooksConnectTest;

class TestStripe : public QObject {
    Q_OBJECT

    // Charge 100.00 (fee 3.20), a walk-in charge 50.00 (fee 1.75), a refund
    // of 20.00, a dispute adjustment −15.00 (fee 15.00), a Radar fee −0.05,
    // a payout of 60.00. Net: 96.80 + 48.25 − 20.00 − 30.00 − 0.05 − 60.00.
    static void seedHistory(FakeBankApi &api, const QString &companyUuid, qint64 invoiceId)
    {
        const QJsonObject invoiceMeta{{QStringLiteral("qindabooks_invoice"), QString::number(invoiceId)},
                                      {QStringLiteral("qindabooks_company"), companyUuid},
                                      {QStringLiteral("invoice_number"), QStringLiteral("INV-1001")}};
        api.stripeTransactions = {
            stripeTxn(QStringLiteral("txn_1"), QStringLiteral("charge"), 10000, 320, unixNoon(QDate(2026, 3, 10)),
                      stripeCharge(QStringLiteral("ch_1"), QStringLiteral("Acme Corp"), invoiceMeta)),
            stripeTxn(QStringLiteral("txn_2"), QStringLiteral("charge"), 5000, 175, unixNoon(QDate(2026, 3, 11)),
                      stripeCharge(QStringLiteral("ch_2"), QStringLiteral("Walk-in Buyer"))),
            stripeTxn(QStringLiteral("txn_3"), QStringLiteral("refund"), -2000, 0, unixNoon(QDate(2026, 3, 12)),
                      QJsonObject{{QStringLiteral("id"), QStringLiteral("re_1")}, {QStringLiteral("object"), QStringLiteral("refund")},
                                  {QStringLiteral("charge"), QStringLiteral("ch_2")}},
                      QStringLiteral("REFUND FOR CHARGE (Walk-in Buyer)")),
            stripeTxn(QStringLiteral("txn_4"), QStringLiteral("adjustment"), -1500, 1500, unixNoon(QDate(2026, 3, 13)),
                      QJsonObject{{QStringLiteral("id"), QStringLiteral("du_1")}, {QStringLiteral("object"), QStringLiteral("dispute")}},
                      QStringLiteral("Chargeback withdrawal for ch_2")),
            stripeTxn(QStringLiteral("txn_5"), QStringLiteral("stripe_fee"), -5, 0, unixNoon(QDate(2026, 3, 14)),
                      QJsonObject(), QStringLiteral("Radar (2026-03-14)")),
            stripeTxn(QStringLiteral("txn_6"), QStringLiteral("payout"), -6000, 0, unixNoon(QDate(2026, 3, 15)),
                      stripePayout(QStringLiteral("po_1"), unixNoon(QDate(2026, 3, 17))), QStringLiteral("STRIPE PAYOUT")),
        };
        // What Stripe would report after that history: 96.80 + 48.25 − 20
        // − 30 − 0.05 − 60 = 35.00, split available/pending.
        api.stripeBalance.insert(QStringLiteral("available"),
                                 QJsonArray{QJsonObject{{QStringLiteral("amount"), 2500}, {QStringLiteral("currency"), QStringLiteral("usd")}}});
        api.stripeBalance.insert(QStringLiteral("pending"),
                                 QJsonArray{QJsonObject{{QStringLiteral("amount"), 1000}, {QStringLiteral("currency"), QStringLiteral("usd")}}});
    }

    static QJsonObject mercuryPayoutDeposit()
    {
        return mercuryTxn(QStringLiteral("m-po1"), 60.00, QStringLiteral("sent"), QStringLiteral("2026-03-17T14:00:00Z"),
                          QStringLiteral("2026-03-17T15:00:00Z"), QStringLiteral("Stripe"),
                          QStringLiteral("STRIPE TRANSFER ST-X1Y2Z3"));
    }

    struct Books {
        Fixture fx;
        qint64 customer = 0;
        qint64 invoice = 0;
        qint64 mercuryBank = 0;
        qint64 clearing = 0;
        explicit Books(bool withMercury)
        {
            customer = fx.customer(QStringLiteral("Acme Corp"));
            invoice = fx.invoice(customer, 10000);
            fx.api.mercuryAccounts = {mercuryAccount(QStringLiteral("acc-1"), QStringLiteral("Mercury Checking"),
                                                     QStringLiteral("202201234"), 60.0)};
            if (withMercury) {
                fx.connectMercury(fx.api.mercuryToken);
                mercuryBank = fx.controller->linkMercuryAccount(QStringLiteral("acc-1"), 0).id;
            }
        }
    };

private slots:
    void initTestCase() { installLogCapture(); }

    void keyIsValidatedAndNeverEchoed()
    {
        Fixture fx;
        const QString wrong = QStringLiteral("rk_test_51WrongKeyWrongKeyWrongKey");
        const auto refused = fx.connectStripe(wrong);
        QVERIFY(refused.has_value() && !refused->ok);
        QVERIFY2(refused->error.contains(QStringLiteral("401")), qPrintable(refused->error));
        QVERIFY(!refused->error.contains(wrong));
        QVERIFY(fx.secrets->keys().isEmpty());
        QVERIFY(!Connections(fx.f()).get(Provider::Stripe));

        const auto accepted = fx.connectStripe(fx.api.stripeKey);
        QVERIFY2(accepted.has_value() && accepted->ok, accepted ? qPrintable(accepted->error) : "timeout");
        QCOMPARE(accepted->accountName, QStringLiteral("Sloom Test Shop"));
        QVERIFY(!accepted->livemode);
        const StripeSettings s = StripeSettings::fromJson(Connections(fx.f()).get(Provider::Stripe)->settings);
        QCOMPARE(ChartOfAccounts(fx.f()).account(s.clearingAccountId)->name, QStringLiteral("Stripe (USD)"));
        QCOMPARE(ChartOfAccounts(fx.f()).account(s.clearingAccountId)->type, AccountType::Bank);
        QCOMPARE(ChartOfAccounts(fx.f()).account(s.feeAccountId)->name, QStringLiteral("Merchant Fees"));
        QCOMPARE(s.currency, QStringLiteral("usd"));
        QVERIFY(s.autoPostFees && s.autoPostPayouts && s.autoApplyInvoices);
        QVERIFY(fx.secrets->contains(fx.uuid() + QStringLiteral("/stripe")));
        QVERIFY(!fx.fileBytes().contains(fx.api.stripeKey.toUtf8()));
        // Reconnecting keeps the accounts (no second "Stripe (USD)").
        QVERIFY(fx.connectStripe(fx.api.stripeKey)->ok);
        QCOMPARE(StripeSettings::fromJson(Connections(fx.f()).get(Provider::Stripe)->settings).clearingAccountId,
                 s.clearingAccountId);
    }

    void historyBooksTheAccountingModel_data()
    {
        QTest::addColumn<bool>("mercuryFirst");
        QTest::newRow("mercury first") << true;
        QTest::newRow("stripe first") << false;
    }

    void historyBooksTheAccountingModel()
    {
        QFETCH(bool, mercuryFirst);
        Books b(true);
        Fixture &fx = b.fx;
        fx.api.pageCap = 2; // six transactions → three pages
        seedHistory(fx.api, fx.uuid(), b.invoice);
        if (mercuryFirst) {
            fx.api.mercuryTransactions[QStringLiteral("acc-1")] = {mercuryPayoutDeposit()};
            QVERIFY(fx.sync()->ok); // Mercury only: the STRIPE deposit waits
            QCOMPARE(Banking(fx.f()).feed(b.mercuryBank, FeedStatus::New).size(), 1);
            QVERIFY(fx.connectStripe(fx.api.stripeKey)->ok);
        } else {
            QVERIFY(fx.connectStripe(fx.api.stripeKey)->ok);
        }
        const StripeSettings s = StripeSettings::fromJson(Connections(fx.f()).get(Provider::Stripe)->settings);
        QCOMPARE(s.payoutAccountId, b.mercuryBank); // payouts land in the linked checking account
        b.clearing = s.clearingAccountId;
        const auto report = fx.sync();
        QVERIFY2(report->ok, qPrintable(report->errors.join(QLatin1Char('\n'))));
        QCOMPARE(report->applied, 1);
        if (!mercuryFirst) {
            // The transfer's bank side waits uncleared until the bank
            // reports the deposit; the next sync matches it.
            QCOMPARE(Banking(fx.f()).feed(b.mercuryBank).size(), 0);
            fx.api.mercuryTransactions[QStringLiteral("acc-1")] = {mercuryPayoutDeposit()};
            const auto later = fx.sync();
            QVERIFY(later->ok);
            QCOMPARE(later->imported, 1);
            QCOMPARE(later->matched, 1);
        }
        const QList<Request> pages = fx.api.requestsTo(QStringLiteral("/v1/balance_transactions"));
        QVERIFY(pages.size() >= 3);
        QCOMPARE(pages[1].query.value(QStringLiteral("starting_after")), QStringLiteral("txn_5"));
        QCOMPARE(pages[0].query.value(QStringLiteral("expand[]")), QStringLiteral("data.source"));

        Banking banking(fx.f());
        // Gross, fee, refund, dispute (+ its fee), Radar fee, payout: 9 lines.
        const QList<BankFeedEntry> feed = banking.feed(b.clearing);
        QCOMPARE(feed.size(), 9);
        const auto byFitid = [&feed](const QString &fitid) {
            for (const BankFeedEntry &e : feed) {
                if (e.fitid == fitid) return e;
            }
            return BankFeedEntry{};
        };
        QCOMPARE(byFitid(QStringLiteral("txn_1")).amount.minor(), 10000);
        QCOMPARE(byFitid(QStringLiteral("txn_1")).status, FeedStatus::Matched);     // invoice payment
        QCOMPARE(byFitid(QStringLiteral("txn_1:fee")).amount.minor(), -320);
        QCOMPARE(byFitid(QStringLiteral("txn_1:fee")).status, FeedStatus::Categorized); // fee auto-posted
        QCOMPARE(byFitid(QStringLiteral("txn_2")).status, FeedStatus::New);         // plain sale: review
        QCOMPARE(byFitid(QStringLiteral("txn_2")).payee, QStringLiteral("Walk-in Buyer"));
        QCOMPARE(byFitid(QStringLiteral("txn_3")).amount.minor(), -2000);
        QCOMPARE(byFitid(QStringLiteral("txn_3")).status, FeedStatus::New);         // refund: review
        QCOMPARE(byFitid(QStringLiteral("txn_4")).status, FeedStatus::New);         // dispute: review
        QCOMPARE(byFitid(QStringLiteral("txn_4:fee")).status, FeedStatus::Categorized);
        QCOMPARE(byFitid(QStringLiteral("txn_5")).status, FeedStatus::Categorized); // Radar fee
        QCOMPARE(byFitid(QStringLiteral("txn_6")).status, FeedStatus::Matched);     // payout transfer
        QCOMPARE(fx.balance(fx.account(QStringLiteral("Merchant Fees"))), 320 + 175 + 1500 + 5);

        // The invoice is paid through Stripe.
        Documents documents(fx.f());
        QCOMPARE(documents.status(*documents.load(b.invoice), kToday), DocumentStatus::Paid);

        // The payout's bank deposit is matched to the transfer, not income.
        const QList<BankFeedEntry> bank = banking.feed(b.mercuryBank);
        QCOMPARE(bank.size(), 1);
        QCOMPARE(bank[0].status, FeedStatus::Matched);
        QCOMPARE(fx.balance(b.mercuryBank), 6000);
        QCOMPARE(fx.balance(fx.account(QStringLiteral("4100"))), 10000); // only the invoice's income

        // Re-syncing both adds and books nothing.
        const auto again = fx.sync();
        QVERIFY(again->ok);
        QCOMPARE(again->imported, 0);
        QCOMPARE(again->applied, 0);
        QCOMPARE(banking.feed(b.clearing).size(), 9);

        // Review the rest (sale → Sales, refund → Sales, dispute → Sales),
        // then the clearing account equals Stripe's own balance.
        for (const QString &fitid : {QStringLiteral("txn_2"), QStringLiteral("txn_3"), QStringLiteral("txn_4")}) {
            QVERIFY(banking.categorize(byFitid(fitid).id, fx.account(QStringLiteral("4000"))).ok);
        }
        QCOMPARE(fx.balance(b.clearing), 2500 + 1000);
        QVERIFY(fx.trialBalanceBalances());
    }

    void foreignInvoiceMetadataIsLeftForReview()
    {
        Books b(false);
        Fixture &fx = b.fx;
        QVERIFY(fx.connectStripe(fx.api.stripeKey)->ok);
        const QJsonObject otherCompany{{QStringLiteral("qindabooks_invoice"), QString::number(b.invoice)},
                                       {QStringLiteral("qindabooks_company"), QStringLiteral("another-company-uuid")},
                                       {QStringLiteral("invoice_number"), QStringLiteral("INV-1001")}};
        fx.api.stripeTransactions = {stripeTxn(QStringLiteral("txn_9"), QStringLiteral("charge"), 10000, 320,
                                               unixNoon(QDate(2026, 3, 20)),
                                               stripeCharge(QStringLiteral("ch_9"), QStringLiteral("Acme"), otherCompany))};
        const auto report = fx.sync();
        QVERIFY(report->ok); // a note, not a broken connection
        QCOMPARE(report->applied, 0);
        QVERIFY(report->errors.join(QLatin1Char(' ')).contains(QStringLiteral("another company")));
        Documents documents(fx.f());
        QCOMPARE(documents.status(*documents.load(b.invoice), kToday), DocumentStatus::Open);
        const qint64 clearing = StripeSettings::fromJson(Connections(fx.f()).get(Provider::Stripe)->settings).clearingAccountId;
        QCOMPARE(Banking(fx.f()).feed(clearing, FeedStatus::New).size(), 1);
    }

    void paymentLinkCarriesTheInvoice()
    {
        Books b(false);
        Fixture &fx = b.fx;
        QVERIFY(fx.connectStripe(fx.api.stripeKey)->ok);
        const auto made = await<PaymentLinkOutcome>([&](auto done) { fx.controller->createPaymentLink(b.invoice, done); });
        QVERIFY2(made.has_value() && made->ok, made ? qPrintable(made->error) : "timeout");
        QCOMPARE(made->url, QStringLiteral("https://buy.stripe.com/test_fake1"));
        const Request price = fx.api.requestsTo(QStringLiteral("/v1/prices")).last();
        QCOMPARE(price.form.value(QStringLiteral("unit_amount")), QStringLiteral("10000"));
        QCOMPARE(price.form.value(QStringLiteral("currency")), QStringLiteral("usd"));
        QVERIFY(price.form.value(QStringLiteral("product_data[name]")).startsWith(QStringLiteral("Invoice INV-1001")));
        const QString uuid = fx.uuid();
        const QByteArray priceKey = price.headers.value("idempotency-key");
        QCOMPARE(priceKey, QStringLiteral("qindabooks-%1-inv%2-10000usd-price").arg(uuid).arg(b.invoice).toUtf8());
        const Request link = fx.api.requestsTo(QStringLiteral("/v1/payment_links")).last();
        QCOMPARE(link.form.value(QStringLiteral("line_items[0][price]")), QStringLiteral("price_fake1"));
        QCOMPARE(link.form.value(QStringLiteral("line_items[0][quantity]")), QStringLiteral("1"));
        QCOMPARE(link.form.value(QStringLiteral("metadata[qindabooks_invoice]")), QString::number(b.invoice));
        QCOMPARE(link.form.value(QStringLiteral("metadata[qindabooks_company]")), uuid);
        QCOMPARE(link.form.value(QStringLiteral("metadata[invoice_number]")), QStringLiteral("INV-1001"));
        QCOMPARE(link.form.value(QStringLiteral("payment_intent_data[metadata][qindabooks_invoice]")), QString::number(b.invoice));
        QCOMPARE(link.form.value(QStringLiteral("payment_intent_data[metadata][qindabooks_company]")), uuid);
        QCOMPARE(link.form.value(QStringLiteral("restrictions[completed_sessions][limit]")), QStringLiteral("1"));
        QVERIFY(link.headers.value("idempotency-key").endsWith("-link"));
        QCOMPARE(link.headers.value("content-type"), QByteArray("application/x-www-form-urlencoded"));
        QCOMPARE(Connections(fx.f()).currentPaymentLink(b.invoice)->url, made->url);

        // Asking again for the same amount reuses the link, no network.
        const int before = int(fx.api.requests.size());
        const auto reused = await<PaymentLinkOutcome>([&](auto done) { fx.controller->createPaymentLink(b.invoice, done); });
        QVERIFY(reused->ok && reused->reused);
        QCOMPARE(fx.api.requests.size(), before);

        // Part-paid: a new link for the new balance, with a new key.
        ReceivePaymentInput pay{b.customer, QDate(2026, 3, 5), fx.f().money(4000), 0, {}, {}, {{b.invoice, fx.f().money(4000)}}};
        QVERIFY(Sales(fx.f()).receivePayment(pay).ok);
        const auto fresh = await<PaymentLinkOutcome>([&](auto done) { fx.controller->createPaymentLink(b.invoice, done); });
        QVERIFY(fresh->ok && !fresh->reused);
        QCOMPARE(fresh->url, QStringLiteral("https://buy.stripe.com/test_fake2"));
        QCOMPARE(fx.api.requestsTo(QStringLiteral("/v1/prices")).last().form.value(QStringLiteral("unit_amount")),
                 QStringLiteral("6000"));
        QVERIFY(fx.api.requestsTo(QStringLiteral("/v1/prices")).last().headers.value("idempotency-key") != priceKey);
        QVERIFY(!fx.fileBytes().contains(fx.api.stripeKey.toUtf8()));
    }

    void paymentLinkNeedsAnOpenInvoice()
    {
        Books b(false);
        Fixture &fx = b.fx;
        const auto notConnected = await<PaymentLinkOutcome>([&](auto done) { fx.controller->createPaymentLink(b.invoice, done); });
        QVERIFY(!notConnected->ok);
        QVERIFY(fx.connectStripe(fx.api.stripeKey)->ok);
        const auto bogus = await<PaymentLinkOutcome>([&](auto done) { fx.controller->createPaymentLink(987654, done); });
        QVERIFY(!bogus->ok);
    }

    void tokensNeverReachLogs()
    {
        const QString logs = capturedLogs().join(QLatin1Char('\n'));
        QVERIFY(!logs.contains(QStringLiteral("rk_test_")));
        QVERIFY(!logs.contains(QStringLiteral("secret-token:")));
    }
};

BOOKS_CONNECT_TEST_MAIN(TestStripe)
#include "tst_books_connect_stripe.moc"
