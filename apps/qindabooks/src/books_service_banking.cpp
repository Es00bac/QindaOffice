// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: bank and card accounts, registers, statement import, the
// bank feed with rules and match suggestions (reconciliation:
// books_service_reconcile.cpp).
#include "books_service.h"

#include "accounts.h"
#include "bank_import.h"
#include "banking.h"
#include "company_file.h"
#include "csv_util.h"
#include "reconciliation.h"

#include <QFile>
#include <QFileInfo>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

QString feedStatusKey(FeedStatus s)
{
    switch (s) {
    case FeedStatus::New: return QStringLiteral("new");
    case FeedStatus::Matched: return QStringLiteral("matched");
    case FeedStatus::Categorized: return QStringLiteral("categorized");
    case FeedStatus::Excluded: return QStringLiteral("excluded");
    }
    return QStringLiteral("new");
}

std::optional<FeedStatus> feedStatusFrom(const QString &key)
{
    if (key == QLatin1String("new") || key == QLatin1String("review")) {
        return FeedStatus::New;
    }
    if (key == QLatin1String("matched")) {
        return FeedStatus::Matched;
    }
    if (key == QLatin1String("categorized")) {
        return FeedStatus::Categorized;
    }
    if (key == QLatin1String("excluded")) {
        return FeedStatus::Excluded;
    }
    return std::nullopt; // "all"
}

QString directionKey(RuleDirection d)
{
    return d == RuleDirection::MoneyIn ? QStringLiteral("in")
        : d == RuleDirection::MoneyOut ? QStringLiteral("out")
                                       : QStringLiteral("any");
}

// An explicit CSV mapping from QML: the short keys the import dialog uses
// ({date, amount, payee, memo} as column indexes, -1 = none) or the full
// CsvMapping names (dateColumn, debitColumn, …, dateFormat, invertSign).
CsvMapping csvMappingFrom(const QVariantMap &m)
{
    CsvMapping mapping;
    const auto column = [&m](const char *shortKey, const char *longKey) {
        for (const char *key : {longKey, shortKey}) {
            if (m.contains(QString::fromLatin1(key))) {
                return m.value(QString::fromLatin1(key)).toInt();
            }
        }
        return -1;
    };
    mapping.dateColumn = column("date", "dateColumn");
    mapping.amountColumn = column("amount", "amountColumn");
    mapping.debitColumn = column("debit", "debitColumn");
    mapping.creditColumn = column("credit", "creditColumn");
    mapping.payeeColumn = column("payee", "payeeColumn");
    mapping.memoColumn = column("memo", "memoColumn");
    mapping.checkColumn = column("check", "checkColumn");
    mapping.fitidColumn = column("fitid", "fitidColumn");
    mapping.hasHeader = m.value(QStringLiteral("hasHeader"), true).toBool();
    mapping.dateFormat = m.value(QStringLiteral("dateFormat")).toString();
    mapping.dayFirst = m.value(QStringLiteral("dayFirst")).toBool();
    mapping.decimalComma = m.value(QStringLiteral("decimalComma")).toBool();
    mapping.invertSign = m.value(QStringLiteral("invertSign")).toBool();
    const QString delimiter = m.value(QStringLiteral("delimiter")).toString();
    if (!delimiter.isEmpty()) {
        mapping.delimiter = delimiter == QLatin1String("\\t") ? QLatin1Char('\t') : delimiter.at(0);
    }
    return mapping;
}

} // namespace

QVariantList BooksService::bankAccounts() const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    ChartOfAccounts chart(*c);
    Banking banking(*c);
    Reconciliation reconciliation(*c);
    for (const Account &a : chart.accounts()) {
        if (a.type != AccountType::Bank && a.type != AccountType::CreditCard) {
            continue;
        }
        const qint64 balance = chart.balance(a.id).minor();
        QString lastReconciled;
        for (const ReconciliationSession &s : reconciliation.history(a.id)) {
            if (s.state == ReconciliationState::Finished && Format::isoDate(s.statementDate) > lastReconciled) {
                lastReconciled = Format::isoDate(s.statementDate);
            }
        }
        out << QVariantMap{{QStringLiteral("id"), a.id},
                           {QStringLiteral("name"), a.fullName},
                           {QStringLiteral("number"), a.number},
                           {QStringLiteral("type"), a.type == AccountType::Bank ? QStringLiteral("Bank")
                                                                                : QStringLiteral("CreditCard")},
                           {QStringLiteral("balance"), balance},
                           {QStringLiteral("balanceText"), money.text(balance)},
                           {QStringLiteral("unreviewed"), banking.feed(a.id, FeedStatus::New).size()},
                           {QStringLiteral("lastReconciled"), lastReconciled}};
    }
    return out;
}

