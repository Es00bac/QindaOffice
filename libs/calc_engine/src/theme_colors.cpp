// SPDX-License-Identifier: GPL-3.0-or-later
// The theme's colours and Excel's tint (theme_colors.h, M90, D-098).

#include "theme_colors.h"

#include <QHash>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Calc {

namespace {

Rgb hex(quint32 value)
{
    return Rgb{quint8((value >> 16) & 0xff), quint8((value >> 8) & 0xff), quint8(value & 0xff)};
}

// The palette calc_io writes when the workbook brings no theme (QXlsx's
// xlsxtheme.cpp: Office's 2007 theme), in theme-index order.
QVector<Rgb> defaultPalette()
{
    return {hex(0xFFFFFF), hex(0x000000), hex(0xEEECE1), hex(0x1F497D), hex(0x4F81BD), hex(0xC0504D),
            hex(0x9BBB59), hex(0x8064A2), hex(0x4BACC6), hex(0xF79646), hex(0x0000FF), hex(0x800080)};
}

} // namespace

QVector<Rgb> themePalette(const QByteArray &themeXml)
{
    QVector<Rgb> palette = defaultPalette();
    if (themeXml.isEmpty()) {
        return palette;
    }
    // DrawingML's scheme order mapped to SpreadsheetML's theme indexes.
    static const QHash<QString, int> schemeSlots{{QStringLiteral("lt1"), 0},     {QStringLiteral("dk1"), 1},
                                           {QStringLiteral("lt2"), 2},     {QStringLiteral("dk2"), 3},
                                           {QStringLiteral("accent1"), 4}, {QStringLiteral("accent2"), 5},
                                           {QStringLiteral("accent3"), 6}, {QStringLiteral("accent4"), 7},
                                           {QStringLiteral("accent5"), 8}, {QStringLiteral("accent6"), 9},
                                           {QStringLiteral("hlink"), 10},  {QStringLiteral("folHlink"), 11}};
    QXmlStreamReader reader(themeXml);
    int slot = -1;
    bool inScheme = false;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("clrScheme")) {
            break;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QString name = reader.name().toString();
        if (name == QLatin1String("clrScheme")) {
            inScheme = true;
            continue;
        }
        if (!inScheme) {
            continue;
        }
        if (schemeSlots.contains(name)) {
            slot = schemeSlots.value(name);
            continue;
        }
        if (slot < 0) {
            continue;
        }
        bool ok = false;
        if (name == QLatin1String("srgbClr")) {
            const quint32 value = reader.attributes().value(QLatin1String("val")).toUInt(&ok, 16);
            if (ok) {
                palette[slot] = hex(value);
            }
            slot = -1;
        } else if (name == QLatin1String("sysClr")) {
            const quint32 value = reader.attributes().value(QLatin1String("lastClr")).toUInt(&ok, 16);
            if (ok) {
                palette[slot] = hex(value);
            }
            slot = -1;
        }
    }
    return palette;
}

Rgb tinted(const Rgb &color, double tint)
{
    if (tint == 0.0) {
        return color;
    }
    // RGB → HLS (all in 0..1), the lightness moved, HLS → RGB.
    const double r = color.r / 255.0;
    const double g = color.g / 255.0;
    const double b = color.b / 255.0;
    const double hi = std::max({r, g, b});
    const double lo = std::min({r, g, b});
    double h = 0;
    double s = 0;
    double l = (hi + lo) / 2.0;
    if (hi != lo) {
        const double d = hi - lo;
        s = l > 0.5 ? d / (2.0 - hi - lo) : d / (hi + lo);
        h = hi == r ? (g - b) / d + (g < b ? 6.0 : 0.0) : (hi == g ? (b - r) / d + 2.0 : (r - g) / d + 4.0);
        h /= 6.0;
    }
    l = tint < 0 ? l * (1.0 + tint) : l * (1.0 - tint) + tint;
    l = std::clamp(l, 0.0, 1.0);
    const auto channel = [](double p, double q, double t) {
        t = t < 0 ? t + 1 : (t > 1 ? t - 1 : t);
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 0.5) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    };
    double rr = l;
    double gg = l;
    double bb = l;
    if (s != 0.0) {
        const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        const double p = 2 * l - q;
        rr = channel(p, q, h + 1.0 / 3.0);
        gg = channel(p, q, h);
        bb = channel(p, q, h - 1.0 / 3.0);
    }
    return Rgb{quint8(std::lround(rr * 255)), quint8(std::lround(gg * 255)), quint8(std::lround(bb * 255))};
}

Rgb themeColor(const QVector<Rgb> &palette, int theme, double tint, const Rgb &fallback)
{
    if (theme < 0 || theme >= palette.size()) {
        return fallback;
    }
    return tinted(palette.at(theme), tint);
}

} // namespace QindaOffice::Calc
