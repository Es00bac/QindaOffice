// SPDX-License-Identifier: GPL-3.0-or-later
// DocumentHost persistence paths: load, recovery restore, save / save-as,
// the close consent and the recovery journal. Split out of
// document_host.cpp to keep both inside the line budget. The flows are
// asynchronous because their questions are QML dialogs (see the header).

#include "document_host.h"

#include "app_services.h"

#include "atomic_file_store.h"
#include "recovery_journal.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace QindaOffice::Quick {

namespace {

// "OpenDocument Text (*.odt)" -> "odt". Empty when the filter matches
// anything (e.g. "All files (*)").
QString suffixFromFilter(const QString &filter)
{
    const int open = filter.indexOf(QLatin1Char('('));
    const int close = filter.indexOf(QLatin1Char(')'), open + 1);
    if (open < 0 || close < 0) {
        return {};
    }
    const QString first = filter.mid(open + 1, close - open - 1)
                              .split(QLatin1Char(' '), Qt::SkipEmptyParts)
                              .value(0);
    const int dot = first.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        return {};
    }
    const QString suffix = first.mid(dot + 1);
    return suffix == QLatin1String("*") ? QString() : suffix;
}

} // namespace

bool DocumentHost::isPristineUntitled() const
{
    if (m_document == nullptr) {
        return reusableWithoutDocument();
    }
    return m_document != nullptr && m_document->canonicalPath().isEmpty()
        && !m_document->isDirty();
}

bool DocumentHost::loadPath(const QString &path)
{
    if (m_document == nullptr && !m_loading) {
        return openWithoutDocument(path);
    }
    if (m_document == nullptr || m_loading) {
        return false;
    }
    if (m_document->loadCanRunOnWorker(path)) {
        // Worker-thread load (multi-second on real workbooks): the window
        // stays responsive — busy feedback up front, completion by signal.
        // The worker writes m_asyncError/m_document's staging area; the
        // QFutureWatcher's finished delivery is the synchronization edge.
        m_loading = true;
        emit loadingChanged();
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        showStatusMessage(tr("Opening %1 …").arg(path), 0);
        m_asyncError.clear();
        auto *watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, path] {
            const bool ok = watcher->result();
            watcher->deleteLater();
            finishAsyncLoad(path, ok);
        });
        watcher->setFuture(QtConcurrent::run([this, path] {
            return m_document->loadStaged(path, &m_asyncError);
        }));
        return true;
    }
    QString error;
    m_loading = true;
    emit loadingChanged();
    const bool ok = m_document->load(path, &error);
    m_loading = false;
    emit loadingChanged();
    if (!ok) {
        emit messageRequested(tr("Open failed"), tr("Could not open %1:\n%2").arg(path, error),
                              QStringLiteral("danger"));
        return false;
    }
    syncTitle(); // the path may have changed even though dirty stayed false
    documentReplaced();
    if (m_services != nullptr) {
        m_services->documentSaved(m_document->canonicalPath());
    }
    showStatusMessage(tr("Opened %1").arg(path));
    return true;
}

void DocumentHost::finishAsyncLoad(const QString &path, bool ok)
{
    m_loading = false;
    emit loadingChanged();
    QGuiApplication::restoreOverrideCursor();
    if (!ok) {
        emit messageRequested(tr("Open failed"),
                              tr("Could not open %1:\n%2").arg(path, m_asyncError),
                              QStringLiteral("danger"));
        return;
    }
    m_document->adoptStagedLoad(path);
    syncTitle();
    documentReplaced();
    if (m_services != nullptr) {
        m_services->documentSaved(m_document->canonicalPath());
    }
    showStatusMessage(tr("Opened %1").arg(path));
}

