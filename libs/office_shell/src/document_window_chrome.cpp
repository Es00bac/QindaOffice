// SPDX-License-Identifier: GPL-3.0-or-later
// DocumentWindow chrome: the permanent status bar, the View menu's
// toolbar/status-bar toggles, Settings ▸ Preferences… and the auto-save
// timer. Split out of document_window.cpp to keep every file under the
// line budget (AGENTS.md source discipline).
//
// Rationale (D-014): before this pass the suite had no settings UI at all —
// Preferences was created disabled and never enabled — and no window ever
// created a status bar until something called showMessage(), at which point
// the bar appeared and pushed the document up.

#include "document_window.h"

#include "app_services.h"
#include "preferences.h"
#include "preferences_dialog.h"

#include "document.h"

#include <atomic_file_store.h>

#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace QindaOffice::Shell {

void DocumentWindow::createChrome()
{
    // Eager, not lazy: a status bar that materialises on the first message
    // resizes the document area under the user's cursor.
    QStatusBar *bar = statusBar();
    bar->setObjectName(QStringLiteral("statusBar"));
    bar->setSizeGripEnabled(false); // the compositor owns grouped geometry
    m_statusInfo = new QLabel(bar);
    m_statusInfo->setObjectName(QStringLiteral("statusInfoLabel"));
    m_statusInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bar->addPermanentWidget(m_statusInfo);

    connect(m_actions.viewShowToolBars, &QAction::toggled, this, [this](bool show) {
        for (QToolBar *toolBar : findChildren<QToolBar *>()) {
            toolBar->setVisible(show);
        }
        if (!m_applyingChromePrefs) {
            Preferences::instance().setShowToolBars(show);
        }
    });
    connect(m_actions.viewShowStatusBar, &QAction::toggled, this, [this](bool show) {
        statusBar()->setVisible(show);
        if (!m_applyingChromePrefs) {
            Preferences::instance().setShowStatusBar(show);
        }
    });
    connect(m_actions.settingsPreferences, &QAction::triggered, this,
            &DocumentWindow::showPreferences);

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setObjectName(QStringLiteral("autoSaveTimer"));
    connect(m_autoSaveTimer, &QTimer::timeout, this, &DocumentWindow::autoSaveTick);

    // Every window of the app follows the store, so a change made in one
    // window reaches the others without a restart.
    connect(&Preferences::instance(), &Preferences::changed, this, [this] {
        applyChromePreferences();
        applyPreferences();
    });
    applyChromePreferences();
}

void DocumentWindow::applyChromePreferences()
{
    // AGENT-GUARD: the toggles write back to the store, so applying the
    // store to them has to be marked or the two chase each other.
    m_applyingChromePrefs = true;
    const Preferences &prefs = Preferences::instance();
    m_actions.viewShowToolBars->setChecked(prefs.showToolBars());
    m_actions.viewShowStatusBar->setChecked(prefs.showStatusBar());
    // setChecked only emits toggled on a real change, so apply the state
    // directly too — otherwise a window created while the preference is
    // already off would show its chrome anyway.
    for (QToolBar *toolBar : findChildren<QToolBar *>()) {
        toolBar->setVisible(prefs.showToolBars());
    }
    statusBar()->setVisible(prefs.showStatusBar());
    m_applyingChromePrefs = false;
    restartAutoSaveTimer();
}

void DocumentWindow::restartAutoSaveTimer()
{
    if (m_autoSaveTimer == nullptr) {
        return;
    }
    const int minutes = Preferences::instance().autoSaveMinutes();
    if (minutes <= 0) {
        m_autoSaveTimer->stop();
        return;
    }
    m_autoSaveTimer->start(minutes * 60 * 1000);
}

void DocumentWindow::autoSaveTick()
{
    // AGENT-GUARD: auto-save must never put a dialog on screen. It runs on a
    // timer the user is not watching, so every case that would ask a
    // question — no path yet, or the file changed underneath us — is skipped
    // and left to the explicit Save the user drives. Crash recovery still
    // covers whatever auto-save declines to write.
    if (m_document == nullptr || !m_document->isDirty()) {
        return;
    }
    const QString path = m_document->canonicalPath();
    if (path.isEmpty()) {
        return;
    }
    const quint64 onDisk = Core::fileRevision(path);
    if (onDisk != 0 && m_document->byteRevision() != 0 && onDisk != m_document->byteRevision()) {
        statusBar()->showMessage(
            tr("Auto-save skipped: %1 changed on disk").arg(path), 8000);
        return;
    }
    QString error;
    if (!m_document->save(path, &error)) {
        statusBar()->showMessage(tr("Auto-save failed: %1").arg(error), 8000);
        return;
    }
    commitSuccessfulSave();
    statusBar()->showMessage(tr("Auto-saved %1").arg(path), 4000);
}

void DocumentWindow::setStatusInfo(const QString &text)
{
    if (m_statusInfo != nullptr) {
        m_statusInfo->setText(text);
    }
}

QString DocumentWindow::statusInfo() const
{
    return m_statusInfo != nullptr ? m_statusInfo->text() : QString();
}

void DocumentWindow::addPreferencePages(PreferencesDialog *dialog)
{
    Q_UNUSED(dialog);
}

void DocumentWindow::applyPreferences() {}

void DocumentWindow::showPreferences()
{
    PreferencesDialog dialog(&Preferences::instance(), this);
    const bool haveServices = m_services != nullptr;
    dialog.setRestorePrevious(haveServices && m_services->restorePreviousDocuments(),
                              haveServices);
    addPreferencePages(&dialog);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (haveServices) {
        // Keep the Settings-menu toggle and the dialog on one source of
        // truth; setChecked drives the existing AppServices connection.
        m_restoreAction->setChecked(dialog.restorePrevious());
    }
    // Preferences::changed() already re-applied chrome for every window;
    // this covers the case where nothing in the store actually changed.
    applyChromePreferences();
    applyPreferences();
}

} // namespace QindaOffice::Shell
