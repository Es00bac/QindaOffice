// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Ai as Ai
import QindaOffice.Note

// The QindaNote window (plan §5.4): the shared OfficeWindow over a
// NoteHost, with the main strip, the ink strip (tools, colour and width
// buttons that follow the active tool, block commands, background), the
// navigator in a dock on the left, the page in a zoomable viewport with a
// zoom island, the find bar, and the dialogs the host asks for.
OfficeWindow {
    id: window

    readonly property NoteHost note: host as NoteHost
    readonly property string activeTool: pageHost.pageItem !== null ? pageHost.pageItem.tool : "pen"

    width: 1180
    height: 780

    // ---- tool strips ----
    toolBars: [
        ToolBarFromModel {
            objectName: "mainBar"
            actions: window.host.actions
            commands: ["fileNewAction", "fileOpenAction", "fileSaveAction", "|",
                    "editUndoAction", "editRedoAction", "|",
                    "editFindAction", "|", "viewZoomOutAction", "viewZoomInAction"]
        },
        ToolBarFromModel {
            objectName: "inkBar"
            compact: true
            actions: window.host.actions
            commands: ["notePenTool", "noteHighlighterTool", "noteEraserTool", "noteTextTool", "|",
                    { component: colorSlot }, { component: widthSlot }, "|",
                    "noteAddTextAction", "noteAddCheckAction", "noteAddImageAction", "|",
                    "notePageNew", "noteSectionNew", { component: backgroundSlot }]
        }
    ]

    // The colour button follows the active tool: the pen's swatches for the
    // pen, the highlighter's for the highlighter; the eraser has none.
    Component {
        id: colorSlot
        Tk.ColorButton {
            objectName: "noteColorButton"
            readonly property bool highlighter: window.activeTool === "highlighter"
            visible: window.activeTool === "pen" || highlighter
            colors: highlighter ? window.note.highlighterColors : window.note.penColors
            color: pageHost.pageItem === null ? "black"
                 : highlighter ? pageHost.pageItem.highlighterColor : pageHost.pageItem.penColor
            tooltip: highlighter ? qsTr("Highlighter colour") : qsTr("Pen colour")
            onColorSelected: function(c) {
                if (highlighter) window.note.applyHighlighterColor(c)
                else window.note.applyPenColor(c)
            }
            onCustomRequested: colorDialog.open()
        }
    }
    Component {
        id: widthSlot
        Tk.ComboBox {
            objectName: "noteWidthCombo"
            small: true
            implicitWidth: 104
            readonly property var table: window.activeTool === "highlighter" ? window.note.highlighterWidths
                                       : window.activeTool === "eraser" ? window.note.eraserSizes
                                       : window.note.penWidths
            visible: window.activeTool !== "text"
            model: table.map(function(e) { return e.name + " " + e.width })
            currentIndex: {
                if (pageHost.pageItem === null) return 0
                const value = window.activeTool === "highlighter" ? pageHost.pageItem.highlighterWidth
                            : window.activeTool === "eraser" ? pageHost.pageItem.eraserRadius
                            : pageHost.pageItem.penWidth
                for (let i = 0; i < table.length; ++i) if (Math.abs(table[i].width - value) < 1e-6) return i
                return -1
            }
            onActivated: function(index) {
                const w = table[index].width
                if (window.activeTool === "highlighter") window.note.applyHighlighterWidth(w)
                else if (window.activeTool === "eraser") window.note.applyEraserRadius(w)
                else window.note.applyPenWidth(w)
            }
        }
    }
    Component {
        id: backgroundSlot
        Tk.ComboBox {
            objectName: "noteBackgroundCombo"
            small: true
            implicitWidth: 96
            model: [qsTr("Blank"), qsTr("Ruled"), qsTr("Grid"), qsTr("Dotted")]
            currentIndex: pageHost.pageItem !== null ? Math.max(0, window.note.backgrounds.indexOf(pageHost.pageItem.background)) : 0
            onActivated: function(index) { window.note.setBackground(window.note.backgrounds[index]) }
        }
    }

    // ---- find bar ----
    findBar: FindBar {
        id: findBar
        placeholderText: qsTr("Search titles and typed text")
        onFindNext: function(text, cs) { window.note.findNext(text, cs) }
        onFindPrevious: function(text, cs) { window.note.findPrevious(text, cs) }
    }

    // ---- content: navigator dock + page ----
    Tk.DockHost {
        id: dock
        objectName: "noteDock"
        model: Tk.DockModel { storageKey: "qindanote/window" }
        workspace: "notes"
        canvas: Tk.Stack {
            NotePage {
                id: pageHost
                note: window.note
            }
            Tk.Island {
                objectName: "zoomIsland"
                Tk.Stack.top: Tk.Theme.space.sm
                Tk.Stack.centerX: true
                Tk.ZoomControl {
                    objectName: "zoomControl"
                    value: pageHost.zoom
                    from: 0.25
                    to: 4
                    onValueModified: function(v) { pageHost.setZoom(v) }
                }
                Tk.Divider { vertical: true; inset: 6 }
                Tk.IconButton { iconName: "maximize"; tooltip: qsTr("Fit width"); onClicked: pageHost.fitWidth() }
            }
        }
        Tk.DockPanel {
            panelId: "navigator"
            title: qsTr("Notebook")
            iconName: "list-tree"
            zone: "left"
            extent: 240
            minWidth: 160
            closable: true
            NoteNavigator {
                note: window.note
                pageMenuNode: {
                    const menus = window.host.menuTree.menus
                    for (let i = 0; i < menus.length; ++i) if (menus[i].id === "notePageMenu") return menus[i]
                    return null
                }
            }
        }
        Tk.DockPanel {
            panelId: "assistant"
            title: qsTr("Assistant")
            iconName: "sparkles"
            zone: "right"
            mode: "hidden"
            extent: 320
            minWidth: 240
            closable: true
            Ai.AssistantPanel {
                objectName: "assistantPanel"
                controller: window.note.assistant
            }
        }
        // Keeps the two toggles honest when a panel is closed from its own
        // header (the list carries panel ids).
        onHiddenPanelsChanged: {
            const hidden = dock.hiddenPanels.map(function(e) { return typeof e === "string" ? e : e.panelId })
            window.host.actions.setChecked("noteShowNavigatorAction", !hidden.includes("navigator"))
            window.host.actions.setChecked("aiAssistantAction", !hidden.includes("assistant"))
        }
    }

    // Zoom commands drive the viewport.
    Connections {
        target: window.host.actions.entry("viewZoomInAction")
        function onTriggered() { pageHost.zoomIn() }
    }
    Connections {
        target: window.host.actions.entry("viewZoomOutAction")
        function onTriggered() { pageHost.zoomOut() }
    }
    Connections {
        target: window.host.actions.entry("viewZoomResetAction")
        function onTriggered() { pageHost.setZoom(1.0) }
    }

    // ---- the host's questions ----
    Connections {
        target: window.note
        function onNavigatorToggleRequested(visible) {
            if (visible) dock.showPanel("navigator"); else dock.hidePanel("navigator")
        }
        function onAssistantToggleRequested(visible) {
            if (visible) dock.showPanel("assistant"); else dock.hidePanel("assistant")
        }
        function onFindRequested() { findBar.open() }
        function onNewSectionRequested() { sectionPrompt.text = qsTr("Section"); sectionPrompt.open() }
        function onRenamePageRequested(sectionId, pageId, title) {
            renamePrompt.sectionId = sectionId; renamePrompt.pageId = pageId
            renamePrompt.text = title; renamePrompt.open()
        }
        function onRenameSectionRequested(sectionId, name) {
            renameSectionPrompt.sectionId = sectionId; renameSectionPrompt.text = name; renameSectionPrompt.open()
        }
        function onDeletePageRequested(title) {
            deletePage.text = qsTr("Delete “%1” and its ink? This cannot be undone.").arg(title); deletePage.open()
        }
        function onDeleteSectionRequested(name, pages) {
            deleteSection.text = qsTr("Delete “%1” and its %n page(s)? This cannot be undone.", "", pages).arg(name); deleteSection.open()
        }
        function onReplaceRequested() { replaceDialog.open() }
        function onExportPdfRequested(suggestedPath) { exportDialog.selectedFile = "file://" + suggestedPath; exportDialog.open() }
        function onAddImageRequested() { imageDialog.open() }
        function onCustomPenColorRequested(current) { colorDialog.selectedColor = current; colorDialog.open() }
    }

    Tk.PromptDialog { id: sectionPrompt; objectName: "newSectionDialog"; title: qsTr("New section"); label: qsTr("Section name"); onAccepted: window.note.createSection(text) }
    Tk.PromptDialog {
        id: renamePrompt; objectName: "renamePageDialog"
        property string sectionId; property string pageId
        title: qsTr("Rename page"); label: qsTr("Page title")
        onAccepted: window.note.renamePage(sectionId, pageId, text)
    }
    Tk.PromptDialog {
        id: renameSectionPrompt; objectName: "renameSectionDialog"
        property string sectionId
        title: qsTr("Rename section"); label: qsTr("Section name")
        onAccepted: window.note.renameSection(sectionId, text)
    }
    Tk.MessageDialog { id: deletePage; objectName: "deletePageDialog"; title: qsTr("Delete page"); variant: "danger"; destructive: true; primaryText: qsTr("Delete"); onAccepted: window.note.deletePageConfirmed() }
    Tk.MessageDialog { id: deleteSection; objectName: "deleteSectionDialog"; title: qsTr("Delete section"); variant: "danger"; destructive: true; primaryText: qsTr("Delete"); onAccepted: window.note.deleteSectionConfirmed() }
    Tk.Dialog {
        id: replaceDialog; objectName: "replaceDialog"
        title: qsTr("Replace"); primaryText: qsTr("Replace All")
        primaryEnabled: findField.text.length > 0
        onOpened: findField.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: window.note.replaceAll(findField.text, replaceField.text)
        Tk.Flex {
            direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Find"); Tk.TextField { id: findField; objectName: "replaceFind" } }
            Tk.PropertyRow { label: qsTr("Replace with"); Tk.TextField { id: replaceField; objectName: "replaceWith" } }
        }
    }
    Dialogs.FileDialog {
        id: exportDialog; title: qsTr("Export PDF"); fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: window.note.exportPdfTo(selectedFile)
    }
    Dialogs.FileDialog {
        id: imageDialog; title: qsTr("Insert Image"); fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"), qsTr("All files (*)")]
        onAccepted: window.note.addImageFile(selectedFile)
    }
    Dialogs.ColorDialog {
        id: colorDialog; title: qsTr("Pen colour")
        onAccepted: window.note.applyCustomPenColor(selectedColor)
    }

    Component.onCompleted: window.note.preferencesPageUrl = Qt.resolvedUrl("NotePreferencesPage.qml")
}
