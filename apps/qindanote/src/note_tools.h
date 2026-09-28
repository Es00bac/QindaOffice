// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The assistant's hands on a note page (D-058, office_ai document_tools.h).
// The snapshot lists the page's typed blocks as [n] (their index); the
// operations rewrite, delete or insert blocks, replace the page's typed
// content with one markdown block, or rename the page. Everything lands as
// ONE BlocksCommand on the page's history (Undo reverts it). Ink and images
// are never touched: ink is not text, and an image has no text to edit.
//
// Blocks are free-floating (D-027), so a rewrite that makes a block taller
// would slide it over the next one; after every apply the blocks are
// reflowed — a block overlapping one above it (horizontally) moves down.

#include "document_tools.h"

#include <QJsonArray>

namespace QindaOffice::Apps::Notes {

class NoteHost;

class NoteTools final : public Ai::DocumentTools {
public:
    explicit NoteTools(NoteHost *host) : m_host(host) {}

    [[nodiscard]] QString documentNoun() const override { return QStringLiteral("note page"); }
    [[nodiscard]] QString snapshot(int budget) override;
    [[nodiscard]] QString operationGuide() const override;
    Ai::EditOutcome apply(const QJsonArray &edits) override;
    void undo() override;

    // Moves blocks down so none overlaps a block above it (exposed for tests).
    static void reflow(QJsonArray *blocks, qreal pageWidth);

private:
    NoteHost *m_host;
    QString m_snapshotPage;   // page id the snapshot was taken of
    QJsonArray m_snapshot;    // its blocks then (ops are checked against them)
};

} // namespace QindaOffice::Apps::Notes
