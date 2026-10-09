// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaCalc window's host (conversion plan §5.2): the workbook
// document, the sheet list, the formula bar and name box, the Insert /
// Format / Data / Sheet command sets, find and replace, the print
// paginator and the assistant. The QML window (qml/CalcWindow.qml)
// renders the grid through a SpreadsheetItem the host is handed at
// start-up (attachGrid). Every dialog is a request signal the window
// answers through an invokable; the host holds no widgets.
//
// Files: calc_host.cpp (document, sheets, grid, status, name box),
// calc_host_structure.cpp (rows/columns/merge/borders/sort/fill/AutoSum/
// freeze/sheets + the menus), calc_host_rules.cpp (conditional formatting,
// validation, autofilter, comments), calc_host_format.cpp (the cell format
// commands and the strip's combos), calc_host_text.cpp (find/replace,
// templates, print, charts), calc_host_ai.cpp (the assistant),
// calc_host_tables.cpp (Excel tables and the chart sheet view, M69).

#include "assistant_controller.h"
#include "document_host.h"
#include "spreadsheet_document.h"
#include "spreadsheet_item.h"

#include <QPageLayout>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <optional>

namespace QindaOffice::Ai {
class ModelCatalog;
}

namespace QindaOffice::Apps::QindaCalc {

class CalcHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(QStringList sheetNames READ sheetNames NOTIFY sheetsChanged)
    Q_PROPERTY(int activeSheet READ activeSheet WRITE setActiveSheet NOTIFY sheetsChanged)
    Q_PROPERTY(QString cursorText READ cursorText NOTIFY cursorChanged)
    Q_PROPERTY(QString formulaText READ formulaText NOTIFY cursorChanged)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(QStringList fontSizes READ fontSizes CONSTANT)
    Q_PROPERTY(QVariantList numberFormats READ numberFormats CONSTANT)
    Q_PROPERTY(QString cellFontFamily READ cellFontFamily NOTIFY cursorChanged)
    Q_PROPERTY(QString cellFontSize READ cellFontSize NOTIFY cursorChanged)
    Q_PROPERTY(int cellNumberFormatIndex READ cellNumberFormatIndex NOTIFY cursorChanged)
    Q_PROPERTY(bool chartsAvailable READ chartsAvailable CONSTANT)
    // [{index, title, kind, anchorRow, anchorCol, widthPx, heightPx}] —
    // the floating charts; a chart sheet's chart is not among them (M69).
    Q_PROPERTY(QVariantList charts READ charts NOTIFY chartsChanged)
    // M69 (D-078): the active sheet is a chart sheet, and the chart it
    // shows in the grid's place (chartSeriesData's map).
    Q_PROPERTY(bool chartSheetActive READ chartSheetActive NOTIFY chartSheetChanged)
    Q_PROPERTY(QVariantMap chartSheetData READ chartSheetData NOTIFY chartSheetChanged)
    Q_PROPERTY(QindaOffice::Ai::AssistantController *assistant READ assistant CONSTANT)
    // M74 (D-083): the cursor is in a PivotTable QindaCalc can change, and
    // the PivotTable Fields pane's model (calc_host_pivot_pane.cpp).
    Q_PROPERTY(bool pivotActive READ pivotActive NOTIFY pivotChanged)
    Q_PROPERTY(QVariantMap pivotPane READ pivotPane NOTIFY pivotChanged)
    // M78 (D-087): the Page Layout tab's paper sizes (calcPaperSize_<code>
    // ids, Excel's order) and Scale to Fit's state {fitToPage, fitToWidth,
    // fitToHeight, scale} for the ribbon's slots.
    Q_PROPERTY(QStringList paperSizeIds READ paperSizeIds CONSTANT)
    Q_PROPERTY(QVariantMap scaling READ scaling NOTIFY pageLayoutChanged)
public:
    CalcHost();
    ~CalcHost() override;

    [[nodiscard]] SpreadsheetDocument *spreadsheetDocument() const { return m_document; }
    [[nodiscard]] SpreadsheetItem *grid() const { return m_grid; }

    // The QML grid item, once (CalcGrid.qml's Component.onCompleted).
    Q_INVOKABLE void attachGrid(SpreadsheetItem *grid);
    // M82: View ▸ New Window's grid (CalcViewWindows.qml) — view only, on
    // the same document, repainted with every change; and the sheets it
    // may show (the visible worksheets, by name).
    Q_INVOKABLE void attachViewGrid(SpreadsheetItem *grid);
    Q_INVOKABLE QStringList viewableSheets() const;

