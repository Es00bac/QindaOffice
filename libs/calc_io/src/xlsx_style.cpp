// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_style.h"

#include "number_format.h"

#include <QXmlStreamReader>

#include <xlsxcolor_p.h>
#include <xlsxformat.h>
#include <xlsxformat_p.h>

namespace QindaOffice::Calc::IO {

namespace {

// The legacy indexed palette (ECMA-376 §18.8.27's table, indexes 0-63).
const QRgb kIndexedColors[64] = {
    0xFF000000, 0xFFFFFFFF, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF,
    0xFF000000, 0xFFFFFFFF, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF,
    0xFF800000, 0xFF008000, 0xFF000080, 0xFF808000, 0xFF800080, 0xFF008080, 0xFFC0C0C0, 0xFF808080,
    0xFF9999FF, 0xFF993366, 0xFFFFFFCC, 0xFFCCFFFF, 0xFF660066, 0xFFFF8080, 0xFF0066CC, 0xFFCCCCFF,
    0xFF000080, 0xFFFF00FF, 0xFFFFFF00, 0xFF00FFFF, 0xFF800080, 0xFF800000, 0xFF008080, 0xFF0000FF,
    0xFF00CCFF, 0xFFCCFFFF, 0xFFCCFFCC, 0xFFFFFF99, 0xFF99CCFF, 0xFFFF99CC, 0xFFCC99FF, 0xFFFFCC99,
    0xFF3366FF, 0xFF33CCCC, 0xFF99CC00, 0xFFFFCC00, 0xFFFF9900, 0xFFFF6600, 0xFF666699, 0xFF969696,
    0xFF003366, 0xFF339966, 0xFF003300, 0xFF333300, 0xFF993300, 0xFF993366, 0xFF333399, 0xFF333333,
};

// ECMA-376 tint as a channel-wise approximation (lumMod/lumOff): negative
// tints darken towards black, positive tints lighten towards white. Matches
// LibreOffice's and openpyxl's reading within rounding.
int applyTint(int channel, double tint)
{
    if (tint < 0.0) {
        return qBound(0, qRound(channel * (1.0 + tint)), 255);
    }
    return qBound(0, qRound(channel * (1.0 - tint) + 255.0 * tint), 255);
}

std::optional<QXlsx::XlsxColor> rawColor(const QXlsx::Format &format, int propertyId)
{
    const QVariant prop = format.property(propertyId);
    if (prop.userType() != qMetaTypeId<QXlsx::XlsxColor>()) {
        return std::nullopt;
    }
    return qvariant_cast<QXlsx::XlsxColor>(prop);
}

void readFontInto(const QXlsx::Format &format, const ThemePalette &palette, CellStyle *style)
{
    style->bold = format.fontBold();
    style->italic = format.fontItalic();
    style->underline = format.fontUnderline() != QXlsx::Format::FontUnderlineNone;
    style->strikeout = format.fontStrikeOut();
    // AGENT-NOTE: QXlsx reads <sz val> with toInt() and writes the int
    // getter, so half-point sizes (10.5, 13.5 — Excel staples) arrive as 0
    // (= engine default) and export whole-point. Restoring them needs the
    // styles.xml xf/font parsing of the M34+ package readers (D-033). The
    // generic property() is used because doubleProperty() rejects the
    // int-typed value QXlsx actually stores.
    const QVariant sizeProp = format.property(QXlsx::FormatPrivate::P_Font_Size);
    style->fontSize = sizeProp.isValid() ? sizeProp.toDouble() : 0.0;
    style->fontFamily = format.fontName();
    if (const auto color = rawColor(format, QXlsx::FormatPrivate::P_Font_Color)) {
        style->fg = resolveXlsxColor(*color, palette);
    }
}

void readFillInto(const QXlsx::Format &format, const ThemePalette &palette, CellStyle *style)
{
    if (format.fillPattern() != QXlsx::Format::PatternSolid) {
        return;
    }
    // AGENT-GUARD: for solid fills QXlsx swaps the two colour roles on
    // purpose ("Excel reverses the role of foreground and background
    // colours", writeFill): the file's fgColor — the visible fill — lands
    // in P_Fill_BgColor. Read THAT one, not patternForegroundColor (which
    // is the file's bgColor, usually absent or white).
    if (const auto color = rawColor(format, QXlsx::FormatPrivate::P_Fill_BgColor)) {
        style->bg = resolveXlsxColor(*color, palette);
    }
}

void readAlignmentInto(const QXlsx::Format &format, CellStyle *style)
{
    switch (format.horizontalAlignment()) {
    case QXlsx::Format::AlignHCenter:
        style->alignment = 1;
        break;
    case QXlsx::Format::AlignRight:
        style->alignment = 2;
        break;
    case QXlsx::Format::AlignHFill:
        style->alignment = 3; // Excel's Fill: the text repeated, never spilled (M82, D-091)
        break;
    default:
        style->alignment = 0;
        break;
    }
    switch (format.verticalAlignment()) {
    case QXlsx::Format::AlignTop:
        style->vAlign = 0;
        break;
    case QXlsx::Format::AlignBottom:
        style->vAlign = 2;
        break;
    default:
        style->vAlign = 1;
        break;
    }
    style->wrap = format.textWrap();
}

void readBordersInto(const QXlsx::Format &format, CellStyle *style)
{
    // The engine's border mask records presence, not per-side styles (D-033):
    // any real edge sets its bit; export writes those bits back as thin.
    int borders = 0;
    if (format.leftBorderStyle() != QXlsx::Format::BorderNone) {
        borders |= 1;
    }
    if (format.rightBorderStyle() != QXlsx::Format::BorderNone) {
        borders |= 2;
    }
    if (format.topBorderStyle() != QXlsx::Format::BorderNone) {
        borders |= 4;
    }
    if (format.bottomBorderStyle() != QXlsx::Format::BorderNone) {
        borders |= 8;
    }
    style->borders = borders;
}

} // namespace

ThemePalette ThemePalette::officeDefault()
{
    // The stock Office scheme (2013+), in theme-index order (lt1/dk1 swapped
    // out of file order, per the struct comment).
    return {{0xFFFFFFFF, 0xFF000000, 0xFFE7E6E6, 0xFF44546A,
             0xFF4472C4, 0xFFED7D31, 0xFFA5A5A5, 0xFFFFC000, 0xFF5B9BD5, 0xFF70AD47,
             0xFF0563C1, 0xFF954F72}};
}

ThemePalette themePaletteFromXml(const QByteArray &themeXml)
{
    // File order inside a:clrScheme: dk1, lt1, dk2, lt2, accent1..6, hlink,
    // folHlink. Each holds srgbClr@val or sysClr@lastClr. Scope strictly to
    // clrScheme — fmtScheme later in the part carries colours of its own.
    QVector<QRgb> fileOrder;
    bool insideScheme = false;
    QXmlStreamReader reader(themeXml);
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement && reader.name() == QLatin1String("clrScheme")) {
            insideScheme = true;
            continue;
        }
        if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("clrScheme")) {
            insideScheme = false;
            continue;
        }
        if (!insideScheme || token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        if (name == QLatin1String("srgbClr")) {
            fileOrder.append(QColor::fromString(QLatin1String("#")
                                                + reader.attributes().value(QLatin1String("val")).toString())
                                   .rgba());
        } else if (name == QLatin1String("sysClr")) {
            fileOrder.append(QColor::fromString(QLatin1String("#")
                                                + reader.attributes().value(QLatin1String("lastClr")).toString())
                                   .rgba());
        }
    }
    if (reader.hasError() || fileOrder.size() < 12) {
        return ThemePalette::officeDefault();
    }
    return {{fileOrder.at(1), fileOrder.at(0), fileOrder.at(3), fileOrder.at(2),
             fileOrder.at(4), fileOrder.at(5), fileOrder.at(6), fileOrder.at(7),
             fileOrder.at(8), fileOrder.at(9), fileOrder.at(10), fileOrder.at(11)}};
}

