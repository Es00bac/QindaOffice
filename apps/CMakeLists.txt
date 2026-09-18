# SPDX-License-Identifier: GPL-3.0-or-later

# One ordinary top-level window per document (ADR-0110); the desktop's
# containers provide tabs/splits. Every app is a thin main() over
# office_quick with its own QML module (D-027).
add_subdirectory(qindanote)
add_subdirectory(qindacalc)
add_subdirectory(qindawrite)
add_subdirectory(qindashow)
add_subdirectory(qindalauncher)
