// SPDX-License-Identifier: GPL-3.0-or-later
#include "office_application.h"

#include "document_window.h"
#include "single_instance.h"

#include "json_settings.h"
#include "paths.h"
#include "plain_text_document.h"
#include "recent_files.h"
#include "recovery_journal.h"
#include "restore_inventory.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm>

namespace QindaOffice::Shell {

OfficeApplication::OfficeApplication(QObject *parent)
    : QObject(parent)
{
}

void OfficeApplication::setWindowFactory(WindowFactory factory)
{
    m_windowFactory = std::move(factory);
}

OfficeApplication::~OfficeApplication() = default;

// Lazily builds the persisted stores so a bare OfficeApplication (tests,
// tool reuse) never touches the user's config/state directories.
void OfficeApplication::ensureStores()
{
    if (m_settings != nullptr) {
        return;
    }
    const QString leaf = Core::appLeaf();
    m_settings = std::make_unique<Core::JsonSettings>(
        Core::configDirFor(leaf) + QStringLiteral("/settings.json"));
    m_recent = std::make_unique<Core::RecentFiles>(this);
    static_cast<void>(m_recent->load(
        Core::configDirFor(leaf) + QStringLiteral("/recent.json"), nullptr));
    m_inventory = std::make_unique<Core::RestoreInventory>(
        Core::stateDirFor(leaf) + QStringLiteral("/open-documents-v1.json"));
    m_inventory->setEnabled(
        m_settings->value(QStringLiteral("restorePrevious"), false).toBool());
    m_journal = std::make_unique<Core::RecoveryJournal>(
        Core::stateDirFor(leaf) + QStringLiteral("/recovery"), this);
}

QString OfficeApplication::serviceName() const
{
    return QStringLiteral("org.qindaqt.") + Core::appLeaf();
}

bool OfficeApplication::acquirePrimary(const QStringList &initialPaths, bool *handedOff)
{
    m_single = std::make_unique<SingleInstance>(
        serviceName(), QStringLiteral("/org/qindaqt/") + Core::appLeaf(), this);
    const bool acquired = m_single->acquireOrHandOff(initialPaths, handedOff);
    if (acquired) {
        m_single->publish();
        connect(m_single.get(), &SingleInstance::openPathsRequested, this,
                [this](const QStringList &paths) { openPaths(paths); });
        return true;
    }
    return false;
}

void OfficeApplication::offerRecovery()
{
    // AGENT-GUARD: build the stores HERE, not lazily on the first window.
    // runOfficeApp calls offerRecovery() before any window exists; with the
    // old lazy-only construction m_journal was always null and crash
    // recovery was silently never offered — the mandated default-on safety
    // net wrote entries no launch ever surfaced.
    ensureStores();
    const QStringList keys = m_journal->pendingKeys();
    for (const QString &key : keys) {
        const auto entry = m_journal->entry(key);
        if (!entry.has_value()) {
            m_journal->clear(key);
            continue;
        }
        const QString name = entry->untitled || entry->path.isEmpty()
            ? tr("an untitled document")
            : entry->path;
        QMessageBox box(QMessageBox::Question, tr("Recover document"),
                        tr("Recover unsaved changes to %1?").arg(name),
                        QMessageBox::NoButton, nullptr);
        // AGENT-NOTE: QMessageBox has no Restore standard button (only
        // RestoreDefaults); role-explicit custom buttons keep the label.
        QPushButton *restore = box.addButton(tr("&Restore"), QMessageBox::AcceptRole);
        QPushButton *discard = box.addButton(tr("&Discard"), QMessageBox::DestructiveRole);
        // AGENT-GUARD: the journal entry is cleared ONLY by an explicit
        // Discard. Neither button carries RejectRole, so Escape/close leaves
        // clickedButton() null — the old `else` branch treated that as
        // Discard and destroyed the user's unsaved work on a keystroke that
        // universally means "not now". Anything short of Discard keeps the
        // entry and re-offers it next launch (plan section 2.7: cleared on
        // save or discard, never on dismissal).
        box.setDefaultButton(restore);
        box.setEscapeButton(QMessageBox::NoButton);
        box.exec();
        if (box.clickedButton() == discard) {
            m_journal->clear(key);
            continue;
        }
        if (box.clickedButton() != restore) {
            continue; // dismissed: keep the entry for the next launch
        }
        if (m_windows.size() >= kMaxWindows) {
            emit diagnosticReported(
                tr("Window limit reached: %1 documents are already open in this process.")
                    .arg(kMaxWindows));
            break; // remaining entries stay on disk
        }
        DocumentWindow *window = createWindow();
        present(window);
        if (!window->restoreFromJournal(entry->path, entry->untitled, entry->payload, key)) {
            // The engine refused the snapshot; do not strand an empty window
            // and do not clear the entry (the user saw the reason).
            window->close();
        }
    }
}

DocumentWindow *OfficeApplication::newWindow(const QString &path)
{
    if (m_windows.size() >= kMaxWindows) {
        emit diagnosticReported(
            tr("Window limit reached: %1 documents are already open in this process.")
                .arg(kMaxWindows));
        return nullptr;
    }
    DocumentWindow *window = createWindow();
    present(window);
    if (!path.isEmpty() && !window->loadPath(path)) {
        window->close();
        return nullptr;
    }
    return window;
}

DocumentWindow *OfficeApplication::createWindow()
{
    auto *window = m_windowFactory != nullptr ? m_windowFactory() : new DocumentWindow;
    // App factories (QindaCalc, later QindaShow/Note) install their own
    // document in their window constructor; only the stub window needs the
    // default plain-text engine.
    if (!window->hasDocument()) {
        window->setDocument(new Core::PlainTextDocument); // M1 stub engine
    }
    wireWindow(window);
    return window;
}

void OfficeApplication::wireWindow(DocumentWindow *window)
{
    m_windows.append(window);
    // AGENT-GUARD: closed windows must be DESTROYED, not merely hidden.
    // Without WA_DeleteOnClose every close left a zombie: reopen-activation
    // raised hidden windows, pristine-window reuse loaded files into
    // invisible windows, and the 32-window cap filled up with corpses.
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setAppServices(this);
    connect(window, &DocumentWindow::newWindowRequested, this,
            [this] { static_cast<void>(newWindow()); });
    connect(window, &DocumentWindow::openPathsRequested, this,
            [this](const QStringList &paths) { openPaths(paths); });
    // The closing path must be captured while the window object is alive;
    // destroyed() hands out a bare QObject past the derived destructor.
    connect(window, &DocumentWindow::closing, this, [this](DocumentWindow *closingWindow) {
        m_lastClosingPath = closingWindow->document() != nullptr
            ? closingWindow->document()->canonicalPath()
            : QString();
    });
    connect(window, &DocumentWindow::destroyed, this, &OfficeApplication::windowClosed);
}

void OfficeApplication::present(DocumentWindow *window)
{
    window->show();
    emit windowCreated(window);
}

QString OfficeApplication::canonical(const QString &path) const
{
    return QFileInfo(path).canonicalFilePath();
}

bool OfficeApplication::openPaths(const QStringList &paths, QString *diagnostic)
{
    // A bare second launch hands off an empty path list; mirroring the
    // first-party terminal's default-request semantics it opens a fresh
    // untitled window rather than doing nothing.
    if (paths.isEmpty()) {
        return newWindow() != nullptr;
    }
    bool allOpened = true;
    for (const QString &path : paths) {
        const QString canon = canonical(path);
        bool alreadyOpen = false;
        if (!canon.isEmpty()) {
            // Canonical-path uniqueness (ADR-0110): activate the existing
            // window instead of opening a duplicate.
            for (DocumentWindow *window : windows()) {
                if (window->document() != nullptr
                    && window->document()->canonicalPath() == canon) {
                    window->raise();
                    window->activateWindow();
                    alreadyOpen = true;
                    break;
                }
            }
        }
        if (alreadyOpen) {
            continue;
        }
        {
            // A pristine untitled window may absorb the first opened path
            // instead of spawning yet another window.
            DocumentWindow *target = nullptr;
            for (DocumentWindow *window : windows()) {
                if (window->isPristineUntitled()) {
                    target = window;
                    break;
                }
            }
            if (target == nullptr) {
                if (m_windows.size() >= kMaxWindows) {
                    allOpened = false;
                    if (diagnostic != nullptr) {
                        *diagnostic = tr("Could not open all documents: window limit reached.");
                    }
                    break;
                }
                target = createWindow();
                present(target);
            }
            if (!target->loadPath(path)) {
                allOpened = false;
            }
        }
    }
    // Keep the restore inventory in step with runtime opens (SC: it was
    // only ever written from close handling, so a crash left nothing to
    // restore even with the feature enabled).
    if (allOpened) {
        persistWindowInventory();
    }
    return allOpened;
}

QList<DocumentWindow *> OfficeApplication::windows() const
{
    QList<DocumentWindow *> result;
    result.reserve(m_windows.size());
    for (const QPointer<DocumentWindow> &window : m_windows) {
        if (window != nullptr) {
            result.append(window.data());
        }
    }
    return result;
}

int OfficeApplication::windowCount() const
{
    int live = 0;
    for (const QPointer<DocumentWindow> &window : m_windows) {
        if (window != nullptr) {
            ++live;
        }
    }
    return live;
}

void OfficeApplication::windowClosed(QObject *object)
{
    // The closing window's own path matters for the final-close rule
    // (ADR-0110): when the last window closes, its path is preserved. The
    // path was captured by the closing() signal — qobject_cast here is
    // always null because destroyed() fires after ~DocumentWindow ran.
    Q_UNUSED(object);
    pruneClosed();
    updateInventoryOnClose(m_lastClosingPath);
    m_lastClosingPath.clear();
}

void OfficeApplication::pruneClosed()
{
    m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(),
                                   [](const QPointer<DocumentWindow> &w) { return w.isNull(); }),
                    m_windows.end());
}