QVariantList BooksService::accountRegister(qint64 accountId, const QString &fromIso, const QString &toIso) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    const std::optional<Account> account = ChartOfAccounts(*c).account(accountId);
    // Money in / money out from the account holder's side (the bank-feed
    // sign): a card's charge is a payment out, a payment onto it money in.
    const bool card = account && account->type == AccountType::CreditCard;
    for (const RegisterEntry &e : Banking(*c).registerFor(accountId, Format::date(fromIso), Format::date(toIso))) {
        const qint64 in = card ? e.decrease.minor() : e.increase.minor();
        const qint64 outgoing = card ? e.increase.minor() : e.decrease.minor();
        out << QVariantMap{{QStringLiteral("lineId"), e.lineId},
                           {QStringLiteral("docId"), e.docId},
                           {QStringLiteral("date"), Format::isoDate(e.date)},
                           {QStringLiteral("kind"), Format::kindKey(e.kind)},
                           {QStringLiteral("kindLabel"), Format::kindLabel(e.kind)},
                           {QStringLiteral("number"), e.number},
                           {QStringLiteral("payee"), e.payee},
                           {QStringLiteral("memo"), e.memo},
                           {QStringLiteral("account"), e.otherAccount},
                           {QStringLiteral("payment"), outgoing},
                           {QStringLiteral("paymentText"), outgoing != 0 ? money.text(outgoing) : QString()},
                           {QStringLiteral("deposit"), in},
                           {QStringLiteral("depositText"), in != 0 ? money.text(in) : QString()},
                           {QStringLiteral("balance"), e.balance.minor()},
                           {QStringLiteral("balanceText"), money.text(e.balance.minor())},
                           {QStringLiteral("cleared"), e.cleared == 2 ? QStringLiteral("R")
                                                       : e.cleared == 1 ? QStringLiteral("C")
                                                                        : QString()},
                           {QStringLiteral("reconciled"), e.cleared == 2}};
    }
    return out;
}

QVariantMap BooksService::importStatement(qint64 accountId, const QUrl &fileUrl, const QVariantMap &mapping)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const QString path = localPath(fileUrl);
    const int exponent = currencyInfo(c->currency()).exponent;
    const bool dayFirst = moneyFormat().locale.dateFormat(QLocale::ShortFormat).startsWith(QLatin1Char('d'));
    BankParseResult parsed;
    if (!mapping.isEmpty()) {
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly)) {
            return Format::failure(in.errorString());
        }
        parsed = BankImport::parseCsv(in.readAll(), csvMappingFrom(mapping), exponent);
    } else {
        QString error;
        parsed = BankImport::parseFile(path, exponent, dayFirst, &error);
        if (!error.isEmpty()) {
            return Format::failure(error);
        }
    }
    QStringList errors;
    for (const BankParseError &e : std::as_const(parsed.errors)) {
        errors << tr("row %1: %2").arg(e.row).arg(e.message);
    }
    if (parsed.format == QLatin1String("csv") && mapping.isEmpty()
        && (!parsed.mapping.isValid() || (parsed.lines.isEmpty() && !parsed.errors.isEmpty()))) {
        // Detection was not sure: hand the columns back for a mapping.
        QFile in(path);
        QStringList columns;
        QVariantList sample;
        if (in.open(QIODevice::ReadOnly)) {
            const QString text = Csv::decode(in.readAll());
            const QList<QStringList> rows = Csv::parse(text, Csv::detectDelimiter(text));
            columns = rows.value(0);
            for (qsizetype r = 1; r < rows.size() && r <= 5; ++r) {
                sample << rows.at(r);
            }
        }
        QVariantMap out = Format::failure(tr("Which columns hold the date, amount and description?"));
        out.insert(QStringLiteral("needsMapping"), true);
        out.insert(QStringLiteral("columns"), columns);
        out.insert(QStringLiteral("sample"), sample);
        out.insert(QStringLiteral("format"), parsed.format);
        out.insert(QStringLiteral("errors"), errors);
        return out;
    }
    if (!parsed.currency.isEmpty() && parsed.currency.compare(c->currency(), Qt::CaseInsensitive) != 0) {
        return Format::failure(tr("This statement is in %1; the company keeps its books in %2.")
                                   .arg(parsed.currency, c->currency()));
    }
    Banking banking(*c);
    const ImportSummary summary = banking.importLines(accountId, parsed.lines, QFileInfo(path).fileName());
    const int ruled = summary.added > 0 ? banking.applyRules(accountId) : 0;
    QVariantMap out = Format::success(accountId);
    out.insert(QStringLiteral("imported"), summary.added);
    out.insert(QStringLiteral("duplicates"), summary.duplicates);
    out.insert(QStringLiteral("errors"), errors);
    out.insert(QStringLiteral("format"), parsed.format);
    out.insert(QStringLiteral("matchedByRules"), ruled);
    out.insert(QStringLiteral("needsMapping"), false);
    return out;
}

