// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell

// The suite's document window: a Tk.AppWindow over a DocumentHost. The
// host owns the commands, the menu tree and every flow; this file renders
// the menu bar and status bar from it and answers the dialogs it asks for
// (open, save as, overwrite consent, close consent, messages, about,
// preferences, print, command search). Apps derive from it: NoteWindow.qml
// is `OfficeWindow { ribbonTabs: [...]; findBar: ...; NotePage {} }`.
//
// AGENT-NOTE (D-041): the toolbar band holds ONE RibbonFromModel built from
// `ribbonTabs` (the app's ribbon layout over its ActionModel). An app may
// still assign `toolBars` itself, which replaces the ribbon.
//
// AGENT-CONTRACT (ADR-0117): width/height are set once here; nothing in
// the suite resizes a shown window.
Tk.AppWindow {
    id: window

    required property DocumentHost host
    // Optional app lifecycle gate. It calls done(true/false) when ready;
    // ordinary per-document Save/Discard/Cancel still runs afterward.
    property var beforeClose: null
    property bool closePreparationPending: false
    property bool closePrepared: false
    function resetClosePreparation() { closePrepared = false }

    title: host.windowTitle
    toolBarsVisible: host.toolBarsVisible
    statusBarVisible: host.statusBarVisible

    // The desktop's QST-1 tokens when they are published; otherwise the
    // preset from Preferences (rule 9, degrade visibly: one log line says
    // which). The bridge fails to load off a QindaQt install (no
    // QindaQt.Tokens module) and loads-but-idles when no session publishes
    // tokens; both cases are the preset.
    //
    // AGENT-GUARD: declared as a property, not a child: Tk.AppWindow seats
    // ONE unanchored content child to fill the window, and a Loader among
    // the children made the app's content a second child (0×0, unseated).
    readonly property Loader themeBridge: Loader {
        objectName: "themeBridge"
        source: "ThemeBridge.qml"
    }
    readonly property bool tokensApplied: themeBridge.status === Loader.Ready
                                          && themeBridge.item !== null && themeBridge.item.applied
    function applyChrome() {
        Tk.Density.modeName = Preferences.density
        if (!window.tokensApplied) {
            Tk.Theme.applyPreset(Preferences.themePreset)
        }
    }
    onTokensAppliedChanged: window.applyChrome()
    Component.onCompleted: {
        window.applyChrome()
        if (themeBridge.status === Loader.Error) {
            console.info("QindaOffice: no QindaQt token bridge, using the " + Preferences.themePreset + " preset")
        } else if (!window.tokensApplied) {
            console.info("QindaOffice: no QindaQt tokens published, using the " + Preferences.themePreset + " preset")
        }
    }
    Connections {
        target: Preferences
        function onChanged() { window.applyChrome() }
    }

    // The app's ribbon layout (RibbonFromModel.qml documents the shape).
    property var ribbonTabs: []
    // Contextual ribbon tabs this window derives in QML ("table", "shape").
    property var ribbonContexts: []
    readonly property RibbonFromModel ribbon: officeRibbon
    toolBars: [
        RibbonFromModel {
            id: officeRibbon
            actions: window.host.actions
            host: window.host
            layout: window.ribbonTabs
            extraContexts: window.ribbonContexts
            visible: window.ribbonTabs.length > 0
        }
    ]

    // Help ▸ Search Commands… (Alt+Q): every command of the menu tree,
    // sectioned by its top-level menu, activated by id through the model.
    function commandList() {
        const out = [], seen = ({})
        const strip = function(text) { return String(text || "").replace(/&&/g, "\u0001").replace(/&/g, "").replace(/\u0001/g, "&") }
        const walk = function(node, section, path) {
            for (const child of (node.children || [])) {
                if (child.kind === "menu") {
                    walk(child, section, path.concat([strip(child.title)]))
                } else if (child.kind === "action" && !seen[child.id]) {
                    const entry = window.host.actions.entry(child.id)
                    if (entry === null || !entry.visible) continue
                    seen[child.id] = true
                    out.push({ id: child.id, label: entry.plainText, shortcut: entry.shortcutText,
                               section: section, iconName: entry.iconName, enabled: entry.enabled,
                               keywords: path.join(" ") })
                }
            }
        }
        for (const menu of window.host.menuTree.menus) walk(menu, strip(menu.title), [])
        return out
    }
    Tk.CommandPalette {
        id: commandPalette
        objectName: "commandPalette"
        hostItem: window.contentItem
        placeholderText: qsTr("Search commands…")
        onActivated: function(id) { window.host.actions.trigger(id) }
    }

    menuBar: MenuBarFromModel {
        objectName: "officeMenuBar"
        tree: window.host.menuTree
        actions: window.host.actions
        visible: window.host.localMenuVisible
    }

    statusBar: OfficeStatusBar {
        objectName: "officeStatusBar"
        host: window.host
    }

    // ---- close consent (ADR-0110: per document, never per process) ----
    onClosing: function(close) {
        if (window.beforeClose !== null && !window.closePrepared) {
            close.accepted = false
            if (!window.closePreparationPending) {
                window.closePreparationPending = true
                window.beforeClose(function(ready) {
                    window.closePreparationPending = false
                    if (ready) {
                        window.closePrepared = true
                        Qt.callLater(window.close)
                    }
                })
            }
            return
        }
        close.accepted = window.host.requestClose()
    }
    Connections {
        target: window.host
        function onCloseAuthorizedChanged() {
            if (window.host.closeAuthorized) {
                window.close()
            }
        }
        function onCloseConsentRequested(title) {
            consent.documentTitle = title
            consent.open()
        }
        function onOpenDialogRequested(startDir, nameFilters) {
            openDialog.currentFolder = "file://" + startDir
            openDialog.nameFilters = nameFilters
            openDialog.open()
        }
        function onSaveAsDialogRequested(startPath, nameFilters) {
            saveDialog.nameFilters = nameFilters
            saveDialog.selectedFile = "file://" + startPath
            saveDialog.open()
        }
        function onOverwriteConsentRequested(path) {
            overwrite.text = qsTr("%1 was modified by another program since it was opened.\nSaving will overwrite those changes.").arg(path)
            overwrite.open()
        }
        function onMessageRequested(title, text, variant) {
            message.title = title
            message.text = text
            message.variant = variant
            message.open()
        }
        function onAboutRequested() { about.open() }
        function onPreferencesRequested() { preferences.open() }
        function onCommandSearchRequested() {
            commandPalette.commands = window.commandList()
            const seed = officeRibbon.trailing !== null ? officeRibbon.trailing.handoffText : ""
            commandPalette.open()
            commandPalette.filterText = seed
            if (officeRibbon.trailing !== null) officeRibbon.trailing.handoffText = ""
        }
        function onPrintRequested() {
            const job = window.host.createPrintJob()
            if (job !== null) {
                printDialog.job = job
                printDialog.open()
            }
        }
        function onPrintPreviewRequested() {
            const job = window.host.createPrintJob()
            if (job !== null) {
                printPreview.job = job
                printPreview.open()
            }
        }
    }

    CloseConsentDialog {
        id: consent
        objectName: "closeConsent"
        onAccepted: window.host.resolveClose("save")
        onTertiary: window.host.resolveClose("discard")
        onRejected: { window.resetClosePreparation(); window.host.resolveClose("cancel") }
    }

    Dialogs.FileDialog {
        id: openDialog
        objectName: "openDialog"
        title: qsTr("Open")
        fileMode: Dialogs.FileDialog.OpenFiles
        onAccepted: window.host.openPathsChosen(selectedFiles)
    }
    Dialogs.FileDialog {
        id: saveDialog
        objectName: "saveDialog"
        title: qsTr("Save As")
        fileMode: Dialogs.FileDialog.SaveFile
        onAccepted: window.host.saveAsPathChosen(selectedFile, selectedNameFilter.name)
        onRejected: window.host.saveAsCancelled()
    }

    Tk.MessageDialog {
        id: overwrite
        objectName: "overwriteConsent"
        title: qsTr("File changed on disk")
        variant: "warning"
        primaryText: qsTr("Save")
        destructive: true
        onAccepted: window.host.resolveOverwrite(true)
        onRejected: window.host.resolveOverwrite(false)
    }
    Tk.MessageDialog {
        id: message
        objectName: "messageDialog"
        primaryText: qsTr("OK")
        secondaryText: ""
    }
    AboutDialog {
        id: about
        host: window.host
    }
    PreferencesDialog {
        id: preferences
        host: window.host
    }
    PrintDialog {
        id: printDialog
        onPrinted: function(ok) {
            window.host.showStatusMessage(ok ? qsTr("Sent to the printer") : job.lastError, ok ? 5000 : 8000)
        }
    }
    PrintPreview {
        id: printPreview
        onPrintRequested: function(job) {
            printDialog.job = job
            printDialog.open()
        }
    }
}
