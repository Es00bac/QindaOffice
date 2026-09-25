// SPDX-License-Identifier: GPL-3.0-or-later
#include "csv_util.h"

#include <QStringDecoder>

namespace QindaOffice::Books::Csv {

QString field(const QString &value, QChar delimiter)
{
    const bool quote = value.contains(delimiter) || value.contains(QLatin1Char('"')) || value.contains(QLatin1Char('\n'))
        || value.contains(QLatin1Char('\r')) || (!value.isEmpty() && (value.front().isSpace() || value.back().isSpace()));
    if (!quote) {
        return value;
    }
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString line(const QStringList &values, QChar delimiter)
{
    QStringList out;
    out.reserve(values.size());
    for (const QString &v : values) {
        out << field(v, delimiter);
    }
    return out.join(delimiter);
}

QByteArray document(const QList<QStringList> &rows, QChar delimiter)
{
    QByteArray out;
    for (const QStringList &row : rows) {
        out += line(row, delimiter).toUtf8();
        out += "\r\n";
    }
    return out;
}

QList<QStringList> parse(const QString &text, QChar delimiter)
{
    QList<QStringList> rows;
    QStringList row;
    QString cell;
    bool quoted = false;
    bool any = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    cell += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') && cell.trimmed().isEmpty()) {
            cell.clear();
            quoted = true;
            any = true;
        } else if (c == delimiter) {
            row << cell;
            cell.clear();
            any = true;
        } else if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            if (any || !cell.isEmpty()) {
                row << cell;
                rows << row;
            }
            row.clear();
            cell.clear();
            any = false;
        } else {
            cell += c;
            any = true;
        }
    }
    if (any || !cell.isEmpty()) {
        row << cell;
        rows << row;
    }
    return rows;
}

QChar detectDelimiter(const QString &text)
{
    const QList<QChar> candidates{QLatin1Char(','), QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char('|')};
    QChar best = QLatin1Char(',');
    int bestScore = -1;
    for (QChar c : candidates) {
        const QList<QStringList> rows = parse(text.left(8192), c);
        if (rows.isEmpty()) {
            continue;
        }
        // Score: columns of the first row, if most rows agree on it.
        const qsizetype columns = rows.first().size();
        int agree = 0;
        for (const QStringList &r : rows) {
            agree += r.size() == columns ? 1 : 0;
        }
        // Consistency first (≥ 80 % of rows agree), then more columns.
        const bool consistent = agree * 10 >= int(rows.size()) * 8;
        const int score = columns > 1 && consistent ? int(columns) * 1000 + agree : agree;
        if (score > bestScore) {
            bestScore = score;
            best = c;
        }
    }
    return best;
}

QString decode(const QByteArray &bytes)
{
    QByteArray data = bytes;
    if (data.startsWith("\xEF\xBB\xBF")) {
        data.remove(0, 3);
    }
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = utf8.decode(data);
    if (!utf8.hasError()) {
        return text;
    }
    return QString::fromLatin1(data);
}

} // namespace QindaOffice::Books::Csv
