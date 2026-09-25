// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MercuryClient: the two Mercury API calls QindaBooks makes (M48, D-048),
// asynchronous over Qt Network. Read-only: a read-only token is enough and
// is what docs/books.md tells the user to create.
//
// AGENT-CONTRACT (Mercury API, docs.mercury.com/reference, checked
// 2026-09-25; the accounts page was last updated 2026-04-22):
//  * Base URL https://api.mercury.com/api/v1; auth `Authorization: Bearer
//    <token>` (the token as Mercury shows it, `secret-token:` prefix and
//    all).
//  * GET /accounts → {accounts: [{id, name, nickname, kind, type, status,
//    accountNumber, availableBalance, currentBalance, …}], page: {…}}.
//    Balances are JSON numbers in dollars (0.01 precision).
//  * GET /account/{id}/transactions?start=YYYY-MM-DD&limit=&offset=
//    &order=asc → {total, transactions: [{id, amount (negative = debit),
//    status (pending | sent | cancelled | failed | reversed | blocked),
//    kind, createdAt, postedAt (null until posted), counterpartyName,
//    bankDescription, note, externalMemo, checkNumber, …}]}. `start`
//    defaults to 30 days ago on Mercury's side, so it is always sent.
// The fake server in tests/books_connect serves exactly these shapes.

#include "connect_http.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;

namespace QindaOffice::Books::Connect {

struct MercuryAccount {
    QString id;
    QString name;     // nickname when set, else name
    QString kind;     // "checking", "savings", … as Mercury names it
    QString status;
    QString lastFour; // of the account number
    qint64 available = 0; // minor units
    qint64 current = 0;
};

struct MercuryTransaction {
    QString id;
    qint64 amount = 0; // minor units, + in / − out (Mercury's own sign)
    QString status;
    QString kind;
    QDateTime createdAt;
    QDateTime postedAt; // invalid until posted
    QString counterpartyName;
    QString bankDescription;
    QString note;
    QString externalMemo;
    QString checkNumber;
    [[nodiscard]] bool posted() const { return status == QLatin1String("sent") && postedAt.isValid(); }
};

struct MercuryPage {
    int total = 0;
    QList<MercuryTransaction> transactions;
};

class MercuryClient : public QObject {
    Q_OBJECT
public:
    // `token` is used only for the Authorization header and redaction.
    MercuryClient(QNetworkAccessManager *manager, QUrl baseUrl, QString token, QObject *parent = nullptr);
    [[nodiscard]] static QUrl defaultBaseUrl();

    using AccountsDone = std::function<void(const ApiResponse &, const QList<MercuryAccount> &)>;
    using PageDone = std::function<void(const ApiResponse &, const MercuryPage &)>;

    void accounts(AccountsDone done);
    void transactions(const QString &accountId, const QDate &start, int offset, int limit, PageDone done);

private:
    void get(const QString &path, const QList<QPair<QString, QString>> &query,
             std::function<void(const ApiResponse &)> done);

    QPointer<QNetworkAccessManager> m_manager;
    QUrl m_base;
    QString m_token;
};

// A JSON money number ("1234.56" as a double) → minor units for a currency
// with `exponent` digits, exactly: false when it carries more fraction
// digits than the currency has (never rounded).
bool jsonMoneyToMinor(const QJsonValue &value, int exponent, qint64 *minor);

} // namespace QindaOffice::Books::Connect
