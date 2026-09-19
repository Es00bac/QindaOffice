// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// CellStyle <-> QXlsx::Format mapping, and the colour resolution the import
// side needs: xlsx colours may be literal RGB, theme+tint, or legacy indexed,
// and QXlsx only surfaces the RGB case through its public accessors — the raw
// XlsxColor comes out of Format::property() (a public accessor returning a
// registered metatype; the FormatPrivate ids and XlsxColor live in private
// headers, the same pattern as xlsxzipreader_p.h).
//
// On write every colour serializes as resolved RGB: QXlsx writes its own
// fixed default theme, so RGB is what every consumer renders identically.

#include "cell.h"

#include <QByteArray>
#include <QColor>
#include <QVector>

#include <optional>

namespace QXlsx {
class Format;
class XlsxColor;
}

namespace QindaOffice::Calc::IO {

// The theme palette a styles.xml colour indexes into. AGENT-GUARD: the file
// stores dk1/lt1/dk2/lt2 in that order, but the theme INDEX addresses them as
// lt1=0, dk1=1, lt2=2, dk2=3 (background/text pairs swap); accents follow at
// 4..9, hyperlink 10, followed 11. slot() takes the INDEX.
struct ThemePalette {
    QVector<QRgb> colors; // 12 entries when complete

    [[nodiscard]] QRgb slot(int themeIndex) const
    {
        return themeIndex >= 0 && themeIndex < colors.size() ? colors.at(themeIndex) : QRgb(0);
    }
    // The stock Office palette, used when the package carries no theme part
    // (writers may omit it) — a generic palette beats a wrong one, and these
    // are the colours those files mean.
    [[nodiscard]] static ThemePalette officeDefault();
};

// Parses an xl/theme themeXml (a:clrScheme); falls back to officeDefault on
// any deviation.
[[nodiscard]] ThemePalette themePaletteFromXml(const QByteArray &themeXml);

// Resolves a raw XlsxColor to an RGB triple: literal RGB as-is, theme+tint
// through the palette (tint as the ECMA-376 lumMod/lumOff channel
// approximation), indexed through the legacy 64-colour table. "auto" and
// anything shapeless yields nullopt (the default ink/fill then applies).
[[nodiscard]] std::optional<Rgb> resolveXlsxColor(const QXlsx::XlsxColor &color,
                                                  const ThemePalette &palette);

[[nodiscard]] CellStyle styleFromFormat(const QXlsx::Format &format,
                                        const ThemePalette &palette);
[[nodiscard]] QXlsx::Format formatFromStyle(const CellStyle &style);

} // namespace QindaOffice::Calc::IO
