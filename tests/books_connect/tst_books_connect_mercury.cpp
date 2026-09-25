// SPDX-License-Identifier: GPL-3.0-or-later
// M48 Mercury gates: a token is validated before it is stored (a refused one
// never is, and never shows up in an error); linking creates or reuses the
// bank account; sync imports POSTED transactions only, a pending one once it
// posts, across pages, and nothing on a second run; plain http to anything
// but loopback is refused; disconnect removes the token and keeps history;
// no token in the company file, results or logs. Plus the v3 storage rules
// (connections, company uuid, payment links).

#include "books_connect_fixture.h"

#include "connect_http.h"
#include "sql_util.h"

#include <QUuid>

using namespace BooksConnectTest;

class TestMercury : public QObject {
    Q_OBJECT

    static void seed(FakeBankApi &api)
    {
        api.mercuryAccounts = {mercuryAccount(QStringLiteral("acc-1"), QStringLiteral("Mercury Checking"),
                                              QStringLiteral("202201234"), 12345.67),
                               mercuryAccount(QStringLiteral("acc-2"), QStringLiteral("Mercury Savings"),
                                              QStringLiteral("202205678"), 500.0)};
        api.mercuryTransactions[QStringLiteral("acc-1")] = {
            mercuryTxn(QStringLiteral("t1"), 2500.00, QStringLiteral("sent"), QStringLiteral("2026-03-02T15:00:00Z"),
                       QStringLiteral("2026-03-02T16:00:00Z"), QStringLiteral("Acme Corp"), QStringLiteral("ACME CORP ACH")),
            mercuryTxn(QStringLiteral("t2"), -42.18, QStringLiteral("sent"), QStringLiteral("2026-03-05T15:00:00Z"),
                       QStringLiteral("2026-03-06T16:00:00Z"), QStringLiteral("Office Depot"), QStringLiteral("OFFICE DEPOT #12")),
            mercuryTxn(QStringLiteral("t3"), -120.00, QStringLiteral("pending"), QStringLiteral("2026-03-28T15:00:00Z"),
                       QString(), QStringLiteral("Adobe"), QStringLiteral("ADOBE *CREATIVE CLD")),
            mercuryTxn(QStringLiteral("t4"), -99.00, QStringLiteral("failed"), QStringLiteral("2026-03-10T15:00:00Z"),
                       QString(), QStringLiteral("Landlord"), QStringLiteral("RENT")),
            mercuryTxn(QStringLiteral("t5"), -0.35, QStringLiteral("sent"), QStringLiteral("2026-03-12T15:00:00Z"),
                       QStringLiteral("2026-03-12T16:00:00Z"), QStringLiteral("Mercury"), QStringLiteral("WIRE FEE")),
        };
    }

private slots:
    void initTestCase() { installLogCapture(); }

    void storageRules()
    {
        Fixture fx;
        const QString uuid = fx.uuid();
        QVERIFY(!QUuid::fromString(uuid).isNull());
        QCOMPARE(fx.uuid(), uuid); // stable
        Connections connections(fx.f());
        ConnectionRecord record;
        record.provider = Provider::Stripe;
        record.settings = QJsonObject{{QStringLiteral("note"), QStringLiteral("sk_live_abcdefghijklmnop")}};
        QVERIFY(!connections.save(record).ok); // a token-shaped value never lands in the file
        record.settings = QJsonObject{{QStringLiteral("x"), QStringLiteral("secret-token:mercury_x")}};
        QVERIFY(!connections.save(record).ok);
        record.provider = QStringLiteral("paypal");
        record.settings = {};
        QVERIFY(!connections.save(record).ok);
        record.provider = Provider::Stripe;
        QVERIFY(connections.save(record).ok);
        QVERIFY(connections.get(Provider::Stripe).has_value());
        QVERIFY(connections.remove(Provider::Stripe).ok);
        QVERIFY(!connections.get(Provider::Stripe).has_value());
        // A payment link is current only while its amount is the open balance.
        const qint64 customer = fx.customer(QStringLiteral("Acme"));
        const qint64 invoice = fx.invoice(customer, 50000);
        PaymentLinkRecord link{0, invoice, Provider::Stripe, 50000, QStringLiteral("USD"),
                               QStringLiteral("https://buy.stripe.com/test_x"), QStringLiteral("plink_x"), {}};
        QVERIFY(connections.recordPaymentLink(link).ok);
        QCOMPARE(connections.currentPaymentLink(invoice)->url, link.url);
        ReceivePaymentInput pay{customer, QDate(2026, 3, 5), fx.f().money(10000), 0, {}, {}, {{invoice, fx.f().money(10000)}}};
        QVERIFY(Sales(fx.f()).receivePayment(pay).ok);
        QVERIFY(!connections.currentPaymentLink(invoice).has_value()); // stale now
        QVERIFY(connections.latestPaymentLink(invoice).has_value());
    }

