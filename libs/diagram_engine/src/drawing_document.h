// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DrawingDocument: a Drawing behind the suite's Core::Document contract
// (M50, D-050) — one .qdiagram file per window (ADR-0110), the recovery
// journal, and undo through the document's QUndoStack.
//
// AGENT-CONTRACT (undo): every change goes through edit(), which runs the
// change on a copy, and — only when it returns true AND the drawing really
// changed — pushes ONE snapshot command (before, after) and emits
// drawingChanged. Undo/redo swap whole snapshots; Qt's implicit sharing
// keeps the untouched pages shared, so a step costs one page's items.
// Commands pushed with the same non-empty `mergeKey` back to back merge into
// one step (keyboard nudges, a run of typing in the inspector).

#include "document.h"
#include "drawing.h"

#include <functional>

namespace QindaOffice::Diagram {

class DrawingDocument : public Core::Document {
    Q_OBJECT
public:
    explicit DrawingDocument(QObject *parent = nullptr);

    [[nodiscard]] const Drawing &drawing() const { return m_drawing; }

    // Runs `change` on a copy of the drawing; see the contract above.
    // Returns whether a step was recorded.
    bool edit(const QString &label, const std::function<bool(Drawing &)> &change, const QString &mergeKey = {});
    // Replaces the whole drawing as one undo step (imports, templates).
    bool replace(const QString &label, const Drawing &drawing);

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override;
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;

    // Used by the undo commands only.
    void restore(const Drawing &drawing);

signals:
    // Any change of content: an edit, undo, redo, load or recovery.
    void drawingChanged();
    // The content was replaced wholesale (load, recovery): views reset.
    void drawingReset();

private:
    Drawing m_drawing;
};

} // namespace QindaOffice::Diagram
