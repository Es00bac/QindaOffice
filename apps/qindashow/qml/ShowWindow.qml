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
// table is, the Chart tab while a chart is — M67; Slide Master in the
// Slide Master view and Playback while a medium is selected — M75), the
// slide sorter docked left (the masters and layouts in the Slide Master
// view), the slide in a page-fitting viewport with the notes pane under it
// (SlideEditor.qml, M67), the transition / layout panel docked right with
// the Comments, Format Background and Text Styles panes (M75, D-084), the
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
        themeIds: window.showHost.design.themeIds
        colorIds: window.showHost.design.colorIds
        fontIds: window.showHost.design.fontIds
        effectIds: window.showHost.design.effectIds
        newSlideIds: window.showHost.layouts.newSlideIds
        layoutIds: window.showHost.layouts.layoutIds
        customShowMenu: window.showHost.slideShow.customShowMenu
        actionButtonIds: window.showHost.actionSettings.buttonIds
    }
    ribbonTabs: ribbonLayout.tabs
    ribbonContexts: (shapeContext.enabled ? ["shape"] : []).concat(tableContext.enabled ? ["table"] : [])
                    .concat(chartContext.enabled ? ["chart"] : [])
                    .concat(window.showHost.masterView.active ? ["masterView"] : [])
                    .concat(mediaContext.enabled ? ["media"] : [])
    readonly property ActionEntry shapeContext: host.actions.entry("showDeleteShapeAction")
    readonly property ActionEntry tableContext: host.actions.entry("showTableInsertRowBelowAction")
    readonly property ActionEntry chartContext: host.actions.entry("showChartEditDataAction")
    readonly property ActionEntry mediaContext: host.actions.entry("showMediaPosterAction")
    // A dock panel shown or hidden as its toggle command asks (M75).
    function setPanel(panelId, visible) {
        const hidden = dock.hiddenPanels.map(function(e) { return typeof e === "string" ? e : e.panelId }).includes(panelId)
        if (visible && hidden) dock.showPanel(panelId)
        else if (!visible && !hidden) dock.hidePanel(panelId)
    }

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
        readonly property bool compact: width < Tk.Theme.size.dialogWidth * 1.5
        workspace: compact ? "showCompact" : "show"
        canvas: SlideEditor {
            id: editor
            showHost: window.showHost
        }
        Tk.DockPanel {
            panelId: "slides"
            title: qsTr("Slides")
            iconName: "layout-list"
            zone: dock.compact ? "top" : "left"
            group: dock.compact ? "showInspector" : ""
            extent: dock.compact ? Tk.Theme.size.control * 5 : 220
            minWidth: 160
            closable: true
            // M75: the Slide Master view shows the masters and layouts here.
            SlideSorter {
                id: sorter
                anchors.fill: parent
                visible: !window.showHost.masterView.active
                showHost: window.showHost
                onRenameSectionRequested: function(section, name) { sectionPrompt.ask(section, name) }
            }
            MasterPane {
                anchors.fill: parent
                visible: window.showHost.masterView.active
                showHost: window.showHost
            }
        }
        Tk.DockPanel {
            panelId: "slide"
            title: qsTr("Slide")
            iconName: "layout-template"
            zone: dock.compact ? "top" : "right"
            group: dock.compact ? "showInspector" : ""
            groupActive: !dock.compact
            extent: dock.compact ? Tk.Theme.size.control * 5 : 280
            minWidth: 200
            closable: true
            SlidePanel { showHost: window.showHost }
        }
        Tk.DockPanel {
            panelId: "comments"
            title: qsTr("Comments")
            iconName: "message-square"
            zone: dock.compact ? "overlay" : "right"
            mode: "hidden"
            extent: 300
            minWidth: 220
            CommentsPane { id: commentsPane; comments: window.showHost.comments }
        }
        Tk.DockPanel {
            panelId: "formatBackground"
            title: qsTr("Format Background")
            iconName: "paint-bucket"
            zone: dock.compact ? "overlay" : "right"
            mode: "hidden"
            extent: 300
            minWidth: 220
            FormatBackgroundPane { showHost: window.showHost }
        }
        Tk.DockPanel {
            panelId: "textStyles"
            title: qsTr("Text Styles")
            iconName: "type"
            zone: dock.compact ? "overlay" : "right"
            mode: "hidden"
            extent: 300
            minWidth: 220
            TextStylesPane { showHost: window.showHost }
        }
        Tk.DockPanel {
            panelId: "assistant"
            title: qsTr("Assistant")
            iconName: "sparkles"
            zone: dock.compact ? "overlay" : "right"
            mode: "hidden"
            extent: 320
            minWidth: 240
            Ai.AssistantPanel { objectName: "assistantPanel"; controller: window.showHost.assistant }
        }
        onHiddenPanelsChanged: {
            const hidden = dock.hiddenPanels.map(function(e) { return typeof e === "string" ? e : e.panelId })
            window.host.actions.setChecked("aiAssistantAction", !hidden.includes("assistant"))
            window.host.actions.setChecked("showCommentsPaneAction", !hidden.includes("comments"))
            window.host.actions.setChecked("showFormatBackgroundAction", !hidden.includes("formatBackground"))
            window.host.actions.setChecked("showMasterTextStylesAction", !hidden.includes("textStyles"))
        }
    }

    EquationDialog { session: window.showHost.equations }

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
        alone: !player.active
        screen: otherScreen !== null ? otherScreen : window.screen
        onEnded: window.showHost.endShow()
    }
    // M91 (D-099): Action, Link, Custom Shows, Set Up Slide Show, the
    // rehearsal's question, Create a Video, Export Pictures, Trim and Fade.
    readonly property SlideShowDialogs showDialogs: SlideShowDialogs { showHost: window.showHost; host: window.contentItem }

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

    // ---- M75 (D-084): design, master view, sections, comments, media ----
    Connections {
        target: window.showHost.design
        function onCustomSizeRequested(width, height) { slideSizeDialog.load(width, height); slideSizeDialog.open() }
        function onScaleChoiceRequested(width, height) { scaleChoice.size = Qt.size(width, height); scaleChoice.open() }
        function onThemeFileRequested() { themeDialog.open() }
        function onTemplateScopeRequested(url) { templateScope.url = url; templateScope.open() }
        function onBackgroundPaneRequested(visible) { window.setPanel("formatBackground", visible) }
    }
    Connections {
        target: window.showHost.masterView
        function onRenameRequested(current) { layoutPrompt.text = current; layoutPrompt.open() }
        function onTextStylesRequested(visible) { window.setPanel("textStyles", visible) }
        // PowerPoint shows the Slide Master tab while the view is open, and
        // the tab one was on again after it closes — Tk.Ribbon does that
        // itself since M85 (QindaTK D-317: a closing contextual tab returns
        // to the tab that was current before it appeared).
        function onActiveChanged() {
            if (window.showHost.masterView.active) Qt.callLater(function() { window.ribbon.activateTab("slideMaster") })
            else window.setPanel("textStyles", false)
        }
    }
    // M82 (D-091): Reuse Slides, and a template's masters for all or one slide.
    Connections {
        target: window.showHost.layouts
        // (The New Slide and Layout menus rebuild the ribbon's tabs when the
        // deck's layouts change; Tk.Ribbon keeps the current tab by id since
        // M85, QindaTK D-317, so nothing here puts it back any more.)
        function onReuseFileRequested() { reuseFileDialog.open() }
        function onReuseDialogRequested() { reuseDialog.open() }
    }
    Dialogs.FileDialog {
        id: reuseFileDialog; title: qsTr("Reuse Slides"); fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("PowerPoint Presentations (*.pptx *.pptm *.potx *.ppsx)"), qsTr("All files (*)")]
        onAccepted: window.showHost.layouts.openReuseFile(selectedFile)
    }
    ReuseSlidesDialog { id: reuseDialog; showHost: window.showHost }
    Tk.MessageDialog {
        id: templateScope; objectName: "templateScopeDialog"
        property url url
        title: qsTr("Browse for Themes"); variant: "question"
        text: qsTr("This file brings its slide masters and layouts. Apply its design to every slide, or only to the selected slide?")
        primaryText: qsTr("Apply to All Slides"); tertiaryText: qsTr("Apply to Selected Slide"); secondaryText: qsTr("Cancel")
        onAccepted: window.showHost.layouts.applyTemplate(url, true)
        onTertiary: { window.showHost.layouts.applyTemplate(url, false); close() }
    }
    Connections {
        target: window.showHost.sections
        function onRenameRequested(section, current) { sectionPrompt.ask(section, current) }
    }
    Connections {
        target: window.showHost.comments
        function onPaneRequested(visible, compose) {
            window.setPanel("comments", visible)
            if (visible && compose) commentsPane.compose()
        }
    }
    Connections {
        target: window.showHost.media
        function onFileRequested(audio) { mediaDialog.audio = audio; mediaDialog.open() }
        function onPosterRequested() { posterDialog.open() }
    }
    SlideSizeDialog { id: slideSizeDialog; onSizeChosen: function(width, height) { window.showHost.design.requestSlideSize(width, height) } }
    Tk.MessageDialog {
        id: scaleChoice; objectName: "slideScaleDialog"
        property size size: Qt.size(0, 0)
        title: qsTr("Slide Size"); variant: "question"
        text: qsTr("You are scaling to a new slide size. Do you want to maximize the size of your content, or scale it down to ensure it will fit on the new slide?")
        primaryText: qsTr("Maximize"); tertiaryText: qsTr("Ensure Fit"); secondaryText: qsTr("Cancel")
        onAccepted: window.showHost.design.applySlideSize(size.width, size.height, true)
        onTertiary: { window.showHost.design.applySlideSize(size.width, size.height, false); close() }
    }
    Tk.PromptDialog {
        id: sectionPrompt; objectName: "renameSectionDialog"
        property int section: -1
        function ask(index, current) { section = index; text = current; open() }
        title: qsTr("Rename Section"); label: qsTr("Section name"); primaryText: qsTr("Rename")
        onAccepted: window.showHost.sections.rename(section, text)
    }
    Tk.PromptDialog {
        id: layoutPrompt; objectName: "renameLayoutDialog"
        title: qsTr("Rename Layout"); label: qsTr("Layout name"); primaryText: qsTr("Rename")
        onAccepted: window.showHost.masterView.renameCurrent(text)
    }
    Dialogs.FileDialog {
        id: themeDialog; title: qsTr("Browse for Themes"); fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Office Themes and Themed Documents (*.thmx *.potx *.pptx)"), qsTr("All files (*)")]
        onAccepted: window.showHost.design.applyThemeFile(selectedFile)
    }
    Dialogs.FileDialog {
        id: mediaDialog; property bool audio: false; fileMode: Dialogs.FileDialog.OpenFile
        title: audio ? qsTr("Insert Audio") : qsTr("Insert Video")
        nameFilters: audio ? [qsTr("Audio files (*.mp3 *.m4a *.wav *.wma *.aac *.flac *.ogg *.oga *.opus *.aiff)"), qsTr("All files (*)")]
                           : [qsTr("Video files (*.mp4 *.m4v *.mov *.wmv *.avi *.mpg *.mpeg *.webm *.mkv)"), qsTr("All files (*)")]
        onAccepted: window.showHost.media.insertFile(selectedFile, audio)
    }
    Dialogs.FileDialog {
        id: posterDialog; title: qsTr("Poster Frame"); fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"), qsTr("All files (*)")]
        onAccepted: window.showHost.media.setPosterFile(selectedFile)
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
