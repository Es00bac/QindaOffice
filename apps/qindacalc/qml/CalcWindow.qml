// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Ai as Ai
import QindaOffice.Calc

// The QindaCalc window (plan §5.2): the shared OfficeWindow over a
// CalcHost, with the ribbon (CalcRibbonLayout.qml: font, size, borders,
// colours, alignment, number format, cells, editing, charts, data tools,
// sheets), the formula bar with its name box, the grid, the sheet tabs, the
// find/replace bar, the assistant dock and the dialogs the host asks for —
// since M74 (D-083) also the PivotTable Fields pane, which shows while the
// cursor is in a PivotTable and Field List is on, and the pivot dialogs;
// since M78 (D-087) the Page Layout, what-if and data-tool dialogs
// (CalcToolDialogs.qml).
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
    function panelHidden(panelId) {
        return dock.hiddenPanels.some(function(e) { return (typeof e === "string" ? e : e.panelId) === panelId })
    }
    // The PivotTable Fields pane follows the cursor, as Excel's does: shown
    // in a PivotTable (a read-only one shows its note) while Field List is
    // on, hidden elsewhere.
    function syncPivotPane() {
        const pane = window.calc.pivotPane
        const fieldList = window.host.actions.entry("calcPivotFieldListAction")
        const show = (pane.active === true || pane.readOnly === true) && fieldList !== null && fieldList.checked
        if (show && window.panelHidden("pivotFields")) dock.showPanel("pivotFields")
        else if (!show && !window.panelHidden("pivotFields")) dock.hidePanel("pivotFields")
    }

    // The ribbon (D-041): CalcRibbonLayout.qml over this window's slots.
    readonly property CalcRibbonLayout ribbonLayout: CalcRibbonLayout {
        host: window.calc
        fontSlot: fontSlot
        sizeSlot: sizeSlot
        textColorSlot: textColorSlot
        fillColorSlot: fillColorSlot
        numberFormatSlot: numberFormatSlot
    }
    ribbonTabs: ribbonLayout.tabs

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
                        // M89 (D-097): Formula AutoComplete — the functions
                        // (with their arguments), names, tables and columns
                        // for the name at the cursor (CalcHost
                        // formulaCompletions); a choice replaces that name.
                        property var completion: ({ from: -1, to: -1, entries: [] })
                        function complete() { completion = window.calc.formulaCompletions(text, cursorPosition) }
                        small: true
                        mono: true
                        Tk.Flex.grow: 1
                        text: window.calc.formulaText
                        suggestions: activeFocus ? completion.entries : []
                        onTextEdited: complete()
                        onCursorPositionChanged: if (activeFocus) complete()
                        onActiveFocusChanged: if (!activeFocus) completion = ({ from: -1, to: -1, entries: [] })
                        onSuggestionChosen: function(entry) {
                            const from = completion.from
                            if (from < 0) return
                            text = text.slice(0, from) + entry.insert + text.slice(completion.to)
                            cursorPosition = from + String(entry.insert).length
                            // A chosen name may still match itself. Leave
                            // the list closed until the next edit so Escape
                            // cancels the formula rather than dismissing that
                            // same completion for a second time.
                            completion = ({ from: -1, to: -1, entries: [] })
                            closeSuggestions()
                        }
                        onAccepted: { window.calc.commitFormulaBar(text); gridHost.grid.forceActiveFocus() }
                        // Ctrl+Shift+Enter: an array formula (M69, D-078).
                        Keys.onPressed: function(event) {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                    && (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)) {
                                window.calc.commitFormulaBar(gridHost.asArrayFormula(text))
                                gridHost.grid.forceActiveFocus()
                                event.accepted = true
                            }
                        }
                        // Escape closes the list first, as Excel's does; then cancels.
                        // AGENT-NOTE: QindaTK's TextField answers Escape too, and its
                        // handler runs first and has already closed the list, so the
                        // list's entries (cleared here) say whether one was showing.
                        Keys.onEscapePressed: function(event) {
                            event.accepted = true
                            if (suggestionCount > 0) {
                                completion = ({ from: -1, to: -1, entries: [] })
                                closeSuggestions()
                                return
                            }
                            text = window.calc.formulaText
                            gridHost.grid.forceActiveFocus()
                        }
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
                contextMenuNode: window.calc.pivotActive ? window.pivotContextNode : window.contextNode
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
        Tk.DockPanel {
            panelId: "pivotFields"
            title: qsTr("PivotTable Fields")
            iconName: "table-2"
            zone: "right"
            mode: "hidden"
            extent: 300
            minWidth: 240
            PivotFieldsPane { host: window.calc }
        }
        onHiddenPanelsChanged: {
            const hidden = dock.hiddenPanels.map(function(e) { return typeof e === "string" ? e : e.panelId })
            window.host.actions.setChecked("aiAssistantAction", !hidden.includes("assistant"))
            // Closing the pane inside a PivotTable turns Field List off (Excel).
            if (hidden.includes("pivotFields") && window.calc.pivotPane.active === true)
                window.host.actions.setChecked("calcPivotFieldListAction", false)
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
            { kind: "action", id: "insertHyperlinkAction" }, { kind: "action", id: "calcOpenHyperlinkAction" },
            { kind: "action", id: "calcRemoveHyperlinkAction" },
            { kind: "separator" },
            { kind: "action", id: "calcGroupAction" }, { kind: "action", id: "calcUngroupAction" },
            { kind: "action", id: "calcHideRowsAction" }, { kind: "action", id: "calcUnhideRowsAction" },
            { kind: "separator" },
            { kind: "action", id: "calcCondFormatAction" }, { kind: "action", id: "calcValidationAction" }
        ]
    })
    // Inside a PivotTable the grid's menu is Excel's PivotTable menu.
    readonly property var pivotContextNode: ({
        kind: "menu", id: "calcPivotContextMenu", title: "",
        children: [
            { kind: "action", id: "editCopyAction" },
            { kind: "separator" },
            { kind: "action", id: "calcPivotRefreshAction" },
            { kind: "separator" },
            { kind: "action", id: "calcPivotSortAscendingAction" }, { kind: "action", id: "calcPivotSortDescendingAction" },
            { kind: "action", id: "calcPivotFilterAction" }, { kind: "action", id: "calcPivotClearFilterAction" },
            { kind: "separator" },
            { kind: "action", id: "calcPivotGroupAction" }, { kind: "action", id: "calcPivotUngroupAction" },
            { kind: "separator" },
            { kind: "action", id: "calcPivotShowDetailsAction" },
            { kind: "action", id: "calcPivotValueFieldSettingsAction" }, { kind: "action", id: "calcPivotFieldSettingsAction" },
            { kind: "action", id: "calcPivotOptionsAction" }, { kind: "action", id: "calcPivotFieldListAction" }
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
        function onImageDialogRequested() { imageDialog.open() }
        function onAssistantToggleRequested(visible) { if (visible === dock.hiddenPanels.includes("assistant")) { if (visible) dock.showPanel("assistant"); else dock.hidePanel("assistant") } }
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
        function onUnhideSheetRequested(hiddenNames) { unhideSheet.hiddenNames = hiddenNames; unhideSheet.open() }
        // M66 (D-075): links and protection.
        function onLinkDialogRequested() { linkDialog.load(); linkDialog.open() }
        function onProtectSheetRequested() { protectDialog.workbook = false; protectDialog.open() }
        function onProtectWorkbookRequested() { protectDialog.workbook = true; protectDialog.open() }
        function onUnprotectRequested(workbook) { unprotectDialog.workbook = workbook; unprotectDialog.open() }
        // M74 (D-083): PivotTables.
        function onPivotChanged() { window.syncPivotPane() }
        function onPivotFieldListToggleRequested(visible) { window.syncPivotPane() }
        function onPivotCreateRequested(mode) { pivotCreate.load(mode); pivotCreate.open() }
        function onPivotValueFieldRequested(index) { pivotValueField.load(index); pivotValueField.open() }
        function onPivotFieldRequested(field) { if (field >= 0) { pivotField.load(field); pivotField.open() } }
        function onPivotFilterRequested(field) { if (field >= 0) { pivotFilter.load(field); pivotFilter.open() } }
        function onPivotGroupRequested(field) { pivotGroup.load(field); pivotGroup.open() }
        function onPivotOptionsRequested() { pivotOptions.load(); pivotOptions.open() }
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
    Tk.Dialog {
        id: unhideSheet
        objectName: "unhideSheetDialog"
        property var hiddenNames: []
        title: qsTr("Unhide sheet")
        primaryText: qsTr("Close")
        Tk.Scroll {
            implicitHeight: Math.min(160, hiddenRows.implicitHeight)
            Tk.Flex {
                id: hiddenRows
                direction: Tk.Flex.Column
                Repeater {
                    model: unhideSheet.hiddenNames
                    Tk.ListRow {
                        required property string modelData
                        objectName: "hiddenSheet_" + modelData
                        text: modelData
                        onClicked: {
                            window.calc.unhideSheets(modelData)
                            unhideSheet.hiddenNames = unhideSheet.hiddenNames.filter(n => n !== modelData)
                            if (unhideSheet.hiddenNames.length === 0) unhideSheet.close()
                        }
                    }
                }
            }
        }
    }
    CondFormatDialog { id: condFormat; host: window.calc; onRuleRequested: function(index) { tools.openRule(index) } }
    // M78 (D-087): Page Setup, what-if, data tools, rule editing.
    CalcToolDialogs { id: tools; host: window.calc; hostItem: window.contentItem; manager: condFormat }
    ValidationDialog { id: validation; host: window.calc }
    CommentDialog { id: commentDialog; onCommentApplied: function(text) { window.calc.setCommentText(text) } }
    FilterPopover { id: filterPopover; onFilterApplied: function(column, kept) { window.calc.applyFilterValues(column, kept) } }
    ChartDialog { id: chartDialog; host: window.calc }
    LinkDialog { id: linkDialog; host: window.calc }
    ProtectDialog { id: protectDialog; host: window.calc }
    UnprotectDialog { id: unprotectDialog; host: window.calc }
    ChartWindows { host: window.calc; parentWindow: window }
    CalcViewWindows { host: window.calc; parentWindow: window } // M82: View ▸ New Window
    PivotCreateDialog { id: pivotCreate; host: window.calc }
    PivotValueFieldDialog { id: pivotValueField; host: window.calc }
    PivotFieldDialog { id: pivotField; host: window.calc }
    PivotFilterDialog { id: pivotFilter; host: window.calc }
    PivotGroupDialog { id: pivotGroup; host: window.calc }
    PivotOptionsDialog { id: pivotOptions; host: window.calc }
    CloseConsentDialog {
        id: templateConsent; objectName: "templateConsentDialog"
        property string path: ""
        title: qsTr("Unsaved changes")
        text: qsTr("Loading the template discards unsaved changes to the current workbook. Save them first?")
        onAccepted: window.calc.loadTemplateResolved(templateConsent.path, "save")
        onTertiary: window.calc.loadTemplateResolved(templateConsent.path, "discard")
        onRejected: window.calc.loadTemplateResolved(templateConsent.path, "cancel")
    }
    Dialogs.FileDialog {
        id: imageDialog
        objectName: "insertImageDialog"
        title: qsTr("Insert Image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"), qsTr("All files (*)")]
        onAccepted: window.calc.insertImageChosen(selectedFile)
    }
    Dialogs.ColorDialog {
        id: colorDialog
        property bool fill: false
        title: fill ? qsTr("Fill colour") : qsTr("Text colour")
        onAccepted: { if (fill) window.calc.applyFillColor(selectedColor); else window.calc.applyTextColor(selectedColor) }
    }
}
