// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Ai as Ai
import QindaOffice.Show

// The QindaShow window (plan §5.3): the shared OfficeWindow over a
// ShowHost, with the ribbon (ShowRibbonLayout.qml; the Shape tab appears
// while shapes are selected, the Table Design / Table Layout tabs while a
// table is, the Chart tab while a chart is — M67), the slide sorter docked
// left, the slide in a page-fitting viewport with the notes pane under it
// (SlideEditor.qml, M67), the transition / layout panel docked right, the
// find bar, the assistant dock, the player and presenter windows while a
// show runs, and the dialogs the host asks for.
OfficeWindow {
    id: window

    readonly property ShowHost showHost: host as ShowHost

    width: 1180
    height: 780

    // The ribbon (D-041): ShowRibbonLayout.qml; Shape is contextual on a
    // selection (Delete Shape is enabled exactly when something is selected).
    readonly property ShowRibbonLayout ribbonLayout: ShowRibbonLayout {
        fillSlot: fillSlot
        strokeSlot: strokeSlot
        shadingSlot: shadingSlot
        penSlot: penSlot
    }
    ribbonTabs: ribbonLayout.tabs
    ribbonContexts: (shapeContext.enabled ? ["shape"] : []).concat(tableContext.enabled ? ["table"] : [])
                    .concat(chartContext.enabled ? ["chart"] : [])
    readonly property ActionEntry shapeContext: host.actions.entry("showDeleteShapeAction")
    readonly property ActionEntry tableContext: host.actions.entry("showTableInsertRowBelowAction")
    readonly property ActionEntry chartContext: host.actions.entry("showChartEditDataAction")

    Component {
        id: fillSlot
        Tk.ColorButton {
            objectName: "fillColorButton"
            small: true
            tooltip: qsTr("Fill colour")
            color: "#5b9bd5"
            colors: ["#5b9bd5", "#ed7d31", "#a5a5a5", "#ffc000", "#4472c4", "#70ad47", "#ffffff", "#202028", "#00000000"]
            onColorSelected: function(c) { window.showHost.applyFillColor(c) }
            onCustomRequested: { colorDialog.target = "fill"; colorDialog.open() }
        }
    }
    Component {
        id: shadingSlot
        Tk.ColorButton {
            objectName: "tableShadingButton"
            small: true
            tooltip: qsTr("Cell shading")
            color: "#5b9bd5"
            colors: ["#5b9bd5", "#ed7d31", "#a5a5a5", "#ffc000", "#4472c4", "#70ad47", "#ffffff", "#202028", "#00000000"]
            onColorSelected: function(c) { window.showHost.applyTableShading(c) }
            onCustomRequested: { colorDialog.target = "shading"; colorDialog.open() }
        }
    }
    Component {
        id: penSlot
        Tk.ColorButton {
            objectName: "tablePenButton"
            small: true
            tooltip: qsTr("Border colour")
            color: "#000000"
            colors: ["#000000", "#7f7f7f", "#ffffff", "#4472c4", "#ed7d31", "#70ad47", "#c00000"]
            onColorSelected: function(c) { window.showHost.setTablePenColor(c) }
            onCustomRequested: { colorDialog.target = "pen"; colorDialog.open() }
        }
    }
    Component {
        id: strokeSlot
        Tk.ColorButton {
            objectName: "strokeColorButton"
            small: true
            tooltip: qsTr("Stroke colour")
            color: "#202028"
            colors: ["#202028", "#7f7f7f", "#5b9bd5", "#ed7d31", "#70ad47", "#c00000", "#ffffff"]
            onColorSelected: function(c) { window.showHost.applyStrokeColor(c) }
            onCustomRequested: { colorDialog.target = "stroke"; colorDialog.open() }
        }
    }

    findBar: FindBar {
        id: findBar
        placeholderText: qsTr("Search titles, shapes and notes")
        onFindNext: function(text, cs) { window.showHost.findNext(text, cs) }
        onFindPrevious: function(text, cs) { window.showHost.findPrevious(text, cs) }
        onClosed: editor.canvasView.canvasItem.forceActiveFocus()
    }

    // ---- content: sorter | canvas | slide panel; assistant docks right ----
    Tk.DockHost {
        id: dock
        objectName: "showDock"
        model: Tk.DockModel { storageKey: "qindashow/window" }
        workspace: "show"
        canvas: SlideEditor {
            id: editor
            showHost: window.showHost
        }
        Tk.DockPanel {
            panelId: "slides"
            title: qsTr("Slides")
            iconName: "layout-list"
            zone: "left"
            extent: 220
            minWidth: 160
            closable: true
            SlideSorter { showHost: window.showHost }
        }
        Tk.DockPanel {
            panelId: "slide"
            title: qsTr("Slide")
            iconName: "layout-template"
            zone: "right"
            extent: 280
            minWidth: 200
            closable: true
            SlidePanel { showHost: window.showHost }
        }
        Tk.DockPanel {
            panelId: "assistant"
            title: qsTr("Assistant")
            iconName: "sparkles"
            zone: "right"
            mode: "hidden"
            extent: 320
            minWidth: 240
            Ai.AssistantPanel { objectName: "assistantPanel"; controller: window.showHost.assistant }
        }
        onHiddenPanelsChanged: {
            const hidden = dock.hiddenPanels.map(function(e) { return typeof e === "string" ? e : e.panelId })
            window.host.actions.setChecked("aiAssistantAction", !hidden.includes("assistant"))
        }
    }

    // The show: a fullscreen player on this window's screen, a presenter
    // view on another screen when one exists. Both exist only while the
    // host's player controller does.
    // Presenter View on one screen (M67) shows the presenter alone there.
    ShowPlayer {
        id: player
        showHost: window.showHost
        controller: window.showHost.player
        active: window.showHost.player !== null && !(window.showHost.presenterView && presenter.otherScreen === null)
        screen: window.screen
        onEnded: window.showHost.endShow()
    }
    PresenterView {
        id: presenter
        showHost: window.showHost
        controller: window.showHost.player
        // AGENT-GUARD (M67): screens compare by NAME — Qt.application.screens
        // holds screen-info objects and window.screen a QScreen, which never
        // compare equal, so one screen looked like two and the presenter
        // covered the player on a single monitor.
        readonly property var otherScreen: {
            for (let i = 0; i < Qt.application.screens.length; ++i) {
                if (Qt.application.screens[i].name !== window.screen.name) return Qt.application.screens[i]
            }
            return null
        }
        active: window.showHost.player !== null && (otherScreen !== null || window.showHost.presenterView)
        screen: otherScreen !== null ? otherScreen : window.screen
        onEnded: window.showHost.endShow()
    }

    Connections { target: window.host.actions.entry("viewZoomInAction"); function onTriggered() { editor.canvasView.zoomIn() } }
    Connections { target: window.host.actions.entry("viewZoomOutAction"); function onTriggered() { editor.canvasView.zoomOut() } }
    Connections { target: window.host.actions.entry("viewZoomResetAction"); function onTriggered() { editor.canvasView.fitPage() } }
    Connections {
        target: window.host.actions.entry("editFindNextAction")
        function onTriggered() { if (!findBar.visible) findBar.open(); else window.showHost.findNext(findBar.text, findBar.caseSensitive) }
    }
    Connections {
        target: window.host.actions.entry("editFindPreviousAction")
        function onTriggered() { if (!findBar.visible) findBar.open(); else window.showHost.findPrevious(findBar.text, findBar.caseSensitive) }
    }

    // ---- the host's questions ----
    Connections {
        target: window.showHost
        function onZoomRequested(zoom) { if (Math.abs(zoom - 1.0) > 1e-6) editor.canvasView.setZoom(zoom); else editor.canvasView.fitPage() }
        function onFindRequested() { findBar.open() }
        function onReplaceRequested() { replaceDialog.open() }
        function onAssistantToggleRequested(visible) { if (visible === dock.hiddenPanels.includes("assistant")) { if (visible) dock.showPanel("assistant"); else dock.hidePanel("assistant") } }
        function onFillColorRequested(current) { colorDialog.target = "fill"; colorDialog.selectedColor = current; colorDialog.open() }
        function onStrokeColorRequested(current) { colorDialog.target = "stroke"; colorDialog.selectedColor = current; colorDialog.open() }
        function onTableShadingRequested(current) { colorDialog.target = "shading"; colorDialog.selectedColor = current; colorDialog.open() }
        function onInsertTableRequested() { insertTableDialog.open() }
        function onSplitCellsRequested(columns, rows, maxColumns, maxRows) { splitCellsDialog.load(columns, rows, maxColumns, maxRows); splitCellsDialog.open() }
        function onChartDataRequested() { chartDataDialog.open() }
        function onImageDialogRequested() { imageDialog.open() }
        function onMastersRequested() { masterDialog.load(window.showHost.masters()); masterDialog.open() }
        function onApplyMasterRequested() { applyMaster.load(window.showHost.masterNames()); applyMaster.open() }
        function onExportPdfRequested(suggested) { exportDialog.selectedFile = "file://" + suggested; exportDialog.open() }
        function onGenerateDeckRequested() { generatePrompt.text = ""; generatePrompt.open() }
        function onTemplateConsentRequested(path) { templateConsent.path = path; templateConsent.open() }
        function onProposalRequested(original, proposal) { proposalDialog.original = original; proposalDialog.proposal = proposal; proposalDialog.open() }
    }

    MasterDialog { id: masterDialog; fontFamilies: window.showHost.fontFamilies; onMastersApplied: function(list) { window.showHost.applyMasters(list) } }
    ApplyMasterDialog { id: applyMaster; onMasterChosen: function(index) { window.showHost.applyMasterToSlide(index) } }
    Tk.Dialog {
        id: replaceDialog; objectName: "replaceDialog"
        title: qsTr("Replace across the deck"); primaryText: qsTr("Replace All")
        primaryEnabled: replaceFind.text.length > 0
        onOpened: replaceFind.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: window.showHost.replaceAll(replaceFind.text, replaceWith.text)
        Tk.Flex {
            direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Find what"); Tk.TextField { id: replaceFind; objectName: "replaceFind"; width: 240 } }
            Tk.PropertyRow { label: qsTr("Replace with"); Tk.TextField { id: replaceWith; objectName: "replaceWith"; width: 240 } }
        }
    }
    Tk.PromptDialog {
        id: generatePrompt; objectName: "generateDeckDialog"
        title: qsTr("Generate deck"); label: qsTr("Topic or outline"); primaryText: qsTr("Generate")
        onAccepted: window.showHost.generateDeck(text)
    }
    Ai.ProposalDialog {
        id: proposalDialog
        onProposalApplied: function(text) { window.showHost.applyProposal(text) }
        onRegenerate: window.showHost.regenerateProposal()
        onRejected: window.showHost.discardProposal()
    }
    CloseConsentDialog {
        id: templateConsent; objectName: "templateConsentDialog"
        property string path: ""
        title: qsTr("Unsaved changes")
        text: qsTr("Loading the template discards unsaved changes to the current presentation. Save them first?")
        onAccepted: window.showHost.loadTemplateResolved(templateConsent.path, "save")
        onTertiary: window.showHost.loadTemplateResolved(templateConsent.path, "discard")
        onRejected: window.showHost.loadTemplateResolved(templateConsent.path, "cancel")
    }
    Dialogs.FileDialog {
        id: imageDialog; title: qsTr("Insert image"); fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.svg *.webp)"), qsTr("All files (*)")]
        onAccepted: window.showHost.insertImageFile(selectedFile)
    }
    Dialogs.FileDialog {
        id: exportDialog; title: qsTr("Export PDF"); fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: window.showHost.exportPdfTo(selectedFile)
    }
    InsertTableDialog { id: insertTableDialog; onTableChosen: function(rows, columns) { window.showHost.insertTable(rows, columns) } }
    SplitCellsDialog { id: splitCellsDialog; onSplitChosen: function(columns, rows) { window.showHost.splitCells(columns, rows) } }
    ChartDataDialog { id: chartDataDialog; editor: window.showHost.chartData; onDataApplied: window.showHost.applyChartData() }
    Dialogs.ColorDialog {
        id: colorDialog
        property string target: "fill" // fill | stroke | shading (table cells) | pen (table borders)
        title: target === "stroke" ? qsTr("Stroke colour") : target === "shading" ? qsTr("Cell shading")
             : target === "pen" ? qsTr("Border colour") : qsTr("Fill colour")
        onAccepted: {
            if (target === "stroke") window.showHost.applyStrokeColor(selectedColor)
            else if (target === "shading") window.showHost.applyTableShading(selectedColor)
            else if (target === "pen") window.showHost.setTablePenColor(selectedColor)
            else window.showHost.applyFillColor(selectedColor)
        }
    }
}
