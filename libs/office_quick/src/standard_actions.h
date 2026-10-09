// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>


namespace QindaOffice::Quick {

// The suite-standard action objectNames (menu-export activation identity).
//
// AGENT-CONTRACT: these strings are how the panel's dbusmenu endpoint and any
// saved keybindings address a command; they must never be renamed or reused
// for a different command (mirror: text-editor.md action table). Add new
// ids for new commands.
namespace ActionId {
inline const char kFileNew[] = "fileNewAction";
inline const char kFileOpen[] = "fileOpenAction";
inline const char kFileCloseWindow[] = "fileCloseWindowAction";
inline const char kFileSave[] = "fileSaveAction";
inline const char kFileSaveAs[] = "fileSaveAsAction";
inline const char kFilePrint[] = "filePrintAction";
inline const char kFilePrintPreview[] = "filePrintPreviewAction";
inline const char kFileQuit[] = "fileQuitAction";
inline const char kEditUndo[] = "editUndoAction";
inline const char kEditRedo[] = "editRedoAction";
inline const char kEditCut[] = "editCutAction";
inline const char kEditCopy[] = "editCopyAction";
inline const char kEditPaste[] = "editPasteAction";
inline const char kEditSelectAll[] = "editSelectAllAction";
inline const char kEditFind[] = "editFindAction";
inline const char kEditReplace[] = "editReplaceAction";
inline const char kEditFindNext[] = "editFindNextAction";
inline const char kEditFindPrevious[] = "editFindPreviousAction";
inline const char kViewZoomIn[] = "viewZoomInAction";
inline const char kViewZoomOut[] = "viewZoomOutAction";
inline const char kViewZoomReset[] = "viewZoomResetAction";
inline const char kViewShowMenuBar[] = "viewShowMenuBarAction";
inline const char kViewShowToolBars[] = "viewShowToolBarsAction";
inline const char kViewShowStatusBar[] = "viewShowStatusBarAction";
// M40 (D-041): the ribbon's one-row mode, and the command search.
inline const char kViewCompactRibbon[] = "viewCompactRibbonAction";
inline const char kHelpSearchCommands[] = "helpSearchCommandsAction";
inline const char kAiAssistant[] = "aiAssistantAction";
inline const char kAgentDisconnect[] = "agentDisconnect";
inline const char kAgentRead[] = "agentRead";
inline const char kAgentEdit[] = "agentEdit";
inline const char kSettingsPreferences[] = "settingsPreferencesAction";
inline const char kSettingsRestorePrevious[] = "settingsRestorePreviousAction";
inline const char kHelpAbout[] = "helpAboutAction";
inline const char kHelpAboutQt[] = "helpAboutQtAction";

// Rich-text formatting commands (QindaWrite; shared with any text host).
inline const char kFormatBold[] = "formatBoldAction";
inline const char kFormatItalic[] = "formatItalicAction";
inline const char kFormatUnderline[] = "formatUnderlineAction";
inline const char kFormatStrikeOut[] = "formatStrikeOutAction";
inline const char kFormatTextColor[] = "formatTextColorAction";
inline const char kCalcFillColor[] = "calcFillColorAction";
inline const char kFormatClearFormat[] = "formatClearFormatAction";
inline const char kFormatAlignLeft[] = "formatAlignLeftAction";
inline const char kFormatAlignCenter[] = "formatAlignCenterAction";
inline const char kFormatAlignRight[] = "formatAlignRightAction";
inline const char kFormatAlignJustify[] = "formatAlignJustifyAction";
inline const char kFormatListBullet[] = "formatListBulletAction";
inline const char kFormatListNumbered[] = "formatListNumberedAction";
inline const char kFormatIndentMore[] = "formatIndentMoreAction";
inline const char kFormatIndentLess[] = "formatIndentLessAction";
inline const char kFormatHighlight[] = "formatHighlightAction";
inline const char kFormatSuperscript[] = "formatSuperscriptAction";
inline const char kFormatSubscript[] = "formatSubscriptAction";
inline const char kFormatGrowFont[] = "formatGrowFontAction";
inline const char kFormatShrinkFont[] = "formatShrinkFontAction";
inline const char kFormatLineSpacingSingle[] = "formatLineSpacingSingleAction";
inline const char kFormatLineSpacingOneHalf[] = "formatLineSpacingOneHalfAction";
inline const char kFormatLineSpacingDouble[] = "formatLineSpacingDoubleAction";

// Document-building commands (QindaWrite inserts; app-specific inserts
// keep their own stable ids below).
inline const char kInsertTable[] = "insertTableAction";
inline const char kInsertImage[] = "insertImageAction";
inline const char kInsertColumns[] = "insertColumnsAction";
inline const char kInsertPageBreak[] = "insertPageBreakAction";
inline const char kInsertDate[] = "insertDateAction";
inline const char kInsertTime[] = "insertTimeAction";
inline const char kInsertSymbol[] = "insertSymbolAction";
inline const char kInsertHorizontalRule[] = "insertHorizontalRuleAction";
inline const char kInsertHyperlink[] = "insertHyperlinkAction";
inline const char kToolsWordCount[] = "toolsWordCountAction";
inline const char kInsertFootnote[] = "insertFootnoteAction";
inline const char kInsertToc[] = "insertTocAction";
inline const char kUpdateToc[] = "updateTocAction";
inline const char kReviewTrackChanges[] = "reviewTrackChangesAction";
inline const char kReviewAcceptChange[] = "reviewAcceptChangeAction";
inline const char kReviewRejectChange[] = "reviewRejectChangeAction";
inline const char kReviewAcceptAll[] = "reviewAcceptAllAction";
inline const char kReviewRejectAll[] = "reviewRejectAllAction";
inline const char kReviewNextChange[] = "reviewNextChangeAction";

// QindaWrite table commands (M14).
inline const char kTableInsertRowAbove[] = "tableInsertRowAboveAction";
inline const char kTableInsertRowBelow[] = "tableInsertRowBelowAction";
inline const char kTableInsertColumnLeft[] = "tableInsertColumnLeftAction";
inline const char kTableInsertColumnRight[] = "tableInsertColumnRightAction";
inline const char kTableDeleteRow[] = "tableDeleteRowAction";
inline const char kTableDeleteColumn[] = "tableDeleteColumnAction";
inline const char kTableMergeCells[] = "tableMergeCellsAction";
inline const char kTableSplitCell[] = "tableSplitCellAction";

// QindaCalc commands.
inline const char kInsertChart[] = "insertChartAction";
inline const char kInsertSheet[] = "insertSheetAction";
inline const char kCalcInsertRows[] = "calcInsertRowsAction";
inline const char kCalcInsertColumns[] = "calcInsertColumnsAction";
inline const char kCalcDeleteRows[] = "calcDeleteRowsAction";
inline const char kCalcDeleteColumns[] = "calcDeleteColumnsAction";
inline const char kCalcClearContents[] = "calcClearContentsAction";
inline const char kCalcRowHeight[] = "calcRowHeightAction";
inline const char kCalcColumnWidth[] = "calcColumnWidthAction";
inline const char kCalcOptimalWidth[] = "calcOptimalWidthAction";
inline const char kCalcMergeCells[] = "calcMergeCellsAction";
inline const char kCalcUnmergeCells[] = "calcUnmergeCellsAction";
inline const char kCalcWrapText[] = "calcWrapTextAction";
inline const char kCalcBordersAll[] = "calcBordersAllAction";
inline const char kCalcBordersOutline[] = "calcBordersOutlineAction";
inline const char kCalcBordersNone[] = "calcBordersNoneAction";
inline const char kCalcSortAscending[] = "calcSortAscendingAction";
inline const char kCalcSortDescending[] = "calcSortDescendingAction";
inline const char kCalcFillDown[] = "calcFillDownAction";
inline const char kCalcFillRight[] = "calcFillRightAction";
inline const char kCalcAutoSum[] = "calcAutoSumAction";
inline const char kCalcFreezePanes[] = "calcFreezePanesAction";
inline const char kCalcSheetRename[] = "calcSheetRenameAction";
inline const char kCalcSheetDelete[] = "calcSheetDeleteAction";
inline const char kCalcSheetDuplicate[] = "calcSheetDuplicateAction";
inline const char kCalcSheetMoveLeft[] = "calcSheetMoveLeftAction";
inline const char kCalcSheetMoveRight[] = "calcSheetMoveRightAction";
inline const char kCalcSheetHide[] = "calcSheetHideAction";
inline const char kCalcSheetUnhide[] = "calcSheetUnhideAction";
inline const char kCalcCondFormat[] = "calcCondFormatAction";
inline const char kCalcValidation[] = "calcValidationAction";
inline const char kCalcAutoFilter[] = "calcAutoFilterAction";
inline const char kCalcComment[] = "calcCommentAction";
inline const char kCalcDeleteComment[] = "calcDeleteCommentAction";
// M66 (D-075): links (Insert ▸ Link is the shared kInsertHyperlink),
// outlines and hidden lines, protection.
inline const char kCalcEditHyperlink[] = "calcEditHyperlinkAction";
inline const char kCalcOpenHyperlink[] = "calcOpenHyperlinkAction";
inline const char kCalcRemoveHyperlink[] = "calcRemoveHyperlinkAction";
inline const char kCalcGroup[] = "calcGroupAction";
inline const char kCalcUngroup[] = "calcUngroupAction";
inline const char kCalcShowDetail[] = "calcShowDetailAction";
inline const char kCalcHideDetail[] = "calcHideDetailAction";
inline const char kCalcHideRows[] = "calcHideRowsAction";
inline const char kCalcUnhideRows[] = "calcUnhideRowsAction";
inline const char kCalcHideColumns[] = "calcHideColumnsAction";
inline const char kCalcUnhideColumns[] = "calcUnhideColumnsAction";
inline const char kCalcProtectSheet[] = "calcProtectSheetAction";
inline const char kCalcUnprotectSheet[] = "calcUnprotectSheetAction";
inline const char kCalcProtectWorkbook[] = "calcProtectWorkbookAction";
inline const char kCalcUnprotectWorkbook[] = "calcUnprotectWorkbookAction";
inline const char kCalcLockCell[] = "calcLockCellAction";
inline const char kCalcHideFormula[] = "calcHideFormulaAction";
// M69 (D-078): Excel tables — Insert ▸ Table and the Total Row.
inline const char kCalcInsertTable[] = "calcInsertTableAction";
inline const char kCalcTableTotalRow[] = "calcTableTotalRowAction";
// M74 (D-083): PivotTables — Insert ▸ PivotTable and the contextual
// PivotTable Analyze and Design tabs (Data ▸ PivotTable in the menus). The
// styles are app-scoped "calcPivotStyle_<Excel's name>" (and _None).
inline const char kCalcInsertPivotTable[] = "calcInsertPivotTableAction";
inline const char kCalcPivotRefresh[] = "calcPivotRefreshAction";
inline const char kCalcPivotRefreshAll[] = "calcPivotRefreshAllAction";
inline const char kCalcPivotChangeSource[] = "calcPivotChangeSourceAction";
inline const char kCalcPivotClear[] = "calcPivotClearAction";
inline const char kCalcPivotMove[] = "calcPivotMoveAction";
inline const char kCalcPivotOptions[] = "calcPivotOptionsAction";
inline const char kCalcPivotFieldList[] = "calcPivotFieldListAction";
inline const char kCalcPivotFieldSettings[] = "calcPivotFieldSettingsAction";
inline const char kCalcPivotValueFieldSettings[] = "calcPivotValueFieldSettingsAction";
inline const char kCalcPivotGroup[] = "calcPivotGroupAction";
inline const char kCalcPivotUngroup[] = "calcPivotUngroupAction";
inline const char kCalcPivotSortAscending[] = "calcPivotSortAscendingAction";
inline const char kCalcPivotSortDescending[] = "calcPivotSortDescendingAction";
inline const char kCalcPivotFilter[] = "calcPivotFilterAction";
inline const char kCalcPivotClearFilter[] = "calcPivotClearFilterAction";
inline const char kCalcPivotShowDetails[] = "calcPivotShowDetailsAction";
inline const char kCalcPivotLayoutCompact[] = "calcPivotLayoutCompactAction";
inline const char kCalcPivotLayoutOutline[] = "calcPivotLayoutOutlineAction";
inline const char kCalcPivotLayoutTabular[] = "calcPivotLayoutTabularAction";
inline const char kCalcPivotRepeatLabels[] = "calcPivotRepeatLabelsAction";
inline const char kCalcPivotNoRepeatLabels[] = "calcPivotNoRepeatLabelsAction";
inline const char kCalcPivotSubtotalsOff[] = "calcPivotSubtotalsOffAction";
inline const char kCalcPivotSubtotalsBottom[] = "calcPivotSubtotalsBottomAction";
inline const char kCalcPivotSubtotalsTop[] = "calcPivotSubtotalsTopAction";
inline const char kCalcPivotGrandTotalsOff[] = "calcPivotGrandTotalsOffAction";
inline const char kCalcPivotGrandTotalsOn[] = "calcPivotGrandTotalsOnAction";
inline const char kCalcPivotGrandTotalsRows[] = "calcPivotGrandTotalsRowsAction";
inline const char kCalcPivotGrandTotalsColumns[] = "calcPivotGrandTotalsColumnsAction";
inline const char kCalcPivotBlankRowsOn[] = "calcPivotBlankRowsOnAction";
inline const char kCalcPivotBlankRowsOff[] = "calcPivotBlankRowsOffAction";
inline const char kCalcPivotRowHeaders[] = "calcPivotRowHeadersAction";
inline const char kCalcPivotColumnHeaders[] = "calcPivotColumnHeadersAction";
inline const char kCalcPivotBandedRows[] = "calcPivotBandedRowsAction";
inline const char kCalcPivotBandedColumns[] = "calcPivotBandedColumnsAction";

// QindaShow commands.
inline const char kShowFillColor[] = "showFillColorAction";
inline const char kShowStrokeColor[] = "showStrokeColorAction";
inline const char kShowInsertTextBox[] = "showInsertTextBoxAction";
inline const char kShowInsertRectangle[] = "showInsertRectangleAction";
inline const char kShowInsertEllipse[] = "showInsertEllipseAction";
inline const char kShowInsertLine[] = "showInsertLineAction";
inline const char kShowInsertArrow[] = "showInsertArrowAction";
inline const char kShowInsertImage[] = "showInsertImageAction";
inline const char kSlideNew[] = "slideNewAction";
inline const char kSlideDuplicate[] = "slideDuplicateAction";
inline const char kSlideDelete[] = "slideDeleteAction";
inline const char kSlideStartShow[] = "slideStartShowAction";
inline const char kShowBringToFront[] = "showBringToFrontAction";
inline const char kShowBringForward[] = "showBringForwardAction";
inline const char kShowSendBackward[] = "showSendBackwardAction";
inline const char kShowSendToBack[] = "showSendToBackAction";
inline const char kShowDuplicateShape[] = "showDuplicateShapeAction";
inline const char kShowDeleteShape[] = "showDeleteShapeAction";
inline const char kShowAlignLeft[] = "showAlignLeftAction";
inline const char kShowAlignHCenter[] = "showAlignHCenterAction";
inline const char kShowAlignRight[] = "showAlignRightAction";
inline const char kShowAlignTop[] = "showAlignTopAction";
inline const char kShowAlignVCenter[] = "showAlignVCenterAction";
inline const char kShowAlignBottom[] = "showAlignBottomAction";
inline const char kShowDistributeH[] = "showDistributeHorizontallyAction";
inline const char kShowDistributeV[] = "showDistributeVerticallyAction";
inline const char kShowSlideMoveUp[] = "showSlideMoveUpAction";
inline const char kShowSlideMoveDown[] = "showSlideMoveDownAction";
inline const char kShowGroup[] = "showGroupAction";
inline const char kShowUngroup[] = "showUngroupAction";
inline const char kShowSnapToGuides[] = "showSnapToGuidesAction";
inline const char kShowMasters[] = "showMastersAction";
inline const char kShowApplyMaster[] = "showApplyMasterAction";
} // namespace ActionId
} // namespace QindaOffice::Quick

// The shared command set of every document window, as ActionModel entries
// (plan docs/plans/qindatk-conversion.md §3.2). Commands a host has not
// wired yet are created DISABLED rather than omitted (D-011/D-012): the
// menus keep their stable shape and a stub is never a live button.
namespace QindaOffice::Quick {
class ActionModel;
struct MenuNode;

void addStandardActions(ActionModel *model, const QString &appDisplayName);

// File / Edit / View / AI / Settings / Help with the anchors apps insert
// against: menu ids "fileMenu", "editMenu", "viewMenu", "aiMenu",
// "settingsMenu", "helpMenu"; the File menu's app-entry anchor is the
// action id ActionId::kFileCloseWindow (insert BEFORE it, never append —
// appending is what put "New from Template" under Quit); the Open Recent
// submenu is "fileRecentMenu".
[[nodiscard]] QList<MenuNode> standardMenuTree();

// Ids of the commands a document host enables once it owns a document.
[[nodiscard]] QStringList documentCommandIds();
} // namespace QindaOffice::Quick
