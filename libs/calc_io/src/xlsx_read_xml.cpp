// SPDX-License-Identifier: GPL-3.0-or-later
// Direct package readers for the sheet-XML features QXlsx has no public
// model for: conditional formats (+ the dxf table), data validations, the
// autofilter and cell comments. Everything here reads the NORMALIZED
// package (xlsx_package.h), so relationship targets are already relative;
// element names are compared as local names because producers may prefix
// (the Open XML SDK writes <x:worksheet>).

#include "xlsx_read_xml.h"

#include "sheet.h"
#include "workbook.h"

#include <QDir>
#include <QXmlStreamReader>

#include <xlsxcolor_p.h>
#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

namespace {

// ---------------------------------------------------------------- utilities

std::optional<CellAddr> addrFromText(const QString &text)
{
    QString clean = text;
    clean.remove(QLatin1Char('$'));
    int i = 0;
    while (i < clean.size() && clean.at(i).isLetter()) {
        ++i;
    }
    if (i == 0 || i == clean.size()) {
        return std::nullopt;
    }
    const std::optional<qint32> col = colFromName(clean.left(i));
    bool ok = false;
    const qint32 row = clean.mid(i).toInt(&ok);
    if (!col.has_value() || !ok || row < 1) {
        return std::nullopt;
    }
    return CellAddr{row - 1, *col};
}

std::optional<CellRange> rangeFromText(const QString &text)
{
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        const std::optional<CellAddr> single = addrFromText(text);
        if (!single.has_value()) {
            return std::nullopt;
        }
        return CellRange{*single, *single};
    }
    const std::optional<CellAddr> first = addrFromText(text.left(colon));
    const std::optional<CellAddr> last = addrFromText(text.mid(colon + 1));
    if (!first.has_value() || !last.has_value()) {
        return std::nullopt;
    }
    return CellRange{*first, *last};
}

// id -> (type, target) for a .rels part. Missing part yields an empty map.
QHash<QString, QPair<QString, QString>> readRels(QXlsx::ZipReader &zip, const QString &relsPath)
{
    QHash<QString, QPair<QString, QString>> out;
    const QByteArray xml = zip.fileData(relsPath);
    if (xml.isEmpty()) {
        return out;
    }
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("Relationship")) {
            continue;
        }
        const auto attributes = reader.attributes();
        out.insert(attributes.value(QLatin1String("Id")).toString(),
                   {attributes.value(QLatin1String("Type")).toString(),
                    attributes.value(QLatin1String("Target")).toString()});
    }
    return out;
}

// sheet index -> part path, from xl/workbook.xml + its rels.
QVector<QString> sheetPartMap(QXlsx::ZipReader &zip)
{
    QVector<QString> out;
    const QHash<QString, QPair<QString, QString>> rels = readRels(zip, QStringLiteral("xl/_rels/workbook.xml.rels"));
    const QByteArray xml = zip.fileData(QStringLiteral("xl/workbook.xml"));
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("sheet")) {
            continue;
        }
        const QString rid = reader.attributes().value(QLatin1String("r:id")).toString();
        const QString target = rels.value(rid).second;
        out.append(target.isEmpty() ? QString()
                                    : QDir::cleanPath(QStringLiteral("xl/") + target));
    }
    return out;
}

// Resolves a relationship target against the directory of the part that
// owns the .rels (e.g. "xl/worksheets").
QString resolveTarget(const QString &ownerDir, const QString &target)
{
    if (target.startsWith(QLatin1Char('/'))) {
        return QDir::cleanPath(target.mid(1));
    }
    return QDir::cleanPath(ownerDir + QLatin1Char('/') + target);
}

// A colour element's value (rgb / theme+tint / indexed), resolved.
std::optional<Rgb> colorFromAttributes(const QXmlStreamAttributes &attributes,
                                       const ThemePalette &palette)
{
    if (attributes.hasAttribute(QLatin1String("rgb"))) {
        // xlsx hex colours carry no '#' ("FFC7CE" is ARGB).
        const QColor c = QColor::fromString(
            QStringLiteral("#") + attributes.value(QLatin1String("rgb")).toString());
        if (c.isValid()) {
            return Rgb{quint8(c.red()), quint8(c.green()), quint8(c.blue())};
        }
    }
    if (attributes.hasAttribute(QLatin1String("theme"))) {
        const QXlsx::XlsxColor raw(attributes.value(QLatin1String("theme")).toString(),
                                   attributes.value(QLatin1String("tint")).toString());
        return resolveXlsxColor(raw, palette);
    }
    if (attributes.hasAttribute(QLatin1String("indexed"))) {
        bool ok = false;
        const int index = attributes.value(QLatin1String("indexed")).toInt(&ok);
        if (ok) {
            return resolveXlsxColor(QXlsx::XlsxColor(index), palette);
        }
    }
    return std::nullopt;
}

