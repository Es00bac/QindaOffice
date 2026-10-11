// SPDX-License-Identifier: GPL-3.0-or-later
// Sparklines read from and written to Excel's x14 markup (xlsx_sparklines.h,
// M90, D-098). Attribute defaults are the x14 schema's (CT_SparklineGroup):
// type line, lineWeight 0.75, displayEmptyCellsAs zero, every flag false,
// minAxisType / maxAxisType individual; the writer leaves defaults out and
// always states "gap" (Excel's own new groups do).

#include "xlsx_sparklines.h"

#include "xlsx_formula.h"
#include "xlsx_parts.h"

#include "parser.h"
#include "sheet.h"
#include "workbook.h"

#include <QLocale>
#include <QXmlStreamReader>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

namespace {

const QLatin1StringView kUri("{05C60535-1F16-4fd2-B633-F4F36F0B64E0}");

bool flagOf(const QXmlStreamAttributes &a, const char *name)
{
    const QStringView value = a.value(QLatin1String(name));
    return value == QLatin1String("1") || value == QLatin1String("true");
}

SparklineColor colorOf(const QXmlStreamAttributes &a)
{
    SparklineColor color;
    bool ok = false;
    const QString rgb = a.value(QLatin1String("rgb")).toString();
    if (rgb.size() >= 6) {
        const quint32 value = rgb.right(6).toUInt(&ok, 16);
        if (ok) {
            color.rgb = Rgb{quint8((value >> 16) & 0xff), quint8((value >> 8) & 0xff), quint8(value & 0xff)};
        }
    }
    const int theme = a.value(QLatin1String("theme")).toInt(&ok);
    if (ok) {
        color.theme = theme;
    }
    color.tint = a.value(QLatin1String("tint")).toDouble();
    return color;
}

SparklineGroup groupOf(const QXmlStreamAttributes &a)
{
    SparklineGroup group;
    group.empty = SparklineGroup::Empty::Zero; // the schema's default
    const auto axis = [&a](const char *name) {
        const QStringView value = a.value(QLatin1String(name));
        return value == QLatin1String("group")    ? SparklineGroup::AxisScale::Group
             : value == QLatin1String("custom")   ? SparklineGroup::AxisScale::Custom
                                                  : SparklineGroup::AxisScale::Individual;
    };
    for (const QXmlStreamAttribute &attribute : a) {
        const QString name = attribute.qualifiedName().toString();
        const QStringView value = attribute.value();
        if (name == QLatin1String("type")) {
            group.type = value == QLatin1String("column")    ? SparklineGroup::Type::Column
                       : value == QLatin1String("stacked") ? SparklineGroup::Type::WinLoss
                                                            : SparklineGroup::Type::Line;
        } else if (name == QLatin1String("displayEmptyCellsAs")) {
            group.empty = value == QLatin1String("gap")    ? SparklineGroup::Empty::Gap
                        : value == QLatin1String("span") ? SparklineGroup::Empty::Span
                                                          : SparklineGroup::Empty::Zero;
        } else if (name == QLatin1String("lineWeight")) {
            group.lineWeight = value.toDouble();
        } else if (name == QLatin1String("manualMin")) {
            group.manualMin = value.toDouble();
        } else if (name == QLatin1String("manualMax")) {
            group.manualMax = value.toDouble();
        } else if (!name.contains(QLatin1Char(':'))
                   && !QStringList{QStringLiteral("markers"), QStringLiteral("high"), QStringLiteral("low"),
                                   QStringLiteral("first"), QStringLiteral("last"), QStringLiteral("negative"),
                                   QStringLiteral("displayXAxis"), QStringLiteral("displayHidden"),
                                   QStringLiteral("rightToLeft"), QStringLiteral("dateAxis"),
                                   QStringLiteral("minAxisType"), QStringLiteral("maxAxisType")}
                            .contains(name)) {
            group.extraAttributes.append({name, value.toString()});
        }
    }
    group.markers = flagOf(a, "markers");
    group.high = flagOf(a, "high");
    group.low = flagOf(a, "low");
    group.first = flagOf(a, "first");
    group.last = flagOf(a, "last");
    group.negative = flagOf(a, "negative");
    group.displayXAxis = flagOf(a, "displayXAxis");
    group.displayHidden = flagOf(a, "displayHidden");
    group.rightToLeft = flagOf(a, "rightToLeft");
    group.dateAxis = flagOf(a, "dateAxis");
    group.minAxis = axis("minAxisType");
    group.maxAxis = axis("maxAxisType");
    return group;
}

SparklineColor *colorSlot(SparklineGroup &group, QStringView name)
{
    if (name == QLatin1String("colorSeries")) return &group.series;
    if (name == QLatin1String("colorNegative")) return &group.negativeColor;
    if (name == QLatin1String("colorAxis")) return &group.axisColor;
    if (name == QLatin1String("colorMarkers")) return &group.markersColor;
    if (name == QLatin1String("colorFirst")) return &group.firstColor;
    if (name == QLatin1String("colorLast")) return &group.lastColor;
    if (name == QLatin1String("colorHigh")) return &group.highColor;
    if (name == QLatin1String("colorLow")) return &group.lowColor;
    return nullptr;
}

QString number(double value)
{
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

QString escaped(QString text)
{
    text.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    text.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    text.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    text.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return text;
}

QString colorXml(const char *name, const SparklineColor &color)
{
    if (!color.isSet()) {
        return {};
    }
    QString xml = QStringLiteral("<x14:") + QLatin1String(name);
    if (color.rgb.has_value()) {
        xml += QStringLiteral(" rgb=\"FF%1\"").arg(QStringLiteral("%1%2%3")
                                                      .arg(color.rgb->r, 2, 16, QLatin1Char('0'))
                                                      .arg(color.rgb->g, 2, 16, QLatin1Char('0'))
                                                      .arg(color.rgb->b, 2, 16, QLatin1Char('0'))
                                                      .toUpper());
    } else {
        xml += QStringLiteral(" theme=\"%1\"").arg(color.theme);
        if (color.tint != 0.0) {
            xml += QStringLiteral(" tint=\"%1\"").arg(number(color.tint));
        }
    }
    return xml + QStringLiteral("/>");
}

QString groupXml(const SparklineGroup &group)
{
    QString xml = QStringLiteral("<x14:sparklineGroup");
    const auto attribute = [&xml](const char *name, const QString &value) {
        xml += QLatin1Char(' ') + QLatin1String(name) + QStringLiteral("=\"") + escaped(value) + QLatin1Char('"');
    };
    if (group.maxAxis == SparklineGroup::AxisScale::Custom) {
        attribute("manualMax", number(group.manualMax));
    }
    if (group.minAxis == SparklineGroup::AxisScale::Custom) {
        attribute("manualMin", number(group.manualMin));
    }
    if (group.lineWeight != 0.75) {
        attribute("lineWeight", number(group.lineWeight));
    }
    if (group.type != SparklineGroup::Type::Line) {
        attribute("type", group.type == SparklineGroup::Type::Column ? QStringLiteral("column") : QStringLiteral("stacked"));
    }
    if (group.dateAxis) {
        attribute("dateAxis", QStringLiteral("1"));
    }
    if (group.empty != SparklineGroup::Empty::Zero) {
        attribute("displayEmptyCellsAs", group.empty == SparklineGroup::Empty::Gap ? QStringLiteral("gap") : QStringLiteral("span"));
    }
    const std::pair<const char *, bool> flags[] = {
        {"markers", group.markers}, {"high", group.high}, {"low", group.low}, {"first", group.first},
        {"last", group.last}, {"negative", group.negative}, {"displayXAxis", group.displayXAxis},
        {"displayHidden", group.displayHidden}};
    for (const auto &[name, on] : flags) {
        if (on) {
            attribute(name, QStringLiteral("1"));
        }
    }
    const auto axisText = [](SparklineGroup::AxisScale scale) {
        return scale == SparklineGroup::AxisScale::Group ? QStringLiteral("group") : QStringLiteral("custom");
    };
    if (group.minAxis != SparklineGroup::AxisScale::Individual) {
        attribute("minAxisType", axisText(group.minAxis));
    }
    if (group.maxAxis != SparklineGroup::AxisScale::Individual) {
        attribute("maxAxisType", axisText(group.maxAxis));
    }
    if (group.rightToLeft) {
        attribute("rightToLeft", QStringLiteral("1"));
    }
    for (const auto &[name, value] : group.extraAttributes) {
        xml += QLatin1Char(' ') + name + QStringLiteral("=\"") + escaped(value) + QLatin1Char('"');
    }
    xml += QLatin1Char('>');
    xml += colorXml("colorSeries", group.series) + colorXml("colorNegative", group.negativeColor)
        + colorXml("colorAxis", group.axisColor) + colorXml("colorMarkers", group.markersColor)
        + colorXml("colorFirst", group.firstColor) + colorXml("colorLast", group.lastColor)
        + colorXml("colorHigh", group.highColor) + colorXml("colorLow", group.lowColor);
    if (group.dateAxis && !group.dateRange.isEmpty()) {
        xml += QStringLiteral("<xm:f>") + escaped(formulaForFile(group.dateRange)) + QStringLiteral("</xm:f>");
    }
    xml += QStringLiteral("<x14:sparklines>");
    for (const Sparkline &line : group.sparklines) {
        xml += QStringLiteral("<x14:sparkline>");
        if (!line.data.isEmpty()) {
            xml += QStringLiteral("<xm:f>") + escaped(formulaForFile(line.data)) + QStringLiteral("</xm:f>");
        }
        xml += QStringLiteral("<xm:sqref>") + addrText(line.location) + QStringLiteral("</xm:sqref></x14:sparkline>");
    }
    return xml + QStringLiteral("</x14:sparklines></x14:sparklineGroup>");
}

} // namespace

void readSparklines(QXlsx::ZipReader &zip, const QString &sheetPart, Sheet *sheet)
{
    QXmlStreamReader reader(zip.fileData(sheetPart));
    QList<SparklineGroup> groups;
    bool inExt = false;
    bool inSparkline = false;
    Sparkline current;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            if (inExt && reader.name() == QLatin1String("sparkline") && inSparkline) {
                if (current.location.isValid() && !groups.isEmpty()) {
                    groups.last().sparklines.append(current);
                }
                inSparkline = false;
            } else if (reader.name() == QLatin1String("ext")) {
                inExt = false;
            }
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        if (name == QLatin1String("ext")) {
            inExt = reader.attributes().value(QLatin1String("uri")).compare(kUri, Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inExt) {
            continue;
        }
        if (name == QLatin1String("sparklineGroup")) {
            groups.append(groupOf(reader.attributes()));
        } else if (groups.isEmpty()) {
            continue;
        } else if (SparklineColor *slot = colorSlot(groups.last(), name)) {
            *slot = colorOf(reader.attributes());
        } else if (name == QLatin1String("sparkline")) {
            inSparkline = true;
            current = Sparkline{};
        } else if (name == QLatin1String("f")) {
            const QString text = formulaFromFile(reader.readElementText().trimmed());
            (inSparkline ? current.data : groups.last().dateRange) = text;
        } else if (name == QLatin1String("sqref")) {
            const ParseResult parsed = parseFormula(reader.readElementText().trimmed().section(QLatin1Char(' '), 0, 0));
            if (parsed.root && parsed.root->kind == Node::Kind::Ref) {
                current.location = parsed.root->ref.range.topLeft;
            }
        }
    }
    QList<SparklineGroup> kept;
    for (const SparklineGroup &group : std::as_const(groups)) {
        if (!group.sparklines.isEmpty()) {
            kept.append(group);
        }
    }
    sheet->setSparklineGroups(kept);
}

void fillSparklinePatch(const Workbook &wb, qint32 sheet, PatchManifest::SheetPatch *patch)
{
    const QList<SparklineGroup> &groups = wb.sheetAt(sheet).sparklineGroups();
    if (groups.isEmpty()) {
        return;
    }
    QString xml = QStringLiteral("<ext uri=\"") + kUri
        + QStringLiteral("\" xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\">"
                         "<x14:sparklineGroups xmlns:xm=\"http://schemas.microsoft.com/office/excel/2006/main\">");
    for (const SparklineGroup &group : groups) {
        if (!group.sparklines.isEmpty()) {
            xml += groupXml(group);
        }
    }
    xml += QStringLiteral("</x14:sparklineGroups></ext>");
    patch->extXml += xml.toUtf8();
}

} // namespace QindaOffice::Calc::IO
