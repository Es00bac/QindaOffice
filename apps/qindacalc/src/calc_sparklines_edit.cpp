// SPDX-License-Identifier: GPL-3.0-or-later
// Sparklines in QindaCalc (calc_sparklines.h, M90, D-098): the colours,
// the axis's custom values and date axis, Group, Ungroup and Clear.

#include "calc_sparklines.h"

#include "calc_host.h"
#include "spreadsheet_document.h"

#include "sheet.h"
#include "theme_colors.h"
#include "workbook.h"

#include <QColor>
#include <QLocale>

#include <algorithm>
#include <iterator>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

// Theme roles as Excel's colour picker names them, and their theme index.
const int kRoleThemes[] = {4, 5, 6, 7, 8, 9, 1, 0, 3, 2};
// Excel's shades: the base colour, lighter 80 / 60 / 40 %, darker 25 / 50 %.
const double kShadeTints[] = {0.0, 0.7999816888943144, 0.5999938962981048, 0.3999755851924192, -0.249977111117893,
                              -0.499984740745262};

using ColorMember = SparklineColor SparklineGroup::*;
const std::pair<const char *, ColorMember> kColors[] = {
    {"series", &SparklineGroup::series},       {"negative", &SparklineGroup::negativeColor},
    {"markers", &SparklineGroup::markersColor}, {"high", &SparklineGroup::highColor},
    {"low", &SparklineGroup::lowColor},         {"first", &SparklineGroup::firstColor},
    {"last", &SparklineGroup::lastColor},       {"axis", &SparklineGroup::axisColor}};

} // namespace

QStringList CalcSparklines::colorRoles() const
{
    return {tr("Accent 1"), tr("Accent 2"), tr("Accent 3"), tr("Accent 4"), tr("Accent 5"), tr("Accent 6"),
            tr("Text 1"),   tr("Background 1"), tr("Text 2"), tr("Background 2"), tr("Custom")};
}

QStringList CalcSparklines::shades() const
{
    return {tr("Base colour"), tr("Lighter 80%"), tr("Lighter 60%"), tr("Lighter 40%"), tr("Darker 25%"), tr("Darker 50%")};
}

QVariantMap CalcSparklines::colorsState() const
{
    const SparklineRef ref = current();
    if (!ref.isValid()) {
        return {};
    }
    const SparklineGroup &group = m_host->sheet().sparklineGroups().at(ref.group);
    const QVector<Rgb> palette = themePalette(m_host->spreadsheetDocument()->workbook()->source.themeXml);
    QVariantMap out;
    for (const auto &[key, member] : kColors) {
        const SparklineColor &color = group.*member;
        const int role = color.rgb.has_value() || color.theme < 0
            ? int(std::size(kRoleThemes))
            : int(std::find(std::begin(kRoleThemes), std::end(kRoleThemes), color.theme) - std::begin(kRoleThemes));
        int shade = 0;
        for (int s = 0; s < int(std::size(kShadeTints)); ++s) {
            shade = std::abs(kShadeTints[s] - color.tint) < std::abs(kShadeTints[shade] - color.tint) ? s : shade;
        }
        const Rgb rgb = color.rgb.has_value() ? *color.rgb : themeColor(palette, color.theme, color.tint, Rgb{0, 0, 0});
        out.insert(QLatin1String(key), QVariantMap{{QStringLiteral("role"), role},
                                                   {QStringLiteral("shade"), shade},
                                                   {QStringLiteral("color"), QColor(rgb.r, rgb.g, rgb.b).name()}});
    }
    return out;
}

void CalcSparklines::applyColors(const QVariantMap &colors)
{
    change([&colors](SparklineGroup &g) {
        for (const auto &[key, member] : kColors) {
            const QVariantMap entry = colors.value(QLatin1String(key)).toMap();
            if (entry.isEmpty()) {
                continue;
            }
            const int role = entry.value(QStringLiteral("role")).toInt();
            SparklineColor color;
            if (role >= 0 && role < int(std::size(kRoleThemes))) {
                color.theme = kRoleThemes[role];
                color.tint = kShadeTints[qBound(0, entry.value(QStringLiteral("shade")).toInt(), int(std::size(kShadeTints)) - 1)];
            } else {
                const QColor custom(entry.value(QStringLiteral("color")).toString());
                color.rgb = Rgb{quint8(custom.red()), quint8(custom.green()), quint8(custom.blue())};
            }
            g.*member = color;
        }
    }, tr("Sparkline Colors"));
}