bool DocumentHost::restoreFromJournal(const QString &path, bool untitled,
                                      const QByteArray &payload, const QString &journalKey)
{
    Q_UNUSED(untitled);
    if (m_document == nullptr) {
        return false;
    }
    // AGENT-GUARD: loadRecovery() emits contentChanged. Before this guard
    // that reached scheduleJournalSnapshot() while m_journalKey was still
    // empty, so every restore allocated a NEW untitled key and wrote a copy
    // of the entry it was restoring. Discard then cleared only the original
    // key and the copy came back on the next launch — every launch doubled
    // the pile until the 32-window cap. The entry on disk already holds this
    // payload, so no snapshot is needed while it loads (m_loading silences
    // scheduleJournalSnapshot), and the key is adopted before any later edit
    // can schedule one.
    // AGENT-GUARD: the window may already have journalled under a key of
    // its own before this restore ran — QindaWrite's document reports a
    // content change while its window is still being built, which
    // allocated a fresh untitled key and scheduled a snapshot whose payload
    // is read when the timer fires, i.e. AFTER this restore: the recovered
    // text landed under that second key, Discard cleared only `journalKey`,
    // and the copy came back on every launch. Drop the stray key first.
    if (!m_journalKey.isEmpty() && m_journalKey != journalKey) {
        clearJournalEntry();
        m_journalKey.clear();
    }
    QString error;
    m_loading = true;
    const bool ok = m_document->loadRecovery(payload, &error);
    m_loading = false;
    if (!ok) {
        emit messageRequested(tr("Recovery failed"),
                              tr("Could not restore the document:\n%1").arg(error),
                              QStringLiteral("danger"));
        return false;
    }
    m_journalKey = journalKey;
    if (!path.isEmpty()) {
        m_document->adoptPath(path);
    }
    syncTitle();
    documentReplaced();
    return true;
}

void DocumentHost::openPathsChosen(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    if (!paths.isEmpty()) {
        emit openPathsRequested(paths);
    }
}

// ---- save -----------------------------------------------------------------

void DocumentHost::save()
{
    if (m_document == nullptr) {
        finishSave(true); // nothing to save is not a failure
        return;
    }
    const QString path = m_document->canonicalPath();
    if (path.isEmpty()) {
        saveAs();
        return;
    }
    m_pending = Pending::Save;
    m_pendingPath = path;
    if (ensureWritableTarget(path)) {
        performSave(path);
    }
}

void DocumentHost::saveAs()
{
    if (m_document == nullptr) {
        finishSave(false);
        return;
    }
    // Start on the document's own file (or on its title under Documents
    // for an untitled one).
    const QString current = m_document->canonicalPath();
    const QString startPath = !current.isEmpty()
        ? current
        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QLatin1Char('/') + m_document->displayTitle();
    m_pending = Pending::SaveAs;
    emit saveAsDialogRequested(startPath, saveNameFilters());
}

void DocumentHost::saveAsCancelled()
{
    m_pending = Pending::None;
    finishSave(false);
}

void DocumentHost::saveAsPathChosen(const QUrl &url, const QString &selectedFilter)
{
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        saveAsCancelled();
        return;
    }
    // AGENT-GUARD: complete the extension ourselves — every engine here
    // dispatches on the suffix, so a name typed without one could not be
    // reopened afterwards.
    if (QFileInfo(path).suffix().isEmpty()) {
        const QString suffix = suffixFromFilter(selectedFilter);
        if (!suffix.isEmpty()) {
            path += QLatin1Char('.') + suffix;
        }
    }
    if (m_services != nullptr && m_services->isPathOpenInAnotherWindow(path, this)) {
        emit messageRequested(tr("Document already open"),
                              tr("%1 is already open in another window.").arg(path),
                              QStringLiteral("warning"));
        m_pending = Pending::None;
        finishSave(false);
        return;
    }
    m_pending = Pending::SaveAs;
    m_pendingPath = path;
    if (ensureWritableTarget(path)) {
        performSave(path);
    }
}

bool DocumentHost::ensureWritableTarget(const QString &path)
{
    // Optimistic concurrency (plan section 5.1): refuse to clobber a file
    // changed on disk since load without explicit consent. Revision 0 on
    // either side means "no earlier on-disk state" and always passes.
    const quint64 onDisk = Core::fileRevision(path);
    if (onDisk == 0 || m_document->byteRevision() == 0 || onDisk == m_document->byteRevision()) {
        return true;
    }
    emit overwriteConsentRequested(path);
    return false;
}

void DocumentHost::resolveOverwrite(bool overwrite)
{
    if (m_pending == Pending::None) {
        return;
    }
    if (!overwrite) {
        m_pending = Pending::None;
        finishSave(false);
        return;
    }
    performSave(m_pendingPath);
}

void DocumentHost::performSave(const QString &path)
{
    m_pending = Pending::None;
    QString error;
    if (!m_document->save(path, &error)) {
        emit messageRequested(tr("Save failed"), tr("Could not save %1:\n%2").arg(path, error),
                              QStringLiteral("danger"));
        finishSave(false);
        return;
    }
    commitSuccessfulSave();
    showStatusMessage(tr("Saved %1").arg(path));
    finishSave(true);
}

void DocumentHost::finishSave(bool ok)
{
    const AfterSave next = m_afterSave;
    m_afterSave = AfterSave::Nothing;
    emit saveFinished(ok);
    if (next == AfterSave::Close && ok) {
        authorizeClose();
    }
}

