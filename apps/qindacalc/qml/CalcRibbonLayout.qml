// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaCalc's ribbon (D-041): File · Home · Insert · Data · Review · Sheet ·
// View (Review since M66, D-075: notes and protection; Insert's Tables
// group since M69, D-078: Insert Table, Total Row), and since M74 (D-083)
// Insert ▸ PivotTable with the contextual PivotTable Analyze and Design
// tabs the "pivot" context shows while the cursor is in a PivotTable.
// Every string is an ActionEntry id (AGENTS.md rule 5); icons given here
// only fill in for entries whose menu rows carry none. The slots are the
// window's font, size, colour and number-format widgets.
QtObject {
    property Component fontSlot: null
    property Component sizeSlot: null
    property Component textColorSlot: null
    property Component fillColorSlot: null
    property Component numberFormatSlot: null

    readonly property var tabs: [
        StandardRibbon.fileTab([], ["calcTemplate_Budget", "calcTemplate_Invoice"], []),
        { id: "home", label: qsTr("Home"), keyTip: "H", groups: [
            StandardRibbon.historyGroup(),
            StandardRibbon.clipboardGroup(),
            { id: "font", label: qsTr("Font"), rows: [
                [{ id: "fontFamily", component: fontSlot }, { id: "fontSize", component: sizeSlot }],
                [{ action: "formatBoldAction", size: "icon" }, { action: "formatItalicAction", size: "icon" },
                 { action: "formatUnderlineAction", size: "icon" }, { action: "formatStrikeOutAction", size: "icon" },
                 { id: "borders", label: qsTr("Borders"), iconName: "grid-2x2", size: "icon",
                   menu: ["calcBordersAllAction", "calcBordersOutlineAction", "calcBordersNoneAction"] },
                 { id: "textColor", component: textColorSlot }, { id: "fillColor", component: fillColorSlot }]] },
            { id: "alignment", label: qsTr("Alignment"), rows: [
                [{ action: "formatAlignLeftAction", size: "icon" }, { action: "formatAlignCenterAction", size: "icon" },
                 { action: "formatAlignRightAction", size: "icon" }, { action: "calcWrapTextAction", size: "icon" }],
                [{ action: "calcMergeCellsAction", iconName: "table-cells-merge" },
                 { action: "calcUnmergeCellsAction", size: "icon", iconName: "table-cells-split" }]] },
            { id: "number", label: qsTr("Number"), rows: [
                [{ id: "numberFormat", component: numberFormatSlot }],
                [{ action: "calcThousandsFormatAction", label: qsTr("#,##0.00"), iconName: "hash" }]] },
            { id: "cells", label: qsTr("Cells"), items: [
                { action: "calcInsertRowsAction", label: qsTr("Insert rows") },
                { action: "calcInsertColumnsAction", label: qsTr("Insert columns") },
                { action: "calcDeleteRowsAction", label: qsTr("Delete rows") },
                { action: "calcDeleteColumnsAction", label: qsTr("Delete columns") },
                { id: "cellSize", label: qsTr("Size"), iconName: "move-horizontal",
                  menu: ["calcRowHeightAction", "calcColumnWidthAction", "calcOptimalWidthAction"] },
                { id: "cellVisibility", label: qsTr("Hide & unhide"), iconName: "eye-off",
                  menu: ["calcHideRowsAction", "calcUnhideRowsAction", "calcHideColumnsAction", "calcUnhideColumnsAction"] }] },
            { id: "editing", label: qsTr("Editing"), items: [
                { action: "calcAutoSumAction", size: "large" },
                { action: "calcFillDownAction", iconName: "arrow-down-to-line" },
                { action: "calcFillRightAction", iconName: "arrow-right" },
                { action: "calcSortAscendingAction" },
                { action: "calcSortDescendingAction" },
                { action: "editFindAction" },
                "editReplaceAction",
                { action: "editSelectAllAction", iconName: "text-select" }] }
        ] },
        { id: "insert", label: qsTr("Insert"), keyTip: "N", groups: [
            { id: "tables", label: qsTr("Tables"), items: [
                { action: "calcInsertPivotTableAction", size: "large", iconName: "table-2" },
                { action: "calcInsertTableAction", size: "large", iconName: "table" },
                { action: "calcTableTotalRowAction", iconName: "sigma" }] },
            { id: "charts", label: qsTr("Charts"), items: [{ action: "insertChartAction", size: "large" }] },
            { id: "illustrations", label: qsTr("Illustrations"), items: [
                { action: "insertImageAction", size: "large", iconName: "image" }] },
            { id: "links", label: qsTr("Links"), items: [
                { action: "insertHyperlinkAction", size: "large" },
                "calcEditHyperlinkAction", "calcOpenHyperlinkAction", "calcRemoveHyperlinkAction"] },
            { id: "comments", label: qsTr("Comments"), items: [
                { action: "calcCommentAction", size: "large" },
                { action: "calcDeleteCommentAction", iconName: "message-square-x" }] },
            { id: "sheets", label: qsTr("Sheets"), items: [{ action: "insertSheetAction", size: "large", iconName: "sheet" }] }
        ] },
        { id: "data", label: qsTr("Data"), keyTip: "D", groups: [
            { id: "sortFilter", label: qsTr("Sort & filter"), items: [
                { action: "calcAutoFilterAction", size: "large" },
                "calcSortAscendingAction", "calcSortDescendingAction"] },
            { id: "dataTools", label: qsTr("Data tools"), items: [
                { action: "calcValidationAction", size: "large", iconName: "list-checks" },
                { action: "calcCondFormatAction", size: "large" }] },
            { id: "fill", label: qsTr("Fill"), items: [
                { action: "calcFillDownAction", iconName: "arrow-down-to-line" },
                { action: "calcFillRightAction", iconName: "arrow-right" }] },
            { id: "outline", label: qsTr("Outline"), items: [
                { action: "calcGroupAction", size: "large" },
                { action: "calcUngroupAction", size: "large" },
                "calcShowDetailAction", "calcHideDetailAction"] }
        ] },
        { id: "review", label: qsTr("Review"), keyTip: "R", groups: [
            { id: "reviewNotes", label: qsTr("Notes"), items: [
                { action: "calcCommentAction", size: "large" },
                { action: "calcDeleteCommentAction", iconName: "message-square-x" }] },
            { id: "protect", label: qsTr("Protect"), items: [
                { action: "calcProtectSheetAction", size: "large" },
                { action: "calcProtectWorkbookAction", size: "large" },
                "calcUnprotectSheetAction", "calcUnprotectWorkbookAction",
                "calcLockCellAction", "calcHideFormulaAction"] }
        ] },
        { id: "sheet", label: qsTr("Sheet"), keyTip: "S", groups: [
            { id: "sheetManage", label: qsTr("Sheets"), items: [
                { action: "insertSheetAction", size: "large", iconName: "sheet" },
                { action: "calcSheetRenameAction", iconName: "pencil" },
                { action: "calcSheetDuplicateAction", iconName: "copy-plus" },
                { action: "calcSheetDeleteAction", iconName: "trash-2" }] },
            { id: "sheetOrder", label: qsTr("Order"), items: [
                { action: "calcSheetMoveLeftAction", iconName: "arrow-left" },
                { action: "calcSheetMoveRightAction", iconName: "arrow-right" }] },
            { id: "sheetVisibility", label: qsTr("Visibility"), items: [
                { action: "calcSheetHideAction", iconName: "eye-off" },
                { action: "calcSheetUnhideAction", iconName: "eye" }] }
        ] },
        StandardRibbon.viewTab([
            { id: "freeze", label: qsTr("Window"), items: [
                { action: "calcFreezePanesAction", size: "large", iconName: "snowflake" }] },
            StandardRibbon.assistantGroup([])
        ]),
        // ---- PivotTables (M74, D-083): Excel's two contextual tabs ----
        { id: "pivotAnalyze", label: qsTr("PivotTable Analyze"), keyTip: "JT", contextId: "pivot", groups: [
            { id: "pivotTable", label: qsTr("PivotTable"), items: [
                { action: "calcPivotOptionsAction", size: "large", label: qsTr("Options") }] },
            { id: "pivotActiveField", label: qsTr("Active Field"), items: [
                { action: "calcPivotFieldSettingsAction", label: qsTr("Field Settings") },
                { action: "calcPivotValueFieldSettingsAction", label: qsTr("Value Field Settings") }] },
            { id: "pivotGroup", label: qsTr("Group"), items: ["calcPivotGroupAction", "calcPivotUngroupAction"] },
            { id: "pivotSortFilter", label: qsTr("Sort & Filter"), items: [
                "calcPivotSortAscendingAction", "calcPivotSortDescendingAction",
                "calcPivotFilterAction", "calcPivotClearFilterAction"] },
            { id: "pivotData", label: qsTr("Data"), items: [
                { id: "pivotRefresh", action: "calcPivotRefreshAction", split: true, size: "large", label: qsTr("Refresh"),
                  iconName: "refresh-cw", menu: ["calcPivotRefreshAction", "calcPivotRefreshAllAction"] },
                { action: "calcPivotChangeSourceAction", size: "large", label: qsTr("Change Data Source") }] },
            { id: "pivotActions", label: qsTr("Actions"), items: [
                { action: "calcPivotClearAction", label: qsTr("Clear") },
                { action: "calcPivotMoveAction", label: qsTr("Move PivotTable") }] },
            { id: "pivotShow", label: qsTr("Show"), items: [
                { action: "calcPivotFieldListAction", size: "large" }, "calcPivotShowDetailsAction"] }
        ] },
        { id: "pivotDesign", label: qsTr("Design"), keyTip: "JY", contextId: "pivot", groups: [
            { id: "pivotLayout", label: qsTr("Layout"), items: [
                { id: "pivotSubtotals", label: qsTr("Subtotals"), iconName: "rows-3", size: "large",
                  menu: ["calcPivotSubtotalsOffAction", "calcPivotSubtotalsBottomAction", "calcPivotSubtotalsTopAction"] },
                { id: "pivotGrandTotals", label: qsTr("Grand Totals"), iconName: "sigma", size: "large",
                  menu: ["calcPivotGrandTotalsOffAction", "calcPivotGrandTotalsOnAction", "calcPivotGrandTotalsRowsAction",
                         "calcPivotGrandTotalsColumnsAction"] },
                { id: "pivotReportLayout", label: qsTr("Report Layout"), iconName: "layout-list", size: "large",
                  menu: ["calcPivotLayoutCompactAction", "calcPivotLayoutOutlineAction", "calcPivotLayoutTabularAction", "|",
                         "calcPivotRepeatLabelsAction", "calcPivotNoRepeatLabelsAction"] },
                { id: "pivotBlankRows", label: qsTr("Blank Rows"), iconName: "rows-2", size: "large",
                  menu: ["calcPivotBlankRowsOnAction", "calcPivotBlankRowsOffAction"] }] },
            { id: "pivotStyleOptions", label: qsTr("Style Options"), items: [
                "calcPivotRowHeadersAction", "calcPivotColumnHeadersAction",
                "calcPivotBandedRowsAction", "calcPivotBandedColumnsAction"] },
            // AGENT-NOTE: QindaTK has no style gallery (D-083's gap list): the
            // styles are a dropdown of the app-scoped calcPivotStyle_* ids.
            { id: "pivotStyles", label: qsTr("PivotTable Styles"), items: [
                { id: "pivotStyleGallery", label: qsTr("Styles"), iconName: "palette", size: "large",
                  menu: ["calcPivotStyle_PivotStyleLight15", "calcPivotStyle_PivotStyleLight16",
                         "calcPivotStyle_PivotStyleLight17", "calcPivotStyle_PivotStyleLight18",
                         "calcPivotStyle_PivotStyleLight19", "calcPivotStyle_PivotStyleLight20",
                         "calcPivotStyle_PivotStyleLight21", "|", "calcPivotStyle_PivotStyleMedium2",
                         "calcPivotStyle_PivotStyleMedium9", "|", "calcPivotStyle_None"] }] }
        ] }
    ]
}
