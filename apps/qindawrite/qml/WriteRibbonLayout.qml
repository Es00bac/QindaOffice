// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaWrite's ribbon (D-041): File · Home · Insert · Layout · References ·
// Mailings · Review · View, plus the contextual Table Design and Table
// Layout tabs while the cursor is in a table (M70, D-079: Word's shading,
// borders and pen, cell size, alignment, data and properties) and Picture
// Format while a picture is selected (M77, D-086: Wrap Text, Position). Every string is an ActionEntry id
// (AGENTS.md rule 5); icons given here only fill in for entries whose menu
// rows carry none. The slots are the window's font, size and colour
// widgets, and the border pen and shading pickers.
QtObject {
    property Component fontSlot: null
    property Component sizeSlot: null
    property Component textColorSlot: null
    property Component highlightSlot: null
    property Component shadingSlot: null
    property Component penStyleSlot: null
    property Component penWidthSlot: null
    property Component penColorSlot: null
    property Component recordSlot: null

    readonly property var borderIds: ["writeBordersBottomAction", "writeBordersTopAction", "writeBordersLeftAction",
                                      "writeBordersRightAction", "|", "writeBordersNoneAction", "writeBordersAllAction",
                                      "writeBordersOutsideAction", "writeBordersInsideAction", "|",
                                      "writeBordersInsideHAction", "writeBordersInsideVAction", "|",
                                      "writeBordersShadingAction"]
    readonly property var caseIds: ["writeChangeCaseAction", "|", "writeCaseSentenceAction", "writeCaseLowerAction",
                                    "writeCaseUpperAction", "writeCaseCapitalizeAction", "writeCaseToggleAction"]

    // Word's list libraries (M73, D-082).
    readonly property var bulletIds: ["writeBullet0", "writeBullet1", "writeBullet2", "writeBullet3", "writeBullet4",
                                      "writeBullet5", "writeBullet6", "|", "writeBulletNoneAction", "writeDefineBulletAction"]
    readonly property var numberingIds: ["writeNumbering0", "writeNumbering1", "writeNumbering2", "writeNumbering3",
                                         "writeNumbering4", "writeNumbering5", "writeNumbering6", "|",
                                         "writeNumberingNoneAction", "writeDefineNumberAction", "|",
                                         "writeRestartNumberingAction", "writeContinueNumberingAction",
                                         "writeSetNumberingValueAction"]
    readonly property var multilevelIds: ["writeMultilevel0", "writeMultilevel1", "writeMultilevel2", "writeMultilevel3", "|",
                                          "writeDefineMultilevelAction", "|", "writeListLevel1", "writeListLevel2",
                                          "writeListLevel3", "writeListLevel4", "writeListLevel5", "writeListLevel6",
                                          "writeListLevel7", "writeListLevel8", "writeListLevel9", "|",
                                          "writeAdjustListIndentsAction"]

    // Word's Mailings (M77, D-086).
    readonly property var mergeKindIds: ["writeMergeLettersAction", "writeMergeEmailAction", "writeMergeEnvelopesAction",
                                         "writeMergeLabelsAction", "writeMergeDirectoryAction", "|", "writeMergeNormalAction"]
    readonly property var mergeFieldIds: ["writeMergeField0", "writeMergeField1", "writeMergeField2", "writeMergeField3",
                                          "writeMergeField4", "writeMergeField5", "writeMergeField6", "writeMergeField7",
                                          "writeMergeField8", "writeMergeField9", "writeMergeField10", "writeMergeField11",
                                          "writeMergeField12", "writeMergeField13", "writeMergeField14", "writeMergeField15",
                                          "writeMergeField16", "writeMergeField17", "writeMergeField18", "writeMergeField19"]
    readonly property var mergeRuleIds: ["writeMergeRuleIfAction", "writeMergeRuleSkipIfAction", "writeMergeRuleNextAction",
                                         "writeMergeRuleNextIfAction", "writeMergeRuleRecordAction", "writeMergeRuleSequenceAction"]
    readonly property var wrapIds: ["writeWrapInlineAction", "|", "writeWrapSquareAction", "writeWrapTightAction",
                                    "writeWrapThroughAction", "writeWrapTopBottomAction", "writeWrapBehindAction",
                                    "writeWrapFrontAction", "|", "writeMoveWithTextAction", "writeFixPositionAction", "|",
                                    "writePictureLayoutAction"]

    readonly property var aiIds: ["writeAirewrite", "writeAishorten", "writeAiexpand", "writeAigrammar",
                                  "writeAisummarize", "writeAicontinue", "writeAiexplain"]
    readonly property var styleIds: ["writeStyle0", "writeStyle1", "writeStyle2", "writeStyle3", "writeStyle4",
                                     "writeStyle5", "writeStyle6", "writeStyle7", "writeStyle8", "writeStyle9"]
    // The document's quick styles (M73): sixteen slots that follow the document.
    readonly property var quickStyleIds: ["writeQuickStyle4", "writeQuickStyle5", "writeQuickStyle6", "writeQuickStyle7",
                                          "writeQuickStyle8", "writeQuickStyle9", "writeQuickStyle10", "writeQuickStyle11",
                                          "writeQuickStyle12", "writeQuickStyle13", "writeQuickStyle14", "writeQuickStyle15",
                                          "|"].concat(styleIds)
    readonly property var styleCommandIds: ["writeStylesPaneAction", "|", "writeNewStyleAction", "writeModifyStyleAction",
                                            "writeUpdateStyleAction", "writeDeleteStyleAction"]

    readonly property var tabs: [
        StandardRibbon.fileTab([], ["writeTemplate_Letter", "writeTemplate_Meeting_Notes"],
                               [{ action: "writeExportPdfAction", iconName: "file-down" }]),
        { id: "home", label: qsTr("Home"), keyTip: "H", groups: [
            StandardRibbon.historyGroup(),
            // The shared Clipboard group with Word's Format Painter beside Cut
            // and Copy (M73).
            { id: "clipboard", label: qsTr("Clipboard"), items: [
                { action: "editPasteAction", size: "large" }, "editCutAction", "editCopyAction",
                { action: "writeFormatPainterAction", iconName: "paintbrush", split: true,
                  menu: ["writeCopyFormatAction", "writePasteFormatAction"] }] },
            { id: "font", label: qsTr("Font"), rows: [
                [{ id: "fontFamily", component: fontSlot }, { id: "fontSize", component: sizeSlot },
                 { action: "formatGrowFontAction", size: "icon" }, { action: "formatShrinkFontAction", size: "icon" },
                 { action: "formatClearFormatAction", size: "icon", iconName: "remove-formatting" },
                 { id: "changeCase", label: qsTr("Change Case"), iconName: "arrow-up-down", size: "icon", menu: caseIds },
                 { action: "writeFontAction", size: "icon", iconName: "type" }],
                [{ action: "formatBoldAction", size: "icon" }, { action: "formatItalicAction", size: "icon" },
                 { action: "formatUnderlineAction", size: "icon" }, { action: "formatStrikeOutAction", size: "icon" },
                 { action: "formatSuperscriptAction", size: "icon" }, { action: "formatSubscriptAction", size: "icon" },
                 { id: "textColor", component: textColorSlot }, { id: "highlight", component: highlightSlot }]] },
            { id: "paragraph", label: qsTr("Paragraph"), rows: [
                [{ action: "formatListBulletAction", size: "icon", split: true, menu: bulletIds },
                 { action: "formatListNumberedAction", size: "icon", split: true, menu: numberingIds },
                 { id: "multilevelList", label: qsTr("Multilevel List"), iconName: "list-tree", size: "icon", menu: multilevelIds },
                 { action: "formatIndentLessAction", size: "icon" }, { action: "formatIndentMoreAction", size: "icon" },
                 { id: "lineSpacing", label: qsTr("Line spacing"), iconName: "rows-3", size: "icon",
                   menu: ["formatLineSpacingSingleAction", "formatLineSpacingOneHalfAction", "formatLineSpacingDoubleAction"] }],
                [{ action: "formatAlignLeftAction", size: "icon" }, { action: "formatAlignCenterAction", size: "icon" },
                 { action: "formatAlignRightAction", size: "icon" }, { action: "formatAlignJustifyAction", size: "icon" },
                 { action: "writeShadingAction", size: "icon", iconName: "paint-bucket" },
                 { id: "paragraphBorders", label: qsTr("Borders"), iconName: "grid-3x3", size: "icon", menu: borderIds },
                 { action: "writeParagraphAction", size: "icon", iconName: "pilcrow" }]] },
            { id: "styles", label: qsTr("Styles"), items: [
                { action: "writeQuickStyle0", size: "large", iconName: "pilcrow" },
                { action: "writeQuickStyle1", iconName: "type" },
                { action: "writeQuickStyle2", iconName: "type" },
                { action: "writeQuickStyle3", iconName: "type" },
                { id: "allStyles", label: qsTr("More styles"), iconName: "list-tree", menu: quickStyleIds },
                { action: "writeStylesPaneAction", iconName: "palette", split: true, menu: styleCommandIds }] },
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
            { id: "tables", label: qsTr("Tables"), items: [{ action: "insertTableAction", size: "large" },
                                                          { action: "writeTextToTableAction", iconName: "table-2" }] },
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
                { action: "formatLineSpacingDoubleAction", iconName: "between-horizontal-start" }] },
            { id: "paragraphLayout", label: qsTr("Paragraph"), items: [
                { action: "writeParagraphAction", size: "large", iconName: "pilcrow" },
                { action: "writeTabsAction", iconName: "ruler" },
                { action: "writeBordersShadingAction", iconName: "sliders-horizontal" },
                { action: "writeNoShadingAction", iconName: "eraser" }] },
            { id: "arrange", label: qsTr("Arrange"), items: [
                { id: "wrapTextLayout", label: qsTr("Wrap Text"), iconName: "wrap-text", menu: wrapIds },
                { action: "writePictureLayoutAction", iconName: "move" }] }
        ] },
        { id: "references", label: qsTr("References"), keyTip: "S", groups: [
            { id: "tableOfContents", label: qsTr("Table of Contents"), items: [
                { action: "insertTocAction", size: "large", iconName: "list-tree" },
                { action: "updateTocAction", iconName: "refresh-cw" }] },
            { id: "footnotes", label: qsTr("Footnotes"), items: [
                { action: "insertFootnoteAction", size: "large" },
                { action: "writeInsertEndnoteAction", iconName: "notebook-pen" },
                { action: "writeNoteOptionsAction", iconName: "settings-2" },
                { action: "writeConvertNotesAction", iconName: "arrow-left-right" }] }
        ] },
        { id: "mailings", label: qsTr("Mailings"), keyTip: "M", groups: [
            { id: "mergeCreate", label: qsTr("Create"), items: [
                { action: "writeEnvelopesAction", size: "large", iconName: "mail" },
                { action: "writeLabelsAction", iconName: "tag" }] },
            { id: "mergeStart", label: qsTr("Start Mail Merge"), items: [
                { id: "startMailMerge", label: qsTr("Start Mail Merge"), iconName: "mails", size: "large", menu: mergeKindIds },
                { id: "selectRecipients", label: qsTr("Select Recipients"), iconName: "users",
                  menu: ["writeMergeNewListAction", "writeMergeExistingListAction", "writeMergePeopleAction"] },
                { action: "writeMergeEditRecipientsAction", iconName: "list-checks" }] },
            { id: "mergeFields", label: qsTr("Write & Insert Fields"), items: [
                { action: "writeMergeHighlightAction", iconName: "highlighter" },
                { action: "writeMergeAddressBlockAction", size: "large", iconName: "book-user" },
                { action: "writeMergeGreetingLineAction", iconName: "handshake" },
                { action: "writeMergeInsertFieldAction", iconName: "braces", split: true, menu: mergeFieldIds },
                { id: "mergeRules", label: qsTr("Rules"), iconName: "workflow", menu: mergeRuleIds },
                { action: "writeMergeMatchFieldsAction", iconName: "link-2" },
                { action: "writeMergeUpdateLabelsAction", iconName: "refresh-cw" }] },
            { id: "mergePreview", label: qsTr("Preview Results"), rows: [
                [{ action: "writeMergePreviewAction", iconName: "eye" },
                 { action: "writeMergeFindAction", size: "icon", iconName: "search" },
                 { action: "writeMergeCheckAction", size: "icon", iconName: "circle-check" }],
                [{ action: "writeMergeFirstAction", size: "icon", iconName: "chevrons-left" },
                 { action: "writeMergePreviousAction", size: "icon", iconName: "chevron-left" },
                 { id: "mergeRecord", component: recordSlot },
                 { action: "writeMergeNextAction", size: "icon", iconName: "chevron-right" },
                 { action: "writeMergeLastAction", size: "icon", iconName: "chevrons-right" }]] },
            { id: "mergeFinish", label: qsTr("Finish"), items: [
                { id: "finishMerge", label: qsTr("Finish && Merge"), iconName: "merge", size: "large",
                  menu: ["writeMergeEditDocumentsAction", "writeMergePrintAction", "writeMergePdfAction",
                         "writeMergeEmailSendAction"] }] }
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
        StandardRibbon.viewTab([{ id: "panes", label: qsTr("Panes"), items: [
            { action: "writeNavigationPaneAction", size: "large", iconName: "navigation" },
            { action: "writeStylesPaneAction", iconName: "palette" },
            { action: "writeShowCommentsAction", iconName: "message-square" }] }]),
        { id: "pictureFormat", label: qsTr("Picture Format"), keyTip: "JP", contextId: "picture", groups: [
            { id: "pictureArrange", label: qsTr("Arrange"), items: [
                { id: "wrapText", label: qsTr("Wrap Text"), iconName: "wrap-text", size: "large", menu: wrapIds },
                { action: "writePictureLayoutAction", iconName: "move" },
                { action: "writeMoveWithTextAction", iconName: "move-vertical" },
                { action: "writeFixPositionAction", iconName: "pin" }] },
            { id: "pictureSize", label: qsTr("Size"), items: [
                { action: "writePictureAction", size: "large", iconName: "image" }] }
        ] },
        { id: "tableDesign", label: qsTr("Table Design"), keyTip: "JT", contextId: "table", groups: [
            { id: "tableShading", label: qsTr("Shading"), items: [
                { id: "shading", component: shadingSlot },
                { action: "writeShadingAction", size: "large", iconName: "paint-bucket" }] },
            { id: "tableBorders", label: qsTr("Borders"), rows: [
                [{ id: "penStyle", component: penStyleSlot }, { id: "penColor", component: penColorSlot }],
                [{ id: "penWidth", component: penWidthSlot },
                 { id: "tableBordersMenu", label: qsTr("Borders"), iconName: "grid-3x3", menu: borderIds }]] }
        ] },
        { id: "tableLayout", label: qsTr("Table Layout"), keyTip: "JL", contextId: "table", groups: [
            { id: "tableSelect", label: qsTr("Table"), items: [
                { id: "tableSelectMenu", label: qsTr("Select"), iconName: "text-select",
                  menu: ["writeTableSelectCellAction", "writeTableSelectColumnAction", "writeTableSelectRowAction",
                         "writeTableSelectTableAction"] },
                { action: "writeTablePropertiesAction", iconName: "settings-2" }] },
            { id: "rowsColumns", label: qsTr("Rows & columns"), items: [
                { id: "tableDeleteMenu", label: qsTr("Delete"), iconName: "trash-2",
                  menu: ["tableDeleteColumnAction", "tableDeleteRowAction", "writeTableDeleteTableAction"] },
                "tableInsertRowAboveAction", "tableInsertRowBelowAction",
                "tableInsertColumnLeftAction", "tableInsertColumnRightAction"] },
            { id: "merge", label: qsTr("Merge"), items: [
                { action: "tableMergeCellsAction", size: "large", iconName: "table-cells-merge" },
                { action: "tableSplitCellAction", size: "large", iconName: "table-cells-split" }] },
            { id: "cellSize", label: qsTr("Cell Size"), items: [
                { id: "tableAutoFitMenu", label: qsTr("AutoFit"), iconName: "move-horizontal",
                  menu: ["writeTableAutoFitContentsAction", "writeTableAutoFitWindowAction", "writeTableFixedWidthAction"] },
                { action: "writeTableDistributeRowsAction", iconName: "align-vertical-distribute-center" },
                { action: "writeTableDistributeColumnsAction", iconName: "align-horizontal-distribute-center" }] },
            { id: "cellAlignment", label: qsTr("Alignment"), rows: [
                [{ action: "writeTableAlignTopLeftAction", size: "icon" }, { action: "writeTableAlignTopCenterAction", size: "icon" },
                 { action: "writeTableAlignTopRightAction", size: "icon" }, { action: "writeTableAlignMiddleLeftAction", size: "icon" },
                 { action: "writeTableAlignMiddleCenterAction", size: "icon" }],
                [{ action: "writeTableAlignMiddleRightAction", size: "icon" }, { action: "writeTableAlignBottomLeftAction", size: "icon" },
                 { action: "writeTableAlignBottomCenterAction", size: "icon" }, { action: "writeTableAlignBottomRightAction", size: "icon" }]] },
            { id: "tableData", label: qsTr("Data"), items: [
                { action: "writeTableSortAction", iconName: "arrow-down-z-a" },
                { action: "writeTableRepeatHeaderAction", iconName: "repeat" },
                { action: "writeTableToTextAction", iconName: "text" }] }
        ] }
    ]
}
