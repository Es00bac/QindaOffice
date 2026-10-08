// SPDX-License-Identifier: GPL-3.0-or-later
#include "ooxml_theme.h"

#include "ooxml_namespaces.h"

#include <QtMath>

#include <array>

namespace QindaOffice::Ooxml {

namespace {

const QString kA = QString::fromUtf16(Ns::A);

// DrawingML percentages are thousandths of a percent: 100000 = 100 %.
double fraction(const XmlElement *e)
{
    return e->attr(u"val").toDouble() / 100000.0;
}

double toLinear(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double toSrgb(double c)
{
    c = qBound(0.0, c, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

QColor fromHex(const QString &hex)
{
    if (hex.size() != 6) {
        return {};
    }
    bool ok = false;
    const uint rgb = hex.toUInt(&ok, 16);
    return ok ? QColor::fromRgb(rgb) : QColor();
}

// DrawingML preset colour names are CSS names in camelCase with dk/lt/med
// abbreviations ("dkSlateGray", "ltGoldenrodYellow", "medSeaGreen").
QColor presetColor(QString name)
{
    if (name.startsWith(u"dk")) {
        name = QStringLiteral("dark") + name.mid(2);
    } else if (name.startsWith(u"lt")) {
        name = QStringLiteral("light") + name.mid(2);
    } else if (name.startsWith(u"med")) {
        name = QStringLiteral("medium") + name.mid(3);
    }
    return QColor::fromString(name.toLower());
}

QColor applyTransforms(QColor color, const XmlElement &element)
{
    double alpha = color.alphaF();
    for (const XmlElement *t : element.elements()) {
        const QStringView n = t->name;
        if (n == u"alpha") {
            alpha = fraction(t);
        } else if (n == u"alphaMod") {
            alpha *= fraction(t);
        } else if (n == u"alphaOff") {
            alpha += fraction(t);
        } else if (n == u"tint") {
            color = applyTint(color, fraction(t));
        } else if (n == u"shade") {
            color = applyShade(color, fraction(t));
        } else if (n == u"lumMod" || n == u"lumOff" || n == u"lum" || n == u"satMod" || n == u"satOff"
                   || n == u"sat" || n == u"hueMod" || n == u"hueOff" || n == u"hue") {
            float h = 0, s = 0, l = 0;
            color.getHslF(&h, &s, &l);
            double hue = h < 0 ? 0.0 : h;
            double sat = s;
            double lum = l;
            const double f = fraction(t);
            if (n == u"lumMod") {
                lum *= f;
            } else if (n == u"lumOff") {
                lum += f;
            } else if (n == u"lum") {
                lum = f;
            } else if (n == u"satMod") {
                sat *= f;
            } else if (n == u"satOff") {
                sat += f;
            } else if (n == u"sat") {
                sat = f;
            } else {
                // Hue is in 60000ths of a degree.
                const double degrees = t->attr(u"val").toDouble() / 60000.0 / 360.0;
                hue = n == u"hue" ? degrees : n == u"hueOff" ? hue + degrees : hue * f;
                hue -= std::floor(hue);
            }
            color = QColor::fromHslF(float(hue), float(qBound(0.0, sat, 1.0)), float(qBound(0.0, lum, 1.0)));
        } else if (n == u"comp") {
            color = QColor(255 - color.red(), 255 - color.green(), 255 - color.blue()).toHsl();
            color.setHslF(std::fmod(color.hslHueF() + 0.5f, 1.0f), color.hslSaturationF(), color.lightnessF());
        } else if (n == u"inv") {
            color = QColor(255 - color.red(), 255 - color.green(), 255 - color.blue());
        } else if (n == u"gray") {
            const int g = qGray(color.rgb());
            color = QColor(g, g, g);
        }
    }
    color.setAlphaF(float(qBound(0.0, alpha, 1.0)));
    return color.toRgb();
}

} // namespace

Theme parseTheme(const XmlElement &themeRoot)
{
    Theme theme;
    theme.name = themeRoot.attr(u"name");
    const XmlElement *elements = themeRoot.child(kA, u"themeElements");
    if (elements == nullptr) {
        return theme;
    }
    if (const XmlElement *scheme = elements->child(kA, u"clrScheme")) {
        for (const XmlElement *slot : scheme->elements()) {
            const QColor color = resolveChildColor(slot, Theme());
            if (color.isValid()) {
                theme.colors.insert(slot->name, color);
            }
        }
    }
    if (const XmlElement *fonts = elements->child(kA, u"fontScheme")) {
        if (const XmlElement *major = fonts->child(kA, u"majorFont")) {
            if (const XmlElement *latin = major->child(kA, u"latin")) {
                theme.majorLatin = latin->attr(u"typeface");
            }
            if (const XmlElement *ea = major->child(kA, u"ea")) {
                theme.majorEastAsian = ea->attr(u"typeface");
            }
        }
        if (const XmlElement *minor = fonts->child(kA, u"minorFont")) {
            if (const XmlElement *latin = minor->child(kA, u"latin")) {
                theme.minorLatin = latin->attr(u"typeface");
            }
            if (const XmlElement *ea = minor->child(kA, u"ea")) {
                theme.minorEastAsian = ea->attr(u"typeface");
            }
        }
    }
    return theme;
}

const Theme &defaultOfficeTheme()
{
    static const Theme theme = [] {
        Theme t;
        t.name = QStringLiteral("Office Theme");
        const std::array<std::pair<const char *, QRgb>, 12> colors{{
            {"dk1", 0x000000}, {"lt1", 0xFFFFFF}, {"dk2", 0x44546A}, {"lt2", 0xE7E6E6},
            {"accent1", 0x4472C4}, {"accent2", 0xED7D31}, {"accent3", 0xA5A5A5}, {"accent4", 0xFFC000},
            {"accent5", 0x5B9BD5}, {"accent6", 0x70AD47}, {"hlink", 0x0563C1}, {"folHlink", 0x954F72},
        }};
        for (const auto &[name, rgb] : colors) {
            t.colors.insert(QString::fromLatin1(name), QColor::fromRgb(rgb));
        }
        t.majorLatin = QStringLiteral("Calibri Light");
        t.minorLatin = QStringLiteral("Calibri");
        return t;
    }();
    return theme;
}

QColor applyTint(const QColor &color, double tint)
{
    // Office (and LibreOffice, which matches it) tints in LINEAR RGB:
    // tint 0.25 = 25 % of the colour, 75 % white.
    if (tint >= 1.0 || tint < 0.0) {
        return color;
    }
    auto channel = [tint](double c) { return toSrgb(1.0 - (1.0 - toLinear(c)) * tint); };
    QColor out = QColor::fromRgbF(float(channel(color.redF())), float(channel(color.greenF())),
                                  float(channel(color.blueF())));
    out.setAlphaF(color.alphaF());
    return out;
}

QColor applyShade(const QColor &color, double shade)
{
    if (shade >= 1.0 || shade < 0.0) {
        return color;
    }
    auto channel = [shade](double c) { return toSrgb(toLinear(c) * shade); };
    QColor out = QColor::fromRgbF(float(channel(color.redF())), float(channel(color.greenF())),
                                  float(channel(color.blueF())));
    out.setAlphaF(color.alphaF());
    return out;
}

QColor resolveColor(const XmlElement *color, const Theme &theme, const ColorMap &map, const QColor &phColor)
{
    if (color == nullptr) {
        return {};
    }
    QColor base;
    const QStringView n = color->name;
    if (n == u"srgbClr") {
        base = fromHex(color->attr(u"val"));
    } else if (n == u"sysClr") {
        base = fromHex(color->attr(u"lastClr"));
        if (!base.isValid()) {
            base = color->attr(u"val") == u"window" ? QColor(Qt::white) : QColor(Qt::black);
        }
    } else if (n == u"schemeClr") {
        QString slot = color->attr(u"val");
        if (slot == u"phClr") {
            base = phColor.isValid() ? phColor : QColor(Qt::black);
        } else {
            static const ColorMap kDefaultMap{{QStringLiteral("bg1"), QStringLiteral("lt1")},
                                              {QStringLiteral("tx1"), QStringLiteral("dk1")},
                                              {QStringLiteral("bg2"), QStringLiteral("lt2")},
                                              {QStringLiteral("tx2"), QStringLiteral("dk2")}};
            slot = map.value(slot, kDefaultMap.value(slot, slot));
            base = theme.colors.value(slot, defaultOfficeTheme().colors.value(slot));
        }
    } else if (n == u"prstClr") {
        base = presetColor(color->attr(u"val"));
    } else if (n == u"scrgbClr") {
        base = QColor::fromRgbF(float(toSrgb(color->attr(u"r").toDouble() / 100000.0)),
                                float(toSrgb(color->attr(u"g").toDouble() / 100000.0)),
                                float(toSrgb(color->attr(u"b").toDouble() / 100000.0)));
    } else if (n == u"hslClr") {
        base = QColor::fromHslF(float(color->attr(u"hue").toDouble() / 60000.0 / 360.0),
                                float(color->attr(u"sat").toDouble() / 100000.0),
                                float(color->attr(u"lum").toDouble() / 100000.0));
    } else {
        return {};
    }
    if (!base.isValid()) {
        return {};
    }
    return applyTransforms(base, *color);
}

QColor resolveChildColor(const XmlElement *parent, const Theme &theme, const ColorMap &map, const QColor &phColor)
{
    if (parent == nullptr) {
        return {};
    }
    for (const XmlElement *e : parent->elements()) {
        const QColor c = resolveColor(e, theme, map, phColor);
        if (c.isValid()) {
            return c;
        }
    }
    return {};
}

namespace {

struct Highlight {
    const char *name;
    QRgb rgb;
};
constexpr std::array<Highlight, 16> kHighlights{{
    {"yellow", 0xFFFF00}, {"green", 0x00FF00}, {"cyan", 0x00FFFF}, {"magenta", 0xFF00FF},
    {"blue", 0x0000FF}, {"red", 0xFF0000}, {"darkBlue", 0x000080}, {"darkCyan", 0x008080},
    {"darkGreen", 0x008000}, {"darkMagenta", 0x800080}, {"darkRed", 0x800000}, {"darkYellow", 0x808000},
    {"darkGray", 0x808080}, {"lightGray", 0xC0C0C0}, {"black", 0x000000}, {"white", 0xFFFFFF},
}};

} // namespace

QColor highlightColor(const QString &name)
{
    for (const Highlight &h : kHighlights) {
        if (name == QLatin1String(h.name)) {
            return QColor::fromRgb(h.rgb);
        }
    }
    return {};
}

QString nearestHighlightName(const QColor &color)
{
    if (!color.isValid() || color.alpha() == 0) {
        return {};
    }
    const Highlight *best = &kHighlights.front();
    int bestDistance = std::numeric_limits<int>::max();
    for (const Highlight &h : kHighlights) {
        const QColor c = QColor::fromRgb(h.rgb);
        const int dr = c.red() - color.red();
        const int dg = c.green() - color.green();
        const int db = c.blue() - color.blue();
        const int distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &h;
        }
    }
    return QString::fromLatin1(best->name);
}

} // namespace QindaOffice::Ooxml
