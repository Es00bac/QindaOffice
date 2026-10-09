// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A slide master's theme as QindaShow edits it (M75, D-084): the twelve
// colours of its colour scheme, the heading and body fonts of its font
// scheme and the name of its effect set (format scheme) — what
// PowerPoint's Design ▸ Themes and Variants (Colors, Fonts, Effects)
// change. The model keeps EFFECTIVE values (D-070), so a theme change is
// written into the master's theme part and the deck is re-resolved through
// the pptx reader (pptx_design.h); nothing downstream chases theme slots.
//
// The built-in sets: Microsoft's Office theme (the default every deck
// starts from) and QindaShow's own colour, font and effect sets.
// themePartXml() writes a theme part the way PowerPoint writes one
// (a:clrScheme, a:fontScheme with the script fonts of the Office set,
// a:fmtScheme with three fill / line / effect / background styles,
// a:objectDefaults, a:extraClrSchemeLst and thm15:themeFamily).

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QString>

#include <array>
#include <optional>

namespace QindaOffice::Show {

// The slots of a DrawingML colour scheme, in a:clrScheme order.
inline constexpr int kThemeSlots = 12;
// a:dk1 a:lt1 a:dk2 a:lt2 a:accent1..6 a:hlink a:folHlink
[[nodiscard]] const std::array<const char *, kThemeSlots> &themeSlotNames();

struct ThemeColors {
    QString name; // a:clrScheme/@name ("Office")
    std::array<QColor, kThemeSlots> values{};
    bool operator==(const ThemeColors &) const = default;
    [[nodiscard]] QColor slot(const QString &name) const;
};

struct ThemeFonts {
    QString name;  // a:fontScheme/@name
    QString major; // headings (+mj-lt)
    QString minor; // body text (+mn-lt)
    bool operator==(const ThemeFonts &) const = default;
};

struct ThemeSpec {
    QString name; // a:theme/@name ("Office Theme")
    ThemeColors colors;
    ThemeFonts fonts;
    QString effects; // a:fmtScheme/@name: the effect set ("Office")
    // A theme taken whole from another file (Design ▸ Browse for Themes):
    // its theme part as it came, written in place of the master's own.
    // Empty: the master's own part, patched with the fields above.
    QByteArray part;
    bool operator==(const ThemeSpec &) const = default;
    [[nodiscard]] bool isEmpty() const { return name.isEmpty() && colors.name.isEmpty() && fonts.name.isEmpty(); }
};

// ---- the built-in sets ----------------------------------------------------------
[[nodiscard]] ThemeSpec officeTheme();
// Whole themes: Office first, then QindaShow's own (Design ▸ Themes).
[[nodiscard]] QList<ThemeSpec> builtInThemes();
// Design ▸ Variants ▸ Colors / Fonts / Effects.
[[nodiscard]] QList<ThemeColors> builtInColorSets();
[[nodiscard]] QList<ThemeFonts> builtInFontSets();
[[nodiscard]] QStringList builtInEffectSets();

// ---- theme parts ----------------------------------------------------------------
// A complete theme part for `theme`, as PowerPoint writes one.
[[nodiscard]] QByteArray themePartXml(const ThemeSpec &theme);
// `source` (a theme part) with the colour, font and format schemes of
// `theme` put in where they differ from `was`; everything else kept.
[[nodiscard]] QByteArray patchedThemePart(const QByteArray &source, const ThemeSpec &theme, const ThemeSpec &was);
// What a theme part holds, in the model's terms (part left empty).
[[nodiscard]] ThemeSpec themeFromPart(const QByteArray &xml);
// The theme of a .pptx / .potx / .ppsx / .pptm (its first master's) or a
// .thmx file, with its part kept whole; nullopt (and `error`) otherwise.
[[nodiscard]] std::optional<ThemeSpec> themeFromFile(const QString &path, QString *error = nullptr);

// The a:fmtScheme of an effect set (the XML inside the theme part).
[[nodiscard]] QString formatSchemeXml(const QString &effects);

} // namespace QindaOffice::Show
