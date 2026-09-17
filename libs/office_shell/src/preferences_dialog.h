// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Settings ▸ Preferences… for the whole suite.
//
// Before this existed the action was created disabled by
// createStandardActions() and never enabled, so every app shipped a dead
// menu entry and the suite had no settings UI at all (D-014).
//
// The shell owns the pages that describe shell behavior (window chrome,
// auto-save, document font); an app adds its own with addPage() — that is
// the seam QindaWrite's spell-check page and QindaCalc's grid page use,
// and it keeps office_shell from having to know about office_ai or any
// app's engine (dependency direction, AGENTS.md "Layout").
//
// Stock Qt widgets only (AGENTS.md rule 3): no QSS, no palette literals.

#include <QDialog>

class QCheckBox;
class QFontComboBox;
class QSpinBox;
class QTabWidget;

namespace QindaOffice::Shell {

class Preferences;

class PreferencesDialog final : public QDialog {
    Q_OBJECT
public:
    // `prefs` outlives the dialog (the process-wide store). Changes are
    // applied on OK; Cancel leaves the store untouched.
    explicit PreferencesDialog(Preferences *prefs, QWidget *parent = nullptr);

    // Adds an application page after the shell's own. The dialog takes
    // ownership. Call before exec().
    void addPage(QWidget *page, const QString &title);

    // "Restore Previous Documents" is AppServices policy, not a Preferences
    // key, so the owning window feeds it in and reads it back. Pass
    // available=false when no services are wired (the control is then
    // shown disabled rather than hidden, so the dialog's shape is stable).
    void setRestorePrevious(bool enabled, bool available);
    [[nodiscard]] bool restorePrevious() const;

private:
    void buildGeneralPage();
    void buildDocumentsPage();
    void apply();

    Preferences *m_prefs;
    QTabWidget *m_tabs;
    QCheckBox *m_showStatusBar = nullptr;
    QCheckBox *m_showToolBars = nullptr;
    QSpinBox *m_defaultZoom = nullptr;
    QCheckBox *m_restorePrevious = nullptr;
    QSpinBox *m_autoSave = nullptr;
    QCheckBox *m_useThemeFont = nullptr;
    QFontComboBox *m_fontFamily = nullptr;
    QSpinBox *m_fontSize = nullptr;
};

} // namespace QindaOffice::Shell