void OfficeApplication::updateInventoryOnClose(const QString &closedPath)
{
    if (m_inventory == nullptr) {
        return;
    }
    // AGENT-GUARD: bounded persistence, default OFF (AGENTS.md rule 7). The
    // inventory used to be rewritten on every open and close regardless of
    // the policy, so a user who never enabled "Restore Previous Documents"
    // still had the path of every document they opened written into
    // $XDG_STATE_HOME. With the policy off we persist nothing at all.
    if (!m_inventory->enabled()) {
        return;
    }
    Core::RestoreInventory::Snapshot snapshot;
    for (DocumentWindow *window : windows()) {
        if (window->document() != nullptr
            && !window->document()->canonicalPath().isEmpty()) {
            snapshot.paths.append(window->document()->canonicalPath());
        }
    }
    if (snapshot.paths.isEmpty() && !closedPath.isEmpty()) {
        // Final close preserves its path for the next launch.
        snapshot.paths.append(closedPath);
        snapshot.activeIndex = 0;
    }
    m_inventory->setSnapshot(snapshot);
    m_inventory->persist(nullptr);
}

void OfficeApplication::persistWindowInventory()
{
    updateInventoryOnClose(QString());
}

bool OfficeApplication::restorePreviousDocuments() const
{
    return m_inventory != nullptr && m_inventory->enabled();
}

