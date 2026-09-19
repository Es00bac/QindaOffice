// SPDX-License-Identifier: GPL-3.0-or-later
// Direct package readers for data validations, the autofilter and cell
// comments (moved out of xlsx_read_xml.cpp in M35 to keep files inside the
// line budget; the header is xlsx_read_xml.h).

#include "xlsx_read_xml.h"

#include "xlsx_parts.h"

#include "workbook.h"

#include <QXmlStreamReader>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

namespace {

QStringList validationListItems(const QString &formula1, Workbook *wb, qint32 sheetIndex)
{
    QStringList items;
    if (formula1.startsWith(QLatin1Char('"'))) {
        // Inline list: "a,b,c" with doubled quotes for a literal quote.
        QString body = formula1.mid(1);
        if (body.endsWith(QLatin1Char('"'))) {
            body.chop(1);
        }
        body.replace(QStringLiteral("\"\""), QStringLiteral("\x01"));
        for (QString item : body.split(QLatin1Char(','))) {
            items.append(item.replace(QStringLiteral("\x01"), QStringLiteral("\"")));
        }
        return items;
    }
    // Range reference: resolved to the current values at import (the live
    // link is lost — documented D-034).
    const QString clean = formula1;
    const int bang = clean.indexOf(QLatin1Char('!'));
    const std::optional<CellRange> range = rangeFromText(clean.mid(bang + 1));
    if (!range.has_value()) {
        return items;
    }
    qint32 sheet = sheetIndex;
    if (bang > 0) {
        QString sheetName = clean.left(bang);
        sheetName.remove(QLatin1Char('\''));
        const qint32 named = wb->sheetIndex(sheetName);
        if (named < 0) {
            return items;
        }
        sheet = named;
    }
    const QVector<QPair<CellAddr, Value>> cells = wb->nonBlankCellsIn(sheet, *range);
    for (const auto &[addr, value] : cells) {
        Q_UNUSED(addr);
        items.append(toText(value));
    }
    return items;
}

} // namespace

QList<ValidationRule> readValidations(const QByteArray &xml, Workbook *wb, qint32 sheetIndex,
                                      int *skipped)
{
    QList<ValidationRule> out;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("dataValidation")) {
            continue;
        }
        const auto attributes = reader.attributes();
        const QStringView type = attributes.value(QLatin1String("type"));
        const QStringView op = attributes.value(QLatin1String("operator"));
        const QStringView errorStyle = attributes.value(QLatin1String("errorStyle"));
        const QString errorMessage = attributes.value(QLatin1String("error")).toString();
        QStringList formulas;
        while (!reader.atEnd()) {
            const auto inner = reader.readNext();
            if (inner == QXmlStreamReader::EndElement && reader.name() == QLatin1String("dataValidation")) {
                break;
            }
            if (inner == QXmlStreamReader::StartElement && reader.name().startsWith(QLatin1String("formula"))) {
                formulas.append(reader.readElementText());
            }
        }
        ValidationRule rule;
        bool usable = true;
        if (!errorStyle.isEmpty() && errorStyle != QLatin1String("stop")) {
            // Warning/Information prompts allow the entry; the engine only
            // hard-rejects, so importing one would block valid input.
            usable = false;
        } else if (type == QLatin1String("list")) {
            rule.kind = ValidationRule::Kind::List;
            rule.list = validationListItems(formulas.value(0), wb, sheetIndex);
            usable = !rule.list.isEmpty();
        } else if (type == QLatin1String("whole") || type == QLatin1String("decimal")) {
            // An absent operator IS between (writers omit the default).
            if (!op.isEmpty() && op != QLatin1String("between")) {
                usable = false; // never approximate a different operator into min/max
            } else {
                bool ok1 = false;
                bool ok2 = false;
                const double min = formulas.value(0).toDouble(&ok1);
                const double max = formulas.value(1).toDouble(&ok2);
                if (ok1 && ok2) {
                    rule.kind = type == QLatin1String("whole") ? ValidationRule::Kind::WholeNumber
                                                               : ValidationRule::Kind::Decimal;
                    rule.min = min;
                    rule.max = max;
                } else {
                    usable = false;
                }
            }
        } else if (type == QLatin1String("textLength")) {
            if (!op.isEmpty() && op != QLatin1String("between")) {
                usable = false;
            } else {
                bool ok1 = false;
                bool ok2 = false;
                const double min = formulas.value(0).toDouble(&ok1);
                const double max = formulas.value(1).toDouble(&ok2);
                if (ok1 && ok2) {
                    rule.kind = ValidationRule::Kind::TextLength;
                    rule.min = min;
                    rule.max = max;
                } else {
                    usable = false;
                }
            }
        } else {
            usable = false; // custom, date, time — not modelled
        }
        if (!usable) {
            ++(*skipped);
            continue;
        }
        rule.message = errorMessage;
        const QString sqref = attributes.value(QLatin1String("sqref")).toString();
        for (const QString &part : sqref.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (const std::optional<CellRange> range = rangeFromText(part)) {
                rule.range = *range;
                out.append(rule);
            }
        }
    }
    return out;
}

