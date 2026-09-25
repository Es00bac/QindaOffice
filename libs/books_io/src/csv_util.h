// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RFC 4180 CSV for every books_io reader and writer: quoting on write,
// quoted fields (with doubled quotes and embedded line breaks) on read,
// delimiter detection and a forgiving text decode for bank downloads.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Books::Csv {

// Quotes when the value holds the delimiter, a quote, CR/LF, or leading/
// trailing whitespace.
[[nodiscard]] QString field(const QString &value, QChar delimiter = QLatin1Char(','));
[[nodiscard]] QString line(const QStringList &values, QChar delimiter = QLatin1Char(','));
// UTF-8, CRLF line endings (RFC 4180), no BOM.
[[nodiscard]] QByteArray document(const QList<QStringList> &rows, QChar delimiter = QLatin1Char(','));

[[nodiscard]] QList<QStringList> parse(const QString &text, QChar delimiter);
// ',', ';', tab or '|' — whichever splits the first lines most consistently.
[[nodiscard]] QChar detectDelimiter(const QString &text);
// UTF-8 (BOM stripped); bytes that are not valid UTF-8 are read as
// Windows-1252/Latin-1, which is what older bank exports use.
[[nodiscard]] QString decode(const QByteArray &bytes);

} // namespace QindaOffice::Books::Csv
