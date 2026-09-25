// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaCalc's ribbon (D-041): File · Home · Insert · Data · Sheet · View.
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
                  menu: ["calcRowHeightAction", "calcColumnWidthAction", "calcOptimalWidthAction"] }] },
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
            { id: "charts", label: qsTr("Charts"), items: [{ action: "insertChartAction", size: "large" }] },
            { id: "illustrations", label: qsTr("Illustrations"), items: [
                { action: "insertImageAction", size: "large", iconName: "image" }] },
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
                { action: "calcFillRightAction", iconName: "arrow-right" }] }
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
        ])
    ]
}
