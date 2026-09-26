# SPDX-License-Identifier: GPL-3.0-or-later

# One ordinary top-level window per document (ADR-0110); the desktop's
# containers provide tabs/splits. Every app is a thin main() over
# office_quick with its own QML module (D-027).
add_subdirectory(qindanote)
add_subdirectory(qindacalc)
add_subdirectory(qindawrite)
add_subdirectory(qindashow)
add_subdirectory(qindalauncher)
add_subdirectory(qindamail)
# QindaBooks (M47, D-047): one company file per window over books_*.
add_subdirectory(qindabooks)
# QindaDiagram (M50, D-050): one drawing per window over diagram_engine.
add_subdirectory(qindadiagram)
# QindaPlan (M51, D-051): one project per window over plan_engine/plan_output.
add_subdirectory(qindaplan)
# QindaBase (M49, D-049): one database per window over base_engine.
add_subdirectory(qindabase)
