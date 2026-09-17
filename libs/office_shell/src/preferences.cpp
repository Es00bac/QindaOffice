// SPDX-License-Identifier: GPL-3.0-or-later
#include "preferences.h"

#include "json_settings.h"
#include "paths.h"

#include <QVariant>

namespace QindaOffice::Shell {
namespace {

// Storage keys. AGENT-GUARD: these strings are the on-disk format of
// preferences.json — renaming one silently resets that preference for every
// existing install, so add a new key instead.
constexpr QLatin1StringView kShowStatusBar("ui.showStatusBar");
constexpr QLatin1StringView kShowToolBars("ui.showToolBars");
constexpr QLatin1StringView kDefaultZoom("ui.defaultZoomPercent");
constexpr QLatin1StringView kAutoSaveMinutes("files.autoSaveMinutes");
constexpr QLatin1StringView kFontFamily("document.fontFamily");
constexpr QLatin1StringView kFontSize("document.fontSize");
constexpr QLatin1StringView kSpellCheck("write.spellCheck");

} // namespace

Preferences &Preferences::instance()
{
    static Preferences prefs(Core::configDirFor(Core::appLeaf())
                             + QStringLiteral("/preferences.json"));
    return prefs;
}

Preferences::Preferences(const QString &path, QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<Core::JsonSettings>(path))
{
}

Preferences::~Preferences() = default;

void Preferences::put(const QString &key, const QVariant &value)
{
    if (m_settings->value(key) == value) {
        return;
    }
    m_settings->setValue(key, value);
    // A preference the user just set must survive a crash; JsonSettings
    // writes atomically, and a failure here is not worth a modal dialog —
    // the setting simply does not persist and the app keeps working.
    (void)m_settings->save(nullptr);
    emit changed();
}

bool Preferences::showStatusBar() const
{
    return m_settings->value(kShowStatusBar, true).toBool();
}

void Preferences::setShowStatusBar(bool on)
{
    put(kShowStatusBar, on);
}

bool Preferences::showToolBars() const
{
    return m_settings->value(kShowToolBars, true).toBool();
}

void Preferences::setShowToolBars(bool on)
{
    put(kShowToolBars, on);
}

int Preferences::defaultZoomPercent() const
{
    const int stored = m_settings->value(kDefaultZoom, 100).toInt();
    return qBound(kMinZoomPercent, stored, kMaxZoomPercent);
}

void Preferences::setDefaultZoomPercent(int percent)
{
    put(kDefaultZoom, qBound(kMinZoomPercent, percent, kMaxZoomPercent));
}

int Preferences::autoSaveMinutes() const
{
    const int stored = m_settings->value(kAutoSaveMinutes, 0).toInt();
    return qBound(0, stored, kMaxAutoSaveMinutes);
}

void Preferences::setAutoSaveMinutes(int minutes)
{
    put(kAutoSaveMinutes, qBound(0, minutes, kMaxAutoSaveMinutes));
}

QString Preferences::documentFontFamily() const
{
    return m_settings->value(kFontFamily, QString()).toString();
}

void Preferences::setDocumentFontFamily(const QString &family)
{
    put(kFontFamily, family);
}

qreal Preferences::documentFontSize() const
{
    const qreal stored = m_settings->value(kFontSize, 0.0).toDouble();
    // 0 means "application default"; anything outside a sane typographic
    // range is treated the same way rather than rendering unreadably.
    return (stored >= 4.0 && stored <= 96.0) ? stored : 0.0;
}

void Preferences::setDocumentFontSize(qreal points)
{
    put(kFontSize, (points >= 4.0 && points <= 96.0) ? points : 0.0);
}

bool Preferences::spellCheckEnabled() const
{
    return m_settings->value(kSpellCheck, true).toBool();
}

void Preferences::setSpellCheckEnabled(bool on)
{
    put(kSpellCheck, on);
}

} // namespace QindaOffice::Shell