std::optional<Rgb> resolveXlsxColor(const QXlsx::XlsxColor &color, const ThemePalette &palette)
{
    if (color.isRgbColor()) {
        const QColor c = color.rgbColor();
        if (!c.isValid()) {
            return std::nullopt;
        }
        return Rgb{quint8(c.red()), quint8(c.green()), quint8(c.blue())};
    }
    if (color.isThemeColor()) {
        const QStringList theme = color.themeColor(); // [index, tint?]
        bool ok = false;
        const int index = theme.value(0).toInt(&ok);
        if (!ok || index < 0 || index >= palette.colors.size()) {
            return std::nullopt;
        }
        const QColor base = QColor::fromRgba(palette.slot(index));
        const double tint = theme.value(1).toDouble();
        return Rgb{quint8(applyTint(base.red(), tint)), quint8(applyTint(base.green(), tint)),
                   quint8(applyTint(base.blue(), tint))};
    }
    if (color.isIndexedColor()) {
        const int index = color.indexedColor();
        if (index < 0 || index >= 64) {
            return std::nullopt;
        }
        const QColor c = QColor::fromRgba(kIndexedColors[index]);
        return Rgb{quint8(c.red()), quint8(c.green()), quint8(c.blue())};
    }
    return std::nullopt;
}

CellStyle styleFromFormat(const QXlsx::Format &format, const ThemePalette &palette)
{
    CellStyle style;
    // Built-in formats may carry only the numFmtId (empty format string).
    style.numberFormat = !format.numberFormat().isEmpty()
        ? format.numberFormat()
        : NumberFormat::builtinCode(format.numberFormatIndex());
    readFontInto(format, palette, &style);
    readFillInto(format, palette, &style);
    readAlignmentInto(format, &style);
    readBordersInto(format, &style);
    return style;
}

