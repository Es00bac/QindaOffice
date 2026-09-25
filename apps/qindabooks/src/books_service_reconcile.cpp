// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: reconciling a bank or card account with its statement.
#include "books_service.h"

#include "company_file.h"
#include "reconciliation.h"

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

// ---- reconciliation --------------------------------------------------------------

QVariantMap BooksService::startReconciliation(qint64 accountId, const QString &statementDateIso,
                                              const QString &endingBalanceText)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    Reconciliation reconciliation(*c);
    if (const std::optional<ReconciliationSession> open = reconciliation.inProgress(accountId)) {
        QVariantMap out = Format::failure(tr("A reconciliation of this account is already in progress; "
                                             "finish or cancel it first."));
        out.insert(QStringLiteral("sessionId"), open->id);
        out.insert(QStringLiteral("inProgress"), true);
        return out;
    }
    const QDate date = Format::date(statementDateIso);
    if (!date.isValid()) {
        return Format::failure(tr("Enter the statement date."));
    }
    QString error;
    const std::optional<qint64> ending = moneyFormat().parse(endingBalanceText, &error);
    if (!ending) {
        return Format::failure(error);
    }
    const Result r = reconciliation.start(accountId, date, c->money(*ending));
    QVariantMap out = Format::result(r);
    out.insert(QStringLiteral("sessionId"), r.ok ? r.id : 0);
    return out;
}

QVariantMap BooksService::reconciliationInProgress(qint64 accountId) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return {};
    }
    const std::optional<ReconciliationSession> s = Reconciliation(*c).inProgress(accountId);
    if (!s) {
        return {};
    }
    return {{QStringLiteral("sessionId"), s->id},
            {QStringLiteral("statementDate"), Format::isoDate(s->statementDate)},
            {QStringLiteral("endingBalance"), s->endingBalance.minor()},
            {QStringLiteral("endingBalanceText"), moneyFormat().text(s->endingBalance.minor())}};
}

QVariantList BooksService::reconcileCandidates(qint64 sessionId) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    for (const ReconcileCandidate &r : Reconciliation(*c).candidates(sessionId)) {
        out << QVariantMap{{QStringLiteral("lineId"), r.lineId},
                           {QStringLiteral("docId"), r.docId},
                           {QStringLiteral("date"), Format::isoDate(r.date)},
                           {QStringLiteral("kind"), Format::kindKey(r.kind)},
                           {QStringLiteral("kindLabel"), Format::kindLabel(r.kind)},
                           {QStringLiteral("number"), r.number},
                           {QStringLiteral("payee"), r.payee},
                           {QStringLiteral("memo"), r.memo},
                           {QStringLiteral("amount"), r.amount.minor()},
                           {QStringLiteral("amountText"), money.text(r.amount.minor())},
                           {QStringLiteral("cleared"), r.marked}};
    }
    return out;
}

QVariantMap BooksService::markCleared(qint64 sessionId, const QVariantList &lineIds, bool cleared)
{
    if (file() == nullptr) {
        return noCompany();
    }
    QList<qint64> ids;
    for (const QVariant &v : lineIds) {
        ids << Format::id(v);
    }
    return Format::result(Reconciliation(*file()).mark(sessionId, ids, cleared));
}

QVariantMap BooksService::reconcileState(qint64 sessionId) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    Reconciliation reconciliation(*c);
    const std::optional<ReconciliationSession> s = reconciliation.session(sessionId);
    if (!s) {
        return Format::failure(tr("No such reconciliation."));
    }
    const MoneyFormat money = moneyFormat();
    const qint64 cleared = reconciliation.clearedBalance(sessionId).minor();
    const qint64 difference = reconciliation.difference(sessionId).minor();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("sessionId"), s->id},
            {QStringLiteral("accountId"), s->accountId},
            {QStringLiteral("statementDate"), Format::isoDate(s->statementDate)},
            {QStringLiteral("beginningBalance"), s->beginningBalance.minor()},
            {QStringLiteral("beginningBalanceText"), money.text(s->beginningBalance.minor())},
            {QStringLiteral("endingBalance"), s->endingBalance.minor()},
            {QStringLiteral("endingBalanceText"), money.text(s->endingBalance.minor())},
            {QStringLiteral("clearedBalance"), cleared},
            {QStringLiteral("clearedBalanceText"), money.text(cleared)},
            {QStringLiteral("difference"), difference},
            {QStringLiteral("differenceText"), money.text(difference)},
            {QStringLiteral("state"), s->state == ReconciliationState::InProgress ? QStringLiteral("inProgress")
                                      : s->state == ReconciliationState::Finished ? QStringLiteral("finished")
                                                                                   : QStringLiteral("undone")},
            {QStringLiteral("canFinish"), s->state == ReconciliationState::InProgress && difference == 0}};
}

QVariantMap BooksService::finishReconciliation(qint64 sessionId)
{
    return file() != nullptr ? Format::result(Reconciliation(*file()).finish(sessionId)) : noCompany();
}

QVariantMap BooksService::cancelReconciliation(qint64 sessionId)
{
    return file() != nullptr ? Format::result(Reconciliation(*file()).cancel(sessionId)) : noCompany();
}

QVariantMap BooksService::undoLastReconciliation(qint64 accountId)
{
    return file() != nullptr ? Format::result(Reconciliation(*file()).undoLast(accountId)) : noCompany();
}

} // namespace QindaOffice::Apps::Books