    void tokenIsValidatedBeforeItIsStored()
    {
        Fixture fx;
        seed(fx.api);
        const QString wrong = QStringLiteral("secret-token:mercury_production_WRONGWRONGWRONG");
        const auto refused = fx.connectMercury(wrong);
        QVERIFY(refused.has_value());
        QVERIFY(!refused->ok);
        QVERIFY2(refused->error.contains(QStringLiteral("401")), qPrintable(refused->error));
        QVERIFY(!refused->error.contains(wrong));
        QVERIFY(fx.secrets->keys().isEmpty());
        QVERIFY(!Connections(fx.f()).get(Provider::Mercury).has_value());

        const auto accepted = fx.connectMercury(fx.api.mercuryToken);
        QVERIFY(accepted.has_value() && accepted->ok);
        QCOMPARE(accepted->accounts.size(), 2);
        QCOMPARE(accepted->accounts.first().lastFour, QStringLiteral("1234"));
        QCOMPARE(accepted->accounts.first().available, 1234567);
        QVERIFY(fx.secrets->contains(fx.uuid() + QStringLiteral("/mercury")));
        QCOMPARE(fx.controller->secretState(Provider::Mercury), ConnectionsController::SecretState::Present);
        QCOMPARE(fx.api.requestsTo(QStringLiteral("/accounts")).last().headers.value("authorization"),
                 "Bearer " + fx.api.mercuryToken.toUtf8());
        QVERIFY(!fx.fileBytes().contains(fx.api.mercuryToken.toUtf8()));
        QVERIFY(!fx.fileBytes().contains("secret-token"));
    }

    void linkingCreatesOrReusesTheBankAccount()
    {
        Fixture fx;
        seed(fx.api);
        QVERIFY(fx.connectMercury(fx.api.mercuryToken)->ok);
        const Result created = fx.controller->linkMercuryAccount(QStringLiteral("acc-1"), 0);
        QVERIFY2(created.ok, qPrintable(created.error));
        const std::optional<Account> account = ChartOfAccounts(fx.f()).account(created.id);
        QCOMPARE(account->name, QStringLiteral("Mercury Checking ••1234"));
        QCOMPARE(account->type, AccountType::Bank);
        const Result reused = fx.controller->linkMercuryAccount(QStringLiteral("acc-2"), fx.account(QStringLiteral("1010")));
        QVERIFY(reused.ok);
        QCOMPARE(reused.id, fx.account(QStringLiteral("1010")));
        QVERIFY(!fx.controller->linkMercuryAccount(QStringLiteral("acc-2"), fx.account(QStringLiteral("6500"))).ok);
        QVERIFY(!fx.controller->linkMercuryAccount(QStringLiteral("nope"), 0).ok);
        const MercurySettings settings =
            MercurySettings::fromJson(Connections(fx.f()).get(Provider::Mercury)->settings);
        QCOMPARE(settings.links.value(QStringLiteral("acc-1")), created.id);
        QCOMPARE(settings.startDate, kToday.addDays(-kDefaultHistoryDays));
        QVERIFY(fx.controller->linkMercuryAccount(QStringLiteral("acc-2"), -1).ok); // unlink
        QVERIFY(!MercurySettings::fromJson(Connections(fx.f()).get(Provider::Mercury)->settings)
                     .links.contains(QStringLiteral("acc-2")));
    }