QXlsx::Format formatFromStyle(const CellStyle &style)
{
    QXlsx::Format format;
    if (!style.numberFormat.isEmpty()
        && style.numberFormat.compare(QLatin1String("General"), Qt::CaseInsensitive) != 0) {
        // Built-in formats serialize reliably through their numFmtId; custom
        // codes go through setNumberFormat (verified written by QXlsx 1.5.1:
        // Styles::fixNumFmt registers them and writeNumFmts serializes).
        static const QHash<QString, int> kBuiltin = {
            {QStringLiteral("0"), 1}, {QStringLiteral("0.00"), 2},
            {QStringLiteral("#,##0"), 3}, {QStringLiteral("#,##0.00"), 4},
            {QStringLiteral("0%"), 9}, {QStringLiteral("0.00%"), 10},
            {QStringLiteral("0.00E+00"), 11}, {QStringLiteral("mm-dd-yy"), 14},
            {QStringLiteral("d-mmm-yy"), 15}, {QStringLiteral("d-mmm"), 16},
            {QStringLiteral("mmm-yy"), 17}, {QStringLiteral("h:mm AM/PM"), 18},
            {QStringLiteral("h:mm:ss AM/PM"), 19}, {QStringLiteral("h:mm"), 20},
            {QStringLiteral("h:mm:ss"), 21}, {QStringLiteral("m/d/yy h:mm"), 22},
            {QStringLiteral("@"), 49},
        };
        const auto it = kBuiltin.constFind(style.numberFormat);
        if (it != kBuiltin.constEnd()) {
            format.setNumberFormatIndex(it.value());
        } else {
            format.setNumberFormat(style.numberFormat);
        }
    }
    format.setFontBold(style.bold);
    format.setFontItalic(style.italic);
    if (style.underline) {
        format.setFontUnderline(QXlsx::Format::FontUnderlineSingle);
    }
    format.setFontStrikeOut(style.strikeout);
    if (style.fontSize > 0) {
        format.setFontSize(style.fontSize);
    }
    if (!style.fontFamily.isEmpty()) {
        format.setFontName(style.fontFamily);
    }
    if (style.fg.has_value()) {
        format.setFontColor(QColor((*style.fg).r, (*style.fg).g, (*style.fg).b));
    }
    if (style.bg.has_value()) {
        format.setFillPattern(QXlsx::Format::PatternSolid);
        // AGENT-GUARD: same inversion as the read side — for a solid fill,
        // QXlsx writes P_Fill_BgColor as the file's fgColor, and fgColor is
        // the colour Excel/LibreOffice actually paint. Setting the public
        // foreground setter would emit only a bgColor element and the fill
        // would vanish for every external consumer.
        format.setPatternBackgroundColor(QColor((*style.bg).r, (*style.bg).g, (*style.bg).b));
    }
    switch (style.alignment) {
    case 1:
        format.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
        break;
    case 2:
        format.setHorizontalAlignment(QXlsx::Format::AlignRight);
        break;
    case 3:
        format.setHorizontalAlignment(QXlsx::Format::AlignHFill);
        break;
    default:
        break;
    }
    switch (style.vAlign) {
    case 0:
        format.setVerticalAlignment(QXlsx::Format::AlignTop);
        break;
    case 2:
        format.setVerticalAlignment(QXlsx::Format::AlignBottom);
        break;
    default:
        break;
    }
    format.setTextWrap(style.wrap);
    if ((style.borders & 1) != 0) {
        format.setLeftBorderStyle(QXlsx::Format::BorderThin);
    }
    if ((style.borders & 2) != 0) {
        format.setRightBorderStyle(QXlsx::Format::BorderThin);
    }
    if ((style.borders & 4) != 0) {
        format.setTopBorderStyle(QXlsx::Format::BorderThin);
    }
    if ((style.borders & 8) != 0) {
        format.setBottomBorderStyle(QXlsx::Format::BorderThin);
    }
    // AGENT-NOTE (M66, D-075): QXlsx neither reads nor writes <protection>,
    // but these properties enter its format key, so a format that differs
    // only in protection still gets an xf of its own; the post-pass then
    // writes the <protection> into exactly that xf (xlsx_post_sheet.cpp).
    if (!style.locked) {
        format.setLocked(false);
    }
    if (style.hiddenFormula) {
        format.setHidden(true);
    }
    return format;
}

} // namespace QindaOffice::Calc::IO
