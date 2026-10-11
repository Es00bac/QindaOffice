// SPDX-License-Identifier: GPL-3.0-or-later
#include "notebook.h"
#include "note_versions.h"
#include "notebook_storage_p.h"

#include <QFile>

namespace QindaOffice::Apps::Notes {
using namespace NotebookStorage;

bool Notebook::persistPage(const NoteSection &section, const NotePage &page, QString *error) const
{
    // AGENT-GUARD: an untitled notebook has no directory. QDir()'s path is
    // ".", so this used to write sections/<id>/pages/<id>.json into whatever
    // directory the process was launched from. In-memory content is covered
    // by the recovery journal until the first real save.
    if (!m_open) {
        if (error != nullptr) {
            *error = QStringLiteral("notebook has no directory yet");
        }
        return false;
    }
    if (!idIsSafe(section.id) || !idIsSafe(page.id)) {
        if (error != nullptr) {
            *error = QStringLiteral("unsafe notebook id");
        }
        return false;
    }
    const QString dirPath = QStringLiteral("sections/%1/pages").arg(section.id);
    if (!m_dir.mkpath(dirPath)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create %1").arg(dirPath);
        }
        return false;
    }
    if (!page.loaded) {
        if (error) *error = QStringLiteral("Cannot save an unloaded page.");
        return false;
    }
    const QString path = m_dir.filePath(dirPath + QStringLiteral("/%1.json").arg(page.id));
    Ink::PageEnvelope current = page.envelope;
    current.id = page.id;
    current.title = page.title;
    const QByteArray data = Ink::pageToJson(current);
    if (m_versionsEnabled && QFile::exists(path)) {
        QFile prior(path);
        if (!prior.open(QIODevice::ReadOnly)) {
            if (error) *error = prior.errorString();
            return false;
        }
        const QByteArray bytes = prior.readAll();
        if (bytes == data) return true;
        Ink::PageEnvelope old;
        if (!Ink::pageFromJson(bytes, &old) || old.id != page.id) {
            if (error) *error = QStringLiteral("The saved page is damaged; it was not overwritten.");
            return false;
        }
        // AGENT-CONTRACT: capture succeeds before the live page is replaced.
        // Autosaves coalesce to five-minute versions; explicit captures and
        // restores bypass that interval (note_host_versions.cpp).
        if (!Versions::capture(m_dir.path(), section.id, old, false, error)) return false;
    }
    return writeJsonAtomically(path, data, error);
}

bool Notebook::loadPage(NoteSection &section, NotePage &page, QString *error) const
{
    if (!m_open || !idIsSafe(section.id) || !idIsSafe(page.id)) {
        if (error != nullptr) {
            *error = QStringLiteral("notebook has no directory yet");
        }
        return false;
    }
    QFile file(m_dir.filePath(
        QStringLiteral("sections/%1/pages/%2.json").arg(section.id, page.id)));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    if (!Ink::pageFromJson(file.readAll(), &page.envelope)) {
        if (error != nullptr) {
            *error = QStringLiteral("page %1 is not valid JSON").arg(page.id);
        }
        return false;
    }
    page.loaded = true;
    return true;
}

} // namespace QindaOffice::Apps::Notes
