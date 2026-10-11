// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaNote's ribbon (D-041): File · Home (the ink tools and blocks) ·
// Text (M84: OneNote's Basic Text, Styles and Tags) · Insert (M84: tables,
// links, files, page templates) · Page · Live (live notes, D-057) · View,
// and the contextual Table tab while the caret is in a table. Every string
// is an ActionEntry id (AGENTS.md rule 5); the slots are the window's
// colour, width and background widgets that follow the active tool, and
// the Text tab's font, size and colour widgets (NoteTextSlots.qml).
QtObject {
    property Component colorSlot: null
    property Component widthSlot: null
    property Component backgroundSlot: null
    property Component fontSlot: null
    property Component sizeSlot: null
    property Component textColorSlot: null
    property Component highlightSlot: null

    readonly property var penColours: ["notePenColor_black", "notePenColor_grey", "notePenColor_blue", "notePenColor_red",
                                       "notePenColor_green", "notePenColor_orange", "notePenColor_purple",
                                       "notePenColor_brown", "|", "notePenColorAction"]
    readonly property var penWidths: ["notePenWidth_fine", "notePenWidth_medium", "notePenWidth_thick",
                                      "notePenWidth_bold", "notePenWidth_marker"]
    readonly property var highlighter: ["noteHighlighterColor_yellow", "noteHighlighterColor_green",
                                        "noteHighlighterColor_pink", "noteHighlighterColor_blue",
                                        "noteHighlighterColor_orange", "|", "noteHighlighterWidth_thin",
                                        "noteHighlighterWidth_medium", "noteHighlighterWidth_wide",
                                        "noteHighlighterWidth_broad"]
    readonly property var eraser: ["noteEraserMode_stroke", "noteEraserMode_partial", "|", "noteEraserSize_small",
                                   "noteEraserSize_medium", "noteEraserSize_large", "noteEraserSize_huge"]
    readonly property var backgrounds: ["notePageBackground_blank", "|", "notePageBackground_ruledNarrow",
                                        "notePageBackground_ruled", "notePageBackground_ruledWide", "|",
                                        "notePageBackground_gridSmall", "notePageBackground_grid",
                                        "notePageBackground_gridLarge", "|", "notePageBackground_dotted"]
    // M84 (D-093).
    readonly property var styles: ["noteStyle_normal", "noteStyle_title", "|", "noteStyle_heading1",
                                   "noteStyle_heading2", "noteStyle_heading3", "noteStyle_heading4",
                                   "noteStyle_heading5", "noteStyle_heading6", "|", "noteStyle_quote",
                                   "noteStyle_code"]
    readonly property var tags: ["noteTag_todo", "noteTag_important", "noteTag_question", "noteTag_remember",
                                 "noteTag_definition", "noteTag_highlight", "noteTag_contact", "noteTag_address",
                                 "noteTag_phone", "noteTag_idea", "noteTag_critical", "noteTag_projectA",
                                 "noteTag_projectB", "|", "noteTagMoreAction", "noteTagCustomizeAction",
                                 "noteTagRemoveAction"]
    readonly property var tableEdits: ["tableInsertRowAboveAction", "tableInsertRowBelowAction",
                                       "tableInsertColumnLeftAction", "tableInsertColumnRightAction", "|",
                                       "tableDeleteRowAction", "tableDeleteColumnAction", "noteTableDeleteAction"]
    readonly property var templates: ["notePageTemplate_blank", "notePageTemplate_lined", "notePageTemplate_grid",
                                      "notePageTemplate_meeting", "notePageTemplate_todo"]
    readonly property var pageColours: ["notePageColor_none", "notePageColor_yellow", "notePageColor_green",
                                        "notePageColor_blue", "notePageColor_pink", "notePageColor_purple",
                                        "notePageColor_orange", "notePageColor_grey", "|", "notePageColorAction"]

    readonly property var tabs: [
        StandardRibbon.fileTab([], [], [{ action: "noteExportPdf", iconName: "file-down" },
                                        { action: "noteImportAction", iconName: "import" }]),
        { id: "home", label: qsTr("Home"), keyTip: "H", groups: [
            StandardRibbon.historyGroup(),
            StandardRibbon.clipboardGroup(),
            { id: "tools", label: qsTr("Tools"), items: [
                { action: "notePenTool", size: "large" }, { action: "noteHighlighterTool", size: "large" },
                { action: "noteEraserTool", size: "large" }, { action: "noteTextTool", size: "large" }] },
            { id: "ink", label: qsTr("Ink"), rows: [
                [{ id: "inkColour", component: colorSlot }, { id: "inkWidth", component: widthSlot }],
                [{ id: "penColours", label: qsTr("Pen"), iconName: "pen-tool", menu: penColours.concat(["|"]).concat(penWidths) },
                 { id: "highlighterOptions", label: qsTr("Marker"), iconName: "highlighter", menu: highlighter },
                 { id: "eraserOptions", label: qsTr("Eraser"), iconName: "eraser", menu: eraser }]] },
            { id: "blocks", label: qsTr("Insert"), items: [
                { action: "noteAddTextAction", size: "large" }, "noteAddCheckAction", "noteAddImageAction",
                "noteDeleteBlockAction"] },
            { id: "pagesHome", label: qsTr("Pages"), items: [
                { action: "notePageNew", size: "large" }, "noteSectionNew",
                { id: "background", component: backgroundSlot }] },
            StandardRibbon.editingGroup()
        ] },
        // M84 (D-093): OneNote's Basic Text, Styles and Tags.
        { id: "text", label: qsTr("Text"), keyTip: "T", groups: [
            { id: "basicText", label: qsTr("Basic Text"), rows: [
                [{ id: "fontFamily", component: fontSlot }, { id: "fontSize", component: sizeSlot },
                 { action: "formatGrowFontAction", size: "icon" }, { action: "formatShrinkFontAction", size: "icon" },
                 { action: "formatClearFormatAction", size: "icon" }],
                [{ action: "formatBoldAction", size: "icon" }, { action: "formatItalicAction", size: "icon" },
                 { action: "formatUnderlineAction", size: "icon" }, { action: "formatStrikeOutAction", size: "icon" },
                 { action: "formatSubscriptAction", size: "icon" }, { action: "formatSuperscriptAction", size: "icon" },
                 { id: "textColour", component: textColorSlot }, { action: "formatTextColorAction", size: "icon" },
                 { id: "highlightColour", component: highlightSlot }, { action: "formatHighlightAction", size: "icon" }]] },
            { id: "paragraph", label: qsTr("Paragraph"), rows: [
                [{ action: "formatListBulletAction", size: "icon" }, { action: "formatListNumberedAction", size: "icon" },
                 { action: "formatIndentLessAction", size: "icon" }, { action: "formatIndentMoreAction", size: "icon" }],
                [{ action: "formatAlignLeftAction", size: "icon" }, { action: "formatAlignCenterAction", size: "icon" },
                 { action: "formatAlignRightAction", size: "icon" }]] },
            { id: "styles", label: qsTr("Styles"), items: [
                { action: "noteStyle_heading1", size: "large" }, "noteStyle_heading2", "noteStyle_heading3",
                { id: "allStyles", label: qsTr("Styles"), iconName: "pilcrow", menu: styles }] },
            { id: "tags", label: qsTr("Tags"), items: [
                { action: "noteTag_todo", size: "large" }, "noteTag_important", "noteTag_question",
                { id: "allTags", label: qsTr("Tags"), iconName: "tag", menu: tags },
                { action: "noteFindTagsAction", size: "large" }, "noteTagSummaryPageAction"] }
        ] },
        // M84: OneNote's Insert tab.
        { id: "insert", label: qsTr("Insert"), keyTip: "N", groups: [
            { id: "tables", label: qsTr("Tables"), items: [
                { action: "insertTableAction", size: "large" },
                { id: "tableMenu", label: qsTr("Table"), iconName: "table-2", menu: tableEdits }] },
            { id: "links", label: qsTr("Links"), items: [
                { action: "insertHyperlinkAction", size: "large" }, "noteLinkFileAction"] },
            { id: "files", label: qsTr("Files"), items: [{ action: "noteImportAction", size: "large" }] },
            { id: "pageTemplates", label: qsTr("Pages"), items: [
                { id: "templateMenu", label: qsTr("Page Templates"), iconName: "layout-template", size: "large",
                  menu: templates }] },
            { id: "insertBlocks", label: qsTr("Blocks"), items: [
                "noteAddTextAction", "noteAddCheckAction", "noteAddImageAction"] }
        ] },
        { id: "page", label: qsTr("Page"), keyTip: "P", groups: [
            { id: "pageManage", label: qsTr("Page"), items: [
                { action: "notePageNew", size: "large" }, "notePageDuplicate", "notePageRename",
                "notePageDelete"] },
            { id: "pageMove", label: qsTr("Navigate"), items: [
                "notePagePrevious", "notePageNext", "notePageMoveUp", "notePageMoveDown"] },
            { id: "pageHistory", label: qsTr("History"), items: ["notePageVersionsAction"] },
            { id: "subpages", label: qsTr("Subpages"), items: ["noteMakeSubpageAction", "notePromoteSubpageAction"] },
            { id: "sections", label: qsTr("Sections"), items: [
                { action: "noteSectionNew", size: "large" },
                { action: "noteSectionRename", iconName: "pencil" },
                { action: "noteSectionDelete", iconName: "trash-2" }] },
            { id: "paper", label: qsTr("Paper"), items: [
                { id: "backgroundMenu", label: qsTr("Rule Lines"), iconName: "grid-3x3", size: "large", menu: backgrounds },
                { id: "pageColourMenu", label: qsTr("Page Colour"), iconName: "paint-bucket", size: "large",
                  menu: pageColours },
                { id: "pageTemplateMenu", label: qsTr("Templates"), iconName: "layout-template", menu: templates }] }
        ] },
        // Live notes (D-057): capture on its own tab — Home is full at 1280 px.
        { id: "live", label: qsTr("Live"), keyTip: "L", groups: [
            { id: "liveSession", label: qsTr("Session"), items: [
                { action: "noteLiveStartAction", size: "large" }, "noteLivePauseAction", "noteLiveStopAction",
                { action: "noteLiveFlagAction", size: "large" }, "noteLiveCatchUpAction"] },
            // M56: after (or during) a session.
            { id: "liveReview", label: qsTr("Review"), items: [
                { action: "noteSessionPanelAction", size: "large" }, "noteStudyPageAction",
                "noteActionItemsToMailAction", "noteSplitByTopicAction"] },
            { id: "liveExport", label: qsTr("Export"), items: [
                "noteExportWordAction", "noteExportMarkdownAction", "noteOpenInWriteAction", "noteExportAnkiAction"] },
            { id: "liveFollowUp", label: qsTr("Follow-up"), items: [
                "noteMeetingRecapAction", "noteProjectPlanAction", "noteTroubleshootingAction",
                "noteFollowUpQuestionsAction", "noteAcceptDraftAction", "noteDiscardDraftAction"] },
            { id: "livePanes", label: qsTr("Panel"), items: [
                { action: "noteLiveNotesAction", size: "large" }, "aiProvidersAction"] }
        ] },
        StandardRibbon.viewTab([
            { id: "panes", label: qsTr("Panes"), items: [{ action: "noteShowNavigatorAction", size: "large" },
                                                         { action: "noteSearchAction", size: "large" },
                                                         "noteFindTagsAction"] },
            // AGENT-NOTE: on View, not Home: Home fills 1280 px exactly at
            // squeeze level 0, and one more item there drops it to icons.
            { id: "touch", label: qsTr("Touch"), items: [{ action: "noteTouchDrawAction", size: "large" }] },
            StandardRibbon.assistantGroup([])
        ]),
        // M84: Table Tools while the caret is in a table (NoteHost's
        // "noteTable" ribbon context).
        { id: "tableTools", label: qsTr("Table"), keyTip: "J", contextId: "noteTable", groups: [
            { id: "tableRows", label: qsTr("Rows & Columns"), items: [
                { action: "tableInsertRowAboveAction", size: "large" }, "tableInsertRowBelowAction",
                "tableInsertColumnLeftAction", "tableInsertColumnRightAction"] },
            { id: "tableDelete", label: qsTr("Delete"), items: [
                "tableDeleteRowAction", "tableDeleteColumnAction", { action: "noteTableDeleteAction", size: "large" }] }
        ] }
    ]
}
