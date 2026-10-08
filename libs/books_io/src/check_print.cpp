// SPDX-License-Identifier: GPL-3.0-or-later
// PrintableCheck: the amount in words and the builder (check_print.h);
// check_renderer.cpp paints it.
#include "check_print.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "lists.h"

#include <QRegularExpression>

#include <array>

namespace QindaOffice::Books {

namespace {

constexpr std::array<const char *, 20> kOnes{"zero",    "one",     "two",       "three",    "four",
                                             "five",    "six",     "seven",     "eight",    "nine",
                                             "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
                                             "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
constexpr std::array<const char *, 10> kTens{"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy",
                                             "eighty", "ninety"};
// Short scale, as US checks write it; qint64 tops out in the quintillions.
constexpr std::array<const char *, 7> kScales{"", "thousand", "million", "billion", "trillion", "quadrillion",
                                              "quintillion"};

// 1…999 in words ("two hundred thirty-four").
QString hundreds(int n)
{
    QStringList words;
    if (n >= 100) {
        words << QString::fromLatin1(kOnes[size_t(n / 100)]) + QStringLiteral(" hundred");
        n %= 100;
    }
    if (n >= 20) {
        QString tens = QString::fromLatin1(kTens[size_t(n / 10)]);
        if (n % 10 != 0) {
            tens += QLatin1Char('-') + QString::fromLatin1(kOnes[size_t(n % 10)]);
        }
        words << tens;
    } else if (n > 0) {
        words << QString::fromLatin1(kOnes[size_t(n)]);
    }
    return words.join(QLatin1Char(' '));
}

QString wholeInWords(qint64 n)
{
    if (n == 0) {
        return QStringLiteral("zero");
    }
    QStringList groups;
    for (size_t scale = 0; n > 0 && scale < kScales.size(); ++scale, n /= 1000) {
        const int group = int(n % 1000);
        if (group != 0) {
            const QString name = QString::fromLatin1(kScales[scale]);
            groups.prepend(name.isEmpty() ? hundreds(group) : hundreds(group) + QLatin1Char(' ') + name);
        }
    }
    return groups.join(QLatin1Char(' '));
}

// Numeric with a four-digit year, in the locale's order: en_US
// "M/d/yy" → "MM/dd/yyyy", de_DE "dd.MM.yy" → "dd.MM.yyyy".
QString checkDate(const QDate &date, const QLocale &locale)
{
    QString format = locale.dateFormat(QLocale::ShortFormat);
    static const QRegularExpression year(QStringLiteral("(?<!y)yy(?!y)"));
    static const QRegularExpression month(QStringLiteral("(?<!M)M(?!M)"));
    static const QRegularExpression day(QStringLiteral("(?<!d)d(?!d)"));
    format.replace(year, QStringLiteral("yyyy")).replace(month, QStringLiteral("MM")).replace(day, QStringLiteral("dd"));
    return locale.toString(date, format);
}

QString accountLabel(const Account &a)
{
    return a.number.isEmpty() ? a.fullName : a.number + QLatin1Char(' ') + a.fullName;
}

QStringList postalLines(const QString &text)
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty()) {
            out << line.trimmed();
        }
    }
    return out;
}

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

} // namespace

QString amountInWords(const Money &amount)
{
    if (amount.isNegative()) {
        return {};
    }
    const int exponent = amount.exponent();
    qint64 scale = 1;
    for (int i = 0; i < exponent; ++i) {
        scale *= 10;
    }
    QString words = wholeInWords(amount.minor() / scale);
    if (exponent > 0) {
        words += QStringLiteral(" and %1/%2")
                     .arg(amount.minor() % scale, exponent, 10, QLatin1Char('0'))
                     .arg(scale);
    }
    words[0] = words[0].toUpper();
    return words;
}

bool isPrintableCheck(CompanyFile &file, qint64 docId, QString *why)
{
    const std::optional<BusinessDocument> doc = Documents(file).load(docId);
    if (!doc) {
        return fail(why, QStringLiteral("no such document"));
    }
    if (doc->kind != DocKind::Check && doc->kind != DocKind::BillPayment) {
        return fail(why, QStringLiteral("only checks and bill payments print as checks"));
    }
    if (doc->state == DocState::Void) {
        return fail(why, QStringLiteral("a void check is not printed"));
    }
    if (doc->state == DocState::Draft) {
        return fail(why, QStringLiteral("save the check before printing it"));
    }
    const std::optional<Account> bank = ChartOfAccounts(file).account(doc->accountId);
    if (!bank || bank->type != AccountType::Bank) {
        return fail(why, QStringLiteral("only a payment from a bank account prints as a check"));
    }
    if (!doc->total.isPositive()) {
        return fail(why, QStringLiteral("a check needs an amount above zero"));
    }
    return true;
}

bool buildPrintableCheck(CompanyFile &file, qint64 docId, PrintableCheck *out, QString *error)
{
    if (!isPrintableCheck(file, docId, error)) {
        return false;
    }
    Documents documents(file);
    const BusinessDocument doc = *documents.load(docId);
    ChartOfAccounts chart(file);
    const QLocale locale = companyLocale(file);
    PrintableCheck c;
    // The number the user gave; a bill payment's reference when it is one.
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    c.number = doc.number;
    if (c.number.isEmpty() && digits.match(doc.reference.trimmed()).hasMatch()) {
        c.number = doc.reference.trimmed();
    }
    c.date = checkDate(doc.date, locale);
    if (const std::optional<Party> party = Lists(file).party(doc.partyId)) {
        c.payee = party->displayName;
        c.address << party->displayName;
        if (!party->companyName.isEmpty() && party->companyName != party->displayName) {
            c.address << party->companyName;
        }
        c.address << postalLines(party->billingAddress);
    }
    c.amount = QStringLiteral("**") + doc.total.format(locale, false);
    c.amountWords = amountInWords(doc.total);
    c.memo = doc.message.simplified();
    c.companyName = file.settings().displayName();
    c.bankAccount = accountLabel(*chart.account(doc.accountId));
    c.total = doc.total.format(locale);
    if (doc.kind == DocKind::BillPayment) {
        c.columns = {{QStringLiteral("Date"), 2, false},
                     {QStringLiteral("Bill"), 4, false},
                     {QStringLiteral("Bill amount"), 2, true},
                     {QStringLiteral("Paid"), 2, true}};
        for (const Application &a : documents.applicationsFor(docId)) {
            if (a.voided || a.sourceDocId != docId) {
                continue;
            }
            const std::optional<BusinessDocument> bill = documents.load(a.targetDocId);
            if (!bill) {
                continue;
            }
            const QString name = !bill->number.isEmpty() ? bill->number : bill->reference;
            c.rows << QStringList{checkDate(bill->date, locale), name, bill->total.format(locale, false),
                                  a.amount.format(locale, false)};
        }
    } else {
        c.columns = {{QStringLiteral("Account"), 4, false},
                     {QStringLiteral("Description"), 5, false},
                     {QStringLiteral("Amount"), 2, true}};
        const DocumentTotals totals = documents.computeTotals(doc);
        for (qsizetype i = 0; i < doc.lines.size(); ++i) {
            const DocumentLine &line = doc.lines[i];
            const std::optional<Account> account = chart.account(line.accountId);
            const Money amount = i < totals.lineAmounts.size() ? totals.lineAmounts[i] : line.amount;
            c.rows << QStringList{account ? accountLabel(*account) : QString(), line.description,
                                  amount.format(locale, false)};
        }
    }
    *out = c;
    return true;
}

} // namespace QindaOffice::Books
