// SPDX-License-Identifier: GPL-3.0-or-later
// The built-in themes, colour, font and effect sets, and theme parts read,
// patched and taken from other files (show_theme.h).

#include "show_theme.h"

#include "show_theme_xml.h"

#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <QFileInfo>

namespace QindaOffice::Show {

namespace {

const QString &nsA()
{
    static const QString s = QString::fromUtf16(Ooxml::Ns::A);
    return s;
}

ThemeColors colors(const char *name, std::initializer_list<const char *> hexes)
{
    ThemeColors out;
    out.name = QString::fromUtf8(name);
    size_t i = 0;
    for (const char *value : hexes) {
        if (i < out.values.size()) {
            out.values[i++] = QColor(QLatin1Char('#') + QLatin1String(value));
        }
    }
    return out;
}

ThemeFonts fonts(const char *name, const char *major, const char *minor)
{
    return {QString::fromUtf8(name), QString::fromUtf8(major), QString::fromUtf8(minor)};
}

ThemeColors colorSet(const QString &name)
{
    for (const ThemeColors &set : builtInColorSets()) {
        if (set.name == name) {
            return set;
        }
    }
    return builtInColorSets().first();
}

ThemeFonts fontSet(const QString &name)
{
    for (const ThemeFonts &set : builtInFontSets()) {
        if (set.name == name) {
            return set;
        }
    }
    return builtInFontSets().first();
}

ThemeSpec theme(const char *name, const char *colorName, const char *fontName, const char *effects)
{
    ThemeSpec spec;
    spec.name = QString::fromUtf8(name);
    spec.colors = colorSet(QString::fromUtf8(colorName));
    spec.fonts = fontSet(QString::fromUtf8(fontName));
    spec.effects = QString::fromUtf8(effects);
    return spec;
}

QColor schemeColor(const Ooxml::XmlElement *slot)
{
    const Ooxml::XmlElement *value = slot != nullptr && !slot->elements().isEmpty() ? slot->elements().first() : nullptr;
    if (value == nullptr) {
        return {};
    }
    const QString hex = value->name == u"sysClr" ? value->attr(u"lastClr") : value->attr(u"val");
    const QColor color(QLatin1Char('#') + hex);
    return color.isValid() ? color : QColor();
}

// A generated scheme element, parsed so it can replace the source's.
Ooxml::XmlElementPtr parsedElement(const QString &xml)
{
    const QString wrapped = QStringLiteral(R"(<a:holder xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">%1</a:holder>)").arg(xml);
    const Ooxml::XmlElementPtr holder = Ooxml::parseXml(wrapped.toUtf8());
    if (!holder || holder->elements().isEmpty()) {
        return nullptr;
    }
    return holder->elements().first()->clone();
}

void replaceChild(Ooxml::XmlElement &parent, const char16_t *name, const QString &xml)
{
    const Ooxml::XmlElementPtr replacement = parsedElement(xml);
    if (!replacement) {
        return;
    }
    for (Ooxml::XmlChild &child : parent.children) {
        if (child.element && child.element->is(nsA(), name)) {
            child.element = replacement;
            return;
        }
    }
    parent.appendElement(replacement);
}

} // namespace

const std::array<const char *, kThemeSlots> &themeSlotNames()
{
    static const std::array<const char *, kThemeSlots> names = {
        "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink"};
    return names;
}

QColor ThemeColors::slot(const QString &name) const
{
    for (size_t i = 0; i < themeSlotNames().size(); ++i) {
        if (name == QLatin1String(themeSlotNames().at(i))) {
            return values.at(i);
        }
    }
    return {};
}

QList<ThemeColors> builtInColorSets()
{
    // AGENT-NOTE: "Office" is Microsoft's default colour scheme (every deck
    // PowerPoint makes starts from it); the others are QindaShow's own.
    // dk1 lt1 dk2 lt2 accent1..6 hlink folHlink.
    return {
        colors("Office", {"000000", "FFFFFF", "44546A", "E7E6E6", "4472C4", "ED7D31", "A5A5A5", "FFC000", "5B9BD5", "70AD47", "0563C1", "954F72"}),
        colors("Harbor", {"000000", "FFFFFF", "1F3A5F", "E6EEF5", "1F6FB2", "2BA6A4", "F2A541", "D9534F", "6C5B9E", "7FB069", "1F6FB2", "6C5B9E"}),
        colors("Meadow", {"000000", "FFFFFF", "2E4A2E", "EEF3E6", "4C8C3F", "9BBF4A", "E0B43A", "D9783B", "3E8EA6", "8A6BB0", "2F7D9A", "7A5C99"}),
        colors("Ember", {"000000", "FFFFFF", "3B1F1A", "F7EDE6", "C8452C", "E9883A", "F2C14E", "8E3B46", "5C7A89", "9A8F5A", "B2412B", "7D4A5B"}),
        colors("Slate", {"000000", "FFFFFF", "2F3640", "E9ECEF", "4A6FA5", "6C8EBF", "8FA9C9", "5A6672", "A3B1BF", "C2563F", "3D6CB0", "7B5E92"}),
        colors("Orchid", {"000000", "FFFFFF", "3D2346", "F3ECF5", "8E4A9E", "C2579B", "E58FA7", "5F7FC1", "49A6A0", "D7A13B", "7445A0", "A1567E"}),
        colors("Sunrise", {"000000", "FFFFFF", "4A2C2A", "FFF4E6", "F26B38", "F7A541", "FFD166", "06A77D", "118AB2", "8D5A97", "118AB2", "8D5A97"}),
        colors("Graphite", {"000000", "FFFFFF", "262626", "F2F2F2", "3C3C3C", "5A5A5A", "787878", "969696", "B4B4B4", "D2D2D2", "404040", "7F7F7F"}),
    };
}

QList<ThemeFonts> builtInFontSets()
{
    // Office's own pair first; the rest are QindaShow's, made of fonts
    // Windows ships (Linux draws them through its metric-compatible twins
    // where one exists: Carlito, Caladea, Liberation).
    return {
        fonts("Office", "Calibri Light", "Calibri"),
        fonts("Classic", "Cambria", "Calibri"),
        fonts("Plain", "Arial", "Arial"),
        fonts("Bookish", "Georgia", "Georgia"),
        fonts("Editorial", "Times New Roman", "Arial"),
        fonts("Clean", "Segoe UI Light", "Segoe UI"),
        fonts("Friendly", "Trebuchet MS", "Trebuchet MS"),
        fonts("Typewriter", "Courier New", "Courier New"),
    };
}

QStringList builtInEffectSets()
{
    return {QStringLiteral("Office"), QStringLiteral("Flat"), QStringLiteral("Soft Shadow")};
}

ThemeSpec officeTheme()
{
    return theme("Office Theme", "Office", "Office", "Office");
}

QList<ThemeSpec> builtInThemes()
{
    return {officeTheme(),
            theme("Harbor", "Harbor", "Classic", "Office"),
            theme("Meadow", "Meadow", "Friendly", "Soft Shadow"),
            theme("Ember", "Ember", "Editorial", "Office"),
            theme("Slate", "Slate", "Clean", "Flat"),
            theme("Orchid", "Orchid", "Plain", "Soft Shadow"),
            theme("Sunrise", "Sunrise", "Office", "Flat")};
}

ThemeSpec themeFromPart(const QByteArray &xml)
{
    ThemeSpec spec;
    const Ooxml::XmlElementPtr root = Ooxml::parseXml(xml);
    if (!root || !root->is(nsA(), u"theme")) {
        return spec;
    }
    spec.name = root->attr(u"name");
    const Ooxml::XmlElement *elements = root->child(nsA(), u"themeElements");
    if (elements == nullptr) {
        return spec;
    }
    if (const Ooxml::XmlElement *scheme = elements->child(nsA(), u"clrScheme")) {
        spec.colors.name = scheme->attr(u"name");
        for (size_t i = 0; i < themeSlotNames().size(); ++i) {
            spec.colors.values[i] = schemeColor(scheme->child(nsA(), QString::fromLatin1(themeSlotNames().at(i))));
        }
    }
    if (const Ooxml::XmlElement *scheme = elements->child(nsA(), u"fontScheme")) {
        spec.fonts.name = scheme->attr(u"name");
        const auto latin = [scheme](const char16_t *which) {
            const Ooxml::XmlElement *font = scheme->child(nsA(), which);
            const Ooxml::XmlElement *face = font != nullptr ? font->child(nsA(), u"latin") : nullptr;
            return face != nullptr ? face->attr(u"typeface") : QString();
        };
        spec.fonts.major = latin(u"majorFont");
        spec.fonts.minor = latin(u"minorFont");
    }
    if (const Ooxml::XmlElement *scheme = elements->child(nsA(), u"fmtScheme")) {
        spec.effects = scheme->attr(u"name");
    }
    return spec;
}

QByteArray patchedThemePart(const QByteArray &source, const ThemeSpec &theme, const ThemeSpec &was)
{
    if (!theme.part.isEmpty()) {
        return theme.part;
    }
    const Ooxml::XmlElementPtr root = Ooxml::parseXml(source);
    Ooxml::XmlElement *elements = root ? root->child(nsA(), u"themeElements") : nullptr;
    if (elements == nullptr) {
        return themePartXml(theme);
    }
    if (theme.name != was.name) {
        root->setAttribute(QString(), QStringLiteral("name"), theme.name);
        // The family extension names the theme too.
        if (Ooxml::XmlElement *list = root->child(nsA(), u"extLst")) {
            for (Ooxml::XmlChild &c : list->children) {
                if (c.element && c.element->attr(u"uri") == u"{05A4C25C-085E-4340-85A3-A5531E510DB2}") {
                    c.element = parsedElement(themeFamilyXml(theme.name));
                }
            }
        }
    }
    if (theme.colors != was.colors) {
        replaceChild(*elements, u"clrScheme", colorSchemeXml(theme.colors));
    }
    if (theme.fonts != was.fonts) {
        replaceChild(*elements, u"fontScheme", fontSchemeXml(theme.fonts));
    }
    if (theme.effects != was.effects) {
        replaceChild(*elements, u"fmtScheme", formatSchemeXml(theme.effects));
    }
    return Ooxml::serializeDocument(*root);
}

std::optional<ThemeSpec> themeFromFile(const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<ThemeSpec> {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    };
    QString loadError;
    const std::shared_ptr<const Ooxml::Package> package = Ooxml::Package::load(path, &loadError);
    if (!package) {
        return fail(loadError.isEmpty() ? QStringLiteral("%1 is not an Office file").arg(QFileInfo(path).fileName()) : loadError);
    }
    const QString main = package->mainPart();
    QString themePart;
    if (package->contentType(main).contains(QLatin1String("theme"))) {
        themePart = main; // a .thmx: the theme is the main part
    } else {
        // A deck or template: its first master's theme.
        const Ooxml::XmlElementPtr presentation = package->xml(main);
        const QString nsP = QString::fromUtf16(Ooxml::Ns::P);
        const QString nsR = QString::fromUtf16(Ooxml::Ns::R);
        const Ooxml::XmlElement *list = presentation ? presentation->child(nsP, u"sldMasterIdLst") : nullptr;
        const Ooxml::XmlElement *entry = list != nullptr ? list->child(nsP, u"sldMasterId") : nullptr;
        const auto masterRel = entry != nullptr ? package->relationship(main, entry->attribute(nsR, u"id")) : std::nullopt;
        const QString master = masterRel ? package->targetPart(main, *masterRel) : QString();
        const auto themeRel = master.isEmpty() ? std::nullopt : package->firstOfType(master, QString::fromUtf16(Ooxml::RelType::Theme));
        themePart = themeRel ? package->targetPart(master, *themeRel) : QString();
    }
    const QByteArray bytes = themePart.isEmpty() ? QByteArray() : package->data(themePart);
    ThemeSpec spec = themeFromPart(bytes);
    if (spec.isEmpty()) {
        return fail(QStringLiteral("%1 holds no theme").arg(QFileInfo(path).fileName()));
    }
    spec.part = bytes;
    return spec;
}

} // namespace QindaOffice::Show
