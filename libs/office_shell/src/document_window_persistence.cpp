// SPDX-License-Identifier: GPL-3.0-or-later
// DocumentWindow persistence paths (plan section 5.1): load, recovery
// restore, and the shared save / save-as / close-consent flow. Split out
// of document_window.cpp to keep every file under the line budget.

#include "document_window.h"

#include "app_services.h"
#include "document.h"
#include "recovery_journal.h"

#include <atomic_file_store.h>

#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QMessageBox>
#include <QStatusBar>

namespace QindaOffice::Shell {

namespace {

// "OpenDocument Text (*.odt);;HTML (*.html)" -> "odt" for the given single
// filter. Empty when the filter matches anything (e.g. "All files (*)").
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

bool DocumentWindow::isPristineUntitled() const
{
    return m_document != nullptr && m_document->canonicalPath().isEmpty()
        && !m_document->isDirty();
}

bool DocumentWindow::loadPath(const QString &path)
{
    if (m_document == nullptr) {
        return false;
    }
    QString error;
    if (!m_document->load(path, &error)) {
        QMessageBox::warning(this, tr("Open failed"),
                             tr("Could not open %1:\n%2").arg(path, error));
        return false;
    }
    syncEditorFromDocument();
    syncTitle(); // the path may have changed even though dirty stayed false
    if (m_services != nullptr) {
        m_services->documentSaved(m_document->canonicalPath());
    }
    statusBar()->showMessage(tr("Opened %1").arg(path), 5000);
    return true;
}

bool DocumentWindow::restoreFromJournal(const QString &path, bool untitled,
                                        const QByteArray &payload, const QString &journalKey)
{
    Q_UNUSED(untitled);
    if (m_document == nullptr) {
        return false;
    }
    QString error;
    if (!m_document->loadRecovery(payload, &error)) {
        QMessageBox::warning(this, tr("Recovery failed"),
                             tr("Could not restore the document:\n%1").arg(error));
        return false;
    }
    if (!path.isEmpty()) {
        // Present the on-disk path in the title; the byte revision is
        // refreshed so a later save notices external changes.
        m_document->adoptPath(path);
    }
    m_journalKey = journalKey;
    syncEditorFromDocument();
    syncTitle();
    return true;
}

bool DocumentWindow::saveDocument()
{
    if (m_document == nullptr) {
        return true; // nothing to save is not a failure
    }
    const QString path = m_document->canonicalPath();
    if (path.isEmpty()) {
        return saveDocumentAs();
    }
    if (!ensureWritableTarget(path)) {
        return false;
    }
    QString error;
    if (!m_document->save(path, &error)) {
        QMessageBox::critical(this, tr("Save failed"),
                              tr("Could not save %1:\n%2").arg(path, error));
        return false;
    }
    commitSuccessfulSave();
    statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    return true;
}

bool DocumentWindow::saveDocumentAs()
{
    if (m_document == nullptr) {
        return false;
    }
    // Start on the document's own file (or on its title under Documents for
    // an untitled one): the empty `dir` argument made every Save As open in
    // the process's working directory with no name filled in.
    const QString current = m_document->canonicalPath();
    const QString startPath = !current.isEmpty()
        ? current
        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QLatin1Char('/') + m_document->displayTitle();
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"), startPath,
                                                saveFilterString(), &selectedFilter);
    if (path.isEmpty()) {
        return false; // cancelled
    }
    // AGENT-GUARD: complete the extension ourselves. Qt only applies a
    // default suffix to its own dialog, so a name typed without one was
    // saved extension-less — and every engine here dispatches on the
    // suffix, so the file could not be reopened afterwards.
    if (QFileInfo(path).suffix().isEmpty()) {
        const QString suffix = suffixFromFilter(selectedFilter);
        if (!suffix.isEmpty()) {
            path += QLatin1Char('.') + suffix;
        }
    }
    if (m_services != nullptr
        && m_services->isPathOpenInAnotherWindow(path, this)) {
        QMessageBox::warning(this, tr("Document already open"),
                             tr("%1 is already open in another window.").arg(path));
        return false;
    }
    if (!ensureWritableTarget(path)) {
        return false;
    }
    QString error;
    if (!m_document->save(path, &error)) {
        QMessageBox::critical(this, tr("Save failed"),
                              tr("Could not save %1:\n%2").arg(path, error));
        return false;
    }
    commitSuccessfulSave();
    statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    return true;
}

void DocumentWindow::commitSuccessfulSave()
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

bool DocumentWindow::ensureWritableTarget(const QString &path)
{
    // Optimistic concurrency (plan section 5.1): refuse to clobber a file
    // changed on disk since load without explicit consent. Revision 0 on
    // either side means "no earlier on-disk state" and always passes.
    const quint64 onDisk = Core::fileRevision(path);
    if (onDisk == 0 || m_document->byteRevision() == 0 || onDisk == m_document->byteRevision()) {
        return true;
    }
    const auto choice = QMessageBox::warning(
        this, tr("File changed on disk"),
        tr("%1 was modified by another program since it was opened.\n"
           "Saving will overwrite those changes.")
            .arg(path),
        QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
    return choice == QMessageBox::Save;
}

void DocumentWindow::scheduleJournalSnapshot()
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
    journal->schedule(m_journalKey, m_document->canonicalPath(),
                      m_document->canonicalPath().isEmpty(),
                      [doc] { return doc ? doc->recoverySnapshot() : QByteArray(); });
}

void DocumentWindow::clearJournalEntry()
{
    if (m_journalKey.isEmpty()) {
        return;
    }
    if (Core::RecoveryJournal *journal = m_services ? m_services->journal() : nullptr;
        journal != nullptr) {
        journal->clear(m_journalKey);
    }
}

} // namespace QindaOffice::Shell
