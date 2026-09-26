// SPDX-License-Identifier: GPL-3.0-or-later
#include "drawing_document.h"

#include "drawing_io.h"

#include <QUndoCommand>
#include <QUndoStack>

namespace QindaOffice::Diagram {

namespace {

// One undo step: the drawing before and after. The first redo (Qt calls it
// on push) is a no-op — the change is already applied.
class SnapshotCommand final : public QUndoCommand {
public:
    SnapshotCommand(DrawingDocument *document, Drawing before, Drawing after, const QString &label,
                    const QString &mergeKey)
        : QUndoCommand(label)
        , m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_mergeKey(mergeKey)
    {
    }

    void undo() override { m_document->restore(m_before); }
    void redo() override
    {
        if (m_first) {
            m_first = false;
            return;
        }
        m_document->restore(m_after);
    }
    [[nodiscard]] int id() const override { return m_mergeKey.isEmpty() ? -1 : 0x51d1a9; }
    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *next = dynamic_cast<const SnapshotCommand *>(other);
        if (next == nullptr || next->m_mergeKey != m_mergeKey || m_mergeKey.isEmpty()) {
            return false;
        }
        m_after = next->m_after;
        return true;
    }

private:
    DrawingDocument *m_document;
    Drawing m_before;
    Drawing m_after;
    QString m_mergeKey;
    bool m_first = true;
};

} // namespace

DrawingDocument::DrawingDocument(QObject *parent)
    : Core::Document(parent)
    , m_drawing(newDrawing())
{
    // Dirty follows the undo stack: undoing back to the saved state is clean.
    connect(m_undoStack, &QUndoStack::cleanChanged, this, [this](bool clean) { setDirty(!clean); });
}

bool DrawingDocument::edit(const QString &label, const std::function<bool(Drawing &)> &change, const QString &mergeKey)
{
    Drawing next = m_drawing;
    if (!change(next) || next == m_drawing) {
        return false;
    }
    Drawing before = m_drawing;
    m_drawing = next;
    m_undoStack->push(new SnapshotCommand(this, std::move(before), std::move(next), label, mergeKey));
    emit drawingChanged();
    notifyContentChanged();
    return true;
}

bool DrawingDocument::replace(const QString &label, const Drawing &drawing)
{
    return edit(label, [&drawing](Drawing &d) {
        d = drawing;
        return true;
    });
}

void DrawingDocument::restore(const Drawing &drawing)
{
    m_drawing = drawing;
    emit drawingChanged();
    notifyContentChanged();
}

bool DrawingDocument::load(const QString &path, QString *error)
{
    Drawing loaded;
    if (!loadDrawing(path, &loaded, error)) {
        return false;
    }
    m_drawing = loaded;
    m_undoStack->clear();
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    emit drawingReset();
    emit drawingChanged();
    return true;
}

bool DrawingDocument::save(const QString &path, QString *error)
{
    if (!saveDrawing(path, m_drawing, error)) {
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    m_undoStack->setClean();
    setDirty(false);
    return true;
}

QStringList DrawingDocument::mimeTypes() const
{
    return {QString::fromLatin1(kFileMime)};
}

QByteArray DrawingDocument::recoverySnapshot() const
{
    return drawingToJson(m_drawing);
}

bool DrawingDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    Drawing recovered;
    if (!drawingFromJson(snapshot, &recovered, error)) {
        return false;
    }
    m_drawing = recovered;
    m_undoStack->clear();
    setDirty(true); // recovered work is not on disk
    emit drawingReset();
    emit drawingChanged();
    return true;
}

} // namespace QindaOffice::Diagram
