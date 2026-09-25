// SPDX-License-Identifier: GPL-3.0-or-later
// QIF (Quicken Interchange Format): !Type:Bank / CCard / Cash / Oth A/L
// sections of one-letter fields ended by '^'. QIF has no transaction ids,
// so dedup falls back to the engine's date+amount+payee+occurrence key.
#include "bank_import.h"

#include "csv_util.h"

#include <QRegularExpression>

namespace QindaOffice::Books::BankImport {

namespace {

// "1/15'26", "01/15/2026", "1/15/26", "15.01.2026", "2026-01-15",
// " 1/ 5' 6" (Quicken pads with spaces). An apostrophe marks a 2000s year.
QDate qifDate(const QString &input, bool dayFirst)
{
    QString t = input;
    t.remove(QLatin1Char(' '));
    const bool apostrophe = t.contains(QLatin1Char('\''));
    const bool dotted = t.contains(QLatin1Char('.'));
    static const QRegularExpression separators(QStringLiteral("[/.'\\-]"));
    const QStringList parts = t.split(separators, Qt::SkipEmptyParts);
    if (parts.size() != 3) {
        return {};
    }
    bool ok1 = false, ok2 = false, ok3 = false;
    const int a = parts[0].toInt(&ok1);
    const int b = parts[1].toInt(&ok2);
    int c = parts[2].toInt(&ok3);
    if (!ok1 || !ok2 || !ok3) {
        return {};
    }
    if (parts[0].size() == 4) {
        return QDate(a, b, c); // ISO order
    }
    if (parts[2].size() <= 2) {
        c += apostrophe || c < 70 ? 2000 : 1900;
    }
    return (dayFirst || dotted) ? QDate(c, b, a) : QDate(c, a, b);
}

} // namespace

BankParseResult parseQif(const QByteArray &data, int exponent, bool dayFirst)
{
    BankParseResult result;
    result.format = QStringLiteral("qif");
    const QStringList lines = Csv::decode(data).split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    bool inTransactions = false;
    bool sawType = false;
    BankLine current;
    QString date;
    QString amount;
    bool any = false;
    int record = 0;
    auto finish = [&] {
        if (!any) {
            return;
        }
        ++record;
        current.date = qifDate(date, dayFirst);
        const std::optional<qint64> value = parseAmount(amount, exponent, false);
        if (!current.date.isValid() || !value) {
            result.errors << BankParseError{record, QStringLiteral("record %1 has no readable date or amount").arg(record)};
        } else {
            current.amount = *value;
            result.lines << current;
        }
        current = BankLine{};
        date.clear();
        amount.clear();
        any = false;
    };
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('!'))) {
            const QString header = line.toLower();
            if (header.startsWith(QLatin1String("!type:"))) {
                sawType = true;
                const QString type = header.mid(6).trimmed();
                inTransactions = type == QLatin1String("bank") || type == QLatin1String("ccard")
                    || type == QLatin1String("cash") || type == QLatin1String("oth a") || type == QLatin1String("oth l");
                result.creditCard = result.creditCard || type == QLatin1String("ccard");
            } else if (header.startsWith(QLatin1String("!account"))) {
                inTransactions = false; // the account list block
            }
            continue;
        }
        if (!inTransactions) {
            continue;
        }
        const QChar code = line.at(0).toUpper();
        const QString value = line.mid(1).trimmed();
        switch (code.unicode()) {
        case '^': finish(); break;
        case 'D': date = value; any = true; break;
        case 'T':
        case 'U':
            if (amount.isEmpty() || code == QLatin1Char('T')) {
                amount = value;
            }
            any = true;
            break;
        case 'P': current.payee = value; any = true; break;
        case 'M': current.memo = value; any = true; break;
        case 'N': current.checkNumber = value; any = true; break;
        default: break; // L category, C cleared, A address, S/E/$ splits
        }
    }
    finish();
    if (!sawType) {
        result.errors.prepend(BankParseError{0, QStringLiteral("not a QIF file (no !Type: header)")});
    }
    return result;
}

} // namespace QindaOffice::Books::BankImport
