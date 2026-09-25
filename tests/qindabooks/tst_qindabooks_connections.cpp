// SPDX-License-Identifier: GPL-3.0-or-later
// M48: BooksService's connection API as the QML calls it — connections(),
// connectMercury / linkMercuryAccount / mercuryLinks, connectStripe /
// stripeSettings / saveStripeSettings, syncConnections →
// connectionSyncFinished, createPaymentLink → paymentLinkFinished (the URL on
// document(id) and in the invoice e-mail, which attaches the PDF), and
// disconnect removing the keyring entry. No result, file or log ever holds a
// token.

#include "books_app_test_support.h"

#include "books_connect/fake_bank_api.h"

#include "connect_secrets.h"
#include "connections.h"
#include "connections_controller.h"

#include <QJsonDocument>
#include <QSignalSpy>

using namespace BooksAppTest;
using namespace BooksConnectTest;
using QindaOffice::Books::Connect::InMemorySecretBackend;

namespace {

QStringList &logs()
{
    static QStringList captured;
    return captured;
}

QtMessageHandler g_previous = nullptr;

void capture(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    logs() << message;
    if (g_previous != nullptr) {
        g_previous(type, context, message);
    }
}

QString str(const char *s)
{
    return QString::fromUtf8(s);
}

QVariantMap byProvider(const QVariantList &list, const QString &provider)
{
    return find(list, str("provider"), provider);
}

} // namespace

class QindaBooksConnectionsTest final : public QObject {
    Q_OBJECT

    QStringList m_results; // every map the API returned, as JSON

    void keep(const QVariant &value)
    {
        m_results << QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
    }

private slots:
    void initTestCase() { g_previous = qInstallMessageHandler(capture); }