    void syncImportsPostedOnlyAcrossPagesOnce()
    {
        Fixture fx;
        seed(fx.api);
        fx.api.pageCap = 2; // five fixtures → three pages
        QVERIFY(fx.connectMercury(fx.api.mercuryToken)->ok);
        const qint64 bank = fx.controller->linkMercuryAccount(QStringLiteral("acc-1"), 0).id;
        const auto first = fx.sync();
        QVERIFY(first.has_value());
        QVERIFY2(first->ok, qPrintable(first->errors.join(QLatin1Char('\n'))));
        QCOMPARE(first->imported, 3); // t1, t2, t5: pending t3 and failed t4 skipped
        const QList<Request> pages = fx.api.requestsTo(QStringLiteral("/account/acc-1/transactions"));
        QCOMPARE(pages.size(), 3);
        QCOMPARE(pages[1].query.value(QStringLiteral("offset")), QStringLiteral("2"));
        QCOMPARE(pages[0].query.value(QStringLiteral("start")), QStringLiteral("2025-12-31")); // 90 days back
        QCOMPARE(pages[0].query.value(QStringLiteral("order")), QStringLiteral("asc"));
        const QList<BankFeedEntry> feed = Banking(fx.f()).feed(bank);
        QCOMPARE(feed.size(), 3);
        QCOMPARE(feed[0].fitid, QStringLiteral("t1"));
        QCOMPARE(feed[0].amount.minor(), 250000);
        QCOMPARE(feed[0].payee, QStringLiteral("Acme Corp"));
        QCOMPARE(feed[0].memo, QStringLiteral("ACME CORP ACH"));
        QCOMPARE(feed[1].amount.minor(), -4218);
        QCOMPARE(feed[1].date, QDate(2026, 3, 6)); // the posted date, not the created one
        QCOMPARE(feed[2].amount.minor(), -35);

        const auto again = fx.sync();
        QVERIFY(again->ok);
        QCOMPARE(again->imported, 0); // idempotent
        // The next run starts from the cursor, reaching back far enough
        // for the pending transaction.
        QCOMPARE(fx.api.requestsTo(QStringLiteral("/account/acc-1/transactions")).last().query.value(
                     QStringLiteral("start")),
                 QStringLiteral("2026-03-05"));

        // The pending card charge posts: imported exactly once.
        QJsonArray &txns = fx.api.mercuryTransactions[QStringLiteral("acc-1")];
        QJsonObject t3 = txns[2].toObject();
        t3.insert(QStringLiteral("status"), QStringLiteral("sent"));
        t3.insert(QStringLiteral("postedAt"), QStringLiteral("2026-03-30T09:00:00Z"));
        txns[2] = t3;
        QCOMPARE(fx.sync()->imported, 1);
        QCOMPARE(fx.sync()->imported, 0);
        QCOMPARE(Banking(fx.f()).feed(bank).size(), 4);
        const auto last = Connections(fx.f()).get(Provider::Mercury);
        QVERIFY(last->lastSync.isValid());
        QVERIFY(last->lastError.isEmpty());
    }

    void rulesRunOnSyncedLines()
    {
        Fixture fx;
        seed(fx.api);
        QVERIFY(fx.connectMercury(fx.api.mercuryToken)->ok);
        const qint64 bank = fx.controller->linkMercuryAccount(QStringLiteral("acc-1"), 0).id;
        BankRule rule;
        rule.name = QStringLiteral("Office supplies");
        rule.payeeContains = QStringLiteral("office depot");
        rule.accountId = fx.account(QStringLiteral("6400"));
        QVERIFY(Banking(fx.f()).createRule(rule).ok);
        QVERIFY(fx.sync()->ok);
        const QList<BankFeedEntry> feed = Banking(fx.f()).feed(bank);
        QCOMPARE(feed[1].suggestedAccountId, fx.account(QStringLiteral("6400")));
        QVERIFY(Banking(fx.f()).acceptSuggestion(feed[1].id).ok);
        QCOMPARE(fx.balance(fx.account(QStringLiteral("6400"))), 4218);
    }