QVariantList BooksService::feed(qint64 accountId, const QString &status) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    Banking banking(*c);
    QHash<qint64, QString> ruleNames;
    for (const BankRule &r : banking.rules()) {
        ruleNames.insert(r.id, r.name);
    }
    for (const BankFeedEntry &e : banking.feed(accountId, feedStatusFrom(status))) {
        QVariantMap suggestion{{QStringLiteral("kind"), QString()},
                               {QStringLiteral("id"), 0},
                               {QStringLiteral("label"), QString()},
                               {QStringLiteral("confidence"), 0}};
        if (e.status == FeedStatus::New && e.suggestedAccountId != 0) {
            suggestion = {{QStringLiteral("kind"), QStringLiteral("rule")},
                          {QStringLiteral("id"), e.suggestedAccountId},
                          {QStringLiteral("label"), tr("%1 (rule: %2)").arg(nameOfAccount(e.suggestedAccountId),
                                                                           ruleNames.value(e.ruleId))},
                          {QStringLiteral("confidence"), 100}};
        } else if (e.status == FeedStatus::New) {
            const QList<MatchSuggestion> matches = banking.suggestMatches(e.id);
            if (!matches.isEmpty()) {
                const MatchSuggestion &m = matches.first();
                suggestion = {{QStringLiteral("kind"), QStringLiteral("match")},
                              {QStringLiteral("id"), m.journalLineId},
                              {QStringLiteral("docId"), m.docId},
                              {QStringLiteral("label"), tr("Match: %1 on %2").arg(m.description,
                                                                                 money.locale.toString(m.date, QLocale::ShortFormat))},
                              {QStringLiteral("confidence"), m.score}};
            }
        }
        out << QVariantMap{{QStringLiteral("id"), e.id},
                           {QStringLiteral("date"), Format::isoDate(e.date)},
                           {QStringLiteral("payee"), e.payee},
                           {QStringLiteral("memo"), e.memo},
                           {QStringLiteral("checkNumber"), e.checkNumber},
                           {QStringLiteral("amount"), e.amount.minor()},
                           {QStringLiteral("amountText"), money.text(e.amount.minor())},
                           {QStringLiteral("status"), feedStatusKey(e.status)},
                           {QStringLiteral("docId"), e.docId},
                           {QStringLiteral("suggestion"), suggestion}};
    }
    return out;
}

QVariantMap BooksService::acceptSuggestion(qint64 lineId)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    Banking banking(*c);
    const std::optional<BankFeedEntry> e = banking.feedEntry(lineId);
    if (!e) {
        return Format::failure(tr("That bank line no longer exists."));
    }
    if (e->suggestedAccountId != 0) {
        return Format::result(banking.acceptSuggestion(lineId));
    }
    const QList<MatchSuggestion> matches = banking.suggestMatches(lineId);
    if (matches.isEmpty()) {
        return Format::failure(tr("Nothing to accept: categorize this line instead."));
    }
    return Format::result(banking.match(lineId, matches.first().journalLineId));
}

