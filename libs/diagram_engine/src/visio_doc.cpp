// SPDX-License-Identifier: GPL-3.0-or-later
// Document context and resolved cell lookup (visio_doc.h).
#include "visio_doc.h"

#include "ooxml_namespaces.h"

#include <algorithm>

namespace QindaOffice::Diagram::Visio {

namespace {

// Visio's fixed palette for colour indexes 0–23 (the ShapeSheet's colour
// numbers; <Colors> adds or overrides entries).
constexpr QRgb kPalette[24] = {
    0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
    0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080, 0xC0C0C0, 0xE6E6E6,
    0xCDCDCD, 0xB3B3B3, 0x9A9A9A, 0x808080, 0x666666, 0x4D4D4D, 0x333333, 0x1A1A1A,
};

constexpr char16_t kVisioThemeNs[] = u"http://schemas.microsoft.com/office/visio/2012/theme";

const XmlElement *findLocal(const XmlElement &root, QStringView ns, QStringView name)
{
    if (root.is(ns, name)) {
        return &root;
    }
    return root.descendant(ns, name);
}

void readVariations(const XmlElement &themeRoot, QList<QList<QColor>> &out, const Ooxml::Theme &theme)
{
    const XmlElement *list = findLocal(themeRoot, kVisioThemeNs, u"variationClrSchemeLst");
    if (list == nullptr) {
        return;
    }
    for (const XmlElement *scheme : list->elements(kVisioThemeNs, u"variationClrScheme")) {
        QList<QColor> colors;
        for (int i = 1; i <= 7; ++i) {
            const QString name = QStringLiteral("varColor%1").arg(i);
            const XmlElement *slot = scheme->child(kVisioThemeNs, name);
            colors.append(slot != nullptr ? Ooxml::resolveChildColor(slot, theme) : QColor());
        }
        out.append(colors);
    }
}

} // namespace

void DocumentInfo::load(const XmlElement &document, const XmlElement *themeRoot)
{
    for (int i = 0; i < 24; ++i) {
        palette.insert(i, QColor::fromRgb(kPalette[i]));
    }
    if (const XmlElement *colors = child(document, u"Colors")) {
        for (const XmlElement *entry : children(*colors, u"ColorEntry")) {
            const QColor color(entry->attribute({}, u"RGB"));
            if (color.isValid()) {
                palette.insert(intAttribute(*entry, u"IX", -1), color);
            }
        }
    }
    if (const XmlElement *faceNames = child(document, u"FaceNames")) {
        int position = 0;
        for (const XmlElement *face : children(*faceNames, u"FaceName")) {
            const QString name = face->attribute({}, u"NameU", face->attribute({}, u"Name"));
            faces.insert(intAttribute(*face, u"ID", position), name);
            ++position;
        }
    }
    styles.load(child(document, u"StyleSheets"));
    if (const XmlElement *settings = child(document, u"DocumentSettings")) {
        defaultLineStyle = intAttribute(*settings, u"DefaultLineStyle", -1);
        defaultFillStyle = intAttribute(*settings, u"DefaultFillStyle", -1);
        defaultTextStyle = intAttribute(*settings, u"DefaultTextStyle", -1);
    }
    theme = Ooxml::defaultOfficeTheme();
    if (themeRoot != nullptr) {
        const Ooxml::Theme parsed = Ooxml::parseTheme(*themeRoot);
        if (parsed.isValid()) {
            theme = parsed;
        }
        readVariations(*themeRoot, variations, theme);
        if (const XmlElement *lines = themeRoot->descendant(Ooxml::Ns::A, u"lnStyleLst")) {
            if (const XmlElement *first = lines->child(Ooxml::Ns::A, u"ln")) {
                bool ok = false;
                const double emu = first->attribute({}, u"w").toDouble(&ok);
                if (ok && emu > 0) {
                    themeLineWeight = emu / 914400.0;
                }
            }
        }
    }
}

QColor DocumentInfo::paletteColor(int index) const
{
    return palette.value(index, QColor(Qt::black));
}

QColor DocumentInfo::themeColor(int index, int variation) const
{
    static const char *const kSlots[] = {"dk1", "lt1", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6"};
    const auto slot = [this](const char *name) {
        return theme.colors.value(QString::fromLatin1(name), QColor(Qt::black));
    };
    if (index >= 0 && index < 8) {
        return slot(kSlots[index]);
    }
    if (index == 8) {
        return slot("lt1");
    }
    const int k = index >= 200 ? index - 200 : index - 100;
    if (k >= 0 && k < 7) {
        const QList<QColor> scheme = variations.value(std::clamp(variation, 0, int(variations.size()) - 1));
        if (k < scheme.size() && scheme.at(k).isValid()) {
            return scheme.at(k);
        }
        return k < 6 ? slot(kSlots[2 + k]) : slot("dk1");
    }
    return slot("accent1");
}

QString DocumentInfo::themeFont() const
{
    return theme.minorLatin.isEmpty() ? QStringLiteral("Calibri") : theme.minorLatin;
}

Lookup::Lookup(const Sheet &sheet, const DocumentInfo &doc, int variation)
    : m_sheet(sheet)
    , m_doc(doc)
    , m_variation(variation)
{
}

int Lookup::styleFor(Category category) const
{
    switch (category) {
    case Category::Line:
        return m_sheet.lineStyle >= 0 ? m_sheet.lineStyle : m_doc.defaultLineStyle;
    case Category::Fill:
        return m_sheet.fillStyle >= 0 ? m_sheet.fillStyle : m_doc.defaultFillStyle;
    case Category::Text:
        return m_sheet.textStyle >= 0 ? m_sheet.textStyle : m_doc.defaultTextStyle;
    case Category::Other:
        break;
    }
    return m_sheet.lineStyle >= 0 ? m_sheet.lineStyle : m_doc.defaultLineStyle;
}

const Cell *Lookup::cell(QStringView name) const
{
    if (const Cell *c = m_sheet.cell(name)) {
        return c;
    }
    const Category category = categoryOf(name);
    return m_doc.styles.cell(styleFor(category), category, name);
}

std::optional<double> Lookup::number(QStringView name) const
{
    const Cell *c = cell(name);
    return c != nullptr ? c->number() : std::nullopt;
}

double Lookup::number(QStringView name, double fallback) const
{
    return number(name).value_or(fallback);
}

QColor Lookup::colorOf(const Cell *c, ColorRole role) const
{
    if (c == nullptr) {
        return {};
    }
    const QString v = c->value.trimmed();
    if (v.startsWith(QLatin1Char('#'))) {
        const QColor color(v);
        return color.isValid() ? color : QColor();
    }
    if (c->themed()) {
        const auto quick = [this](const char *cellName, int fallback) {
            return int(number(QString::fromLatin1(cellName)).value_or(fallback));
        };
        switch (role) {
        case ColorRole::Fill:
            return m_doc.themeColor(quick("QuickStyleFillColor", 100), m_variation);
        case ColorRole::Line:
            return m_doc.themeColor(quick("QuickStyleLineColor", 100), m_variation).darker(133);
        case ColorRole::Shadow:
            return m_doc.themeColor(quick("QuickStyleShadowColor", 100), m_variation).darker(150);
        case ColorRole::Font:
            return {}; // light or dark by the fill: the reader decides
        case ColorRole::Other:
            return m_doc.themeColor(100, m_variation);
        }
    }
    bool ok = false;
    const int index = v.toInt(&ok);
    return ok ? m_doc.paletteColor(index) : QColor();
}

QColor Lookup::color(QStringView name, ColorRole role) const
{
    return colorOf(cell(name), role);
}

const Cell *Lookup::textCell(QStringView sectionName, int ix, QStringView name) const
{
    if (const Section *s = m_sheet.section(sectionName)) {
        if (const Row *r = s->indexed(ix)) {
            if (const Cell *c = r->cell(name)) {
                return c;
            }
        }
        if (ix != 0) {
            if (const Row *r = s->indexed(0)) {
                if (const Cell *c = r->cell(name)) {
                    return c;
                }
            }
        }
    }
    return m_doc.styles.rowCell(styleFor(Category::Text), sectionName, 0, name);
}

QString Lookup::fontName(const Cell *c) const
{
    if (c == nullptr || c->themed() || c->value.isEmpty()) {
        return m_doc.themeFont();
    }
    bool ok = false;
    const int index = c->value.toInt(&ok);
    if (ok) {
        return m_doc.faces.value(index, m_doc.themeFont());
    }
    return c->value;
}

double Lookup::lineWeight() const
{
    const Cell *c = cell(u"LineWeight");
    if (c == nullptr) {
        return 0.01;
    }
    return c->themed() ? m_doc.themeLineWeight : c->number().value_or(0.01);
}

} // namespace QindaOffice::Diagram::Visio
