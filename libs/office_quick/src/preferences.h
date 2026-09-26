// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The suite's user preferences (plan section 2.9: the suite's own settings
// live in $XDG_CONFIG_HOME/qindaoffice/<app>/, atomic JSON). One store per
// process — one app owns one process (ADR-0110) — reached through
// Preferences::instance(); tests construct their own over a temp path.
//
// AGENT-CONTRACT (D-012, "a stub is a bug"): every key here is READ by
// something. Settings ▸ Preferences… shows exactly these, so adding a key
// means wiring it in the same change, and a key nothing consumes must be
// deleted rather than left in the dialog as a control that does nothing.
//
// Values are written through immediately (an atomic JSON save per change),
// so a crash never loses a preference the user just set, and `changed()`
// fires once per mutation so every open window of the app re-applies.

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace QindaOffice::Core {
class JsonSettings;
}

namespace QindaOffice::Quick {

class Preferences final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Preferences)
    QML_SINGLETON
    Q_PROPERTY(bool showStatusBar READ showStatusBar WRITE setShowStatusBar NOTIFY changed)
    Q_PROPERTY(bool showToolBars READ showToolBars WRITE setShowToolBars NOTIFY changed)
    // The ribbon draws one row per group (View ▸ Compact Ribbon, D-041).
    Q_PROPERTY(bool ribbonCompact READ ribbonCompact WRITE setRibbonCompact NOTIFY changed)
    Q_PROPERTY(int defaultZoomPercent READ defaultZoomPercent WRITE setDefaultZoomPercent NOTIFY changed)
    Q_PROPERTY(int autoSaveMinutes READ autoSaveMinutes WRITE setAutoSaveMinutes NOTIFY changed)
    Q_PROPERTY(QString documentFontFamily READ documentFontFamily WRITE setDocumentFontFamily NOTIFY changed)
    Q_PROPERTY(qreal documentFontSize READ documentFontSize WRITE setDocumentFontSize NOTIFY changed)
    Q_PROPERTY(bool spellCheckEnabled READ spellCheckEnabled WRITE setSpellCheckEnabled NOTIFY changed)
    Q_PROPERTY(bool notePressureEnabled READ notePressureEnabled WRITE setNotePressureEnabled NOTIFY changed)
    Q_PROPERTY(QString noteSmoothing READ noteSmoothing WRITE setNoteSmoothing NOTIFY changed)
    Q_PROPERTY(bool noteTouchDrawing READ noteTouchDrawing WRITE setNoteTouchDrawing NOTIFY changed)
    Q_PROPERTY(QString density READ density WRITE setDensity NOTIFY changed)
    Q_PROPERTY(QString themePreset READ themePreset WRITE setThemePreset NOTIFY changed)
public:
    // The process-wide store for QML (`Preferences.density`); CppOwnership.
    static Preferences *create(QQmlEngine *, QJSEngine *);

    // Bounds shared with the dialog's spin boxes so the stored value and
    // the control can never disagree.
    static constexpr int kMinZoomPercent = 25;
    static constexpr int kMaxZoomPercent = 400;
    static constexpr int kMaxAutoSaveMinutes = 120;

    // Per-app store under Core::configDirFor(Core::appLeaf()).
    [[nodiscard]] static Preferences &instance();

    explicit Preferences(const QString &path, QObject *parent = nullptr);
    ~Preferences() override;

    // Chrome. Applied by DocumentWindow to every window of the app.
    [[nodiscard]] bool showStatusBar() const;
    void setShowStatusBar(bool on);
    [[nodiscard]] bool showToolBars() const;
    void setShowToolBars(bool on);
    [[nodiscard]] bool ribbonCompact() const;
    void setRibbonCompact(bool on);

    // Zoom a freshly opened document starts at (View ▸ Zoom still moves the
    // window's own zoom without touching this).
    [[nodiscard]] int defaultZoomPercent() const;
    void setDefaultZoomPercent(int percent);

    // 0 disables auto-save. Auto-save only ever rewrites a document that
    // already has a path — it never invents one, and never silently
    // converts a format (see DocumentWindow::autoSaveTick).
    [[nodiscard]] int autoSaveMinutes() const;
    void setAutoSaveMinutes(int minutes);

    // Empty family / 0 size mean "the application's own default"; that is
    // what a fresh install has, so the suite does not impose a font the
    // platform theme did not choose.
    [[nodiscard]] QString documentFontFamily() const;
    void setDocumentFontFamily(const QString &family);
    [[nodiscard]] qreal documentFontSize() const;
    void setDocumentFontSize(qreal points);

    // QindaWrite's spell checker; read at window construction.
    [[nodiscard]] bool spellCheckEnabled() const;
    void setSpellCheckEnabled(bool on);

    // QindaNote's pen behaviour (M22; the Notes preferences page writes
    // them, NoteWindow::applyPreferences reads them into the page view).
    [[nodiscard]] bool notePressureEnabled() const;
    void setNotePressureEnabled(bool on);
    // "off" | "light" (default) | "strong" — anything else reads as light.
    [[nodiscard]] QString noteSmoothing() const;
    void setNoteSmoothing(const QString &mode);
    // A finger on a touchscreen inks with the pen, highlighter and eraser
    // (default on); off, one finger pans and a resting palm draws nothing.
    [[nodiscard]] bool noteTouchDrawing() const;
    void setNoteTouchDrawing(bool on);

    // Toolkit density ("compact" | "comfortable"; Tk.Density.modeName) and
    // the theme preset used when the QindaQt desktop's tokens are absent
    // (on the desktop the bridge wins). Both read by OfficeWindow.qml.
    [[nodiscard]] QString density() const;
    void setDensity(const QString &mode);
    [[nodiscard]] QString themePreset() const;
    void setThemePreset(const QString &preset);

signals:
    // One signal for the whole store: consumers re-read what they care
    // about. Keeping it coarse means a new key cannot forget its signal.
    void changed();

private:
    void put(const QString &key, const QVariant &value);

    std::unique_ptr<Core::JsonSettings> m_settings;
};

} // namespace QindaOffice::Quick
