// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: journal entries, transfers, deposits and the audit trail.
#include "books_service.h"

#include "banking.h"
#include "company_file.h"
#include "documents.h"
#include "journal.h"

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

struct ParsedLines {
    QList<JournalLine> lines;
    qint64 debits = 0;
    qint64 credits = 0;
    QString error;
};

// Journal-entry lines from QML: {accountId, debit (text), credit (text),
// memo, partyId, classId}. Rows with neither side are the editor's blanks.
ParsedLines parseJournalLines(const QVariantList &rows, const MoneyFormat &money, CompanyFile &file)
{
    ParsedLines out;
    int n = 0;
    for (const QVariant &v : rows) {
        ++n;
        const QVariantMap l = v.toMap();
        QString error;
        const std::optional<qint64> debit = money.parse(l.value(QStringLiteral("debit")), &error);
        const std::optional<qint64> credit = debit ? money.parse(l.value(QStringLiteral("credit")), &error)
                                                   : std::nullopt;
        if (!debit || !credit) {
            out.error = QObject::tr("Line %1: %2").arg(n).arg(error);
            return out;
        }
        const qint64 accountId = Format::id(l.value(QStringLiteral("accountId")));
        if (*debit == 0 && *credit == 0) {
            continue;
        }
        JournalLine line;
        line.accountId = accountId;
        line.debit = file.money(*debit);
        line.credit = file.money(*credit);
        line.partyId = Format::id(l.value(QStringLiteral("partyId")));
        line.classId = Format::id(l.value(QStringLiteral("classId")));
        line.memo = l.value(QStringLiteral("memo")).toString();
        out.debits += *debit;
        out.credits += *credit;
        out.lines << line;
    }
    return out;
}

} // namespace

QVariantMap BooksService::checkJournalEntry(const QVariantMap &map) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const MoneyFormat money = moneyFormat();
    const ParsedLines parsed = parseJournalLines(map.value(QStringLiteral("lines")).toList(), money, *c);
    const qint64 difference = parsed.debits - parsed.credits;
    return {{QStringLiteral("ok"), parsed.error.isEmpty()},
            {QStringLiteral("error"), parsed.error},
            {QStringLiteral("balanced"), parsed.error.isEmpty() && difference == 0 && parsed.lines.size() >= 2},
            {QStringLiteral("debit"), parsed.debits},
            {QStringLiteral("credit"), parsed.credits},
            {QStringLiteral("difference"), difference},
            {QStringLiteral("debitText"), money.text(parsed.debits)},
            {QStringLiteral("creditText"), money.text(parsed.credits)},
            {QStringLiteral("differenceText"), money.text(difference < 0 ? -difference : difference)}};
}

QVariantMap BooksService::postJournalEntry(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const ParsedLines parsed = parseJournalLines(map.value(QStringLiteral("lines")).toList(), moneyFormat(), *c);
    if (!parsed.error.isEmpty()) {
        return Format::failure(parsed.error);
    }
    JournalEntryInput entry;
    entry.date = Format::date(map.value(QStringLiteral("date")));
    if (!entry.date.isValid()) {
        entry.date = todayDate();
    }
    entry.number = map.value(QStringLiteral("number")).toString().trimmed();
    entry.memo = map.value(QStringLiteral("memo")).toString();
    entry.lines = parsed.lines;
    const QString reason = map.value(QStringLiteral("lockOverrideReason")).toString().trimmed();
    const PostOptions options = reason.isEmpty() ? PostOptions{} : PostOptions{true, reason};
    const qint64 id = Format::id(map.value(QStringLiteral("id")));
    Ledger ledger(*c);
    if (id != 0) {
        QString why = map.value(QStringLiteral("reason")).toString().trimmed();
        return Format::result(ledger.replaceJournalEntry(id, entry, why.isEmpty() ? tr("Edited in QindaBooks") : why,
                                                         options));
    }
    return Format::result(ledger.postJournalEntry(entry, options));
}

QVariantMap BooksService::transfer(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    QString error;
    const std::optional<qint64> amount = moneyFormat().parse(map.value(QStringLiteral("amount")), &error);
    if (!amount) {
        return Format::failure(error);
    }
    QDate date = Format::date(map.value(QStringLiteral("date")));
    if (!date.isValid()) {
        date = todayDate();
    }
    return Format::result(Banking(*c).transfer(Format::id(map.value(QStringLiteral("fromAccountId"))),
                                               Format::id(map.value(QStringLiteral("toAccountId"))), c->money(*amount),
                                               date, map.value(QStringLiteral("memo")).toString()));
}

QVariantMap BooksService::deposit(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    QList<qint64> ids;
    for (const QVariant &v : map.value(QStringLiteral("paymentIds")).toList()) {
        ids << Format::id(v);
    }
    if (ids.isEmpty()) {
        return Format::failure(tr("Choose the payments to deposit."));
    }
    qint64 account = Format::id(map.value(QStringLiteral("accountId")));
    if (account == 0) {
        account = Format::id(map.value(QStringLiteral("bankAccountId")));
    }
    QDate date = Format::date(map.value(QStringLiteral("date")));
    if (!date.isValid()) {
        date = todayDate();
    }
    return Format::result(Banking(*c).deposit(account, date, ids, map.value(QStringLiteral("memo")).toString()));
}

QVariantList BooksService::undepositedPayments() const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    Documents documents(*c);
    for (qint64 id : Banking(*c).undepositedPayments()) {
        const std::optional<BusinessDocument> d = documents.load(id);
        if (!d) {
            continue;
        }
        out << QVariantMap{{QStringLiteral("id"), d->id},
                           {QStringLiteral("kind"), Format::kindKey(d->kind)},
                           {QStringLiteral("kindLabel"), Format::kindLabel(d->kind)},
                           {QStringLiteral("number"), d->number.isEmpty() ? d->reference : d->number},
                           {QStringLiteral("date"), Format::isoDate(d->date)},
                           {QStringLiteral("partyId"), d->partyId},
                           {QStringLiteral("partyName"), nameOfParty(d->partyId)},
                           {QStringLiteral("amount"), d->total.minor()},
                           {QStringLiteral("amountText"), money.text(d->total.minor())}};
    }
    return out;
}

QVariantList BooksService::auditTrail(const QString &entity, qint64 id) const
{
    QVariantList out;
    if (CompanyFile *c = file()) {
        for (const AuditEntry &e : Ledger(*c).auditTrail(entity, id)) {
            out << QVariantMap{{QStringLiteral("id"), e.id},
                               {QStringLiteral("at"), e.at},
                               {QStringLiteral("user"), e.user},
                               {QStringLiteral("action"), e.action},
                               {QStringLiteral("entity"), e.entity},
                               {QStringLiteral("entityId"), e.entityId},
                               {QStringLiteral("before"), e.beforeJson},
                               {QStringLiteral("after"), e.afterJson},
                               {QStringLiteral("reason"), e.reason}};
        }
    }
    return out;
}

} // namespace QindaOffice::Apps::Books
