// SPDX-License-Identifier: GPL-3.0-or-later
#include "csv.h"

#include <QSet>
#include <QStringDecoder>

namespace QindaOffice::Csv {

int Table::columnCount() const
{
    int count = 0;
    for (const QStringList &row : rows) {
        count = qMax(count, row.size());
    }
    return count;
}

QString Table::cellAt(int row, int col) const
{
    if (row < 0 || row >= rows.size()) {
        return QString();
    }
    const QStringList &r = rows.at(row);
    return col >= 0 && col < r.size() ? r.at(col) : QString();
}

QStringList splitPlainLine(const QString &line, const QString &separator)
{
    return line.split(separator);
}

namespace {

QString detectSeparator(const QString &sample)
{
    static const QStringList kCandidates = {QStringLiteral(","), QStringLiteral(";"),
                                            QStringLiteral("\t"), QStringLiteral("|")};
    QString best;
    int bestScore = -1;
    for (const QString &candidate : kCandidates) {
        const QStringList lines = sample.split(QLatin1Char('\n')).mid(0, 20);
        int linesWith = 0;
        int columns = 0;
        for (const QString &line : lines) {
            if (line.contains(candidate)) {
                ++linesWith;
                columns = qMax(columns, line.count(candidate) + 1);
            }
        }
        const int score = linesWith * 100 + columns;
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best.isEmpty() ? QStringLiteral(",") : best;
}

} // namespace

Table parse(const QByteArray &data, const ReadOptions &options, QString *error)
{
    Table table;
    if (error != nullptr) {
        error->clear();
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(data);
    if (text.isEmpty() && !data.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("input is not valid UTF-8");
        }
        return table;
    }
    if (text.startsWith(QChar(0xfeff))) {
        text.remove(0, 1); // strip BOM
    }
    const QString sample = text.left(8192);
    table.separator = options.separator.isEmpty() ? detectSeparator(sample) : options.separator;
    table.decimal = options.decimal;
    if (table.decimal.isEmpty()) {
        table.decimal = table.separator == QLatin1String(";") ? QStringLiteral(",")
                                                              : QStringLiteral(".");
    }

    QStringList fields;
    QString field;
    bool inQuotes = false;
    int i = 0;
    const int n = text.size();
    auto endRow = [&]() {
        fields.append(field);
        field.clear();
        table.rows.append(fields);
        fields.clear();
    };
    while (i < n) {
        const QChar c = text.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    i += 2;
                } else {
                    inQuotes = false;
                    ++i;
                }
            } else {
                field += c;
                ++i;
            }
            continue;
        }
        if (c == QLatin1Char('"') && field.isEmpty()) {
            inQuotes = true;
            ++i;
        } else if (c == table.separator) {
            fields.append(field);
            field.clear();
            ++i;
        } else if (c == QLatin1Char('\n')) {
            endRow();
            ++i;
        } else if (c == QLatin1Char('\r')) {
            if (i + 1 < n && text.at(i + 1) == QLatin1Char('\n')) {
                endRow();
                i += 2;
            } else {
                field += c;
                ++i;
            }
        } else {
            field += c;
            ++i;
        }
    }
    if (!field.isEmpty() || !fields.isEmpty()) {
        endRow();
    }
    return table;
}

QByteArray serialize(const Table &table, const WriteOptions &options)
{
    QString separator = options.separator;
    if (options.excelCompatible && separator == QLatin1String(",")) {
        separator = QStringLiteral(";"); // locale separator for the Excel path
    }
    const QString crlf = QStringLiteral("\r\n");
    QString out;
    if (options.excelCompatible) {
        out += QChar(0xfeff); // BOM
    }
    for (const QStringList &row : table.rows) {
        for (int i = 0; i < row.size(); ++i) {
            if (i > 0) {
                out += separator;
            }
            const QString &cell = row.at(i);
            const bool needsQuotes = cell.contains(separator) || cell.contains(QLatin1Char('"'))
                || cell.contains(QLatin1Char('\n')) || cell.contains(QLatin1Char('\r'));
            if (needsQuotes) {
                out += QLatin1Char('"');
                QString escaped = cell;
                escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                out += escaped;
                out += QLatin1Char('"');
            } else {
                out += cell;
            }
        }
        out += crlf;
    }
    return out.toUtf8();
}

} // namespace QindaOffice::Csv
