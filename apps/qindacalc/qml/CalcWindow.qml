// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Ai as Ai
import QindaOffice.Calc

// The QindaCalc window (plan §5.2): the shared OfficeWindow over a
// CalcHost, with the main strip and the format strip (font, size, bold /
// italic / underline, colours, alignment, number format, then the
// structural and rule commands), the formula bar with its name box, the
// grid, the sheet tabs, the find/replace bar, the assistant dock and the
// dialogs the host asks for.
OfficeWindow {
    id: window

    readonly property CalcHost calc: host as CalcHost

    width: 1180
    height: 780

    function menuNode(id) {
        const menus = window.host.menuTree.menus
        for (let i = 0; i < menus.length; ++i) if (menus[i].id === id) return menus[i]
        return null
    }

    toolBars: [
        ToolBarFromModel {
            objectName: "mainBar"
            actions: window.host.actions
            commands: ["fileNewAction", "fileOpenAction", "fileSaveAction", "|",
                       "editUndoAction", "editRedoAction", "|",
                       "editCutAction", "editCopyAction", "editPasteAction", "|",
                       "editFindAction", "|", "viewZoomOutAction", "viewZoomInAction", "|",
                       "insertChartAction", "insertSheetAction"]
        },
        ToolBarFromModel {
            objectName: "formatBar"
            compact: true
            actions: window.host.actions
            commands: [{ component: fontSlot }, { component: sizeSlot }, "|",
                       "formatBoldAction", "formatItalicAction", "formatUnderlineAction",
                       { component: textColorSlot }, { component: fillColorSlot }, "|",
                       "formatAlignLeftAction", "formatAlignCenterAction", "formatAlignRightAction", "|",
                       { component: numberFormatSlot }, "|",
                       "calcAutoSumAction", "calcSortAscendingAction", "calcSortDescendingAction",
                       "calcMergeCellsAction", "calcBordersAllAction", "calcWrapTextAction",
                       "calcInsertRowsAction", "calcDeleteRowsAction", "calcAutoFilterAction",
                       "calcCondFormatAction", "calcCommentAction"]
        }
    ]

    Component {
        id: fontSlot
        Tk.ComboBox {
            objectName: "cellFontFamilyCombo"
            small: true
            implicitWidth: 160
            model: window.calc.fontFamilies
            currentIndex: Math.max(0, window.calc.fontFamilies.indexOf(window.calc.cellFontFamily))
            onActivated: function(index) { window.calc.applyFontFamily(window.calc.fontFamilies[index]) }
        }
    }
    Component {
        id: sizeSlot
        Tk.ComboBox {
            objectName: "cellFontSizeCombo"
            small: true
            editable: true
            implicitWidth: 64
            model: window.calc.fontSizes
            currentIndex: window.calc.fontSizes.indexOf(window.calc.cellFontSize)
            onActivated: function(index) { window.calc.applyFontSize(window.calc.fontSizes[index]) }
            onAccepted: window.calc.applyFontSize(editText)
        }
    }
    Component {
        id: textColorSlot
        Tk.ColorButton {
            objectName: "cellTextColorButton"
            small: true
            tooltip: qsTr("Text colour")
            color: Tk.Theme.color.text
            colors: ["#202028", "#7f7f7f", "#1c62d4", "#d21c1c", "#1f9d3a", "#f28c19", "#7d3ac1", "#8d5a2b", "#ffffff"]
            onColorSelected: function(c) { window.calc.applyTextColor(c) }
            onCustomRequested: { colorDialog.fill = false; colorDialog.open() }
        }
    }
    Component {
        id: fillColorSlot
        Tk.ColorButton {
            objectName: "cellFillColorButton"
            small: true
            tooltip: qsTr("Fill colour")
            color: "#fff3b0"
            colors: ["#ffffff", "#fff3b0", "#d9f2d0", "#d6e6ff", "#ffd9d9", "#ece0ff", "#e8e8ec", "#c8c8cc", "#202028"]
            onColorSelected: function(c) { window.calc.applyFillColor(c) }
            onCustomRequested: { colorDialog.fill = true; colorDialog.open() }
        }
    }
    Component {
        id: numberFormatSlot
        Tk.ComboBox {
            objectName: "cellNumberFormatCombo"
            small: true
            implicitWidth: 150
            model: window.calc.numberFormats.map(function(e) { return e.label })
            currentIndex: window.calc.cellNumberFormatIndex
            onActivated: function(index) { window.calc.applyNumberFormat(index) }
        }
    }

    findBar: FindBar {
        id: findBar
        replaceEnabled: true
        placeholderText: qsTr("Search inputs and values")
        onFindNext: function(text, cs) { window.calc.findNext(text, cs) }
        onFindPrevious: function(text, cs) { window.calc.findPrevious(text, cs) }
        onReplaceOne: function(text, replacement, cs) { window.calc.replaceCurrent(text, replacement, cs) }
        onReplaceAll: function(text, replacement, cs) { window.calc.replaceAll(text, replacement, cs) }
        onClosed: gridHost.grid.forceActiveFocus()
    }

    // ---- content: formula bar, grid, sheet tabs; assistant in a dock ----
    Tk.DockHost {
        id: dock
        objectName: "calcDock"
        model: Tk.DockModel { storageKey: "qindacalc/window" }
        workspace: "calc"
        canvas: Tk.Flex {
            direction: Tk.Flex.Column
            gap: 0
            Tk.Box {
                objectName: "formulaBar"
                color: Tk.Theme.color.surface
                borderBottom: Tk.Theme.size.border
                borderColor: Tk.Theme.color.divider
                padding: Tk.Theme.space.xs
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.xs
                    align: Tk.Flex.Center
                    Tk.TextField {
                        id: nameBox
                        objectName: "nameBox"
                        small: true
                        implicitWidth: 88
                        text: window.calc.cursorText
                        tooltip: qsTr("Go to a cell or named range, or name the selection")
                        onAccepted: { window.calc.commitNameBox(text); gridHost.grid.forceActiveFocus() }
                        Keys.onEscapePressed: { text = window.calc.cursorText; gridHost.grid.forceActiveFocus() }
                        Connections { target: window.calc; function onCursorChanged() { if (!nameBox.activeFocus) nameBox.text = window.calc.cursorText } }
                    }
                    Tk.Mono { text: "fx"; Tk.Flex.shrink: 0 }
                    Tk.TextField {
                        id: formulaField
                        objectName: "formulaField"
                        small: true
                        mono: true
                        Tk.Flex.grow: 1
                        text: window.calc.formulaText
                        onAccepted: { window.calc.commitFormulaBar(text); gridHost.grid.forceActiveFocus() }
                        Keys.onEscapePressed: { text = window.calc.formulaText; gridHost.grid.forceActiveFocus() }
                        Connections { target: window.calc; function onCursorChanged() { if (!formulaField.activeFocus) formulaField.text = window.calc.formulaText } }
                    }
                }
            }
            CalcGrid {
                id: gridHost
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minHeight: 0
                host: window.calc
                contextMenuNode: window.contextNode
            }
            CalcSheetTabs {
                host: window.calc
                sheetMenuNode: window.menuNode("calcSheetMenu")
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
            Ai.AssistantPanel { objectName: "assistantPanel"; controller: window.calc.assistant }
        }
        onHiddenPanelsChanged: {
            const hidden = dock.hiddenPanels.map(function(e) { return typeof e === "string" ? e : e.panelId })
            window.host.actions.setChecked("aiAssistantAction", !hidden.includes("assistant"))
        }
    }

    // The grid's context menu: the Edit commands first, then structure
    // and rules — the same entries as the menu bar (rule 5: one command,
    // one entry).
    readonly property var contextNode: ({
        kind: "menu", id: "calcGridContextMenu", title: "",
        children: [
            { kind: "action", id: "editCutAction" }, { kind: "action", id: "editCopyAction" },
            { kind: "action", id: "editPasteAction" }, { kind: "action", id: "calcClearContentsAction" },
            { kind: "separator" },
            { kind: "action", id: "calcInsertRowsAction" }, { kind: "action", id: "calcInsertColumnsAction" },
            { kind: "action", id: "calcDeleteRowsAction" }, { kind: "action", id: "calcDeleteColumnsAction" },
            { kind: "separator" },
            { kind: "action", id: "calcMergeCellsAction" }, { kind: "action", id: "calcUnmergeCellsAction" },
            { kind: "separator" },
            { kind: "action", id: "calcSortAscendingAction" }, { kind: "action", id: "calcSortDescendingAction" },
            { kind: "separator" },
            { kind: "action", id: "calcRowHeightAction" }, { kind: "action", id: "calcColumnWidthAction" },
            { kind: "separator" },
            { kind: "action", id: "calcCommentAction" }, { kind: "action", id: "calcDeleteCommentAction" },
            { kind: "separator" },
            { kind: "action", id: "calcCondFormatAction" }, { kind: "action", id: "calcValidationAction" }
        ]
    })

    // Find Next / Previous re-open the bar and step from there.
    Connections {
        target: window.host.actions.entry("editFindNextAction")
        function onTriggered() { if (!findBar.visible) findBar.open(); else window.calc.findNext(findBar.text, findBar.caseSensitive) }
    }
    Connections {
        target: window.host.actions.entry("editFindPreviousAction")
        function onTriggered() { if (!findBar.visible) findBar.open(); else window.calc.findPrevious(findBar.text, findBar.caseSensitive) }
    }
    Connections {
        target: window.host.actions.entry("formatTextColorAction")
        function onTriggered() { colorDialog.fill = false; colorDialog.open() }
    }
    Connections {
        target: window.host.actions.entry("calcFillColorAction")
        function onTriggered() { colorDialog.fill = true; colorDialog.open() }
    }

    // ---- the host's questions ----
    Connections {
        target: window.calc
        function onFindRequested(replace) { if (replace) findBar.openReplace(); else findBar.open() }
        function onAssistantToggleRequested(visible) { if (visible) dock.showPanel("assistant"); else dock.hidePanel("assistant") }
        function onRowHeightRequested(current) { sizeDialog.forRows = true; sizeDialog.value = current; sizeDialog.open() }
        function onColumnWidthRequested(current) { sizeDialog.forRows = false; sizeDialog.value = current; sizeDialog.open() }
        function onRenameSheetRequested(index, name) { renameSheet.sheetIndex = index; renameSheet.text = name; renameSheet.open() }
        function onDeleteSheetRequested(index, name) {
            deleteSheet.sheetIndex = index
            deleteSheet.text = qsTr("Delete “%1” and everything on it? Formulas that name it will break.").arg(name)
            deleteSheet.open()
        }
        function onCondFormatRequested() { condFormat.load(); condFormat.open() }
        function onValidationRequested() { validation.load(); validation.open() }
        function onCommentRequested(cell, text) { commentDialog.cell = cell; commentDialog.text = text; commentDialog.open() }
        function onFilterMenuRequested(column, values, allowed, x, y) { filterPopover.show(column, values, allowed, gridHost.grid.mapToItem(null, x, y)) }
        function onChartDialogRequested(initialRange) { chartDialog.rangeText = initialRange; chartDialog.open() }
        function onTemplateConsentRequested(path) { templateConsent.path = path; templateConsent.open() }
    }

    SizeDialog { id: sizeDialog; onValueApplied: function(v) { if (forRows) window.calc.applyRowHeight(v); else window.calc.applyColumnWidth(v) } }
    Tk.PromptDialog {
        id: renameSheet; objectName: "renameSheetDialog"
        property int sheetIndex: -1
        title: qsTr("Rename sheet"); label: qsTr("Name")
        onAccepted: window.calc.renameSheet(sheetIndex, text)
    }
    Tk.MessageDialog {
        id: deleteSheet; objectName: "deleteSheetDialog"
        property int sheetIndex: -1
        title: qsTr("Delete sheet"); variant: "danger"; destructive: true; primaryText: qsTr("Delete")
        onAccepted: window.calc.deleteSheetConfirmed(sheetIndex)
    }
    CondFormatDialog { id: condFormat; host: window.calc }
    ValidationDialog { id: validation; host: window.calc }
    CommentDialog { id: commentDialog; onCommentApplied: function(text) { window.calc.setCommentText(text) } }
    FilterPopover { id: filterPopover; onFilterApplied: function(column, kept) { window.calc.applyFilterValues(column, kept) } }
    ChartDialog { id: chartDialog; host: window.calc }
    ChartWindows { host: window.calc; parentWindow: window }
    CloseConsentDialog {
        id: templateConsent; objectName: "templateConsentDialog"
        property string path: ""
        title: qsTr("Unsaved changes")
        text: qsTr("Loading the template discards unsaved changes to the current workbook. Save them first?")
        onAccepted: window.calc.loadTemplateResolved(templateConsent.path, "save")
        onTertiary: window.calc.loadTemplateResolved(templateConsent.path, "discard")
        onRejected: window.calc.loadTemplateResolved(templateConsent.path, "cancel")
    }
    Dialogs.ColorDialog {
        id: colorDialog
        property bool fill: false
        title: fill ? qsTr("Fill colour") : qsTr("Text colour")
        onAccepted: { if (fill) window.calc.applyFillColor(selectedColor); else window.calc.applyTextColor(selectedColor) }
    }
}
