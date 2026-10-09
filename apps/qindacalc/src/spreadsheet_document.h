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

#include "data_table.h"
#include "data_tools.h"
#include "document.h"
#include "pivot_edit.h"
#include "scenario.h"
#include "text_to_columns.h"
#include "sheet_rules.h"
#include "workbook.h"

#include <QUndoStack>

#include <functional>
#include <memory>
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
    // The snapshot is compressed JSON: the journal's binary bound.
    [[nodiscard]] qint64 recoveryPayloadLimit() const override;

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
    // M78 (D-087): `kept` replaces the rules kept as Excel wrote them in the
    // same step (Clear Rules from Entire Sheet).
    void setCondFormats(const QList<CalcEng::CondFormatRule> &rules,
                        const std::optional<QList<CalcEng::KeptRule>> &kept = std::nullopt);
    void setValidations(const QList<CalcEng::ValidationRule> &rules);
    void setAutoFilter(const CalcEng::AutoFilter &filter);
    void setComment(const CalcEng::CellAddr &addr, const CalcEng::CellComment &comment);
    // Freeze panes and sheet visibility (M37): document state (it
    // round-trips to xlsx), so the document owns it — one undo step each.
    void setFreeze(qint32 rows, qint32 cols);
    void setSheetHidden(qint32 index, bool hidden);
    // Chart insert is a document mutation like any other: snapshot, push,
    // dirty, notify. It used to append straight into Workbook::charts, so a
    // chart could not be undone and never reached the recovery journal.
    void addChart(const CalcEng::ChartSpec &spec);
    // Inserts a picture anchored at (row, col) as one undo step. `path` is
    // read here so the document owns the bytes; false when the file cannot
    // be read or holds no image this build can decode.
    Q_INVOKABLE bool addImageFromFile(const QString &path, int row, int col);
    // Defines (or redefines) a named range over `target`, undoably. An
    // unqualified target is pinned to the active sheet first: a name is an
    // absolute address, so it must not follow whichever sheet the formula
    // using it happens to live on.
    void defineName(const QString &name, const CalcEng::RangeRef &target);
    void setActiveSheet(qint32 index)
    {
        m_activeSheet = index;
        m_workbook.setActiveTab(index);
    }
    [[nodiscard]] qint32 activeSheet() const { return m_activeSheet; }

    // ---- M66 (D-075): calc_feature_ops.cpp. One undo step each. ----
    // A hyperlink over `range` (an existing one there is replaced). A link
    // made over an empty cell writes its display text into it, as Excel's
    // Insert ▸ Link does.
    void setHyperlink(const CalcEng::CellRange &range, const CalcEng::Hyperlink &link);
    void removeHyperlinks(const CalcEng::CellRange &range);
    // Data ▸ Group / Ungroup over [first, last] of an axis.
    void groupLines(CalcEng::OutlineAxis axis, qint32 first, qint32 last, bool group);
    // Show Detail / Hide Detail of the group at `index` (a detail line or
    // the group's summary line); false when there is no group there.
    bool setDetailShown(CalcEng::OutlineAxis axis, qint32 index, bool shown);
    // The same for one known group (the outline gutter's +/- buttons).
    void setGroupShown(CalcEng::OutlineAxis axis, const CalcEng::OutlineGroup &group, bool shown);
    // The outline's level buttons (1 shows only what no group holds).
    void showOutlineLevel(CalcEng::OutlineAxis axis, int level);
    // Format ▸ Hide / Unhide rows or columns.
    void setLinesHidden(CalcEng::OutlineAxis axis, qint32 first, qint32 last, bool hidden);
    // Review ▸ Protect Sheet: `options` says what stays allowed; an empty
    // password protects without one (Excel's hash otherwise, D-075).
    void protectSheet(const CalcEng::SheetProtection &options, const QString &password);
    // False, with editRefused, when the password does not match.
    bool unprotectSheet(const QString &password);
    // Review ▸ Protect Workbook: locks the sheet structure.
    void protectWorkbook(const QString &password);
    bool unprotectWorkbook(const QString &password);
    // The guards every edit path asks: true — and editRefused emitted —
    // when the active sheet's protection (or the workbook's structure
    // lock) forbids the change.
    bool refuseLockedCells(const CalcEng::CellRange &range);
    bool refuseOnProtectedSheet(bool actionLocked, const QString &action);
    bool refuseStructureChange();
    // ---- M69 (D-078): calc_table_ops.cpp. One undo step each. ----
    // The guard every cell-content edit asks first: a chart sheet has no
    // cells, part of a legacy array formula cannot change (Excel's rule),
    // and locked cells of a protected sheet refuse (refuseLockedCells).
    bool refuseCellEdit(const CalcEng::CellRange &range);
    // Inserting or deleting rows (columns) through a legacy array block is
    // refused, as in Excel.
    bool refuseArraySplit(bool rows, qint32 at, qint32 count, bool insert);
    // Ctrl+Shift+Enter: `input` ("=…", or "{=…}" as the formula bar shows
    // one) as a legacy array formula over `block`. Refused (editRefused)
    // over part of another array, merged cells or a table, or when the
    // formula does not parse.
    void applyArrayFormula(const CalcEng::CellRange &block, const QString &input);
    // Insert ▸ Table over `range`: false, with editRefused, when it cannot
    // be one (calc_engine's createTable says why).
    bool insertTable(const CalcEng::CellRange &range);
    // The Total Row of the table at `addr` on or off.
    bool setTotalsRow(const CalcEng::CellAddr &addr, bool on);
    // ---- M74 (D-083): PivotTables, calc_pivot_ops.cpp. One undo step each;
    // false (with editRefused saying why) when nothing changed. ----
    // Insert ▸ PivotTable: a pivot over `source`, on a new sheet before the
    // active one (at A3, Excel's place) or at `at` on `sheet`.
    CalcEng::PivotRef insertPivotTable(const CalcEng::PivotSource &source, bool newSheet, qint32 sheet,
                                       const CalcEng::CellAddr &at);
    // Any change to a pivot's definition — the field list, Field and Value
    // Field Settings, layout, totals, style, sort and filters — laid out at
    // once. `replace` answers Excel's "There's already data … replace it?".
    using PivotChange = std::function<void(CalcEng::PivotTable &, const CalcEng::PivotCache &)>;
    bool changePivot(const CalcEng::PivotRef &ref, const PivotChange &change, const QString &undoText,
                     bool replace = false);
    bool refreshPivot(const CalcEng::PivotRef &ref);
    bool refreshAllPivots();
    bool clearPivot(const CalcEng::PivotRef &ref);
    bool movePivot(const CalcEng::PivotRef &ref, qint32 sheet, const CalcEng::CellAddr &at);
    bool changePivotSource(const CalcEng::PivotRef &ref, const CalcEng::PivotSource &source);
    bool groupPivot(const CalcEng::PivotRef &ref, qint32 field, const QList<CalcEng::PivotGroupBy> &parts,
                    double start, double end, double interval);
    bool groupPivotItems(const CalcEng::PivotRef &ref, qint32 field, const QList<qint32> &items);
    bool ungroupPivot(const CalcEng::PivotRef &ref, qint32 field);
    // Show Details: a new sheet before the active one listing, as a table,
    // the records behind the cell at `addr` of the pivot.
    bool showPivotDetails(const CalcEng::PivotRef &ref, const CalcEng::CellAddr &addr);
    // The guards: a write into a pivot's output, a structural edit through
    // one (Excel's refusals).
    bool refusePivotEdit(const CalcEng::CellRange &range);
    bool refusePivotSplit(bool rows, qint32 at, qint32 count, bool insert);
    // ---- M78 (D-087): calc_m78_ops.cpp. One undo step each; false (with
    // editRefused saying why) when nothing changed. ----
    // Page Setup, the print area and the print titles of `sheet`.
    void setPrintSettings(qint32 sheet, const CalcEng::Sheet::PageSetup &setup,
                          const std::optional<CalcEng::CellRange> &area,
                          const std::optional<CalcEng::CellRange> &titleRows,
                          const std::optional<CalcEng::CellRange> &titleCols, const QString &undoText);
    // The active sheet's scenarios, replaced wholesale (add, edit, delete).
    void setScenarios(const CalcEng::ScenarioSet &set, const QString &undoText);
    bool showScenario(qint32 index);
    // Scenario Summary: a new sheet before the active one (Excel's place)
    // with the report for `results`; its index, or -1.
    qint32 addScenarioSummary(const QList<CalcEng::CellRange> &results);
    // Data ▸ What-If Analysis ▸ Data Table over the active sheet.
    bool addDataTable(const CalcEng::DataTableSpec &spec);
    // Goal Seek runs on the live workbook (its status dialog shows the
    // result in the grid); OK makes it one undo step from `before`, Cancel
    // puts `before` back without one.
    [[nodiscard]] std::shared_ptr<WorkbookSnapshot> snapshotNow() const;
    void commitSnapshotStep(const WorkbookSnapshot &before, const QString &undoText);
    void restoreWithoutUndo(const WorkbookSnapshot &before);
    // Text to Columns: the converted cells (an empty input clears).
    bool applyTextColumns(const QList<CalcEng::TextColumnCell> &cells, const CalcEng::CellRange &block);
    // Remove Duplicates over `range` (calc_engine's data_tools.h).
    std::optional<CalcEng::RemoveDuplicatesResult> removeDuplicateRows(const CalcEng::CellRange &range,
                                                                       const QList<qint32> &columns, bool header);
    // A block of values at `at` (Consolidate's result).
    bool writeValues(const CalcEng::CellAddr &at, const QList<QList<CalcEng::Value>> &grid, const QString &undoText);
    // Takes back the last undo step when it is still `undoText` (a
    // validation Warning answered No).
    bool undoIfTop(const QString &undoText);
    // What the last opened file holds that a save cannot keep (calc_io's
    // open warnings), for the window to show once.
    [[nodiscard]] QStringList openWarnings() const { return m_openWarnings; }

    // Undo seam: the command object restores a snapshot through here so
    // dirty/content/structure notifications stay uniform.
    void applySnapshot(const WorkbookSnapshot &snapshot);
    // The half of applySnapshot that is not the rebuild: used when the
    // workbook already holds the snapshot's state (see WorkbookUndoCommand).
    void noteSnapshotApplied(const WorkbookSnapshot &snapshot);

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
    // M78 (D-087): a value a Warning or Information rule questions. It is
    // in the cell (one undo step, "Set cell"); the window asks, and taking
    // it back is undoIfTop.
    void inputQuestioned(const CalcEng::CellAddr &addr, const QString &title, const QString &message,
                         bool warning);
    // Sheet list/count changed (undo of sheet insert, sheet insert);
    // windows rebuild their tab bars on this.
    void structureChanged();
    // A save that completed but could not carry everything (CSV holds one
    // sheet). The window surfaces it; silence here would be data loss the
    // user never heard about.
    void saveWarning(const QString &message);
    // An edit protection forbids (a locked cell, a locked structure) or a
    // wrong password. The window says why; nothing changed.
    void editRefused(const QString &message);

private:
    void pushUndo(const WorkbookSnapshot &before, const QString &text);
    // A new sheet before the active one ("Sheet<N>"), active; its index.
    qint32 insertSheetBeforeActive();
    // The common tail of the pivot edits: an undo step, or a refusal.
    bool finishPivotEdit(const WorkbookSnapshot &before, bool done, const QString &undoText, const QString &error);
    // restyleRange's work without its undo step (a link's look joins the
    // link's step).
    void restyleCells(const CalcEng::CellRange &range, const StylePatch &patch);
    // M78: the value typed input would give at `addr` (a formula's result).
    [[nodiscard]] CalcEng::Value inputValueAt(const CalcEng::CellAddr &addr, const QString &input) const;

    CalcEng::Workbook m_workbook;
    // Staging area for the worker-thread load (loadStaged writes it off the
    // GUI thread, adoptStagedLoad moves it in on the GUI thread; the host
    // never starts a second load while one is staged).
    CalcEng::Workbook m_staged;
    QStringList m_stagedWarnings;
    QStringList m_openWarnings;
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
    // Cell protection (M66): in force once the sheet is protected.
    std::optional<bool> locked;
    std::optional<bool> hiddenFormula;
};

} // namespace QindaOffice::Apps::QindaCalc