QVariantMap BooksService::categorize(qint64 lineId, const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    return Format::result(Banking(*c).categorize(lineId, Format::id(map.value(QStringLiteral("accountId"))),
                                                 Format::id(map.value(QStringLiteral("partyId"))),
                                                 Format::id(map.value(QStringLiteral("classId"))),
                                                 map.value(QStringLiteral("memo")).toString()));
}

QVariantMap BooksService::exclude(qint64 lineId)
{
    return file() != nullptr ? Format::result(Banking(*file()).exclude(lineId)) : noCompany();
}

QVariantMap BooksService::restore(qint64 lineId)
{
    return file() != nullptr ? Format::result(Banking(*file()).restore(lineId)) : noCompany();
}

QVariantList BooksService::rules() const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    for (const BankRule &r : Banking(*c).rules()) {
        out << QVariantMap{{QStringLiteral("id"), r.id},
                           {QStringLiteral("name"), r.name},
                           {QStringLiteral("priority"), r.priority},
                           {QStringLiteral("payeeContains"), r.payeeContains},
                           {QStringLiteral("direction"), directionKey(r.direction)},
                           {QStringLiteral("amountMin"), r.amountMin ? money.plain(*r.amountMin) : QString()},
                           {QStringLiteral("amountMax"), r.amountMax ? money.plain(*r.amountMax) : QString()},
                           {QStringLiteral("bankAccountId"), r.bankAccountId},
                           {QStringLiteral("accountId"), r.accountId},
                           {QStringLiteral("accountName"), nameOfAccount(r.accountId)},
                           {QStringLiteral("partyId"), r.partyId},
                           {QStringLiteral("classId"), r.classId},
                           {QStringLiteral("memo"), r.memo},
                           {QStringLiteral("active"), r.active}};
    }
    return out;
}

QVariantMap BooksService::saveRule(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const MoneyFormat money = moneyFormat();
    BankRule rule;
    rule.name = map.value(QStringLiteral("name")).toString().trimmed();
    rule.payeeContains = map.value(QStringLiteral("payeeContains")).toString().trimmed();
    if (rule.name.isEmpty()) {
        rule.name = rule.payeeContains;
    }
    rule.priority = map.value(QStringLiteral("priority")).toInt();
    const QString direction = map.value(QStringLiteral("direction")).toString();
    rule.direction = direction == QLatin1String("in")  ? RuleDirection::MoneyIn
        : direction == QLatin1String("out")            ? RuleDirection::MoneyOut
                                                       : RuleDirection::Any;
    for (const auto &[key, field] : {std::pair{"amountMin", &rule.amountMin}, std::pair{"amountMax", &rule.amountMax}}) {
        const QVariant v = map.value(QString::fromLatin1(key));
        if (v.toString().trimmed().isEmpty()) {
            continue;
        }
        QString error;
        const std::optional<qint64> minor = money.parse(v, &error);
        if (!minor) {
            return Format::failure(error);
        }
        *field = *minor;
    }
    rule.bankAccountId = Format::id(map.value(QStringLiteral("bankAccountId")));
    rule.accountId = Format::id(map.value(QStringLiteral("accountId")));
    rule.partyId = Format::id(map.value(QStringLiteral("partyId")));
    rule.classId = Format::id(map.value(QStringLiteral("classId")));
    rule.memo = map.value(QStringLiteral("memo")).toString();
    rule.active = map.value(QStringLiteral("active"), true).toBool();
    // AGENT-NOTE: the engine has no rule update; an edit replaces the rule
    // (new id) inside one transaction, so a failed save keeps the old one.
    SqlTransaction txn(*c);
    Banking banking(*c);
    if (const qint64 id = Format::id(map.value(QStringLiteral("id"))); id != 0) {
        if (const Result removed = banking.deleteRule(id); !removed.ok) {
            return Format::result(removed);
        }
    }
    const Result created = banking.createRule(rule);
    if (!created.ok) {
        return Format::result(created);
    }
    if (!txn.commit()) {
        return Format::failure(txn.error());
    }
    return Format::result(created);
}

QVariantMap BooksService::deleteRule(qint64 id)
{
    return file() != nullptr ? Format::result(Banking(*file()).deleteRule(id)) : noCompany();
}

int BooksService::applyRules(qint64 accountId)
{
    return file() != nullptr ? Banking(*file()).applyRules(accountId) : 0;
}

} // namespace QindaOffice::Apps::Books
