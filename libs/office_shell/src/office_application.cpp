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
    if (m_journal == nullptr) {
        return; // stores not built (windowless runs) - nothing to offer
    }
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
        box.addButton(tr("&Discard"), QMessageBox::DestructiveRole);
        box.setDefaultButton(restore);
        box.exec();
        if (box.clickedButton() == restore) {
            DocumentWindow *window = createWindow();
            present(window);
            static_cast<void>(
                window->restoreFromJournal(entry->path, entry->untitled, entry->payload, key));
        } else {
            m_journal->clear(key);
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
    auto *window = new DocumentWindow;
    auto *document = new Core::PlainTextDocument; // M1 stub engine
    window->setDocument(document);
    wireWindow(window);
    return window;
}

void OfficeApplication::wireWindow(DocumentWindow *window)
{
    m_windows.append(window);
    window->setAppServices(this);
    connect(window, &DocumentWindow::newWindowRequested, this,
            [this] { static_cast<void>(newWindow()); });
    connect(window, &DocumentWindow::openPathsRequested, this,
            [this](const QStringList &paths) { openPaths(paths); });
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
    // (ADR-0110): when the last window closes, its path is preserved.
    QString closedPath;
    if (auto *window = qobject_cast<DocumentWindow *>(object);
        window != nullptr && window->document() != nullptr) {
        closedPath = window->document()->canonicalPath();
    }
    pruneClosed();
    updateInventoryOnClose(closedPath);
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
}

void OfficeApplication::restoreFromInventory()
{
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
