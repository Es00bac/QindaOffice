// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The assistant's hands in a QindaCalc workbook (D-058, office_ai
// document_tools.h). The model sees the active sheet's filled cells with
// A1 addresses — each as the user would type it (a value, or =formula
// followed by its current result) — plus the cursor and selection, and
// writes cells, blocks of cells and clears ranges. One reply's operations
// become ONE applyInputs() call, so one Undo reverts them.
//
// Scope: the active sheet only (the snapshot names the others). Like
// Replace All, bulk input skips data-validation prompts.

#include "document_tools.h"

namespace QindaOffice::Apps::QindaCalc {

class CalcHost;
class SpreadsheetDocument;

class CalcTools final : public Ai::DocumentTools {
public:
    static constexpr int kMaxSnapshotCells = 3000;
    static constexpr int kMaxEditCells = 20000;

    CalcTools(SpreadsheetDocument *document, CalcHost *host) : m_document(document), m_host(host) {}

    [[nodiscard]] QString documentNoun() const override { return QStringLiteral("spreadsheet"); }
    [[nodiscard]] QString snapshot(int budget) override;
    [[nodiscard]] QString operationGuide() const override;
    Ai::EditOutcome apply(const QJsonArray &edits) override;
    void undo() override;

private:
    SpreadsheetDocument *m_document;
    CalcHost *m_host;
};

} // namespace QindaOffice::Apps::QindaCalc