    // ---- sheets ----
    // The tab strip is VISIBLE sheets only (hidden sheets have no tab,
    // Excel's model); every public index here is the strip's view index,
    // mapped to the engine's sheet index inside the host.
    [[nodiscard]] QStringList sheetNames() const;
    [[nodiscard]] int activeSheet() const;
    void setActiveSheet(int index);
    Q_INVOKABLE void moveSheet(int from, int to);
    Q_INVOKABLE void renameSheet(int index, const QString &name);
    Q_INVOKABLE void deleteSheetConfirmed(int index);
    // The sheet-tab context menu acts on the tab under the pointer.
    Q_INVOKABLE void activateSheetForMenu(int index);
    // Hide/Unhide Sheet (engine index space, called from the sheet menu
    // after activateSheetForMenu pinned the target).
    void setSheetHidden(bool hidden);
    Q_INVOKABLE void unhideSheets(const QString &name);

    // ---- formula bar and name box ----
    [[nodiscard]] QString cursorText() const;
    [[nodiscard]] QString formulaText() const;
    Q_INVOKABLE void commitFormulaBar(const QString &text);
    Q_INVOKABLE void commitNameBox(const QString &text);

    // ---- format strip (calc_host_format.cpp) ----
    [[nodiscard]] QStringList fontFamilies() const;
    [[nodiscard]] QStringList fontSizes() const;
    [[nodiscard]] QVariantList numberFormats() const;
    [[nodiscard]] QString cellFontFamily() const;
    [[nodiscard]] QString cellFontSize() const;
    [[nodiscard]] int cellNumberFormatIndex() const;
    Q_INVOKABLE void applyFontFamily(const QString &family);
    Q_INVOKABLE void applyFontSize(const QString &size);
    Q_INVOKABLE void applyNumberFormat(int index);
    Q_INVOKABLE void applyTextColor(const QColor &color);
    Q_INVOKABLE void applyFillColor(const QColor &color);

    // ---- structure dialog answers (calc_host_structure.cpp) ----
    Q_INVOKABLE void applyRowHeight(qreal px);
    Q_INVOKABLE void applyColumnWidth(qreal px);

    // ---- rule dialog answers (calc_host_rules.cpp) ----
    // Rules as maps: {range:"A1:B9", op:int, first, second, text,
    // background:"#rrggbb"|"", foreground, bold, italic}.
    Q_INVOKABLE QVariantList condFormatRules() const;
    Q_INVOKABLE void setCondFormatRules(const QVariantList &rules);
    Q_INVOKABLE QString selectionRangeText() const;
    // {kind:int(-1 = none), min, max, list:"a, b", message}
    Q_INVOKABLE QVariantMap validationAtCursor() const;
    Q_INVOKABLE void setValidationRule(const QVariantMap &rule);
    Q_INVOKABLE QString commentAtCursor() const;
    Q_INVOKABLE void setCommentText(const QString &text);
    Q_INVOKABLE void applyFilterValues(int column, const QStringList &kept);

    // ---- text (calc_host_text.cpp) ----
    Q_INVOKABLE bool findNext(const QString &needle, bool caseSensitive);
    Q_INVOKABLE bool findPrevious(const QString &needle, bool caseSensitive);
    Q_INVOKABLE void replaceCurrent(const QString &needle, const QString &replacement, bool caseSensitive);
    Q_INVOKABLE int replaceAll(const QString &needle, const QString &replacement, bool caseSensitive);
    // The picture picker's answer: anchored at the grid cursor, one undo
    // step. Takes the QML file URL, which is not a path until it is.
    Q_INVOKABLE void insertImageChosen(const QUrl &file);
    Q_INVOKABLE void loadTemplateResolved(const QString &path, const QString &choice);
    // ---- charts ----
    [[nodiscard]] bool chartsAvailable() const;
    [[nodiscard]] QVariantList charts() const;
    // {kind:int, title, rangeText, headerH, headerV} → the document.
    Q_INVOKABLE void insertChartSpec(const QVariantMap &spec);
    // Series data for a chart preview / window (chart_series.h).
    Q_INVOKABLE QVariantMap chartSeries(const QVariantMap &spec) const;
    Q_INVOKABLE QVariantMap chartSeriesAt(int index) const;
    // ---- tables and chart sheets (calc_host_tables.cpp, M69) ----
    [[nodiscard]] bool chartSheetActive() const;
    [[nodiscard]] QVariantMap chartSheetData() const;

