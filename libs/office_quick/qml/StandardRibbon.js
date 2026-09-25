// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// The ribbon tabs every document app shares (D-041): File first-class (the
// local menu bar disappears when the panel exports the menu, so the ribbon
// must reach New/Open/Save/Print itself) and View last. Apps pass the
// extra groups/items their own File and View need. Shapes are the ones
// RibbonFromModel.qml documents; every string is an ActionEntry id.

function fileTab(extraGroups, templatesMenu, exportItems) {
    const groups = [
        { id: "fileDocument", label: qsTr("Document"), items: [
            { action: "fileNewAction", size: "large",
              menu: templatesMenu && templatesMenu.length ? templatesMenu : undefined,
              split: templatesMenu && templatesMenu.length ? true : undefined },
            { action: "fileOpenAction", size: "large" },
            { action: "fileSaveAction", size: "large" },
            { action: "fileSaveAsAction", iconName: "file-output" },
            { action: "fileCloseWindowAction" } ] },
        { id: "filePrint", label: qsTr("Print & export"), items: [
            { action: "filePrintAction", size: "large" },
            { action: "filePrintPreviewAction", iconName: "eye" } ].concat(exportItems || []) }
    ]
    return { id: "file", label: qsTr("File"), keyTip: "F", groups: groups.concat(extraGroups || []) }
}

function viewTab(extraGroups) {
    return { id: "view", label: qsTr("View"), keyTip: "W", groups: (extraGroups || []).concat([
        { id: "viewZoom", label: qsTr("Zoom"), items: [
            { action: "viewZoomInAction", size: "large" },
            { action: "viewZoomOutAction" },
            { action: "viewZoomResetAction", iconName: "scan" } ] },
        { id: "viewShow", label: qsTr("Show"), items: [
            { action: "viewCompactRibbonAction" },
            { action: "viewShowStatusBarAction", iconName: "panel-bottom" },
            { action: "viewShowMenuBarAction", iconName: "menu" },
            { action: "viewShowToolBarsAction", iconName: "panel-top-close" } ] },
        { id: "viewHelp", label: qsTr("Help"), items: [
            { action: "helpSearchCommandsAction", size: "large" },
            { action: "settingsPreferencesAction", iconName: "settings" } ] }
    ]) }
}

function clipboardGroup() {
    return { id: "clipboard", label: qsTr("Clipboard"), items: [
        { action: "editPasteAction", size: "large" },
        "editCutAction", "editCopyAction" ] }
}

function historyGroup() {
    return { id: "history", label: qsTr("History"), items: ["editUndoAction", "editRedoAction"] }
}

function editingGroup(extra) {
    return { id: "editing", label: qsTr("Editing"), items: [
        { action: "editFindAction", size: "large" },
        "editReplaceAction",
        { action: "editSelectAllAction", iconName: "text-select" } ].concat(extra || []) }
}

function assistantGroup(aiMenuIds) {
    return { id: "assistant", label: qsTr("Assistant"), items: [
        { action: "aiAssistantAction", size: "large", iconName: "sparkles" } ].concat(
        aiMenuIds && aiMenuIds.length
            ? [{ id: "aiActions", label: qsTr("Writing tools"), iconName: "wand-sparkles", menu: aiMenuIds }] : []) }
}
