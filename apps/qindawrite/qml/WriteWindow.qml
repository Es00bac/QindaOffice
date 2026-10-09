// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Ai as Ai
import QindaOffice.Write

// The QindaWrite window (plan §5.1): the shared OfficeWindow over a
// WriteHost, with the ribbon (WriteRibbonLayout.qml: font, size, character
// styles, colours, alignment, lists, styles, inserts, references, review,
// the contextual Table tab), the ruler above the
// page, the pages in a zoomable viewport, the find/replace bar, the
// assistant dock and the dialogs the host asks for.
OfficeWindow {
    id: window

    readonly property WriteHost write: host as WriteHost

    width: 1180
    height: 780

    // The ribbon (D-041): WriteRibbonLayout.qml; Table appears while the
    // cursor is in a table (the table commands are enabled exactly then).
    readonly property WriteRibbonLayout ribbonLayout: WriteRibbonLayout {
        fontSlot: fontSlot
        sizeSlot: sizeSlot
        textColorSlot: textColorSlot
        highlightSlot: highlightSlot
        shadingSlot: shadingSlot
        penStyleSlot: penStyleSlot
        penWidthSlot: penWidthSlot
        penColorSlot: penColorSlot
    }
    ribbonTabs: ribbonLayout.tabs
    ribbonContexts: tableContext.enabled ? ["table"] : []
    // Enabled exactly while the cursor is in a table (table_layout.h).
    readonly property ActionEntry tableContext: host.actions.entry("writeTablePropertiesAction")
    // Word's formatting (M70, D-079): the border pen, the dialogs' hosts.
    readonly property QtObject borders: window.write.borders
    readonly property QtObject tableLayout: window.write.tableLayout

    Component {
        id: fontSlot
        Tk.ComboBox {
            objectName: "fontFamilyCombo"
            small: true
            implicitWidth: 170
            model: window.write.fontFamilies
            currentIndex: Math.max(0, window.write.fontFamilies.indexOf(window.write.fontFamily))
            onActivated: function(index) { window.write.applyFontFamily(window.write.fontFamilies[index]); pageHost.pageItem.forceActiveFocus() }
        }
    }
    Component {
        id: sizeSlot
        Tk.ComboBox {
            objectName: "fontSizeCombo"
            small: true
            editable: true
            implicitWidth: 64
            model: window.write.fontSizes
            currentIndex: window.write.fontSizes.indexOf(window.write.fontSize)
            onActivated: function(index) { window.write.applyFontSize(window.write.fontSizes[index]); pageHost.pageItem.forceActiveFocus() }
            onAccepted: { window.write.applyFontSize(editText); pageHost.pageItem.forceActiveFocus() }
        }
    }
    Component {
        id: textColorSlot
        Tk.ColorButton {
            objectName: "textColorButton"
            small: true
            tooltip: qsTr("Text colour")
            color: "#202028"
            colors: ["#202028", "#7f7f7f", "#1c62d4", "#d21c1c", "#1f9d3a", "#f28c19", "#7d3ac1", "#8d5a2b", "#ffffff"]
            onColorSelected: function(c) { window.write.applyTextColor(c) }
            onCustomRequested: { colorDialog.target = "text"; colorDialog.open() }
        }
    }
    Component {
        id: highlightSlot
        Tk.ColorButton {
            objectName: "highlightButton"
            small: true
            tooltip: qsTr("Highlight colour")
            color: "#fff176"
            colors: ["#fff176", "#b9f6ca", "#b3e5fc", "#ffcdd2", "#e1bee7", "#ffe0b2", "#e0e0e0", "#ffffff"]
            onColorSelected: function(c) { window.write.applyHighlightColor(c) }
            onCustomRequested: { colorDialog.target = "highlight"; colorDialog.open() }
        }
    }
    Component {
        id: shadingSlot
        Tk.ColorButton {
            objectName: "shadingButton"
            small: true
            tooltip: qsTr("Shading")
            color: window.borders.shadingColor
            colors: ["#d9e2f3", "#deeaf6", "#e2efd9", "#fff2cc", "#fbe4d5", "#ededed", "#1f4e79", "#c00000", "#ffffff"]
            onColorSelected: function(c) { window.borders.applyShading(c) }
            onCustomRequested: { colorDialog.target = "shading"; colorDialog.open() }
        }
    }
    Component {
        id: penStyleSlot
        Tk.ComboBox {
            objectName: "penStyleCombo"
            small: true
            implicitWidth: 120
            tooltip: qsTr("Line style")
            model: window.borders.penStyles
            currentIndex: window.borders.penStyleIndex
            onActivated: function(index) { window.borders.penStyleIndex = index }
        }
    }
    Component {
        id: penWidthSlot
        Tk.ComboBox {
            objectName: "penWidthCombo"
            small: true
            implicitWidth: 76
            tooltip: qsTr("Line weight")
            model: window.borders.penSizes
            currentIndex: window.borders.penSizeIndex
            onActivated: function(index) { window.borders.penSizeIndex = index }
        }
    }
    Component {
        id: penColorSlot
        Tk.ColorButton {
            objectName: "penColorButton"
            small: true
            tooltip: qsTr("Pen colour")
            color: window.borders.penColor.valid ? window.borders.penColor : "#000000"
            colors: ["#000000", "#7f7f7f", "#1f4e79", "#2f5496", "#4472c4", "#c00000", "#ed7d31", "#70ad47", "#ffffff"]
            onColorSelected: function(c) { window.borders.penColor = c }
            onCustomRequested: { colorDialog.target = "pen"; colorDialog.open() }
        }
    }

    findBar: FindBar {
        id: findBar
        replaceEnabled: true
        placeholderText: qsTr("Search the document")
        onFindNext: function(text, cs) { window.write.findNext(text, cs) }
        onFindPrevious: function(text, cs) { window.write.findPrevious(text, cs) }
        onReplaceOne: function(text, replacement, cs) { window.write.replaceCurrent(text, replacement, cs) }
        onReplaceAll: function(text, replacement, cs) { window.write.replaceAll(text, replacement, cs) }
        onClosed: pageHost.pageItem.forceActiveFocus()
    }

    // ---- content: ruler over the pages; the assistant in a dock ----
    Tk.DockHost {
        id: dock
        objectName: "writeDock"
        model: Tk.DockModel { storageKey: "qindawrite/window" }
        workspace: "write"
        canvas: Tk.Flex {
            direction: Tk.Flex.Column
            gap: 0
            WriteRuler {
                write: window.write
                page: pageHost
            }
            WritePage {
                id: pageHost
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minHeight: 0
                write: window.write
            }
        }
        Tk.DockPanel {
            panelId: "comments"
            title: qsTr("Comments")
            iconName: "message-square"
            zone: "right"
            mode: "hidden"
            extent: 300
            minWidth: 220
            CommentsPanel { id: commentsPanel; objectName: "commentsPanel"; comments: window.write.comments }
        }
        Tk.DockPanel {
            panelId: "assistant"
            title: qsTr("Assistant")
            iconName: "sparkles"
            zone: "right"
            mode: "hidden"
            extent: 320
            minWidth: 240
            Ai.AssistantPanel { objectName: "assistantPanel"; controller: window.write.assistant }
        }
        onHiddenPanelsChanged: {
            const hidden = dock.hiddenPanels.map(function(e) { return typeof e === "string" ? e : e.panelId })
            window.host.actions.setChecked("aiAssistantAction", !hidden.includes("assistant"))
            window.host.actions.setChecked("writeShowCommentsAction", !hidden.includes("comments"))
        }
    }

    // Zoom commands drive the viewport; the preference sets the start.
    Connections { target: window.host.actions.entry("viewZoomInAction"); function onTriggered() { pageHost.zoomIn() } }
    Connections { target: window.host.actions.entry("viewZoomOutAction"); function onTriggered() { pageHost.zoomOut() } }
    Connections { target: window.host.actions.entry("viewZoomResetAction"); function onTriggered() { pageHost.setZoom(1.0) } }
    Connections {
        target: window.host.actions.entry("editFindNextAction")
        function onTriggered() { if (!findBar.visible) findBar.open(); else window.write.findNext(findBar.text, findBar.caseSensitive) }
    }
    Connections {
        target: window.host.actions.entry("editFindPreviousAction")
        function onTriggered() { if (!findBar.visible) findBar.open(); else window.write.findPrevious(findBar.text, findBar.caseSensitive) }
    }

    // ---- the host's questions ----
    Connections {
        target: window.write
        function onZoomRequested(zoom) { pageHost.setZoom(zoom) }
        function onFindRequested(replace) { if (replace) findBar.openReplace(); else findBar.open() }
        function onAssistantToggleRequested(visible) { if (visible === dock.hiddenPanels.includes("assistant")) { if (visible) dock.showPanel("assistant"); else dock.hidePanel("assistant") } }
        function onTextColorRequested(current) { colorDialog.target = "text"; colorDialog.selectedColor = current; colorDialog.open() }
        function onHighlightRequested() { colorDialog.target = "highlight"; colorDialog.open() }
        function onTableDialogRequested() { tableDialog.columnsMode = false; tableDialog.open() }
        function onColumnsDialogRequested() { tableDialog.columnsMode = true; tableDialog.open() }
        function onImageDialogRequested() { imageDialog.open() }
        function onSymbolRequested() { symbolDialog.open() }
        function onHyperlinkRequested(selected) { hyperlinkDialog.selected = selected; hyperlinkDialog.open() }
        function onWordCountRequested(counts) {
            wordCount.text = (counts.selectionOnly ? qsTr("Selection") : qsTr("Whole document"))
                + "\n\n" + qsTr("Words: %1\nCharacters: %2\nCharacters (no spaces): %3\nParagraphs: %4")
                    .arg(counts.words).arg(counts.characters).arg(counts.charactersNoSpaces).arg(counts.paragraphs)
            wordCount.open()
        }
        function onFootnoteRequested(id, text) { footnoteDialog.noteId = id; footnoteDialog.text = text; footnoteDialog.open() }
        function onPageSetupRequested() { pageSetup.load(window.write.pageSetup); pageSetup.open() }
        function onCommentsPaneToggleRequested(visible, compose) {
            const hidden = dock.hiddenPanels.map(function(e) { return typeof e === "string" ? e : e.panelId }).includes("comments")
            if (visible && hidden) dock.showPanel("comments")
            else if (!visible && !hidden) dock.hidePanel("comments")
            if (visible && compose) commentsPanel.compose()
        }
        function onPictureDialogRequested() {
            const info = window.write.pictureInfo()
            if (info.width !== undefined) { pictureDialog.load(info); pictureDialog.open() }
        }
        function onExportPdfRequested(suggested) { exportDialog.selectedFile = "file://" + suggested; exportDialog.open() }
        function onTemplateConsentRequested(path) { templateConsent.path = path; templateConsent.open() }
        function onProposalRequested(original, proposal) { proposalDialog.original = original; proposalDialog.proposal = proposal; proposalDialog.open() }
    }

    // ---- Word's formatting dialogs (M70, D-079) ----
    Connections {
        target: window.write.paragraphFormat
        function onParagraphDialogRequested() { paragraphDialog.load(target.paragraphInfo()); paragraphDialog.open() }
        function onTabsDialogRequested() { tabsDialog.load(target.tabsInfo()); tabsDialog.open() }
    }
    Connections {
        target: window.write.fontFormat
        function onFontDialogRequested() {
            fontDialog.families = window.write.fontFamilies
            fontDialog.sizes = window.write.fontSizes
            fontDialog.load(target.fontInfo())
            fontDialog.open()
        }
    }
    Connections {
        target: window.borders
        function onDialogRequested() { bordersDialog.load(target.dialogInfo()); bordersDialog.open() }
    }
    Connections {
        target: window.tableLayout
        function onPropertiesDialogRequested() { tableProperties.load(target.propertiesInfo()); tableProperties.open() }
        function onSortDialogRequested() { sortDialog.load(target.sortInfo()); sortDialog.open() }
        function onTextToTableDialogRequested() { textToTable.load(target.textToTableInfo("tab")); textToTable.open() }
    }
    ParagraphDialog {
        id: paragraphDialog
        onParagraphApplied: function(answer) { window.write.paragraphFormat.applyParagraph(answer) }
        onTabsRequested: { tabsDialog.load(window.write.paragraphFormat.tabsInfo()); tabsDialog.open() }
    }
    TabsDialog { id: tabsDialog; onTabsApplied: function(answer) { window.write.paragraphFormat.applyTabs(answer) } }
    FontDialog {
        id: fontDialog
        onFontApplied: function(answer) { window.write.fontFormat.applyFont(answer) }
        onCustomColorRequested: function(what) { colorDialog.target = "font-" + what; colorDialog.open() }
    }
    BordersShadingDialog {
        id: bordersDialog
        borders: window.borders
        onBordersApplied: function(answer) { window.borders.applyDialog(answer) }
        onCustomColorRequested: function(what) { colorDialog.target = "borders-" + what; colorDialog.open() }
    }
    TablePropertiesDialog { id: tableProperties; onPropertiesApplied: function(answer) { window.tableLayout.applyProperties(answer) } }
    SortDialog { id: sortDialog; onSortApplied: function(answer) { window.tableLayout.applySort(answer) } }
    TextToTableDialog {
        id: textToTable
        onSeparatorPicked: function(separator) { textToTable.columns = window.tableLayout.textToTableInfo(separator).columns }
        onConvertApplied: function(answer) { window.tableLayout.applyTextToTable(answer) }
    }
    TableDialog {
        id: tableDialog
        onTableApplied: function(rows, columns) { window.write.insertTable(rows, columns) }
        onColumnsApplied: function(columns, gutter) { window.write.insertColumns(columns, gutter) }
    }
    SymbolDialog { id: symbolDialog; symbols: window.write.symbols; onSymbolPicked: function(text) { window.write.insertSymbol(text) } }
    HyperlinkDialog { id: hyperlinkDialog; onLinkApplied: function(url, label) { window.write.insertHyperlink(url, label) } }
    FootnoteDialog { id: footnoteDialog; onFootnoteApplied: function(id, text) { window.write.commitFootnote(id, text) } }
    PageSetupDialog {
        id: pageSetup
        onSetupApplied: function(setup) { window.write.applyPageSetup(setup) }
    }
    PictureDialog { id: pictureDialog; onPictureApplied: function(picture) { window.write.applyPicture(picture) } }
    Tk.MessageDialog { id: wordCount; objectName: "wordCountDialog"; title: qsTr("Word Count"); variant: "info"; primaryText: qsTr("OK"); secondaryText: "" }
    Ai.ProposalDialog {
        id: proposalDialog
        onProposalApplied: function(text) { window.write.applyProposal(text) }
        onRegenerate: window.write.regenerateProposal()
        onRejected: window.write.discardProposal()
    }
    CloseConsentDialog {
        id: templateConsent; objectName: "templateConsentDialog"
        property string path: ""
        title: qsTr("Unsaved changes")
        text: qsTr("Loading the template discards unsaved changes to the current document. Save them first?")
        onAccepted: window.write.loadTemplateResolved(templateConsent.path, "save")
        onTertiary: window.write.loadTemplateResolved(templateConsent.path, "discard")
        onRejected: window.write.loadTemplateResolved(templateConsent.path, "cancel")
    }
    Dialogs.FileDialog {
        id: imageDialog; title: qsTr("Insert Image"); fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.svg *.webp)"), qsTr("All files (*)")]
        onAccepted: window.write.insertImageFile(selectedFile)
    }
    Dialogs.FileDialog {
        id: exportDialog; title: qsTr("Export PDF"); fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: window.write.exportPdfTo(selectedFile)
    }
    // One colour picker for every "Custom…": `target` says whose colour it is.
    Dialogs.ColorDialog {
        id: colorDialog
        property string target: "text"
        title: target === "highlight" ? qsTr("Highlight colour") : target === "text" ? qsTr("Text colour") : qsTr("Colour")
        onAccepted: {
            switch (target) {
            case "highlight": window.write.applyHighlightColor(selectedColor); break
            case "shading": window.borders.applyShading(selectedColor); break
            case "pen": window.borders.penColor = selectedColor; break
            case "borders-border": bordersDialog.penColor = selectedColor; bordersDialog.penAuto = false; break
            case "borders-fill": bordersDialog.fill = String(selectedColor); bordersDialog.fillChanged = true; break
            case "font-font": fontDialog.color = String(selectedColor); fontDialog.mark("color"); break
            case "font-underline": fontDialog.underlineColor = String(selectedColor); fontDialog.mark("underlineColor"); break
            default: window.write.applyTextColor(selectedColor)
            }
        }
    }
    Component.onCompleted: window.write.preferencesPageUrl = Qt.resolvedUrl("WritePreferencesPage.qml")
}
