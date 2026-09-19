// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaCalc document: a calc_engine Workbook behind the suite's Core::
// Document interface (plan section 6.2). Loading/saving dispatches on the
// extension: .xlsx natively via calc_io, .csv/.tsv through the engine's
// RFC 4180 reader, and foreign formats (ods/xls...) through the headless
// LibreOffice bridge in a later pass (M3 documents the ods path).
//
// M12 editing depth: every structured mutation (cell input, clear, paste,
// style patch, sheet insert) snapshots the workbook before/after and
// pushes one QUndoStack command, so Ctrl+Z/Ctrl+Y behave like every other
// spreadsheet (see calc_undo.h for the snapshot design).

#include "document.h"
#include "sheet_rules.h"
#include "workbook.h"

#include <QUndoStack>

#include <optional>

namespace QindaOffice::Calc {
class Workbook;
}

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

struct WorkbookSnapshot;
struct StylePatch;

class SpreadsheetDocument final : public Core::Document {
    Q_OBJECT
public:
    SpreadsheetDocument();

    [[nodiscard]] CalcEng::Workbook *workbook() { return &m_workbook; }
    [[nodiscard]] const CalcEng::Workbook *workbook() const { return &m_workbook; }

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    // Native formats (xlsx/csv/tsv) read on a worker thread: a real-world
    // workbook takes seconds to import and a frozen window reads as a dead
    // app. Foreign formats keep the GUI path — the LibreOffice bridge runs
    // a nested event loop that belongs to the GUI thread.
    [[nodiscard]] bool loadCanRunOnWorker(const QString &path) const override;
    bool loadStaged(const QString &path, QString *error) override;
    void adoptStagedLoad(const QString &path) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override;
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;

    // UI-facing edits route through here so dirty/content signals stay
    // uniform for journaling and title updates.
    void applyInput(const CalcEng::CellAddr &addr, const QString &input);
    void clearCells(const CalcEng::CellRange &range);
    // Applies many cell inputs as ONE undo step. Replace All would otherwise
    // push a command per cell, so undoing it would take as many Ctrl+Z
    // presses as there were matches.
    void applyInputs(const QList<QPair<CalcEng::CellAddr, QString>> &edits,
                     const QString &undoText);
    void pasteCells(const CalcEng::CellAddr &at, const QStringList &rows);
    void restyleRange(const CalcEng::CellRange &range, const StylePatch &patch,
                      const QString &undoText);
    void insertSheet(const QString &name);

    // Structural edits (calc_structure_ops.cpp). Each is ONE undo step and
    // goes through calc_engine's structure.h, so formulas, named ranges,
    // merges, geometry and charts all follow the move — a plain cell shuffle
    // here would leave every formula pointing at the wrong data.
    void insertRows(qint32 at, qint32 count);
    void removeRows(qint32 at, qint32 count);
    void insertColumns(qint32 at, qint32 count);
    void removeColumns(qint32 at, qint32 count);
    void sortRange(const CalcEng::CellRange &range, qint32 keyColumn, bool ascending,
                   bool headerRow);
    void mergeCells(const CalcEng::CellRange &range);
    void unmergeCells(const CalcEng::CellAddr &anchor);
    void setColumnWidths(const QList<qint32> &columns, qreal px);
    void setRowHeights(const QList<qint32> &rows, qreal px);
    // Copies the range's first row (right) or first column (down) over the
    // rest of it, shifting relative references like a real fill does.
    void fillRange(const CalcEng::CellRange &range, bool down);
    // Sheet-list edits; renaming and moving keep formulas working.
    void removeSheet(qint32 index);
    void renameSheet(qint32 index, const QString &name);
    void moveSheet(qint32 from, qint32 to);
    void duplicateSheet(qint32 index, const QString &name);

    // Per-sheet rules (calc_rules_ops.cpp). Each is ONE undo step and lands
    // in the recovery payload like everything else.
    void setCondFormats(const QList<CalcEng::CondFormatRule> &rules);
    void setValidations(const QList<CalcEng::ValidationRule> &rules);
    void setAutoFilter(const CalcEng::AutoFilter &filter);
    void setComment(const CalcEng::CellAddr &addr, const CalcEng::CellComment &comment);
    // Chart insert is a document mutation like any other: snapshot, push,
    // dirty, notify. It used to append straight into Workbook::charts, so a
    // chart could not be undone and never reached the recovery journal.
    void addChart(const CalcEng::ChartSpec &spec);
    // Defines (or redefines) a named range over `target`, undoably. An
    // unqualified target is pinned to the active sheet first: a name is an
    // absolute address, so it must not follow whichever sheet the formula
    // using it happens to live on.
    void defineName(const QString &name, const CalcEng::RangeRef &target);
    void setActiveSheet(qint32 index) { m_activeSheet = index; }
    [[nodiscard]] qint32 activeSheet() const { return m_activeSheet; }

    // Undo seam: the command object restores a snapshot through here so
    // dirty/content/structure notifications stay uniform.
    void applySnapshot(const WorkbookSnapshot &snapshot);

    [[nodiscard]] QUndoStack *undoStack() { return &m_undoStack; }

    // Text seams shared by the view (formula bar, painting), the
    // clipboard and the find bar. Active sheet unless noted.
    [[nodiscard]] QString inputTextAt(const CalcEng::CellAddr &addr) const;
    [[nodiscard]] QString displayTextAt(qint32 sheet, const CalcEng::CellAddr &addr) const;
    [[nodiscard]] QString displayTextAt(const CalcEng::CellAddr &addr) const
    {
        return displayTextAt(m_activeSheet, addr);
    }

signals:
    // A typed value a data-validation rule refused. The window surfaces it;
    // silently dropping the input would look like the app lost a keystroke.
    void inputRejected(const CalcEng::CellAddr &addr, const QString &reason);
    // Sheet list/count changed (undo of sheet insert, sheet insert);
    // windows rebuild their tab bars on this.
    void structureChanged();
    // A save that completed but could not carry everything (CSV holds one
    // sheet). The window surfaces it; silence here would be data loss the
    // user never heard about.
    void saveWarning(const QString &message);

private:
    void pushUndo(const WorkbookSnapshot &before, const QString &text);

    CalcEng::Workbook m_workbook;
    // Staging area for the worker-thread load (loadStaged writes it off the
    // GUI thread, adoptStagedLoad moves it in on the GUI thread; the host
    // never starts a second load while one is staged).
    CalcEng::Workbook m_staged;
    qint32 m_activeSheet = 0;
    QUndoStack m_undoStack;
};

// A partial update to the CellStyle of every cell in a range: each set
// field overrides, fg/bg accept a nested optional to also *clear* a color.
struct StylePatch {
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underline;
    std::optional<bool> strikeout;
    std::optional<int> alignment;
    std::optional<int> vAlign;
    std::optional<QString> numberFormat;
    std::optional<QString> fontFamily;
    std::optional<qreal> fontSize;
    std::optional<std::optional<CalcEng::Rgb>> fg;
    std::optional<std::optional<CalcEng::Rgb>> bg;
    std::optional<bool> wrap;
    // Border bit field (bit0 left, bit1 right, bit2 top, bit3 bottom), as
    // CellStyle stores it. Set to 0 to clear every edge.
    std::optional<int> borders;
};

} // namespace QindaOffice::Apps::QindaCalc
