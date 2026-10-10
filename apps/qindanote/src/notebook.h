// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Notebook structure (plan section 6.4): one notebook = one directory -
// notebook.json (name + section list), sections/<id>/pages/<id>.json with
// the ink envelope (office_ink), and assets/ for dropped files. Sync-
// friendly, human-readable JSON throughout.

#include "ink_persistence.h"

#include <QDir>

namespace QindaOffice::Apps::Notes {

struct NotePage {
    QString id;
    QString title;
    Ink::PageEnvelope envelope;
    // AGENT-GUARD: pages load LAZILY — load() reads only the index, so an
    // unopened page's envelope is empty in memory. Everything that writes a
    // page must consult this flag: save() used to write every page
    // unconditionally, so saving a notebook after viewing one page
    // overwrote every other page's file with a blank envelope.
    // True for pages created in memory (addPage) and pages read back by
    // loadPage(); false for index entries whose file has not been read.
    bool loaded = false;
    // M84 (D-093): OneNote's subpages — 0 a page, 1 a subpage of the page
    // above, 2 a subpage of that. Kept in notebook.json ("level", written
    // when above 0); a page is never more than one level below the page
    // above it (NoteHost::changePageLevel).
    int level = 0;
};

struct NoteSection {
    QString id;
    QString name;
    QVector<NotePage> pages;
};

class Notebook {
public:
    [[nodiscard]] const QString &name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }
    [[nodiscard]] const QVector<NoteSection> &sections() const { return m_sections; }
    // Empty until create()/load() succeeds (QDir::path() of a
    // default-constructed QDir is ".", which must not count).
    [[nodiscard]] QString directory() const { return m_open ? m_dir.path() : QString(); }
    // An untitled notebook has no directory; every path-touching operation
    // must refuse rather than fall back to QDir()'s "." (which scattered
    // sections/<id>/pages/<id>.json into the process's working directory).
    [[nodiscard]] bool isOpen() const { return m_open; }

    [[nodiscard]] bool create(const QString &path, QString *error = nullptr);
    [[nodiscard]] bool load(const QString &path, QString *error = nullptr);
    [[nodiscard]] bool save(QString *error = nullptr) const;
    // Save As support: writes the full structure into `targetPath` and, only
    // on success, repoints the notebook there. The notebook keeps its old
    // location when the copy fails.
    [[nodiscard]] bool copyStructure(const QString &targetPath, QString *error = nullptr);

    NoteSection &addSection(const QString &name);
    NotePage &addPage(NoteSection &section, const QString &title);
    // Structure edits (note_structure.cpp drives them from the UI).
    // removePage/removeSection delete the page files too: leaving orphans
    // behind would resurrect the pages on the next load.
    bool removePage(const QString &sectionId, const QString &pageId);
    bool removeSection(const QString &sectionId);
    bool renamePage(const QString &sectionId, const QString &pageId, const QString &title);
    bool renameSection(const QString &sectionId, const QString &name);
    // Moves a page within its section; `delta` is -1 (up) or +1 (down).
    bool movePage(const QString &sectionId, const QString &pageId, int delta);
    [[nodiscard]] bool persistPage(const NoteSection &section, const NotePage &page,
                                   QString *error = nullptr) const;
    [[nodiscard]] bool loadPage(NoteSection &section, NotePage &page, QString *error) const;

    // Crash-recovery payload (plan section 2.7): the whole in-memory
    // notebook — index plus the envelopes of every page that is loaded —
    // and the source directory so unloaded pages come back from disk on
    // restore instead of being resurrected blank.
    [[nodiscard]] QByteArray toRecoveryJson() const;
    [[nodiscard]] bool fromRecoveryJson(const QByteArray &json, QString *error = nullptr);

    // Stable-id lookups. AGENT-GUARD: the UI must hold ids, never NotePage*/
    // NoteSection* — the underlying QVectors reallocate on every append/load,
    // and a stale pointer read freed memory (and once wrote garbage-named
    // page files).
    [[nodiscard]] NoteSection *sectionById(const QString &id);
    [[nodiscard]] NotePage *pageById(const QString &sectionId, const QString &pageId);
    [[nodiscard]] NoteSection *sectionOf(const QString &pageId);

    static QString newId();

private:
    QDir m_dir;
    bool m_open = false;
    QString m_name;
    QVector<NoteSection> m_sections;
};

} // namespace QindaOffice::Apps::Notes