// --------------------------------------------------------------------- dxf

// The dxf table in xl/styles.xml: the formats conditional-format rules
// reference by dxfId. Excel's dxf convention carries the visible fill in
// patternFill's bgColor.
QList<CondFormat> readDxfs(QXlsx::ZipReader &zip, const ThemePalette &palette)
{
    QList<CondFormat> out;
    const QByteArray xml = zip.fileData(QStringLiteral("xl/styles.xml"));
    if (xml.isEmpty()) {
        return out;
    }
    QXmlStreamReader reader(xml);
    bool insideDxf = false;
    CondFormat current;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement && reader.name() == QLatin1String("dxf")) {
            insideDxf = true;
            current = CondFormat{};
            continue;
        }
        if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("dxf")) {
            insideDxf = false;
            out.append(current);
            continue;
        }
        if (!insideDxf || token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        if (name == QLatin1String("b")) {
            current.bold = true;
        } else if (name == QLatin1String("i")) {
            current.italic = true;
        } else if (name == QLatin1String("color")) {
            current.foreground = colorFromAttributes(reader.attributes(), palette);
        } else if (name == QLatin1String("bgColor") || name == QLatin1String("fgColor")) {
            // dxf: bgColor is the visible fill; fgColor is the fallback.
            const std::optional<Rgb> color = colorFromAttributes(reader.attributes(), palette);
            if (color.has_value() && (name == QLatin1String("bgColor") || !current.background.has_value())) {
                current.background = color;
            }
        }
    }
    return out;
}

// -------------------------------------------------------- conditional rules

using Op = CondFormatRule::Op;

std::optional<Op> cellIsOperator(const QStringView op)
{
    static const QHash<QString, Op> map = {
        {QStringLiteral("greaterThan"), Op::GreaterThan},
        {QStringLiteral("greaterThanOrEqual"), Op::GreaterThanOrEqual},
        {QStringLiteral("lessThan"), Op::LessThan},
        {QStringLiteral("lessThanOrEqual"), Op::LessThanOrEqual},
        {QStringLiteral("equal"), Op::EqualTo},
        {QStringLiteral("notEqual"), Op::NotEqualTo},
        {QStringLiteral("between"), Op::Between},
    };
    const auto it = map.constFind(op.toString());
    return it != map.constEnd() ? std::make_optional(it.value()) : std::nullopt;
}

// One <conditionalFormatting> block: its rules apply to every range in the
// sqref, so a multi-range block clones per range (order preserved).
void readCondFormatBlock(QXmlStreamReader &reader, const QList<CellRange> &ranges,
                         const QList<CondFormat> &dxfs, const ThemePalette &palette,
                         QList<QPair<int, CondFormatRule>> *out, int *skipped)
{
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement
            && reader.name() == QLatin1String("conditionalFormatting")) {
            return;
        }
        if (token != QXmlStreamReader::StartElement || reader.name() != QLatin1String("cfRule")) {
            continue;
        }
        const auto attributes = reader.attributes();
        const QStringView type = attributes.value(QLatin1String("type"));
        const int priority = attributes.value(QLatin1String("priority")).toInt();
        CondFormatRule rule;
        rule.stopIfTrue = attributes.value(QLatin1String("stopIfTrue")) == QLatin1String("1")
            || attributes.value(QLatin1String("stopIfTrue")) == QLatin1String("true");
        bool usable = true;
        QStringList formulas;
        while (!reader.atEnd()) {
            const auto inner = reader.readNext();
            if (inner == QXmlStreamReader::EndElement && reader.name() == QLatin1String("cfRule")) {
                break;
            }
            if (inner != QXmlStreamReader::StartElement) {
                continue;
            }
            if (reader.name() == QLatin1String("formula")) {
                formulas.append(reader.readElementText());
            } else if (reader.name() == QLatin1String("color") && type == QLatin1String("dataBar")) {
                const std::optional<Rgb> color = colorFromAttributes(reader.attributes(), palette);
                if (color.has_value()) {
                    rule.barColor = *color;
                }
            }
        }
        if (type == QLatin1String("cellIs")) {
            const std::optional<Op> op = cellIsOperator(attributes.value(QLatin1String("operator")));
            bool okFirst = false;
            const double first = formulas.value(0).toDouble(&okFirst);
            if (op.has_value() && okFirst) {
                rule.op = *op;
                rule.first = first;
                if (*op == Op::Between) {
                    bool okSecond = false;
                    const double second = formulas.value(1).toDouble(&okSecond);
                    if (!okSecond) {
                        usable = false;
                    } else {
                        rule.second = second;
                    }
                }
            } else {
                usable = false;
            }
        } else if (type == QLatin1String("expression")) {
            rule.op = Op::Expression;
            rule.formula = formulas.value(0);
            usable = !rule.formula.isEmpty() && prepareCondRule(rule);
        } else if (type == QLatin1String("dataBar")) {
            rule.op = Op::DataBar;
        } else if (type == QLatin1String("containsText")) {
            rule.op = Op::ContainsText;
            rule.text = attributes.value(QLatin1String("text")).toString();
            usable = !rule.text.isEmpty();
        } else if (type == QLatin1String("containsBlanks")) {
            rule.op = Op::IsEmpty;
        } else if (type == QLatin1String("containsNoBlanks")) {
            rule.op = Op::NotEmpty;
        } else {
            usable = false; // colorScale, iconSet, top10, ... — not modelled
        }
        if (!usable) {
            ++(*skipped);
            continue;
        }
        // dxfId only when the attribute is present — value("dxfId").toInt()
        // on an absent attribute is 0 and would steal dxf 0's format.
        if (attributes.hasAttribute(QLatin1String("dxfId"))) {
            const int dxfId = attributes.value(QLatin1String("dxfId")).toInt();
            if (dxfId >= 0 && dxfId < dxfs.size()) {
                rule.format = dxfs.at(dxfId);
            }
        }
        for (const CellRange &range : ranges) {
            rule.range = range;
            out->append({priority, rule});
        }
    }
}

