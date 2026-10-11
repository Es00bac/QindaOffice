// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_document.h"

#include <QFileInfo>

namespace QindaOffice::Apps::Notes {

NoteDocument::NoteDocument()
{
    // A pristine untitled notebook: lazily created on first save, but NOT
    // empty. An empty notebook showed an empty navigator and a page surface
    // bound to nothing, so a fresh QindaNote window could not be written in
    // at all until the user found Page ▸ New Page.
    NoteSection &section = m_notebook.addSection(tr("Notes"));
    m_notebook.addPage(section, tr("Page 1"));
}

bool NoteDocument::load(const QString &path, QString *error)
{
    if (!m_notebook.load(path, error)) {
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    emit contentChanged();
    return true;
}

bool NoteDocument::save(const QString &path, QString *error)
{
    // AGENT-GUARD: Save As must honour its argument for every notebook state.
    // The earlier release ignored `path` whenever a notebook was already
    // open, silently writing to the OLD directory and reporting the new one.
    const QString target = path;
    if (m_notebook.directory().isEmpty()) {
        if (!m_notebook.create(target, error)) {
            return false;
        }
    } else if (QDir(target).absolutePath() != QDir(m_notebook.directory()).absolutePath()
               && !m_notebook.copyStructure(target, error)) {
        // Save As to a different directory: materialise the notebook there.
        return false;
    }
    if (!m_notebook.save(error)) {
        return false;
    }
    setCanonicalPath(m_notebook.directory());
    refreshByteRevision();
    setDirty(false);
    return true;
}

QStringList NoteDocument::mimeTypes() const
{
    return {QStringLiteral("application/x-qinda-notebook")};
}

bool NoteDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    if (!m_notebook.fromRecoveryJson(snapshot, error)) {
        return false;
    }
    setCanonicalPath(m_notebook.directory());
    refreshByteRevision();
    setDirty(true); // recovered content is unsaved by definition
    emit contentChanged();
    return true;
}

} // namespace QindaOffice::Apps::Notes
