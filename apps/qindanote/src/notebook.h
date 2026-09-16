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

    [[nodiscard]] bool create(const QString &path, QString *error = nullptr);
    [[nodiscard]] bool load(const QString &path, QString *error = nullptr);
    [[nodiscard]] bool save(QString *error = nullptr) const;
    // Save As support: writes the full structure into `targetPath` and, only
    // on success, repoints the notebook there. The notebook keeps its old
    // location when the copy fails.
    [[nodiscard]] bool copyStructure(const QString &targetPath, QString *error = nullptr);

    NoteSection &addSection(const QString &name);
    NotePage &addPage(NoteSection &section, const QString &title);
    [[nodiscard]] bool persistPage(const NoteSection &section, const NotePage &page,
                                   QString *error = nullptr) const;
    [[nodiscard]] bool loadPage(NoteSection &section, NotePage &page, QString *error) const;

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