    void theWholeFlow()
    {
        Company c;
        QVERIFY(ok(c.created));
        FakeBankApi api;
        api.mercuryAccounts = {mercuryAccount(str("acc-1"), str("Mercury Checking"), str("202201234"), 12345.67)};
        api.mercuryTransactions[str("acc-1")] = {
            mercuryTxn(str("m1"), 250.00, str("sent"), str("2026-03-20T15:00:00Z"), str("2026-03-20T16:00:00Z"),
                       str("Acme Corp"), str("ACME ACH")),
            mercuryTxn(str("m2"), -12.00, str("pending"), str("2026-03-30T15:00:00Z"), QString(), str("Cafe"), str("CAFE"))};
        auto *controller = c.service.connectionsController();
        controller->setEndpoints(api.mercuryBase(), api.stripeBase());
        const auto secrets = std::dynamic_pointer_cast<InMemorySecretBackend>(controller->secrets());
        QVERIFY2(secrets != nullptr, "the tests run on the in-memory keyring");

        QVariantList list = c.service.connections();
        keep(list);
        QCOMPARE(list.size(), 2);
        QCOMPARE(byProvider(list, str("mercury")).value(str("status")).toString(), str("disconnected"));
        QCOMPARE(byProvider(list, str("stripe")).value(str("status")).toString(), str("disconnected"));

        // ---- Mercury ----
        QVariantMap mercury = c.service.connectMercury(str("secret-token:mercury_production_NOPE0000"));
        keep(mercury);
        QVERIFY(!ok(mercury));
        QVERIFY(secrets->keys().isEmpty());
        mercury = c.service.connectMercury(api.mercuryToken);
        keep(mercury);
        QVERIFY2(ok(mercury), qPrintable(mercury.value(str("error")).toString()));
        const QVariantMap account = mercury.value(str("accounts")).toList().value(0).toMap();
        QCOMPARE(account.value(str("id")).toString(), str("acc-1"));
        QCOMPARE(account.value(str("lastFour")).toString(), str("1234"));
        QCOMPARE(account.value(str("balanceText")).toString(), str("$12,345.67"));
        const QVariantMap linked = c.service.linkMercuryAccount(str("acc-1"), 0);
        keep(linked);
        QVERIFY(ok(linked));
        const QVariantMap link = c.service.mercuryLinks().value(0).toMap();
        QCOMPARE(link.value(str("booksAccountId")).toLongLong(), idOf(linked));
        QCOMPARE(link.value(str("booksAccountName")).toString(), str("Mercury Checking ••1234"));
        QVERIFY(link.value(str("linked")).toBool());

        // ---- Stripe ----
        const QVariantMap stripe = c.service.connectStripe(api.stripeKey);
        keep(stripe);
        QVERIFY2(ok(stripe), qPrintable(stripe.value(str("error")).toString()));
        QCOMPARE(stripe.value(str("accountName")).toString(), str("Sloom Test Shop"));
        QVariantMap settings = c.service.stripeSettings();
        keep(settings);
        QVERIFY(settings.value(str("connected")).toBool());
        QCOMPARE(settings.value(str("clearingAccountName")).toString(), str("Stripe (USD)"));
        QCOMPARE(settings.value(str("feeAccountName")).toString(), str("Merchant Fees"));
        QCOMPARE(settings.value(str("payoutAccountId")).toLongLong(), idOf(linked));
        QVERIFY(ok(c.service.saveStripeSettings({{str("autoPostFees"), false}})));
        QVERIFY(!c.service.stripeSettings().value(str("autoPostFees")).toBool());
        QVERIFY(!ok(c.service.saveStripeSettings({{str("clearingAccountId"), c.account(str("6500"))}})));
        QVERIFY(ok(c.service.saveStripeSettings({{str("autoPostFees"), true}})));

        // ---- payment link → document, PDF e-mail ----
        const qint64 customer = c.customer(str("Acme Corp"), str("ap@acme.test"));
        const qint64 invoice = c.invoice(customer, str("150.00"));
        QSignalSpy linked2(&c.service, &BooksService::paymentLinkFinished);
        const QString linkRequest = c.service.createPaymentLink(invoice);
        QVERIFY(!linkRequest.isEmpty());
        QTRY_COMPARE(linked2.count(), 1);
        QCOMPARE(linked2.first().at(0).toString(), linkRequest);
        const QVariantMap made = linked2.first().at(1).toMap();
        keep(made);
        QVERIFY2(made.value(str("ok")).toBool(), qPrintable(made.value(str("error")).toString()));
        const QString url = made.value(str("url")).toString();
        QCOMPARE(c.service.document(invoice).value(str("paymentLinkUrl")).toString(), url);
        QStringList arguments;
        c.service.setLauncher([&arguments](const QString &, const QStringList &a) {
            arguments = a;
            return true;
        });
        const QVariantMap mail = c.service.emailDocument(invoice);
        keep(mail);
        QVERIFY(ok(mail));
        QVERIFY(mail.value(str("attached")).toBool());
        QCOMPARE(arguments.value(0), str("--attach"));
        QVERIFY(QFileInfo::exists(arguments.value(1)));
        QVERIFY2(QUrl::fromPercentEncoding(arguments.value(2).toUtf8()).contains(str("Pay online: ") + url),
                 qPrintable(arguments.value(2)));

        // ---- sync ----
        QSignalSpy synced(&c.service, &BooksService::connectionSyncFinished);
        const QString syncRequest = c.service.syncConnections();
        QVERIFY(c.service.syncingConnections());
        QCOMPARE(byProvider(c.service.connections(), str("mercury")).value(str("status")).toString(), str("syncing"));
        QTRY_COMPARE_WITH_TIMEOUT(synced.count(), 1, 15000);
        QCOMPARE(synced.first().at(0).toString(), syncRequest);
        const QVariantMap result = synced.first().at(1).toMap();
        keep(result);
        QVERIFY2(result.value(str("ok")).toBool(), qPrintable(result.value(str("errors")).toStringList().join(str("\n"))));
        QCOMPARE(result.value(str("imported")).toInt(), 1); // the posted one; pending waits
        QVERIFY(!c.service.syncingConnections());
        list = c.service.connections();
        keep(list);
        QCOMPARE(byProvider(list, str("mercury")).value(str("status")).toString(), str("connected"));
        QVERIFY(!byProvider(list, str("mercury")).value(str("lastSync")).toString().isEmpty());
        QCOMPARE(byProvider(list, str("mercury")).value(str("summary")).toString(), str("1 account(s) linked"));
        QVERIFY(byProvider(list, str("stripe")).value(str("summary")).toString().contains(str("Sloom Test Shop")));
        QCOMPARE(c.service.feed(idOf(linked), str("all")).size(), 1);

        // A second request while one runs fails fast, with its own id.
        const QString first = c.service.syncConnections();
        const QString second = c.service.syncConnections();
        QTRY_COMPARE_WITH_TIMEOUT(synced.count(), 3, 15000);
        QVERIFY(first != second);

        // ---- disconnect ----
        const QString stripeKeyName = QindaOffice::Books::Connections(*c.service.file()).companyUuid() + str("/stripe");
        QVERIFY(secrets->contains(stripeKeyName));
        const QVariantMap gone = c.service.disconnect(str("stripe"));
        keep(gone);
        QVERIFY2(ok(gone), qPrintable(gone.value(str("error")).toString()));
        QVERIFY(!secrets->contains(stripeKeyName));
        QCOMPARE(byProvider(c.service.connections(), str("stripe")).value(str("status")).toString(), str("disconnected"));
        QVERIFY(!c.service.stripeSettings().value(str("connected")).toBool());
        QVERIFY(!ok(c.service.disconnect(str("paypal"))));

        // ---- no token anywhere ----
        const QString everything = m_results.join(str("\n")) + logs().join(str("\n"));
        QVERIFY(!everything.contains(api.stripeKey));
        QVERIFY(!everything.contains(api.mercuryToken));
        QVERIFY(!everything.contains(str("secret-token:")));
        QFile company(c.service.file()->path());
        QVERIFY(company.open(QIODevice::ReadOnly));
        const QByteArray bytes = company.readAll();
        QVERIFY(!bytes.contains(api.stripeKey.toUtf8()));
        QVERIFY(!bytes.contains(api.mercuryToken.toUtf8()));
    }

    void withoutConnectionsTheKeyringIsNeverAsked()
    {
        Company c;
        QVERIFY(ok(c.created));
        const auto secrets = std::dynamic_pointer_cast<InMemorySecretBackend>(c.service.connectionsController()->secrets());
        secrets->setUnavailable(true); // any keyring call would fail loudly
        QSignalSpy synced(&c.service, &BooksService::connectionSyncFinished);
        c.service.syncConnections();
        QTRY_COMPARE(synced.count(), 1);
        QVERIFY(!synced.first().at(1).toMap().value(str("ok")).toBool());
        QCOMPARE(byProvider(c.service.connections(), str("mercury")).value(str("status")).toString(), str("disconnected"));
        secrets->setUnavailable(false);
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksConnectionsTest)
#include "tst_qindabooks_connections.moc"
