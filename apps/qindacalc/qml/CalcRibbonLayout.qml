// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Calc

// QindaCalc's ribbon (D-041): File · Home · Insert · Data · Review · Sheet ·
// View (Review since M66, D-075: notes and protection; Insert's Tables
// group since M69, D-078: Insert Table, Total Row), and since M74 (D-083)
// Insert ▸ PivotTable with the contextual PivotTable Analyze and Design
// tabs the "pivot" context shows while the cursor is in a PivotTable. M78
// (D-087): Home ▸ Styles ▸ Conditional Formatting, Insert ▸ Header &
// Footer, the Page Layout tab (Page Setup, Scale to Fit with its Width /
// Height / Scale slots, Sheet Options), Data ▸ Data Tools and Forecast
// (What-If Analysis), View ▸ Workbook Views and the Freeze Panes variants.
// Every string is an ActionEntry id (AGENTS.md rule 5); icons given here
// only fill in for entries whose menu rows carry none. The slots are the
// window's font, size, colour and number-format widgets.
QtObject {
    id: layout

    // M78 (D-087): the host the Page Layout tab's slots and paper sizes read.
    property CalcHost host: null
    property Component fontSlot: null
    property Component sizeSlot: null
    property Component textColorSlot: null
    property Component fillColorSlot: null
    property Component numberFormatSlot: null

    // Scale to Fit (M78): Width and Height in pages (Automatic = 0) and
    // the Scale percent, over the active sheet's Page Setup.
    readonly property var pageCounts: [qsTr("Automatic"), qsTr("1 page"), qsTr("2 pages"), qsTr("3 pages"), qsTr("4 pages"),
                                       qsTr("5 pages"), qsTr("6 pages"), qsTr("7 pages"), qsTr("8 pages"), qsTr("9 pages")]
    property Component scaleWidthSlot: Component {
        Tk.ComboBox {
            objectName: "scaleWidthCombo"
            small: true
            implicitWidth: 104
            tooltip: qsTr("Width: pages wide")
            model: layout.pageCounts
            currentIndex: layout.host !== null ? Math.min(9, layout.host.scaling.fitToWidth) : 0
            onActivated: function(index) { layout.host.setFitTo(index, layout.host.scaling.fitToHeight) }
        }
    }
    property Component scaleHeightSlot: Component {
        Tk.ComboBox {
            objectName: "scaleHeightCombo"
            small: true
            implicitWidth: 104
            tooltip: qsTr("Height: pages tall")
            model: layout.pageCounts
            currentIndex: layout.host !== null ? Math.min(9, layout.host.scaling.fitToHeight) : 0
            onActivated: function(index) { layout.host.setFitTo(layout.host.scaling.fitToWidth, index) }
        }
    }
    property Component scalePercentSlot: Component {
        Tk.NumberField {
            objectName: "scalePercentField"
            small: true
            implicitWidth: 92
            from: 10
            to: 400
            stepSize: 5
            suffix: "%"
            tooltip: qsTr("Scale (Adjust to; Automatic width and height)")
            enabled: layout.host !== null && !layout.host.scaling.fitToPage
            value: layout.host !== null ? layout.host.scaling.scale : 100
            onValueModified: function(v) { layout.host.setScalePercent(Math.round(v)) }
        }
    }

    // M90 (D-098): Table Design's Table Name box — Enter renames the table
    // (every reference follows); Excel's refusal shows in the status bar.
    property Component tableNameSlot: Component {
        Tk.TextField {
            objectName: "tableNameField"
            implicitWidth: 120
            tooltip: qsTr("Table Name")
            text: layout.host !== null ? layout.host.tableName : ""
            onAccepted: if (layout.host !== null && text !== layout.host.tableName) layout.host.renameTableTo(text)
        }
    }

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
            { id: "styles", label: qsTr("Styles"), items: [
                { id: "condFormatMenu", label: qsTr("Conditional Formatting"), iconName: "paint-bucket", size: "large",
                  menu: ["calcCondFormatTop10Action", "calcCondFormatAboveAverageAction", "calcCondFormatDuplicatesAction", "|",
                         "calcCondFormatDataBarsAction", "calcCondFormatColorScaleAction", "calcCondFormatIconSetAction", "|",
                         "calcCondFormatNewRuleAction", "calcCondFormatClearSelectionAction", "calcCondFormatClearSheetAction", "|",
                         "calcCondFormatAction"] }] },
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
            // M90 (D-098): Excel's Sparklines group.
            { id: "sparklines", label: qsTr("Sparklines"), items: [
                { action: "calcInsertSparklineLineAction", label: qsTr("Line") },
                { action: "calcInsertSparklineColumnAction", label: qsTr("Column") },
                { action: "calcInsertSparklineWinLossAction", label: qsTr("Win/Loss") }] },
            { id: "links", label: qsTr("Links"), items: [
                { action: "insertHyperlinkAction", size: "large" },
                "calcEditHyperlinkAction", "calcOpenHyperlinkAction", "calcRemoveHyperlinkAction"] },
            { id: "comments", label: qsTr("Comments"), items: [
                { action: "calcCommentAction", size: "large" },
                { action: "calcDeleteCommentAction", iconName: "message-square-x" }] },
            { id: "sheets", label: qsTr("Sheets"), items: [{ action: "insertSheetAction", size: "large", iconName: "sheet" }] },
            { id: "insertText", label: qsTr("Text"), items: [{ action: "calcHeaderFooterAction", size: "large" }] }
        ] },
        // ---- Page Layout (M78, D-087): Excel's tab ----
        { id: "pageLayout", label: qsTr("Page Layout"), keyTip: "P", groups: [
            { id: "pageSetup", label: qsTr("Page Setup"), items: [
                { id: "margins", label: qsTr("Margins"), iconName: "square-dashed", size: "large",
                  menu: ["calcMarginsNormalAction", "calcMarginsWideAction", "calcMarginsNarrowAction", "|", "calcMarginsCustomAction"] },
                { id: "orientation", label: qsTr("Orientation"), iconName: "rotate-cw", size: "large",
                  menu: ["calcOrientationPortraitAction", "calcOrientationLandscapeAction"] },
                { id: "paperSize", label: qsTr("Size"), iconName: "file", size: "large",
                  menu: (layout.host !== null ? layout.host.paperSizeIds : []).concat(["|", "calcPaperSizeMoreAction"]) },
                { id: "printArea", label: qsTr("Print Area"), iconName: "frame",
                  menu: ["calcSetPrintAreaAction", "calcClearPrintAreaAction"] },
                { id: "breaks", label: qsTr("Breaks"), iconName: "separator-horizontal",
                  menu: ["calcInsertPageBreakAction", "calcRemovePageBreakAction", "calcResetPageBreaksAction"] },
                { action: "calcPrintTitlesAction" },
                { action: "calcPageSetupAction", iconName: "layout-template" }] },
            { id: "scaleToFit", label: qsTr("Scale to Fit"), rows: [
                [{ id: "scaleWidth", component: scaleWidthSlot }, { id: "scaleHeight", component: scaleHeightSlot }],
                [{ id: "scalePercent", component: scalePercentSlot },
                 { id: "scaling", label: qsTr("Scaling"), iconName: "scaling",
                   menu: ["calcScaleNoneAction", "calcScaleFitSheetAction", "calcScaleFitColumnsAction", "calcScaleFitRowsAction"] }]] },
            { id: "sheetOptions", label: qsTr("Sheet Options"), items: [
                "calcPrintGridlinesAction", "calcPrintHeadingsAction"] }
        ] },
        { id: "data", label: qsTr("Data"), keyTip: "D", groups: [
            { id: "sortFilter", label: qsTr("Sort & filter"), items: [
                { action: "calcAutoFilterAction", size: "large" },
                "calcSortAscendingAction", "calcSortDescendingAction"] },
            { id: "dataTools", label: qsTr("Data tools"), items: [
                { action: "calcTextToColumnsAction", size: "large" },
                { action: "calcFlashFillAction" },
                { action: "calcRemoveDuplicatesAction" },
                { id: "dataValidation", action: "calcValidationAction", split: true, label: qsTr("Data Validation"),
                  iconName: "list-checks", menu: ["calcValidationAction", "calcCircleInvalidAction", "calcClearCirclesAction"] },
                { action: "calcConsolidateAction" }] },
            { id: "forecast", label: qsTr("Forecast"), items: [
                { id: "whatIf", label: qsTr("What-If Analysis"), iconName: "wand", size: "large",
                  menu: ["calcScenarioManagerAction", "calcGoalSeekAction", "calcDataTableAction"] }] },
            { id: "fill", label: qsTr("Fill"), items: [
                { action: "calcFillDownAction", iconName: "arrow-down-to-line" },
                { action: "calcFillRightAction", iconName: "arrow-right" }] },
            { id: "outline", label: qsTr("Outline"), items: [
                { action: "calcGroupAction", size: "large" },
                { action: "calcUngroupAction", size: "large" },
                "calcShowDetailAction", "calcHideDetailAction"] },
            // M90 (D-098): Excel's Analyze group (the Solver add-in).
            { id: "analyze", label: qsTr("Analyze"), items: [{ action: "calcSolverAction", size: "large" }] }
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
            { id: "workbookViews", label: qsTr("Workbook Views"), items: [
                { action: "calcViewNormalAction", size: "large" },
                { action: "calcViewPageBreakPreviewAction", size: "large" },
                { action: "calcCustomViewsAction", size: "large", label: qsTr("Custom Views") }] },
            { id: "freeze", label: qsTr("Window"), items: [
                { id: "freezeMenu", action: "calcFreezePanesAction", split: true, size: "large", iconName: "snowflake",
                  label: qsTr("Freeze Panes"),
                  menu: ["calcFreezePanesAction", "calcFreezeTopRowAction", "calcFreezeFirstColumnAction"] },
                "calcViewSplitAction", "calcViewNewWindowAction"] },
            StandardRibbon.assistantGroup([])
        ]),
        // ---- M90 (D-098): Excel's contextual Sparkline and Table Design tabs ----
        { id: "sparkline", label: qsTr("Sparkline"), keyTip: "JS", contextId: "sparkline", groups: [
            { id: "sparklineEdit", label: qsTr("Sparkline"), items: [
                { id: "sparklineEditData", action: "calcSparklineEditDataAction", split: true, size: "large",
                  label: qsTr("Edit Data"), iconName: "square-pen",
                  menu: ["calcSparklineEditDataAction", "calcSparklineEditSingleAction", "|",
                         "calcSparklineHiddenEmptyAction"] }] },
            { id: "sparklineType", label: qsTr("Type"), items: [
                "calcSparklineTypeLineAction", "calcSparklineTypeColumnAction", "calcSparklineTypeWinLossAction"] },
            { id: "sparklineShow", label: qsTr("Show"), rows: [
                ["calcSparklineHighPointAction", "calcSparklineLowPointAction", "calcSparklineNegativePointsAction"],
                ["calcSparklineFirstPointAction", "calcSparklineLastPointAction", "calcSparklineMarkersAction"]] },
            { id: "sparklineStyle", label: qsTr("Style"), items: [
                { action: "calcSparklineColorsAction", size: "large", label: qsTr("Colors") }] },
            { id: "sparklineGroup", label: qsTr("Group"), items: [
                { id: "sparklineAxis", label: qsTr("Axis"), iconName: "ruler", size: "large",
                  menu: ["calcSparklineAxisGeneralAction", "calcSparklineAxisDateAction", "calcSparklineAxisShowAction",
                         "calcSparklineAxisRightToLeftAction", "|", "calcSparklineMinAutoAction",
                         "calcSparklineMinSameAction", "calcSparklineMinCustomAction", "|",
                         "calcSparklineMaxAutoAction", "calcSparklineMaxSameAction", "calcSparklineMaxCustomAction"] },
                "calcSparklineGroupAction", "calcSparklineUngroupAction",
                { id: "sparklineClear", label: qsTr("Clear"), iconName: "eraser",
                  menu: ["calcSparklineClearAction", "calcSparklineClearGroupAction"] }] }
        ] },
        { id: "tableDesign", label: qsTr("Table Design"), keyTip: "JT", contextId: "table", groups: [
            { id: "tableProperties", label: qsTr("Properties"), rows: [
                [{ id: "tableName", component: tableNameSlot }],
                ["calcTableRenameAction", "calcTableResizeAction"]] },
            { id: "tableStyleOptions", label: qsTr("Table Style Options"), items: [
                { action: "calcTableTotalRowAction", iconName: "sigma" }] }
        ] },
        // ---- PivotTables (M74, D-083): Excel's two contextual tabs ----
        { id: "pivotAnalyze", label: qsTr("PivotTable Analyze"), keyTip: "JT", contextId: "pivot", groups: [
            { id: "pivotTable", label: qsTr("PivotTable"), items: [
                { action: "calcPivotOptionsAction", size: "large", label: qsTr("Options") }] },
            { id: "pivotActiveField", label: qsTr("Active Field"), items: [
                { action: "calcPivotFieldSettingsAction", label: qsTr("Field Settings") },
                { action: "calcPivotValueFieldSettingsAction", label: qsTr("Value Field Settings") }] },
            { id: "pivotCalculations", label: qsTr("Calculations"), items: ["calcPivotCalculatedFieldAction"] },
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