void OfficeApplication::setRestorePreviousDocuments(bool enabled)
{
    ensureStores();
    m_inventory->setEnabled(enabled);
    m_settings->setValue(QStringLiteral("restorePrevious"), enabled);
    m_settings->save(nullptr);
    if (enabled) {
        // Seed the inventory from the windows that are already open — while
        // the policy was off nothing was written, so without this the first
        // launch after enabling would restore an empty set.
        persistWindowInventory();
    } else {
        m_inventory->setSnapshot({});
        static_cast<void>(m_inventory->persist(nullptr));
    }
}

void OfficeApplication::restoreFromInventory()
{
    ensureStores();
    if (m_inventory == nullptr || !m_inventory->enabled()) {
        return;
    }
    const Core::RestoreInventory::Snapshot snapshot = m_inventory->snapshot();
    if (snapshot.paths.isEmpty()) {
        return;
    }
    openPaths(snapshot.paths);
    const QList<DocumentWindow *> open = windows();
    if (!open.isEmpty()) {
        const int index = qBound(0, snapshot.activeIndex, open.size() - 1);
        open.at(index)->raise();
        open.at(index)->activateWindow();
    }
}

Core::RecoveryJournal *OfficeApplication::journal()
{
    ensureStores();
    return m_journal.get();
}

Core::RecentFiles *OfficeApplication::recentFiles()
{
    ensureStores();
    return m_recent.get();
}

bool OfficeApplication::isPathOpenInAnotherWindow(const QString &canonicalPath,
                                                  const DocumentWindow *self) const
{
    for (DocumentWindow *window : windows()) {
        if (window == self || window->document() == nullptr) {
            continue;
        }
        if (window->document()->canonicalPath() == canonicalPath) {
            return true;
        }
    }
    return false;
}

QString OfficeApplication::allocateUntitledJournalKey()
{
    ensureStores();
    return m_journal->allocateUntitledKey();
}

QString OfficeApplication::journalKeyForPath(const QString &canonicalPath) const
{
    return Core::RecoveryJournal::keyForPath(canonicalPath);
}

void OfficeApplication::documentSaved(const QString &canonicalPath)
{
    if (m_recent != nullptr && !canonicalPath.isEmpty()) {
        m_recent->add(canonicalPath);
    }
}

} // namespace QindaOffice::Shell
