// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pieces of a theme part (internal to show_model; show_theme.h has the
// public entry points): the elements PowerPoint spells for a colour
// scheme, a font scheme and the theme family extension.

#include "show_theme.h"

namespace QindaOffice::Show {

[[nodiscard]] QString colorSchemeXml(const ThemeColors &colors);
[[nodiscard]] QString fontSchemeXml(const ThemeFonts &fonts);
// The a:ext holding thm15:themeFamily for a theme named `name`.
[[nodiscard]] QString themeFamilyXml(const QString &name);

} // namespace QindaOffice::Show