QString CalcSparklines::axisValue(bool max) const
{
    const SparklineRef ref = current();
    if (!ref.isValid()) {
        return {};
    }
    const SparklineGroup &group = m_host->sheet().sparklineGroups().at(ref.group);
    return QLocale::c().toString(max ? group.manualMax : group.manualMin, 'g', 15);
}

QString CalcSparklines::applyAxisValue(bool max, const QString &value)
{
    bool ok = false;
    const double number = QLocale::c().toDouble(value.trimmed(), &ok);
    if (!ok) {
        return tr("Enter a number.");
    }
    change([max, number](SparklineGroup &g) {
        (max ? g.maxAxis : g.minAxis) = SparklineGroup::AxisScale::Custom;
        (max ? g.manualMax : g.manualMin) = number;
    }, tr("Sparkline Axis"));
    return {};
}

QString CalcSparklines::dateRange() const
{
    const SparklineRef ref = current();
    return ref.isValid() ? m_host->sheet().sparklineGroups().at(ref.group).dateRange : QString();
}

QString CalcSparklines::applyDateAxis(const QString &range)
{
    const SparklineRef ref = current();
    if (!ref.isValid()) {
        return tr("Select a sparkline first.");
    }
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    const SparklineGroup &group = m_host->sheet().sparklineGroups().at(ref.group);
    // The dates are one row or column the size of the sparklines' data.
    QString error;
    const std::optional<QList<Sparkline>> dates = sparklinesFor(*document->workbook(), document->activeSheet(), range,
                                                                addrText(group.sparklines.first().location), &error);
    if (!dates.has_value()) {
        return tr("The date range is not valid.");
    }
    const QString text = dates->first().data;
    change([text](SparklineGroup &g) {
        g.dateAxis = true;
        g.dateRange = text;
    }, tr("Sparkline Axis"));
    return {};
}

void CalcSparklines::group()
{
    const QList<qint32> touched = touchedGroups();
    const SparklineRef ref = current();
    if (touched.size() < 2 || !ref.isValid()) {
        return;
    }
    // Excel: the group takes the look of the active cell's sparkline.
    const QList<SparklineGroup> &groups = m_host->sheet().sparklineGroups();
    SparklineGroup merged = groups.at(ref.group);
    merged.sparklines.clear();
    QList<SparklineGroup> out;
    for (qsizetype g = 0; g < groups.size(); ++g) {
        if (touched.contains(qint32(g))) {
            merged.sparklines += groups.at(g).sparklines;
        } else {
            out.append(groups.at(g));
        }
    }
    out.append(merged);
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    document->setSparklineGroups(document->activeSheet(), out, tr("Group Sparklines"));
    m_host->refreshGrid();
    m_host->syncFromCursor();
}

void CalcSparklines::ungroup()
{
    const QList<qint32> touched = touchedGroups();
    if (touched.isEmpty()) {
        return;
    }
    QList<SparklineGroup> out;
    const QList<SparklineGroup> &groups = m_host->sheet().sparklineGroups();
    for (qsizetype g = 0; g < groups.size(); ++g) {
        if (!touched.contains(qint32(g))) {
            out.append(groups.at(g));
            continue;
        }
        for (const Sparkline &line : groups.at(g).sparklines) {
            SparklineGroup single = groups.at(g);
            single.sparklines = {line};
            out.append(single);
        }
    }
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    document->setSparklineGroups(document->activeSheet(), out, tr("Ungroup Sparklines"));
    m_host->refreshGrid();
    m_host->syncFromCursor();
}

void CalcSparklines::clear(bool wholeGroups)
{
    const QList<qint32> touched = touchedGroups();
    if (touched.isEmpty()) {
        return;
    }
    const CellRange selection = m_host->selection();
    const CellAddr cursor = m_host->cursor();
    QList<SparklineGroup> out;
    const QList<SparklineGroup> &groups = m_host->sheet().sparklineGroups();
    for (qsizetype g = 0; g < groups.size(); ++g) {
        if (touched.contains(qint32(g)) && wholeGroups) {
            continue;
        }
        SparklineGroup kept = groups.at(g);
        kept.sparklines.removeIf([&](const Sparkline &line) { return selection.contains(line.location) || line.location == cursor; });
        if (!kept.sparklines.isEmpty()) {
            out.append(kept);
        }
    }
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    document->setSparklineGroups(document->activeSheet(), out,
                                 wholeGroups ? tr("Clear Sparkline Groups") : tr("Clear Sparklines"));
    m_host->refreshGrid();
    m_host->syncFromCursor();
}

} // namespace QindaOffice::Apps::QindaCalc
