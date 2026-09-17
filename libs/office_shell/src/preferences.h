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

#include <memory>

namespace QindaOffice::Core {
class JsonSettings;
}

namespace QindaOffice::Shell {

class Preferences final : public QObject {
    Q_OBJECT
public:
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

signals:
    // One signal for the whole store: consumers re-read what they care
    // about. Keeping it coarse means a new key cannot forget its signal.
    void changed();

private:
    void put(const QString &key, const QVariant &value);

    std::unique_ptr<Core::JsonSettings> m_settings;
};

} // namespace QindaOffice::Shell
