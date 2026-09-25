// SPDX-License-Identifier: GPL-3.0-or-later
// CSV bank statements: header heuristics, content fallback, date-format
// and decimal-comma detection, signed or debit/credit amounts.
#include "bank_import.h"

#include "csv_util.h"

#include <QRegularExpression>

namespace QindaOffice::Books::BankImport {

namespace {

QString normalizeHeader(const QString &h)
{
    static const QRegularExpression junk(QStringLiteral("[^a-z0-9#/ ]+"));
    QString s = h.toLower();
    s.replace(junk, QStringLiteral(" "));
    return s.simplified();
}

bool looksNumeric(const QString &text)
{
    static const QRegularExpression number(
        QStringLiteral("^\\(?\\s*[-+\\x{2212}]?\\s*[^\\d\\sA-Za-z]{0,2}\\s*[A-Z]{0,3}\\s*\\d[\\d.,' ]*\\s*-?\\)?\\s*"
                       "(CR|DR|[A-Z]{3})?$"));
    return number.match(text.trimmed()).hasMatch();
}

// The column whose normalised header is the EARLIEST entry of `exact`
// (the lists are in priority order: "payee" beats "details"), else one
// that contains any of `partial` (and none of `exclude`).
int findColumn(const QStringList &headers, const QStringList &exact, const QStringList &partial,
               const QStringList &exclude = {}, const QList<int> &taken = {})
{
    for (const QString &wanted : exact) {
        for (int i = 0; i < headers.size(); ++i) {
            if (!taken.contains(i) && headers[i] == wanted) {
                return i;
            }
        }
    }
    for (int i = 0; i < headers.size(); ++i) {
        if (taken.contains(i)) {
            continue;
        }
        bool excluded = false;
        for (const QString &x : exclude) {
            excluded = excluded || headers[i].contains(x);
        }
        for (const QString &p : partial) {
            if (!excluded && headers[i].contains(p)) {
                return i;
            }
        }
    }
    return -1;
}

void mapByHeader(const QStringList &raw, CsvMapping &m)
{
    QStringList h;
    for (const QString &s : raw) {
        h << normalizeHeader(s);
    }
    QList<int> taken;
    auto take = [&taken](int column) {
        if (column >= 0) {
            taken << column;
        }
        return column;
    };
    m.dateColumn = take(findColumn(h, {QStringLiteral("date"), QStringLiteral("posted date"), QStringLiteral("posting date"),
                                       QStringLiteral("post date"), QStringLiteral("transaction date"),
                                       QStringLiteral("trans date"), QStringLiteral("booking date"), QStringLiteral("value date")},
                                   {QStringLiteral("date")}, {QStringLiteral("due")}));
    m.debitColumn = take(findColumn(h, {QStringLiteral("debit"), QStringLiteral("debits"), QStringLiteral("withdrawal"),
                                        QStringLiteral("withdrawals"), QStringLiteral("paid out"), QStringLiteral("money out"),
                                        QStringLiteral("debit amount"), QStringLiteral("out"), QStringLiteral("charges")},
                                    {QStringLiteral("withdraw"), QStringLiteral("debit amount")}, {}, taken));
    m.creditColumn = take(findColumn(h, {QStringLiteral("credit"), QStringLiteral("credits"), QStringLiteral("deposit"),
                                         QStringLiteral("deposits"), QStringLiteral("paid in"), QStringLiteral("money in"),
                                         QStringLiteral("credit amount"), QStringLiteral("in")},
                                     {QStringLiteral("deposit"), QStringLiteral("credit amount")}, {}, taken));
    m.amountColumn = take(findColumn(h, {QStringLiteral("amount"), QStringLiteral("transaction amount"), QStringLiteral("value")},
                                     {QStringLiteral("amount")}, {QStringLiteral("balance"), QStringLiteral("original")},
                                     taken));
    if (m.amountColumn >= 0 && m.debitColumn >= 0 && m.creditColumn < 0) {
        m.debitColumn = -1; // "Amount" + a stray "Debit" flag column: use the amount
    }
    m.typeColumn = take(findColumn(h, {QStringLiteral("type"), QStringLiteral("transaction type"), QStringLiteral("dr/cr"),
                                       QStringLiteral("debit/credit"), QStringLiteral("cr/dr")},
                                   {}, {}, taken));
    m.payeeColumn = take(findColumn(h, {QStringLiteral("payee"), QStringLiteral("description"), QStringLiteral("name"),
                                        QStringLiteral("merchant"), QStringLiteral("merchant name"),
                                        QStringLiteral("details"), QStringLiteral("narrative"), QStringLiteral("counterparty"),
                                        QStringLiteral("beneficiary"), QStringLiteral("transaction description")},
                                    {QStringLiteral("payee"), QStringLiteral("description"), QStringLiteral("merchant")},
                                    {}, taken));
    m.memoColumn = take(findColumn(h, {QStringLiteral("memo"), QStringLiteral("notes"), QStringLiteral("note"),
                                       QStringLiteral("additional information"), QStringLiteral("extended description")},
                                   {QStringLiteral("memo"), QStringLiteral("note")}, {}, taken));
    m.checkColumn = take(findColumn(h, {QStringLiteral("check"), QStringLiteral("check number"), QStringLiteral("check no"),
                                        QStringLiteral("check #"), QStringLiteral("cheque"), QStringLiteral("cheque number"),
                                        QStringLiteral("chq"), QStringLiteral("num")},
                                    {QStringLiteral("check"), QStringLiteral("cheque")}, {}, taken));
    m.fitidColumn = take(findColumn(h, {QStringLiteral("id"), QStringLiteral("transaction id"), QStringLiteral("fitid"),
                                        QStringLiteral("reference"), QStringLiteral("ref"), QStringLiteral("bank reference"),
                                        QStringLiteral("transaction reference"), QStringLiteral("reference number")},
                                    {QStringLiteral("transaction id"), QStringLiteral("reference")}, {}, taken));
}

// No header: the date column is the one where every cell is a date, the
// amount the first all-numeric column, the payee the longest text.
void mapByContent(const QList<QStringList> &rows, CsvMapping &m, bool dayFirst)
{
    const int columns = rows.isEmpty() ? 0 : int(rows.first().size());
    QList<int> numeric;
    qsizetype longest = -1;
    for (int c = 0; c < columns; ++c) {
        QStringList cells;
        bool allNumeric = true;
        qsizetype length = 0;
        for (const QStringList &r : rows) {
            const QString v = c < r.size() ? r[c].trimmed() : QString();
            cells << v;
            allNumeric = allNumeric && (v.isEmpty() || looksNumeric(v));
            length += v.size();
        }
        if (m.dateColumn < 0 && !detectDateFormat(cells, dayFirst).isEmpty()) {
            m.dateColumn = c;
        } else if (allNumeric) {
            numeric << c;
        } else if (length > longest) {
            longest = length;
            m.payeeColumn = c;
        }
    }
    if (!numeric.isEmpty()) {
        m.amountColumn = numeric.first();
    }
}

} // namespace

CsvMapping detectCsvMapping(const QByteArray &data, bool dayFirst)
{
    const QString text = Csv::decode(data);
    CsvMapping m;
    m.dayFirst = dayFirst;
    m.delimiter = Csv::detectDelimiter(text);
    const QList<QStringList> rows = Csv::parse(text, m.delimiter);
    if (rows.isEmpty()) {
        return m;
    }
    bool firstRowHasDate = false;
    for (const QString &cell : rows.first()) {
        firstRowHasDate = firstRowHasDate || !detectDateFormat({cell}, dayFirst).isEmpty();
    }
    m.hasHeader = !firstRowHasDate;
    if (m.hasHeader) {
        mapByHeader(rows.first(), m);
    }
    if (!m.isValid()) {
        m.dateColumn = m.amountColumn = m.debitColumn = m.creditColumn = m.payeeColumn = -1;
        mapByContent(m.hasHeader ? rows.mid(1) : rows, m, dayFirst);
    }
    // Decimal comma: ';' files (the European convention) or amounts that
    // mostly end in ",dd".
    static const QRegularExpression commaDecimal(QStringLiteral(",\\d{1,2}\\)?\\s*-?$"));
    int comma = 0;
    int total = 0;
    for (qsizetype r = m.hasHeader ? 1 : 0; r < rows.size(); ++r) {
        for (int c : {m.amountColumn, m.debitColumn, m.creditColumn}) {
            if (c >= 0 && c < rows[r].size() && !rows[r][c].trimmed().isEmpty()) {
                ++total;
                comma += commaDecimal.match(rows[r][c].trimmed()).hasMatch() ? 1 : 0;
            }
        }
    }
    m.decimalComma = total > 0 && (comma * 2 > total || (m.delimiter == QLatin1Char(';') && comma > 0));
    return m;
}

BankParseResult parseCsv(const QByteArray &data, int exponent, bool dayFirst)
{
    return parseCsv(data, detectCsvMapping(data, dayFirst), exponent);
}

BankParseResult parseCsv(const QByteArray &data, const CsvMapping &mapping, int exponent)
{
    BankParseResult result;
    result.format = QStringLiteral("csv");
    CsvMapping m = mapping;
    const QString text = Csv::decode(data);
    if (m.delimiter.isNull()) {
        m.delimiter = Csv::detectDelimiter(text);
    }
    const QList<QStringList> all = Csv::parse(text, m.delimiter);
    const QList<QStringList> rows = m.hasHeader ? all.mid(1) : all;
    const int firstRow = m.hasHeader ? 2 : 1;
    if (!m.isValid()) {
        result.errors << BankParseError{0, QStringLiteral("no date and amount columns could be identified; map them")};
        result.mapping = m;
        return result;
    }
    if (m.dateFormat.isEmpty()) {
        QStringList dates;
        for (const QStringList &r : rows) {
            if (m.dateColumn < r.size()) {
                dates << r[m.dateColumn];
            }
        }
        m.dateFormat = detectDateFormat(dates, m.dayFirst);
        if (m.dateFormat.isEmpty()) {
            // Mixed rows: fall back to what the majority parses as.
            m.dateFormat = detectDateFormat(dates.mid(0, 1), m.dayFirst);
        }
    }
    result.mapping = m;
    auto cell = [](const QStringList &r, int c) { return c >= 0 && c < r.size() ? r[c].trimmed() : QString(); };
    // A debit/credit marker column only signs amounts in files whose
    // amount column is unsigned; when any amount is negative the file is
    // already signed and the "type" is just a description (DEBIT_CARD …).
    bool unsignedAmounts = m.amountColumn >= 0 && m.typeColumn >= 0;
    for (const QStringList &r : rows) {
        const std::optional<qint64> a = parseAmount(cell(r, m.amountColumn), exponent, m.decimalComma);
        unsignedAmounts = unsignedAmounts && !(a && *a < 0);
    }
    for (int i = 0; i < rows.size(); ++i) {
        const QStringList &r = rows[i];
        const int rowNo = firstRow + i;
        bool blank = true;
        for (const QString &v : r) {
            blank = blank && v.trimmed().isEmpty();
        }
        if (blank) {
            continue;
        }
        BankLine line;
        line.date = parseDate(cell(r, m.dateColumn), m.dateFormat);
        if (!line.date.isValid()) {
            result.errors << BankParseError{rowNo, QStringLiteral("unreadable date \"%1\"").arg(cell(r, m.dateColumn))};
            continue;
        }
        std::optional<qint64> amount;
        if (m.amountColumn >= 0) {
            amount = parseAmount(cell(r, m.amountColumn), exponent, m.decimalComma);
            const QString type = cell(r, m.typeColumn).toLower();
            if (unsignedAmounts && amount && *amount > 0 && (type.startsWith(QLatin1String("debit")) || type == QLatin1String("dr")
                                          || type.startsWith(QLatin1String("withdraw")))) {
                amount = -*amount; // unsigned amount + a debit marker
            }
        } else {
            const QString out = cell(r, m.debitColumn);
            const QString in = cell(r, m.creditColumn);
            const std::optional<qint64> debit = out.isEmpty() ? std::optional<qint64>(0) : parseAmount(out, exponent, m.decimalComma);
            const std::optional<qint64> credit = in.isEmpty() ? std::optional<qint64>(0) : parseAmount(in, exponent, m.decimalComma);
            if (debit && credit && !(out.isEmpty() && in.isEmpty())) {
                amount = (*credit < 0 ? -*credit : *credit) - (*debit < 0 ? -*debit : *debit);
            }
        }
        if (!amount) {
            result.errors << BankParseError{rowNo, QStringLiteral("unreadable amount")};
            continue;
        }
        line.amount = m.invertSign ? -*amount : *amount;
        line.payee = cell(r, m.payeeColumn);
        line.memo = cell(r, m.memoColumn);
        line.checkNumber = cell(r, m.checkColumn);
        line.fitid = cell(r, m.fitidColumn);
        if (line.payee.isEmpty()) {
            line.payee = line.memo;
        }
        result.lines << line;
    }
    return result;
}

} // namespace QindaOffice::Books::BankImport
