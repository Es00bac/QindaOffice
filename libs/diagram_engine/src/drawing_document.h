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

#include <QStringList>

#include <functional>
#include <memory>

namespace QindaOffice::Diagram {

namespace Visio {
struct Source;
}

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

    // .qdiagram, or a Visio drawing/template (M65, D-073: visio_io.h) —
    // the suffix decides, on open and on save.
    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    // What the last load could only show approximately (Visio), for the
    // status bar; empty for a .qdiagram.
    [[nodiscard]] QStringList loadWarnings() const { return m_loadWarnings; }
    // True when the drawing was read from a Visio file whose source the
    // document still holds (unchanged shapes are written back verbatim).
    [[nodiscard]] bool hasVisioSource() const { return m_visio != nullptr; }
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
    // AGENT-CONTRACT (D-068 rule 3 for Visio): the package the drawing was
    // read from, immutable and shared with every save; the items carry its
    // token (visio_io.h). Recovery restores the drawing without it, and a
    // later Visio save looks for it again at the document's path.
    void reattachVisioSource();

    Drawing m_drawing;
    std::shared_ptr<const Visio::Source> m_visio;
    QStringList m_loadWarnings;
};

} // namespace QindaOffice::Diagram