AutoFilter readAutoFilter(const QByteArray &xml)
{
    AutoFilter out;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("autoFilter")) {
            continue;
        }
        const std::optional<CellRange> range =
            rangeFromText(reader.attributes().value(QLatin1String("ref")).toString());
        if (!range.has_value()) {
            continue;
        }
        out.range = *range;
        out.active = true;
        while (!reader.atEnd()) {
            const auto token = reader.readNext();
            if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("autoFilter")) {
                break;
            }
            if (token != QXmlStreamReader::StartElement
                || reader.name() != QLatin1String("filterColumn")) {
                continue;
            }
            // colId is relative to the filter range's first column.
            const qint32 col = out.range.topLeft.col
                + reader.attributes().value(QLatin1String("colId")).toInt();
            QStringList values;
            while (!reader.atEnd()) {
                const auto inner = reader.readNext();
                if (inner == QXmlStreamReader::EndElement
                    && reader.name() == QLatin1String("filterColumn")) {
                    break;
                }
                if (inner == QXmlStreamReader::StartElement && reader.name() == QLatin1String("filter")) {
                    values.append(reader.attributes().value(QLatin1String("val")).toString());
                }
            }
            if (!values.isEmpty()) {
                out.allowed.insert(col, values);
            }
        }
    }
    return out;
}

QHash<quint64, CellComment> readComments(QXlsx::ZipReader &zip, const QString &sheetPart)
{
    QHash<quint64, CellComment> out;
    const int slash = sheetPart.lastIndexOf(QLatin1Char('/'));
    const QString ownerDir = sheetPart.left(slash);
    const QString relsPath = ownerDir + QStringLiteral("/_rels") + sheetPart.mid(slash)
        + QStringLiteral(".rels");
    const QHash<QString, QPair<QString, QString>> rels = readRels(zip, relsPath);
    QString commentsPart;
    for (auto it = rels.constBegin(); it != rels.constEnd(); ++it) {
        if (it.value().first.endsWith(QLatin1String("/comments"))) {
            commentsPart = resolveTarget(ownerDir, it.value().second);
            break;
        }
    }
    if (commentsPart.isEmpty()) {
        return out;
    }
    const QByteArray xml = zip.fileData(commentsPart);
    if (xml.isEmpty()) {
        return out;
    }
    QStringList authors;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement && reader.name() == QLatin1String("author")) {
            authors.append(reader.readElementText());
            continue;
        }
        if (token != QXmlStreamReader::StartElement || reader.name() != QLatin1String("comment")) {
            continue;
        }
        const auto attributes = reader.attributes();
        const std::optional<CellAddr> addr = addrFromText(attributes.value(QLatin1String("ref")).toString());
        if (!addr.has_value()) {
            reader.skipCurrentElement();
            continue;
        }
        CellComment comment;
        comment.author = authors.value(attributes.value(QLatin1String("authorId")).toInt());
        // The text is one or more rich runs; concatenate the plain text.
        while (!reader.atEnd()) {
            const auto inner = reader.readNext();
            if (inner == QXmlStreamReader::EndElement && reader.name() == QLatin1String("comment")) {
                break;
            }
            if (inner == QXmlStreamReader::StartElement && reader.name() == QLatin1String("t")) {
                comment.text += reader.readElementText();
            }
        }
        out.insert(addr->key(), comment);
    }
    return out;
}

} // namespace QindaOffice::Calc::IO
