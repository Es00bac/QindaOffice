// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaWrite's ribbon (D-041): File · Home · Insert · Layout · Review ·
// View, plus the contextual Table tab while the cursor is in a table. Every
// string is an ActionEntry id (AGENTS.md rule 5); icons given here only fill
// in for entries whose menu rows carry none. The four slots are the
// window's font, size and colour widgets.
QtObject {
    property Component fontSlot: null
    property Component sizeSlot: null
    property Component textColorSlot: null
    property Component highlightSlot: null

    readonly property var aiIds: ["writeAirewrite", "writeAishorten", "writeAiexpand", "writeAigrammar",
                                  "writeAisummarize", "writeAicontinue", "writeAiexplain"]
    readonly property var styleIds: ["writeStyle0", "writeStyle1", "writeStyle2", "writeStyle3", "writeStyle4",
                                     "writeStyle5", "writeStyle6", "writeStyle7", "writeStyle8", "writeStyle9"]

    readonly property var tabs: [
        StandardRibbon.fileTab([], ["writeTemplate_Letter", "writeTemplate_Meeting_Notes"],
                               [{ action: "writeExportPdfAction", iconName: "file-down" }]),
        { id: "home", label: qsTr("Home"), keyTip: "H", groups: [
            StandardRibbon.historyGroup(),
            StandardRibbon.clipboardGroup(),
            { id: "font", label: qsTr("Font"), rows: [
                [{ id: "fontFamily", component: fontSlot }, { id: "fontSize", component: sizeSlot },
                 { action: "formatGrowFontAction", size: "icon" }, { action: "formatShrinkFontAction", size: "icon" },
                 { action: "formatClearFormatAction", size: "icon", iconName: "remove-formatting" }],
                [{ action: "formatBoldAction", size: "icon" }, { action: "formatItalicAction", size: "icon" },
                 { action: "formatUnderlineAction", size: "icon" }, { action: "formatStrikeOutAction", size: "icon" },
                 { action: "formatSuperscriptAction", size: "icon" }, { action: "formatSubscriptAction", size: "icon" },
                 { id: "textColor", component: textColorSlot }, { id: "highlight", component: highlightSlot }]] },
            { id: "paragraph", label: qsTr("Paragraph"), rows: [
                [{ action: "formatListBulletAction", size: "icon" }, { action: "formatListNumberedAction", size: "icon" },
                 { action: "formatIndentLessAction", size: "icon" }, { action: "formatIndentMoreAction", size: "icon" },
                 { id: "lineSpacing", label: qsTr("Line spacing"), iconName: "rows-3", size: "icon",
                   menu: ["formatLineSpacingSingleAction", "formatLineSpacingOneHalfAction", "formatLineSpacingDoubleAction"] }],
                [{ action: "formatAlignLeftAction", size: "icon" }, { action: "formatAlignCenterAction", size: "icon" },
                 { action: "formatAlignRightAction", size: "icon" }, { action: "formatAlignJustifyAction", size: "icon" }]] },
            { id: "styles", label: qsTr("Styles"), items: [
                { action: "writeStyle0", size: "large", iconName: "pilcrow" },
                { action: "writeStyle1", iconName: "heading" },
                { action: "writeStyle2", iconName: "heading-1" },
                { action: "writeStyle3", iconName: "heading-2" },
                { action: "writeStyle8", iconName: "text-quote" },
                { id: "allStyles", label: qsTr("All styles"), iconName: "list-tree", menu: styleIds }] },
            StandardRibbon.editingGroup()
        ] },
        { id: "insert", label: qsTr("Insert"), keyTip: "N", groups: [
            { id: "pages", label: qsTr("Pages"), items: [
                { action: "insertPageBreakAction", size: "large", iconName: "separator-horizontal" }] },
            { id: "headerFooter", label: qsTr("Header & Footer"), items: [
                { action: "writeEditHeaderAction", size: "large", iconName: "panel-top" },
                { action: "writeEditFooterAction", iconName: "panel-bottom" },
                { action: "writeInsertPageNumberAction", iconName: "hash" },
                { action: "writeInsertPageCountAction", iconName: "sigma" },
                { action: "writeCloseHeaderFooterAction", iconName: "x" }] },
            { id: "tables", label: qsTr("Tables"), items: [{ action: "insertTableAction", size: "large" }] },
            { id: "illustrations", label: qsTr("Illustrations"), items: [
                { action: "insertImageAction", size: "large" }, { action: "writePictureAction", iconName: "image" }] },
            { id: "links", label: qsTr("Links"), items: [{ action: "insertHyperlinkAction", size: "large" }] },
            { id: "text", label: qsTr("Text"), items: [
                "insertColumnsAction", { action: "insertHorizontalRuleAction", iconName: "minus" },
                { action: "insertSymbolAction", iconName: "omega" }, { action: "insertDateAction", iconName: "calendar" },
                { action: "insertTimeAction", iconName: "clock" }] },
            { id: "references", label: qsTr("References"), items: [
                { action: "insertFootnoteAction", size: "large" },
                { action: "insertTocAction", iconName: "list-tree" },
                { action: "updateTocAction", iconName: "refresh-cw" }] }
        ] },
        { id: "layout", label: qsTr("Layout"), keyTip: "L", groups: [
            { id: "pageSetup", label: qsTr("Page"), items: [
                { action: "writePageSetupAction", size: "large", iconName: "file-text" },
                "insertColumnsAction"] },
            { id: "spacing", label: qsTr("Spacing"), items: [
                { action: "formatLineSpacingSingleAction", iconName: "rows-2" },
                { action: "formatLineSpacingOneHalfAction", iconName: "rows-3" },
                { action: "formatLineSpacingDoubleAction", iconName: "between-horizontal-start" }] }
        ] },
        { id: "review", label: qsTr("Review"), keyTip: "R", groups: [
            { id: "proofing", label: qsTr("Proofing"), items: [
                { action: "toolsWordCountAction", size: "large", iconName: "hash" }] },
            { id: "tracking", label: qsTr("Tracking"), items: [
                { action: "reviewTrackChangesAction", size: "large" },
                { action: "reviewNextChangeAction", iconName: "chevron-right" },
                { action: "reviewAcceptChangeAction", iconName: "check" },
                { action: "reviewRejectChangeAction", iconName: "x" },
                { action: "reviewAcceptAllAction", iconName: "check-check" },
                { action: "reviewRejectAllAction", iconName: "circle-x" }] },
            { id: "comments", label: qsTr("Comments"), items: [
                { action: "writeNewCommentAction", size: "large", iconName: "message-square-plus" },
                { action: "writeDeleteCommentAction", iconName: "message-square-x" },
                { action: "writePreviousCommentAction", iconName: "chevron-up" },
                { action: "writeNextCommentAction", iconName: "chevron-down" },
                { action: "writeShowCommentsAction", iconName: "message-square" }] },
            StandardRibbon.assistantGroup(aiIds)
        ] },
        StandardRibbon.viewTab([]),
        { id: "table", label: qsTr("Table"), keyTip: "T", contextId: "table", groups: [
            { id: "rowsColumns", label: qsTr("Rows & columns"), items: [
                "tableInsertRowAboveAction", "tableInsertRowBelowAction",
                "tableInsertColumnLeftAction", "tableInsertColumnRightAction",
                "tableDeleteRowAction", "tableDeleteColumnAction"] },
            { id: "merge", label: qsTr("Merge"), items: [
                { action: "tableMergeCellsAction", size: "large", iconName: "table-cells-merge" },
                { action: "tableSplitCellAction", size: "large", iconName: "table-cells-split" }] }
        ] }
    ]
}
