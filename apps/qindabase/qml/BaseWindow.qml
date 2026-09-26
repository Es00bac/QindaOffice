// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Base.App

// The QindaBase window (D-049): one database file per window. The
// navigation pane lists the tables, queries, forms and reports; the main
// area shows one of them — a datasheet, a designer, a form, a report — or
// the start page (the template gallery when no database is open yet).
// Commands arrive from the host as actionRequested(id) and are answered in
// BaseCommands.qml; views never write the database themselves, they call
// the host's BaseService.
//
// AGENT-NOTE: every record value arrives as display text from the engine;
// this QML never does arithmetic on records or money.
OfficeWindow {
    id: window

    readonly property BaseHost baseHost: host as BaseHost
    readonly property BaseService base: baseHost !== null ? baseHost.base : null
    readonly property string view: baseHost !== null ? baseHost.currentView : "start"
    readonly property bool hasDatabase: base !== null && base.hasDatabase
    property alias commands: commands_
    property alias datasheetView: datasheetView_
    property alias tableDesignView: tableDesignView_
    property alias queryDesignView: queryDesignView_
    property alias formView: formView_
    property alias reportView: reportView_
    property alias relationshipsView: relationshipsView_
    property alias sqlView: sqlView_
    property alias navigation: navigation_
    readonly property bool narrow: width < Tk.Theme.size.panelMinWidth * 3

    width: 1320
    height: 840

    readonly property BaseRibbonLayout ribbonLayout: BaseRibbonLayout { }
    ribbonTabs: ribbonLayout.tabs

    function trigger(id) { window.host.actions.trigger(id) }
    function notify(title, text, variant) {
        if (variant === "danger") console.warn("QindaBase: " + title + ": " + text)
        toast.title = title
        toast.message = text
        toast.variant = variant || "info"
        toast.show()
    }

    // AGENT-GUARD: the window seats exactly one Item; the Toast lives inside it.
    Item {
        objectName: "baseContent"
        Tk.Flex {
            objectName: "baseRoot"
            anchors.fill: parent
            direction: Tk.Flex.Row
            gap: 0
            NavigationPane {
                id: navigation_
                objectName: "navigationPane"
                window: window
                visible: window.hasDatabase && window.baseHost.navigationVisible
                Tk.Flex.basis: Math.round(Math.max(Tk.Theme.size.panelMinWidth * 0.72,
                                                   Math.min(Tk.Theme.size.panelMinWidth * 1.1, window.width * 0.22)))
                Tk.Flex.shrink: 0
            }
            Tk.Divider { vertical: true; visible: navigation_.visible }
            Item {
                objectName: "baseMain"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                clip: true
                StartView { anchors.fill: parent; window: window; visible: !window.hasDatabase || window.view === "start" }
                DatasheetView { id: datasheetView_; anchors.fill: parent; window: window; visible: window.hasDatabase && window.view === "datasheet" }
                TableDesignView { id: tableDesignView_; anchors.fill: parent; window: window; visible: window.hasDatabase && window.view === "tableDesign" }
                QueryDesignView { id: queryDesignView_; anchors.fill: parent; window: window; visible: window.hasDatabase && window.view === "queryDesign" }
                FormView { id: formView_; anchors.fill: parent; window: window; visible: window.hasDatabase && window.view === "form" }
                ReportView { id: reportView_; anchors.fill: parent; window: window; visible: window.hasDatabase && window.view === "report" }
                RelationshipsView { id: relationshipsView_; anchors.fill: parent; window: window; visible: window.hasDatabase && window.view === "relationships" }
                SqlView { id: sqlView_; anchors.fill: parent; window: window; visible: window.hasDatabase && window.view === "sql" }
                Tk.Toast {
                    id: toast
                    objectName: "baseToast"
                    z: 10
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Tk.Theme.space.lg
                }
            }
        }
    }

    BaseCommands { id: commands_; window: window }
    // (the report wizard, the SQL view and friends are reached by tests
    // through these aliases too)

    // ---- dialogs (Popups are fine as window children) ----
    PlanDialog { id: planDialog_; objectName: "planDialog" }
    RelationshipDialog { id: relationshipDialog_; window: window }
    ParametersDialog { id: parametersDialog_ }
    ImportCsvDialog { id: importCsvDialog_; window: window }
    AccessImportDialog { id: accessImportDialog_; window: window }
    FormWizardDialog { id: formWizard_; window: window }
    ReportWizardDialog { id: reportWizard_; window: window }
    property alias planDialog: planDialog_
    property alias relationshipDialog: relationshipDialog_
    property alias parametersDialog: parametersDialog_
    property alias importCsvDialog: importCsvDialog_
    property alias accessImportDialog: accessImportDialog_
    property alias formWizard: formWizard_
    property alias reportWizard: reportWizard_
    property alias promptDialog: promptDialog_
    property alias confirmDialog: confirmDialog_
    property alias exportDialog: exportDialog_
    property alias importDialog: importDialog_

    // One prompt and one confirmation, reused with a callback.
    Tk.PromptDialog {
        id: promptDialog_
        objectName: "basePrompt"
        property var callback: null
        primaryText: qsTr("OK")
        onAccepted: { const f = callback; callback = null; if (f) f(text.trim()) }
        onRejected: callback = null
        function ask(title, label, value, primary, done) {
            promptDialog_.title = title
            promptDialog_.label = label
            promptDialog_.text = value || ""
            promptDialog_.primaryText = primary || qsTr("OK")
            promptDialog_.callback = done
            promptDialog_.open()
        }
    }
    Tk.MessageDialog {
        id: confirmDialog_
        objectName: "baseConfirm"
        property var callback: null
        onAccepted: { const f = callback; callback = null; if (f) f("primary") }
        onTertiary: { const f = callback; callback = null; if (f) f("tertiary") }
        onRejected: callback = null
        function ask(title, text, variant, primary, done, tertiary) {
            confirmDialog_.title = title
            confirmDialog_.text = text
            confirmDialog_.variant = variant || "question"
            confirmDialog_.primaryText = primary || qsTr("OK")
            confirmDialog_.secondaryText = qsTr("Cancel")
            confirmDialog_.tertiaryText = tertiary || ""
            confirmDialog_.destructive = variant === "danger"
            confirmDialog_.callback = done
            confirmDialog_.open()
        }
    }
    Dialogs.FileDialog {
        id: exportDialog_
        objectName: "baseExportDialog"
        property var callback: null
        title: qsTr("Export")
        fileMode: Dialogs.FileDialog.SaveFile
        onAccepted: { const f = callback; callback = null; if (f) f(selectedFile) }
        function ask(filters, suggested, done) {
            exportDialog_.nameFilters = filters
            exportDialog_.selectedFile = "file://" + suggested
            exportDialog_.callback = done
            exportDialog_.open()
        }
    }
    Dialogs.FileDialog {
        id: importDialog_
        objectName: "baseImportDialog"
        property var callback: null
        title: qsTr("Import")
        fileMode: Dialogs.FileDialog.OpenFile
        onAccepted: { const f = callback; callback = null; if (f) f(selectedFile) }
        function ask(filters, done) {
            importDialog_.nameFilters = filters
            importDialog_.callback = done
            importDialog_.open()
        }
    }

    Connections {
        target: window.base
        function onNotify(title, text, variant) { window.notify(title, text, variant) }
    }
    Component.onCompleted: window.baseHost.preferencesPageUrl = Qt.resolvedUrl("BasePreferencesPage.qml")
}
