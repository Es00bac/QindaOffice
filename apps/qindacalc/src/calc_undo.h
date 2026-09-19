// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Calc editing depth (M12): whole-workbook snapshots behind a QUndoStack.
// A spreadsheet edit is a structured operation (parse, dependency
// re-index, recalc, charts), so cell-level inverse commands would
// duplicate the engine; snapshot/replace is the simple correct inverse.
// Sheets in practice are sparse hashes of a few hundred edited cells, so
// a deep copy per edit is cheap (AGENT-NOTE: revisit if profiling ever
// shows snapshot cost — the seam is takeSnapshot/restoreSnapshot only).

#include "workbook.h"

#include <QUndoCommand>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;

struct SheetSnapshot {
    QString name;
    QHash<quint64, CalcEng::Cell> cells;
    QHash<qint32, qreal> colWidths;
    QHash<qint32, qreal> rowHeights;
    QHash<quint64, qint32> styleIds;
    QList<CalcEng::CellRange> merges;
    // AGENT-GUARD: the sheet's RULES are part of its state. Leaving them out
    // of the snapshot means Ctrl+Z after adding a conditional format does
    // nothing, and the crash-recovery payload (which is this snapshot in
    // JSON) silently drops every rule the document had.
    QList<CalcEng::CondFormatRule> condFormats;
    QList<CalcEng::ValidationRule> validations;
    CalcEng::AutoFilter filter;
    QHash<quint64, CalcEng::CellComment> comments;
    QList<CalcEng::TableSpec> tables;
    // View & print state is document state (M37): it round-trips to xlsx.
    qint32 freezeRows = 0;
    qint32 freezeCols = 0;
    bool hidden = false;
    CalcEng::CellAddr cursor{0, 0};
    std::optional<CalcEng::CellRange> printArea;
    std::optional<CalcEng::CellRange> printTitleRows;
    std::optional<CalcEng::CellRange> printTitleCols;
    CalcEng::Sheet::PageSetup pageSetup;
};

struct WorkbookSnapshot {
    QVector<SheetSnapshot> sheets;
    QVector<CalcEng::CellStyle> styles;
    QHash<QString, CalcEng::RangeRef> names;
    QList<CalcEng::ChartSpec> charts;
    QList<CalcEng::ImageSpec> images;
    qint32 activeSheet = 0;
};

[[nodiscard]] WorkbookSnapshot takeSnapshot(const CalcEng::Workbook &workbook,
                                            qint32 activeSheet);

// Rebuilds the workbook from the snapshot: fresh book assembled through
// public mutators, dependency graph re-registered from the stored ASTs,
// one full recalc (restores volatile/cycle state), then move-assigned in.
// Callers emit dirty/content signals (see SpreadsheetDocument).
void restoreSnapshot(CalcEng::Workbook *workbook, const WorkbookSnapshot &snapshot);

// One undo step: swap between the before/after workbook states and notify
// the document layer so views, title and journal see the change.
class WorkbookUndoCommand final : public QUndoCommand {
public:
    WorkbookUndoCommand(SpreadsheetDocument *document, const WorkbookSnapshot &before,
                        const WorkbookSnapshot &after, const QString &text,
                        QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    void apply(const WorkbookSnapshot &snapshot);

    SpreadsheetDocument *m_document;
    WorkbookSnapshot m_before;
    WorkbookSnapshot m_after;
    // QUndoStack::push() redoes immediately; the first one is a no-op here.
    bool m_pushed = false;
};

} // namespace QindaOffice::Apps::QindaCalc
