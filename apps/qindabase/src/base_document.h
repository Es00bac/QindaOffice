// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// BaseDocument: one database file as a Core::Document, so a QindaBase
// window plugs into DocumentHost like every other app (rule 1: one
// database = one window).
//
// AGENT-CONTRACT (lifecycle, D-049 §4; DocumentHost is the other side):
//  * Every edit commits to SQLite immediately (Access semantics), so an
//    opened database is never dirty: Save is a successful no-op, Ctrl+W
//    closes without asking, and there is nothing for the recovery journal.
//  * Save As = "save a copy and switch to it" (VACUUM INTO next to the
//    target, atomic rename, reopen); the original stays as it was.
//  * An UNTITLED database (File ▸ New) lives in a private scratch file and
//    is dirty until its first Save As; Discard deletes it.
//  * Undo is the document's QUndoStack of Changes (base_file.h): every
//    service edit pushes one command. A step that can no longer be replayed
//    (the records changed behind it) clears the stack and reports why.

#include "base_file.h"
#include "document.h"

#include <QTemporaryDir>

#include <memory>

namespace QindaOffice::Apps::Base {

class BaseDocument : public Core::Document {
    Q_OBJECT
public:
    static constexpr const char *kMimeType = "application/x-qindabase";
    static constexpr const char *kSuffix = "qbase";

    explicit BaseDocument(QObject *parent = nullptr);
    ~BaseDocument() override;

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override;
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;

    // A new database without a path yet (scratch file), from a template or
    // empty; dirty until Save As gives it a home.
    bool createUntitled(const QString &templateId, QString *error = nullptr);
    // A new database at `path` (must not exist).
    bool createAt(const QString &path, const QString &templateId, QString *error = nullptr);

    // The open database; null before load/create. Replaced by Save As —
    // never cache the pointer across fileReplaced().
    [[nodiscard]] QindaOffice::Base::BaseFile *file() const { return m_file.get(); }
    [[nodiscard]] bool isUntitled() const { return m_scratch != nullptr; }
    // The path QindaTK's providers open (the scratch file when untitled).
    [[nodiscard]] QString workingPath() const { return m_file ? m_file->path() : QString(); }

    // One undo step per change; a macro when several belong together
    // (an import: the new table and its rows).
    void pushChange(const QindaOffice::Base::Change &change);
    void pushChanges(const QString &label, const QList<QindaOffice::Base::Change> &changes);
    // Called first by save(): the host saves an open designer's unsaved
    // design (false with an error stops the save).
    void setDesignFlush(std::function<bool(QString *)> flush) { m_designFlush = std::move(flush); }
    // The window's designer has unsaved edits: the document reads dirty.
    void setDesignPending(bool pending);

signals:
    void fileReplaced();
    // An undo/redo could not be replayed; the history was cleared.
    void undoFailed(const QString &message);

private:
    void adopt(std::unique_ptr<QindaOffice::Base::BaseFile> file, const QString &path, bool untitled);
    friend class ChangeCommand;
    void replayFailed(const QString &message);

    // AGENT-GUARD: declaration order = the scratch dir outlives the open
    // file (members are destroyed in reverse), so SQLite closes first.
    std::unique_ptr<QTemporaryDir> m_scratch;
    std::unique_ptr<QindaOffice::Base::BaseFile> m_file;
    std::function<bool(QString *)> m_designFlush;
    bool m_designPending = false;
};

} // namespace QindaOffice::Apps::Base
