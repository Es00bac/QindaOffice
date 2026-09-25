// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the M48 tests: a company file (general chart), a
// ConnectionsController over it with an in-memory keyring, the fake
// Mercury/Stripe server, "today" pinned to 2026-03-31, and a capture of
// every Qt log message (the tests assert no token ever reaches one).

#include "fake_bank_api.h"

#include "accounts.h"
#include "banking.h"
#include "company_file.h"
#include "connections.h"
#include "connections_controller.h"
#include "documents.h"
#include "lists.h"
#include "reports.h"
#include "sales.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>
#include <optional>

namespace BooksConnectTest {

using namespace QindaOffice::Books;
using namespace QindaOffice::Books::Connect;

inline const QDate kToday(2026, 3, 31);

inline QStringList &capturedLogs()
{
    static QStringList logs;
    return logs;
}

inline QtMessageHandler &previousHandler()
{
    static QtMessageHandler handler = nullptr;
    return handler;
}

inline void captureLogs(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    capturedLogs() << message;
    if (previousHandler() != nullptr) {
        previousHandler()(type, context, message);
    }
}

// Call from initTestCase(): QTest installs its own handler in qExec, so the
// capture chains in front of it.
inline void installLogCapture()
{
    previousHandler() = qInstallMessageHandler(captureLogs);
}

// Unix seconds of local noon on `date` (Stripe `created`).
inline qint64 unixNoon(const QDate &date)
{
    return QDateTime(date, QTime(12, 0), QTimeZone::systemTimeZone()).toSecsSinceEpoch();
}

// Runs `start(callback)` and spins the event loop until the callback ran.
template<typename Outcome, typename Start>
std::optional<Outcome> await(Start start, int timeoutMs = 10000)
{
    auto result = std::make_shared<std::optional<Outcome>>();
    start([result](const Outcome &o) { *result = o; });
    static_cast<void>(QTest::qWaitFor([result] { return result->has_value(); }, timeoutMs));
    return *result;
}

struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<CompanyFile> file;
    FakeBankApi api;
    std::shared_ptr<InMemorySecretBackend> secrets = std::make_shared<InMemorySecretBackend>();
    std::unique_ptr<ConnectionsController> controller;

    Fixture()
    {
        CompanySettings s;
        s.legalName = QStringLiteral("Test Books LLC");
        QString error;
        file = CompanyFile::create(dir.filePath(QStringLiteral("company.qbooks")), s, QStringLiteral("general"), &error);
        if (!file) {
            qFatal("could not create the test company: %s", qPrintable(error));
        }
        controller = std::make_unique<ConnectionsController>([this] { return file.get(); }, secrets);
        controller->setEndpoints(api.mercuryBase(), api.stripeBase());
        controller->setToday([] { return kToday; });
    }

    CompanyFile &f() { return *file; }
    qint64 account(const QString &numberOrName) const { return ChartOfAccounts(*file).find(numberOrName); }
    qint64 balance(qint64 accountId) const { return ChartOfAccounts(*file).balance(accountId).minor(); }
    QString uuid() { return Connections(*file).companyUuid(); }

    std::optional<MercuryConnectOutcome> connectMercury(const QString &token)
    {
        return await<MercuryConnectOutcome>([&](auto done) { controller->connectMercury(token, done); });
    }
    std::optional<StripeConnectOutcome> connectStripe(const QString &key)
    {
        return await<StripeConnectOutcome>([&](auto done) { controller->connectStripe(key, done); });
    }
    std::optional<SyncReport> sync()
    {
        return await<SyncReport>([&](auto done) { controller->sync(done); });
    }

    qint64 customer(const QString &name)
    {
        Party p;
        p.kind = PartyKind::Customer;
        p.displayName = name;
        return Lists(*file).createParty(p).id;
    }
    qint64 invoice(qint64 customerId, qint64 cents, const QDate &date = QDate(2026, 3, 1))
    {
        BusinessDocument doc;
        doc.kind = DocKind::Invoice;
        doc.partyId = customerId;
        doc.date = date;
        DocumentLine line;
        line.accountId = account(QStringLiteral("4100"));
        line.description = QStringLiteral("Work");
        line.amount = file->money(cents);
        doc.lines << line;
        return Sales(*file).createInvoice(doc).id;
    }
    // Every byte of the company file (the tests grep it for tokens).
    QByteArray fileBytes() const
    {
        QFile f(file->path());
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    [[nodiscard]] bool trialBalanceBalances() const
    {
        const ReportTable tb = Reports(*file).trialBalance(kToday, Basis::Accrual);
        return tb.money(QStringLiteral("total"), QStringLiteral("debit"))
                   == tb.money(QStringLiteral("total"), QStringLiteral("credit"))
            && file->integrityProblems().isEmpty();
    }
};

} // namespace BooksConnectTest

#define BOOKS_CONNECT_TEST_MAIN(TestClass)                                                 \
    int main(int argc, char *argv[])                                                       \
    {                                                                                      \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                           \
        qputenv("QQO_BOOKS_SECRET_BACKEND", "memory");                                     \
        QCoreApplication app(argc, argv);                                                  \
        TestClass test;                                                                    \
        return QTest::qExec(&test, argc, argv);                                            \
    }
