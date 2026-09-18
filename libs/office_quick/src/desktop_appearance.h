// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DesktopAppearance (conversion plan §3.3, D-027): publishes the QindaQt
// desktop's theme into the QindaQt.Tokens singleton of an engine, the way
// the desktop's own standalone apps do (QindaStudio, the Welcome app):
// nothing publishes tokens INTO a process for it — the app reads
// `appearance.theme` from org.qindaqt.Settings1, loads that theme from the
// installed catalog and publishes it; QindaTK's bridge (ThemeBridge.qml →
// QindaTK.QindaQt) then derives the toolkit's roles from the published
// tokens. Off the desktop nothing is published and the window keeps the
// preset from Preferences.

#include <QString>

class QQmlEngine;

namespace QindaOffice::Quick {

class DesktopAppearance {
public:
    // Reads the desktop's theme and accessibility settings and publishes
    // them. False (with `error`) when no desktop answered, the token module
    // is not installed, or the theme is unknown — the caller logs and
    // keeps the preset.
    static bool publishDesktopTheme(QQmlEngine &engine, QString *error = nullptr);

    // Publishes one installed theme by id (`qinda-dark`, …); the seam the
    // tests use and what the above does once it knows the id.
    static bool publishTheme(QQmlEngine &engine, const QString &themeId, bool reducedMotion,
                             QString *error = nullptr);
};

} // namespace QindaOffice::Quick