    [[nodiscard]] Ai::AssistantController *assistant() const { return m_assistant; }

    // ---- links, outline, protection (calc_host_features.cpp, M66) ----
    // The Link dialog's state for the cursor: {address, location, display,
    // tooltip, hasLink, sheets: [visible sheet names]}.
    Q_INVOKABLE QVariantMap linkAtCursor() const;
    // {address, location, display, tooltip}: a link over the selection.
    Q_INVOKABLE void applyLink(const QVariantMap &link);
    // Follows the link at a cell — its hyperlink, or a HYPERLINK() formula's
    // — inside the workbook (the cursor moves there) or out of it (through
    // the URL opener). False when there is nothing to follow.
    Q_INVOKABLE bool openLinkAt(int row, int col);
    // {password, formatCells, …: true = allowed}: Review ▸ Protect Sheet.
    Q_INVOKABLE void protectSheetWith(const QVariantMap &options);
    Q_INVOKABLE bool unprotectSheetWith(const QString &password);
    Q_INVOKABLE void protectWorkbookWith(const QString &password);
    Q_INVOKABLE bool unprotectWorkbookWith(const QString &password);
    // The outline gutter: a level button, and the +/- button of the group
    // at `level` whose button sits on `line` (its summary line, or its
    // first line when it has none).
    Q_INVOKABLE void showOutlineLevel(bool rows, int level);
    Q_INVOKABLE void toggleOutlineGroup(bool rows, int line, int level);
    // ---- PivotTables (calc_host_pivot*.cpp, M74) ----
    [[nodiscard]] bool pivotActive() const;
    // {active, readOnly, note, name, fields: [{index, name, checked, kind}],
    //  filters / columns / rows / values: [{position, field, caption}]}.
    [[nodiscard]] QVariantMap pivotPane() const;
    // The pane: the checkbox, a field dragged onto an area, an entry dragged
    // within or between areas, an entry taken off. Areas: "filters",
    // "columns", "rows", "values".
    Q_INVOKABLE void pivotToggleField(int field, bool on);
    Q_INVOKABLE void pivotAddField(int field, const QString &area, int position);
    Q_INVOKABLE void pivotMoveField(const QString &fromArea, int fromPosition, const QString &toArea, int toPosition);
    Q_INVOKABLE void pivotRemoveField(const QString &area, int position);
    // Insert ▸ PivotTable, Change Data Source, Move PivotTable:
    // {source, location, newSheet} in, the same out.
    Q_INVOKABLE QVariantMap pivotCreateState() const;
    Q_INVOKABLE bool insertPivotTableFrom(const QVariantMap &choice);
    Q_INVOKABLE bool changePivotSourceTo(const QString &source);
    Q_INVOKABLE bool movePivotTo(const QString &location);
    // The dialogs' states and answers (calc_host_pivot_fields.cpp).
    Q_INVOKABLE QVariantMap pivotValueFieldState(int dataIndex) const;
    Q_INVOKABLE QStringList pivotBaseItems(int field) const;
    Q_INVOKABLE void applyValueFieldSettings(int dataIndex, const QVariantMap &settings);
    Q_INVOKABLE QVariantMap pivotFieldState(int field) const;
    Q_INVOKABLE void applyFieldSettings(int field, const QVariantMap &settings);
    Q_INVOKABLE QVariantMap pivotFilterState(int field) const;
    Q_INVOKABLE void applyPivotFilter(int field, const QVariantMap &filter);
    Q_INVOKABLE QVariantMap pivotGroupState(int field) const;
    Q_INVOKABLE void applyPivotGroup(int field, const QVariantMap &group);
    Q_INVOKABLE QVariantMap pivotOptionsState() const;
    Q_INVOKABLE void applyPivotOptions(const QVariantMap &options);
    // ---- M78 (D-087): Page Layout (calc_host_pagelayout.cpp) ----
    // Page Setup's state for the active sheet: {orientation, paperSize,
    // papers:[{code,name}], fitToPage, scale, fitToWidth, fitToHeight,
    // firstPageNumber ("" = Auto), margins {left,right,top,bottom,header,
    // footer} in inches, centerHorizontally, centerVertically, header /
    // footer parts {oddHeader:{left,center,right}, …}, differentOddEven,
    // differentFirst, scaleWithDoc, alignWithMargins, presets:[{code,label}],
    // printArea, titleRows ("$1:$2"), titleCols ("$A:$B"), gridLines,
    // headings, blackAndWhite, draft, overThenDown, cellComments, errors}.
    [[nodiscard]] QStringList paperSizeIds() const;
    [[nodiscard]] QVariantMap scaling() const;
    Q_INVOKABLE QVariantMap pageSetupState() const;
    // The dialog's answer: one undo step. Returns "" or what is wrong
    // (Excel's messages for a print area or titles it cannot read).
    Q_INVOKABLE QString applyPageSetup(const QVariantMap &state);
    // The ribbon's Scale to Fit slots: pages wide / tall (0 = Automatic)
    // and the scale percent (Adjust to).
    Q_INVOKABLE void setFitTo(int width, int height);
    Q_INVOKABLE void setScalePercent(int percent);
    // The Header & Footer preview text of a code for the dialog (fields
    // filled in for page 1).
    Q_INVOKABLE QString headerFooterPreview(const QString &left, const QString &center, const QString &right) const;
    // ---- M78: What-If Analysis (calc_host_whatif.cpp) ----
    // Goal Seek: {setCell, changingCell} prefilled from the cursor.
    Q_INVOKABLE QVariantMap goalSeekState() const;
    // Runs it and keeps the workbook as Goal Seek leaves it until
    // finishGoalSeek: {ok, error, found, setCell, target, current, iterations}.
    Q_INVOKABLE QVariantMap runGoalSeek(const QString &setCell, double target, const QString &changingCell);
    // Goal Seek Status: OK keeps the value (one undo step), Cancel restores.
    Q_INVOKABLE void finishGoalSeek(bool keep);
    // Scenario Manager: [{name, cells, comment, hidden, locked}] of the active sheet.
    Q_INVOKABLE QVariantList scenarioList() const;
    // {name, cells: "B1,B2", values: [..], comment, locked, hidden} of one
    // scenario (index -1: a new one over the selection, with its values).
    Q_INVOKABLE QVariantMap scenarioState(int index) const;
    // The current values of `cells` ("B1,B2" or "B1:B2").
    Q_INVOKABLE QStringList scenarioCellValues(const QString &cells) const;
    // Add (index -1) or edit: "" or Excel's message; one undo step.
    Q_INVOKABLE QString saveScenario(int index, const QVariantMap &scenario);
    Q_INVOKABLE void deleteScenario(int index);
    Q_INVOKABLE QString showScenarioAt(int index);
    // Summary: a new "Scenario Summary" sheet for `resultCells` ("B5,B6").
    Q_INVOKABLE QString scenarioSummaryFor(const QString &resultCells);
    // Data Table over the selection with the input cells ("" = none).
    Q_INVOKABLE QString createDataTable(const QString &rowInput, const QString &colInput);
    // ---- M78: Data Tools (calc_host_datatools.cpp) ----
    // Text to Columns: {lines (the selected column's texts, first 200),
    // destination, breaks (guessed fixed widths)}; the preview of options;
    // the answer ("" done, "confirm" = destination not empty and
    // `overwrite` false, else what is wrong).
    Q_INVOKABLE QVariantMap textToColumnsState() const;
    Q_INVOKABLE QVariantList textToColumnsPreview(const QVariantMap &options) const;
    Q_INVOKABLE QString applyTextToColumns(const QVariantMap &options, bool overwrite);
    // Remove Duplicates: {range, header, columns: [{index, name}]}; the answer
    // is Excel's message ("2 duplicate values found and removed; 5 unique values remain.").
    Q_INVOKABLE QVariantMap removeDuplicatesState(bool header) const;
    Q_INVOKABLE QString applyRemoveDuplicates(const QVariantList &columns, bool header);
    // Consolidate: {function, references: [], topRow, leftColumn, functions};
    // apply puts the block at the cursor ("" or what is wrong).
    Q_INVOKABLE QVariantMap consolidateState() const;
    Q_INVOKABLE QString applyConsolidate(const QVariantMap &options);
    // ---- M78: conditional formats (calc_host_condformat.cpp) ----
    // One rule as the New / Edit Formatting Rule dialog edits it (index -1:
    // a new rule over the selection); the answer: "" or what is wrong.
    Q_INVOKABLE QVariantMap condFormatRuleState(int index) const;
    Q_INVOKABLE QString saveCondFormatRule(int index, const QVariantMap &rule);
    // The rules Manage Rules lists that QindaCalc keeps as written.
    Q_INVOKABLE QVariantList keptRuleList() const;
    // The validation dialog's answer when the input is refused or
    // questioned (Warning: Yes keeps it; Information: OK keeps it).
    Q_INVOKABLE void answerQuestionedInput(bool keep);
    // ---- M82 (D-091): View ▸ Custom Views (calc_host_views.cpp) ----
    // The workbook's views [{name, guid}], in order; Add (the workbook and
    // this window as they are, as view `name` — one of that name is
    // replaced) and Show answer "" or what is wrong.
    Q_INVOKABLE QVariantList customViewList() const;
    Q_INVOKABLE QString addCustomView(const QString &name, bool printSettings, bool hiddenRowsCols);
    Q_INVOKABLE QString showCustomView(const QString &guid);
    Q_INVOKABLE void deleteCustomView(const QString &guid);
    // How a link leaves the app (default QDesktopServices::openUrl, which
    // goes through the desktop portal when sandboxed). Tests record here.
    void setUrlOpener(std::function<bool(const QUrl &)> opener) { m_openUrl = std::move(opener); }

signals:
    void sheetsChanged();
    void cursorChanged();
    void chartsChanged();
    void chartSheetChanged();
    // Dialogs the window answers (see the invokables above).
    void rowHeightRequested(qreal current);
    void columnWidthRequested(qreal current);
    void renameSheetRequested(int index, const QString &name);
    void deleteSheetRequested(int index, const QString &name);
    void unhideSheetRequested(const QStringList &hiddenNames);
    void condFormatRequested();
    void validationRequested();
    void commentRequested(const QString &cell, const QString &text);
    void filterMenuRequested(int column, const QStringList &values, const QStringList &allowed,
                             qreal x, qreal y);
    void contextMenuRequested(qreal x, qreal y);
    void chartDialogRequested(const QString &initialRange);
    void imageDialogRequested();
    void templateConsentRequested(const QString &path);
    void findRequested(bool replace);
    // M66: the Link dialog, and the protection dialogs (`needsPassword`
    // false: unprotect at once, nothing to ask).
    void linkDialogRequested();
    void protectSheetRequested();
    void protectWorkbookRequested();
    void unprotectRequested(bool workbook);
    void assistantToggleRequested(bool visible);
    // M74: the PivotTable dialogs ("create", "source", "move") and the pane.
    void pivotChanged();
    void pivotCreateRequested(const QString &mode);
    void pivotValueFieldRequested(int dataIndex);
    void pivotFieldRequested(int field);
    void pivotFilterRequested(int field);
    void pivotGroupRequested(int field);
    void pivotOptionsRequested();
    void pivotFieldListToggleRequested(bool visible);
    // M78 (D-087): Page Setup ("page", "margins", "header", "sheet"), the
    // what-if and data-tool dialogs, a conditional-format rule (-1 new),
    // and a validation Warning / Information question after an input.
    void pageSetupRequested(const QString &tab);
    void goalSeekRequested();
    void scenarioManagerRequested();
    void dataTableRequested(const QString &selection);
    void textToColumnsRequested();
    void removeDuplicatesRequested();
    void consolidateRequested();
    void condFormatRuleRequested(int index);
    void inputQuestioned(const QString &title, const QString &message, bool warning);
    void pageLayoutChanged();
    void customViewsRequested(); // M82
    void viewWindowRequested();  // M82: View ▸ New Window

protected:
    [[nodiscard]] QString filterString() const override;
    [[nodiscard]] QString saveFilterString() const override;
    void applyPreferences() override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;
    // M78 (D-087): the sheet's paper and orientation (calc_print.cpp).
    [[nodiscard]] bool printPrefersLandscape() const override;
    [[nodiscard]] std::optional<QPageLayout> printPageLayout() const override;

private:
    // calc_host.cpp
    void wireEditCommands();
    void wireGrid();
    // View (tab-strip) index <-> engine sheet index. -1 when out of range.
    [[nodiscard]] qint32 engineSheetIndex(qint32 viewIndex) const;
    [[nodiscard]] qint32 viewSheetIndex(qint32 engineIndex) const;
    void updateSelectionStatus();
    void syncFromCursor();
    void goToRange(const CalcEng::RangeRef &ref);
    void commitInput(const CalcEng::CellAddr &addr, const QString &input);
    [[nodiscard]] CalcEng::CellRange selection() const;
    [[nodiscard]] CalcEng::CellAddr cursor() const;
    [[nodiscard]] const CalcEng::Sheet &sheet() const;
    void refreshGrid();
    // calc_host_structure.cpp
    void createStructureActions();
    void insertMenus();
    void syncStructureFromCursor();
    void applyBorders(int bits, bool outlineOnly);
    void sortSelection(bool ascending);
    void autoSum();
    void toggleFreeze(bool on);
    [[nodiscard]] CalcEng::CellRange blockAroundSelection() const;
    // calc_host_rules.cpp
    void createRuleActions();
    void syncRulesFromCursor();
    void toggleFilter(bool on);
    // calc_host_format.cpp
    void createFormatActions();
    void syncFormatFromCursor();
    void applyPatch(const StylePatch &patch, const QString &undoText);
    // calc_host_text.cpp
    void createTextActions();
    [[nodiscard]] QList<CalcEng::CellAddr> matches(const QString &needle, bool caseSensitive) const;
    void loadTemplate(const QString &path);
    // calc_host_ai.cpp
    void createAssistant();
    // calc_host_features.cpp
    void createFeatureActions();
    void insertFeatureMenus();
    void syncFeaturesFromCursor();
    // calc_host_tables.cpp (M69)
    void createTableActions();
    void insertTableMenus();
    void syncTablesFromCursor();
    // True when `input` was an array formula ("{=…}") and has been entered.
    bool commitArrayInput(const CalcEng::CellAddr &addr, const QString &input);
    [[nodiscard]] CalcEng::OutlineAxis groupAxis() const;
    // calc_host_pivot*.cpp (M74)
    void createPivotActions();
    void insertPivotMenus();
    void syncPivotFromCursor();
    void wirePivotGrid();
    [[nodiscard]] CalcEng::PivotRef currentPivot() const;
    [[nodiscard]] qint32 pivotFieldAtCursor() const;
    void changeCurrentPivot(const SpreadsheetDocument::PivotChange &change, const QString &undoText);
    void sortPivot(bool ascending);
    void groupAtCursor();
    [[nodiscard]] std::optional<CalcEng::PivotSource> pivotSourceFrom(const QString &text) const;
    [[nodiscard]] std::optional<QPair<qint32, CalcEng::CellAddr>> pivotPlaceFrom(const QString &text) const;
    // calc_host_pagelayout.cpp (M78)
    void createPageLayoutActions();
    void insertPageLayoutMenus();
    void syncPageLayoutFromCursor();
    void changePageSetup(const std::function<void(CalcEng::Sheet::PageSetup &)> &change, const QString &undoText);
    void refreshPageBreakView();
    // calc_host_whatif.cpp / calc_host_datatools.cpp / calc_host_condformat.cpp (M78)
    void createWhatIfActions();
    void createDataToolActions();
    void createCondFormatActions();
    void insertDataToolMenus();
    void syncDataToolsFromCursor();
    void flashFillAtCursor();
    void refreshInvalidCircles();
    void quickCondFormat(CalcEng::CondFormatRule rule, const QString &undoText);
    // calc_host_views.cpp (M82, D-091): View ▸ Split, New Window and
    // Custom Views.
    void createViewActions();
    void insertViewMenus();
    void syncViewsFromCursor();
    // A cell reference typed in a dialog ("B2", "$B$2", "Sheet1!B2") on the
    // active sheet; invalid when it is not one.
    [[nodiscard]] CalcEng::CellAddr cellFromText(const QString &text) const;
    [[nodiscard]] QList<CalcEng::CellAddr> cellsFromText(const QString &text) const;

    SpreadsheetDocument *m_document = nullptr;
    QPointer<SpreadsheetItem> m_grid;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantController *m_assistant = nullptr;
    std::unique_ptr<Ai::DocumentContextProvider> m_aiContext;
    std::unique_ptr<Ai::EditApplier> m_aiApplier;
    std::unique_ptr<Ai::DocumentTools> m_aiTools; // D-058, calc_tools.h
    QString m_pendingTemplate;
    std::function<bool(const QUrl &)> m_openUrl;
    bool m_syncing = false;
    CalcEng::PivotRef m_lastPivot;          // the pivot the cursor was in (pivotChanged)
    CalcEng::PivotRef m_readOnlyPivotNoted; // the read-only pivot whose note was shown
    // M78 (D-087): the workbook before Goal Seek changed it (Cancel puts it
    // back, OK makes the change one undo step), and Circle Invalid Data on.
    std::shared_ptr<WorkbookSnapshot> m_goalSeekBefore;
    bool m_circlesOn = false;
    QString m_condPreset; // the rule kind a gallery command opens the rule dialog on
};

} // namespace QindaOffice::Apps::QindaCalc