void DocumentHost::commitSuccessfulSave()
{
    // Cleared on save (plan section 2.7); migrate the key first in case the
    // save renamed the document (Save As).
    const QString newKey = m_services != nullptr
        ? m_services->journalKeyForPath(m_document->canonicalPath())
        : Core::RecoveryJournal::keyForPath(m_document->canonicalPath());
    if (Core::RecoveryJournal *journal = m_services ? m_services->journal() : nullptr;
        journal != nullptr) {
        if (!m_journalKey.isEmpty() && m_journalKey != newKey) {
            journal->clear(m_journalKey);
        }
        journal->clear(newKey);
    }
    m_journalKey = newKey;
    if (m_services != nullptr) {
        m_services->documentSaved(m_document->canonicalPath());
    }
    syncTitle();
}

// ---- close ----------------------------------------------------------------

bool DocumentHost::requestClose()
{
    if (m_closeAuthorized) {
        emit closing(this);
        return true;
    }
    if (m_document == nullptr || !m_document->isDirty()) {
        m_closeAuthorized = true;
        emit closeAuthorizedChanged();
        emit closing(this);
        return true;
    }
    // Per-document Save/Discard/Cancel consent (ADR-0110): the window shows
    // the dialog and answers through resolveClose().
    emit closeConsentRequested(title());
    return false;
}

void DocumentHost::resolveClose(const QString &choice)
{
    if (choice == QLatin1String("save")) {
        m_afterSave = AfterSave::Close;
        save();
        return;
    }
    if (choice == QLatin1String("discard")) {
        // Discard: the journal entry goes with the close (plan section
        // 2.7: cleared on save/discard).
        clearJournalEntry();
        authorizeClose();
        return;
    }
    // cancel: nothing changes
}

void DocumentHost::authorizeClose()
{
    if (m_closeAuthorized) {
        return;
    }
    m_closeAuthorized = true;
    emit closeAuthorizedChanged();
    if (m_window != nullptr) {
        // The window's onClosing sees closeAuthorized and accepts; the
        // application then destroys the host.
        m_window->close();
    }
}

// ---- journal and auto-save -------------------------------------------------

void DocumentHost::scheduleJournalSnapshot()
{
    if (m_document == nullptr || m_loading) {
        return;
    }
    Core::RecoveryJournal *journal = m_services != nullptr ? m_services->journal() : nullptr;
    if (journal == nullptr) {
        return;
    }
    if (m_journalKey.isEmpty()) {
        m_journalKey = m_services->allocateUntitledJournalKey();
    }
    const QPointer<Core::Document> doc = m_document;
    // A document with nothing unsaved is not a recovery point: the payload
    // is read when the debounce fires, and an empty one removes the entry
    // (RecoveryJournal::writeNow), so a pristine window never leaves one.
    journal->schedule(m_journalKey, m_document->canonicalPath(),
                      m_document->canonicalPath().isEmpty(), [doc] {
                          return doc && doc->isDirty() ? doc->recoverySnapshot() : QByteArray();
                      }, m_document->recoveryPayloadLimit());
}

void DocumentHost::clearJournalEntry()
{
    if (m_journalKey.isEmpty()) {
        return;
    }
    if (Core::RecoveryJournal *journal = m_services ? m_services->journal() : nullptr;
        journal != nullptr) {
        journal->clear(m_journalKey);
    }
}

void DocumentHost::autoSaveTick()
{
    // AGENT-GUARD: auto-save must never put a dialog on screen. Every case
    // that would ask a question — no path yet, or the file changed
    // underneath us — is skipped and left to the explicit Save.
    if (m_document == nullptr || !m_document->isDirty() || m_pending != Pending::None) {
        return;
    }
    const QString path = m_document->canonicalPath();
    if (path.isEmpty()) {
        return;
    }
    const quint64 onDisk = Core::fileRevision(path);
    if (onDisk != 0 && m_document->byteRevision() != 0 && onDisk != m_document->byteRevision()) {
        showStatusMessage(tr("Auto-save skipped: %1 changed on disk").arg(path), 8000);
        return;
    }
    QString error;
    if (!m_document->save(path, &error)) {
        showStatusMessage(tr("Auto-save failed: %1").arg(error), 8000);
        return;
    }
    commitSuccessfulSave();
    showStatusMessage(tr("Auto-saved %1").arg(path), 4000);
}

} // namespace QindaOffice::Quick
