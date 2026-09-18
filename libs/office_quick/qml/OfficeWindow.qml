// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell

// The suite's document window: a Tk.AppWindow over a DocumentHost. The
// host owns the commands, the menu tree and every flow; this file renders
// the menu bar and status bar from it and answers the dialogs it asks for
// (open, save as, overwrite consent, close consent, messages, about,
// preferences, print). Apps derive from it: NoteWindow.qml is
// `OfficeWindow { toolBars: [...]; findBar: ...; NotePage {} }`.
//
// AGENT-CONTRACT (ADR-0117): width/height are set once here; nothing in
// the suite resizes a shown window.
Tk.AppWindow {
    id: window

    required property DocumentHost host

    title: host.windowTitle
    toolBarsVisible: host.toolBarsVisible
    statusBarVisible: host.statusBarVisible

    // The desktop's QST-1 tokens when they are published (the bridge fails
    // to load off the QindaQt desktop: the preset below stays — rule 9,
    // degrade visibly, never block).
    // AGENT-GUARD: declared as a property, not a child: Tk.AppWindow seats
    // ONE unanchored content child to fill the window, and a Loader among
    // the children made the app's content a second child (0×0, unseated).
    readonly property Loader themeBridge: Loader {
        objectName: "themeBridge"
        source: "ThemeBridge.qml"
        onStatusChanged: if (status === Loader.Error) console.info("QindaOffice: no QindaQt tokens, using the " + Preferences.themePreset + " preset")
    }
    Component.onCompleted: {
        Tk.Density.modeName = Preferences.density
        if (themeBridge.status !== Loader.Ready) {
            Tk.Theme.applyPreset(Preferences.themePreset)
        }
    }
    Connections {
        target: Preferences
        function onChanged() {
            Tk.Density.modeName = Preferences.density
            if (themeBridge.status !== Loader.Ready) {
                Tk.Theme.applyPreset(Preferences.themePreset)
            }
        }
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
        onRejected: window.host.resolveClose("cancel")
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
