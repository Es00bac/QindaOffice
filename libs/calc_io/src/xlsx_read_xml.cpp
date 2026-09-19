// SPDX-License-Identifier: GPL-3.0-or-later
// Direct package readers for conditional formats (+ the dxf table) and the
// orchestration of the per-sheet extras (validations/autofilter/comments in
// xlsx_read_parts.cpp, charts in xlsx_read_charts.cpp). Reads the NORMALIZED
// package, so relationship targets are already relative; element names are
// compared as local names because producers may prefix.

#include "xlsx_read_xml.h"

#include "xlsx_parts.h"

#include "sheet.h"
#include "workbook.h"

#include <QXmlStreamReader>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

namespace {

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
    const QList<TableSpec> tables = readTables(zip, parts.at(sheetIndex));
    if (!tables.isEmpty()) {
        sheet->setTables(tables);
    }
    if (skipped > 0) {
        qInfo("xlsx: sheet %d: skipped %d unmappable rule(s)", int(sheetIndex), skipped);
    }
}

} // namespace QindaOffice::Calc::IO
