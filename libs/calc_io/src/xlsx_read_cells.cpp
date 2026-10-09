// SPDX-License-Identifier: GPL-3.0-or-later
// Typed literal cells straight from the worksheet XML (xlsx_read_cells.h).
// Element names are compared as local names: producers may prefix them
// (the Open XML SDK writes <x:c>).

#include "xlsx_read_cells.h"

#include "xlsx_parts.h"
#include "xlsx_text.h"

#include "functions/functions.h"

#include <QDateTime>
#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO {

namespace {

// The text of a rich string element (<si>, <is>): every <t> directly in it
// or in one of its <r> runs, phonetic runs and properties skipped. The
// reader is left on the element's end tag. `element` must be a literal.
QString richText(QXmlStreamReader &reader, QLatin1StringView element)
{
    QString text;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.name() == element) {
            break;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        if (reader.name() == QLatin1String("t")) {
            text += reader.readElementText();
        } else if (reader.name() != QLatin1String("r")) {
            reader.skipCurrentElement(); // rPh, rPr, phoneticPr, extLst
        }
    }
    return decodeXString(text);
}

std::optional<Value> isoDate(const QString &text)
{
    QDateTime when = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!when.isValid()) {
        const QDate date = QDate::fromString(text, Qt::ISODate);
        if (!date.isValid()) {
            return std::nullopt;
        }
        when = QDateTime(date, QTime(0, 0));
    }
    return serialFromDate(when.date()) + when.time().msecsSinceStartOfDay() / 86400000.0;
}

TypedCell typedValue(const QString &type, const QString &v, const QString &inlineText,
                     const QStringList &sharedStrings)
{
    if (type == QLatin1String("s")) {
        bool ok = false;
        const int index = v.trimmed().toInt(&ok);
        return {ok && index >= 0 && index < sharedStrings.size() ? Value(sharedStrings.at(index)) : Value()};
    }
    if (type == QLatin1String("inlineStr")) {
        return {inlineText};
    }
    if (type == QLatin1String("str")) {
        return {decodeXString(v)};
    }
    if (type == QLatin1String("b")) {
        const QString flag = v.trimmed();
        return {flag == QLatin1String("1") || flag.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0};
    }
    if (type == QLatin1String("e")) {
        return {errorFromFile(v)};
    }
    if (type == QLatin1String("d")) {
        if (const std::optional<Value> serial = isoDate(v.trimmed())) {
            return {*serial, true};
        }
        return {decodeXString(v)};
    }
    // t="n" or no t at all: Excel's number. An empty <v/> is a blank cell;
    // anything that does not read as a number is kept as the text it is
    // rather than lost.
    const QString number = v.trimmed();
    if (number.isEmpty()) {
        return {};
    }
    bool ok = false;
    const double d = number.toDouble(&ok); // QString::toDouble is C-locale
    return {ok ? Value(d) : Value(v)};
}

} // namespace

QStringList readSharedStrings(const QByteArray &sstXml)
{
    QStringList out;
    QXmlStreamReader reader(sstXml);
    while (!reader.atEnd()) {
        if (reader.readNext() == QXmlStreamReader::StartElement && reader.name() == QLatin1String("si")) {
            out.append(richText(reader, QLatin1StringView("si")));
        }
    }
    return out;
}

QHash<quint64, TypedCell> readTypedCells(const QByteArray &sheetXml, const QStringList &sharedStrings)
{
    return scanSheetCells(sheetXml, sharedStrings, {}).literals;
}

CellScan scanSheetCells(const QByteArray &sheetXml, const QStringList &sharedStrings, const QSet<int> &watchedXfs)
{
    CellScan out;
    QXmlStreamReader reader(sheetXml);
    qint32 row = 0; // 1-based; tracked for producers that omit r
    qint32 col = 0;
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        if (reader.name() == QLatin1String("row")) {
            const QString r = reader.attributes().value(QLatin1String("r")).toString();
            row = r.isEmpty() ? row + 1 : r.toInt();
            col = 0;
            continue;
        }
        if (reader.name() != QLatin1String("c")) {
            continue;
        }
        const auto attributes = reader.attributes();
        const QString type = attributes.value(QLatin1String("t")).toString();
        CellAddr addr{row - 1, col};
        if (const std::optional<CellAddr> parsed = addrFromText(attributes.value(QLatin1String("r")).toString())) {
            addr = *parsed;
        }
        col = addr.col + 1;
        if (!watchedXfs.isEmpty() && attributes.hasAttribute(QLatin1String("s"))) {
            const int xf = attributes.value(QLatin1String("s")).toInt();
            if (watchedXfs.contains(xf) && addr.isValid()) {
                out.watchedCells.insert(addr.key(), xf);
            }
        }
        const int cm = attributes.value(QLatin1String("cm")).toInt();
        QString v;
        QString inlineText;
        bool formula = false;
        bool valued = false;
        while (!reader.atEnd()) {
            const auto token = reader.readNext();
            if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("c")) {
                break;
            }
            if (token != QXmlStreamReader::StartElement) {
                continue;
            }
            if (reader.name() == QLatin1String("v")) {
                v = reader.readElementText();
                valued = true;
            } else if (reader.name() == QLatin1String("is")) {
                inlineText = richText(reader, QLatin1StringView("is"));
                valued = true;
            } else {
                if (reader.name() == QLatin1String("f")) {
                    formula = true;
                    const auto f = reader.attributes();
                    if (f.value(QLatin1String("t")) == QLatin1String("array") && addr.isValid()) {
                        const auto ref = rangeFromText(f.value(QLatin1String("ref")).toString());
                        out.arrays.append({addr, ref.value_or(CellRange{addr, addr}), cm});
                    }
                }
                reader.skipCurrentElement();
            }
        }
        if (!formula && valued && addr.isValid()) {
            out.literals.insert(addr.key(), typedValue(type, v, inlineText, sharedStrings));
        }
    }
    return out;
}

} // namespace QindaOffice::Calc::IO