QList<CondFormatRule> readCondFormats(const QByteArray &xml, const QList<CondFormat> &dxfs,
                                      const ThemePalette &palette, int *skipped)
{
    QList<QPair<int, CondFormatRule>> byPriority;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("conditionalFormatting")) {
            continue;
        }
        QList<CellRange> ranges;
        const QString sqref = reader.attributes().value(QLatin1String("sqref")).toString();
        for (const QString &part : sqref.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (const std::optional<CellRange> range = rangeFromText(part)) {
                ranges.append(*range);
            }
        }
        if (ranges.isEmpty()) {
            reader.skipCurrentElement();
            continue;
        }
        readCondFormatBlock(reader, ranges, dxfs, palette, &byPriority, skipped);
    }
    // Excel's evaluation order is ascending priority number; the engine's
    // list order is exactly that (first-wins per field).
    std::stable_sort(byPriority.begin(), byPriority.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    QList<CondFormatRule> out;
    out.reserve(byPriority.size());
    for (const auto &[priority, rule] : byPriority) {
        Q_UNUSED(priority);
        out.append(rule);
    }
    return out;
}

// ----------------------------------------------------------- data validation

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

// ------------------------------------------------------------- auto filter

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

// ---------------------------------------------------------------- comments

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

} // namespace

void readSheetExtras(const QString &packagePath, Workbook *wb, qint32 sheetIndex,
                     const ThemePalette &palette, Sheet *sheet)
{
    QXlsx::ZipReader zip(packagePath);
    if (!zip.exists()) {
        return;
    }
    const QVector<QString> parts = sheetPartMap(zip);
    if (sheetIndex < 0 || sheetIndex >= parts.size() || parts.at(sheetIndex).isEmpty()) {
        return;
    }
    const QByteArray xml = zip.fileData(parts.at(sheetIndex));
    if (xml.isEmpty()) {
        return;
    }
    int skipped = 0;
    const QList<CondFormat> dxfs = readDxfs(zip, palette);
    QList<CondFormatRule> rules = readCondFormats(xml, dxfs, palette, &skipped);
    if (!rules.isEmpty()) {
        // Imported rules keep their precedence order; anything already on
        // the sheet (nothing, on import) would come first.
        sheet->setCondFormats(rules);
    }
    const QList<ValidationRule> validations = readValidations(xml, wb, sheetIndex, &skipped);
    if (!validations.isEmpty()) {
        sheet->setValidations(validations);
    }
    const AutoFilter filter = readAutoFilter(xml);
    if (filter.active) {
        sheet->setAutoFilter(filter);
    }
    const QHash<quint64, CellComment> comments = readComments(zip, parts.at(sheetIndex));
    if (!comments.isEmpty()) {
        sheet->setComments(comments);
    }
    if (skipped > 0) {
        qInfo("xlsx: sheet %d: skipped %d unmappable rule(s)", int(sheetIndex), skipped);
    }
}

} // namespace QindaOffice::Calc::IO