    void plainHttpElsewhereIsRefused()
    {
        QString why;
        QVERIFY(!ConnectHttp::urlAllowed(QUrl(QStringLiteral("http://api.mercury.com/api/v1")), &why));
        QVERIFY(why.contains(QStringLiteral("https")));
        QVERIFY(ConnectHttp::urlAllowed(QUrl(QStringLiteral("http://127.0.0.1:9/x"))));
        QVERIFY(ConnectHttp::urlAllowed(QUrl(QStringLiteral("https://api.mercury.com/api/v1"))));
        QVERIFY(!ConnectHttp::urlAllowed(QUrl(QStringLiteral("http://127.0.0.1.example.com/"))));
        Fixture fx;
        seed(fx.api);
        fx.controller->setEndpoints(QUrl(QStringLiteral("http://example.invalid/api/v1")), fx.api.stripeBase());
        const auto outcome = fx.connectMercury(fx.api.mercuryToken);
        QVERIFY(outcome.has_value() && !outcome->ok);
        QVERIFY2(outcome->error.contains(QStringLiteral("refused")), qPrintable(outcome->error));
        QVERIFY(fx.api.requests.isEmpty());
        QVERIFY(fx.secrets->keys().isEmpty());
    }

    void disconnectRemovesTheTokenAndKeepsHistory()
    {
        Fixture fx;
        seed(fx.api);
        QVERIFY(fx.connectMercury(fx.api.mercuryToken)->ok);
        const qint64 bank = fx.controller->linkMercuryAccount(QStringLiteral("acc-1"), 0).id;
        QVERIFY(fx.sync()->ok);
        const auto done = await<bool>([&](auto callback) {
            fx.controller->disconnect(Provider::Mercury, [callback](bool ok, const QString &) { callback(ok); });
        });
        QVERIFY(done.has_value() && *done);
        QVERIFY(fx.secrets->keys().isEmpty());
        QVERIFY(!Connections(fx.f()).get(Provider::Mercury).has_value());
        QCOMPARE(Banking(fx.f()).feed(bank).size(), 3); // imported history stays
        QVERIFY(ChartOfAccounts(fx.f()).account(bank).has_value());
        const auto nothing = fx.sync();
        QVERIFY(!nothing->ok);
    }

    void missingTokenIsReported()
    {
        Fixture fx;
        seed(fx.api);
        QVERIFY(fx.connectMercury(fx.api.mercuryToken)->ok);
        fx.controller->linkMercuryAccount(QStringLiteral("acc-1"), 0);
        // The keyring lost it (another machine, a wiped wallet …).
        fx.secrets->remove(fx.uuid() + QStringLiteral("/mercury"), [](const SecretOutcome &) {});
        QTRY_VERIFY(fx.secrets->keys().isEmpty());
        const auto report = fx.sync();
        QVERIFY(!report->ok);
        QVERIFY(report->errors.join(QLatin1Char(' ')).contains(QStringLiteral("token again")));
        QCOMPARE(fx.controller->secretState(Provider::Mercury), ConnectionsController::SecretState::Missing);
    }

    void onlyUsdBooksConnect()
    {
        Fixture fx;
        seed(fx.api);
        CompanySettings s = fx.f().settings();
        s.currency = QStringLiteral("EUR"); // nothing posted yet, so allowed
        QVERIFY(fx.f().setSettings(s).ok);
        const auto outcome = fx.connectMercury(fx.api.mercuryToken);
        QVERIFY(outcome.has_value() && !outcome->ok);
        QVERIFY(outcome->error.contains(QStringLiteral("US dollars")));
        QVERIFY(fx.api.requests.isEmpty());
    }

    void tokensNeverReachLogs()
    {
        const QString logs = capturedLogs().join(QLatin1Char('\n'));
        QVERIFY(!logs.contains(QStringLiteral("secret-token:")));
        QVERIFY(!logs.contains(QStringLiteral("WRONGWRONG")));
    }
};

BOOKS_CONNECT_TEST_MAIN(TestMercury)
#include "tst_books_connect_mercury.moc"
