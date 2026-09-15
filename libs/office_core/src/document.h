// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QUndoStack;

namespace QindaOffice::Core {

// Abstract document base for the four apps (plan section 5.1): canonical
// path, sanitized display title, dirty flag, per-document undo stack, and
// the byte revision of the file as last read. One Document belongs to
// exactly one document window (ADR-0110).
//
// Ownership/lifetime: created on the GUI thread, parented to its window.
// Error behavior: load/save return false and set a human-readable error;
// nothing throws.
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject *parent = nullptr);
    ~Document() override;

    [[nodiscard]] QString canonicalPath() const { return m_path; }
    [[nodiscard]] QString displayTitle() const;
    [[nodiscard]] bool isDirty() const { return m_dirty; }
    [[nodiscard]] QUndoStack *undoStack() const { return m_undoStack; }
    [[nodiscard]] quint64 byteRevision() const { return m_byteRevision; }

    // Load/save the canonical serialization at `path`. Implementations must
    // setCanonicalPath(), refreshByteRevision() and setDirty(false) on
    // success.
    virtual bool load(const QString &path, QString *error = nullptr) = 0;
    virtual bool save(const QString &path, QString *error = nullptr) = 0;

    // MIME types this document can load natively (feeds the open dialog and
    // the .desktop MimeType list).
    [[nodiscard]] virtual QStringList mimeTypes() const = 0;

    // Native serialization for the recovery journal (plan section 2.7);
    // loadRecovery restores exactly what recoverySnapshot produced, without
    // touching the on-disk file or the byte revision.
    [[nodiscard]] virtual QByteArray recoverySnapshot() const = 0;
    virtual bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) = 0;

    void setDirty(bool dirty);
    // Adopts a canonical path without touching content (recovery restore,
    // Save As commit) and refreshes the byte revision from disk.
    void adoptPath(const QString &path);
    // Records the current on-disk revision of m_path (called by load/save).
    void refreshByteRevision();
    // Emitted by views on every content edit; the window debounces it into
    // the recovery journal.
    void notifyContentChanged();

signals:
    void dirtyChanged(bool dirty);
    void contentChanged();

protected:
    void setCanonicalPath(const QString &path);

    QUndoStack *m_undoStack = nullptr;
    QString m_path;
    bool m_dirty = false;
    quint64 m_byteRevision = 0;
};

} // namespace QindaOffice::Core
