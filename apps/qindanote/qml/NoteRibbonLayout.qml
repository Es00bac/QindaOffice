// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaNote's ribbon (D-041): File · Home (the ink tools and blocks) ·
// Page · View. Every string is an ActionEntry id (AGENTS.md rule 5); the
// slots are the window's colour, width and background widgets that follow
// the active tool.
QtObject {
    property Component colorSlot: null
    property Component widthSlot: null
    property Component backgroundSlot: null

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
    readonly property var backgrounds: ["notePageBackground_blank", "notePageBackground_ruled",
                                        "notePageBackground_grid", "notePageBackground_dotted"]

    readonly property var tabs: [
        StandardRibbon.fileTab([], [], [{ action: "noteExportPdf", iconName: "file-down" }]),
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
        { id: "page", label: qsTr("Page"), keyTip: "P", groups: [
            { id: "pageManage", label: qsTr("Page"), items: [
                { action: "notePageNew", size: "large" }, "notePageDuplicate", "notePageRename",
                "notePageDelete"] },
            { id: "pageMove", label: qsTr("Navigate"), items: [
                "notePagePrevious", "notePageNext", "notePageMoveUp", "notePageMoveDown"] },
            { id: "sections", label: qsTr("Sections"), items: [
                { action: "noteSectionNew", size: "large" },
                { action: "noteSectionRename", iconName: "pencil" },
                { action: "noteSectionDelete", iconName: "trash-2" }] },
            { id: "paper", label: qsTr("Paper"), items: [
                { id: "backgroundMenu", label: qsTr("Background"), iconName: "grid-3x3", size: "large", menu: backgrounds }] }
        ] },
        StandardRibbon.viewTab([
            { id: "panes", label: qsTr("Panes"), items: [{ action: "noteShowNavigatorAction", size: "large" }] },
            StandardRibbon.assistantGroup([])
        ])
    ]
}
