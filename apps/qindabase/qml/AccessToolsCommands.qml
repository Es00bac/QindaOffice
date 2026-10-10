// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The window's answers to the M86 commands (D-094, base_host_actions_access.cpp):
// Access's query wizards, the table events (data macros), hyperlinks,
// Access's XML import, linked tables and the application log — and the
// AutoExec macro a database runs as it opens. BaseCommands.qml routes these
// ids here (Indexes and the Union query type go to their designers).
//
// AGENT-CONTRACT (with base_host_actions_access.cpp): every id there that is
// not a designer's own is in `ids` and handled in `handle`.
QtObject {
    id: tools

    property var window: null
    readonly property var host: window !== null ? window.baseHost : null
    readonly property var base: window !== null ? window.base : null
    readonly property var commands: window !== null ? window.commands : null
    readonly property var events: ({ baseDataMacroBeforeChangeAction: "beforeChange", baseDataMacroBeforeDeleteAction: "beforeDelete",
                                     baseDataMacroAfterInsertAction: "afterInsert", baseDataMacroAfterUpdateAction: "afterUpdate",
                                     baseDataMacroAfterDeleteAction: "afterDelete" })
    readonly property var wizards: ({ baseSimpleQueryWizardAction: "simple", baseCrosstabWizardAction: "crosstab",
                                      baseFindDuplicatesWizardAction: "duplicates", baseFindUnmatchedWizardAction: "unmatched" })
    readonly property var ids: Object.keys(events).concat(Object.keys(wizards)).concat([
        "baseEditHyperlinkAction", "baseOpenHyperlinkAction", "baseImportXmlAction", "baseLinkTableAction",
        "baseLinkedTableManagerAction", "baseRefreshLinksAction", "baseApplicationLogAction"])

    function handles(id) { return ids.indexOf(id) >= 0 }

    // A table event's data macro in the macro designer (M86).
    function editDataMacro(table, event) {
        commands.leave(function() {
            const map = base.dataMacroDesign(table, event)
            if (!map.ok) { window.notify(qsTr("Cannot design"), map.error, "danger"); return }
            window.macroDesignView.load(map)
            commands.show("macroDesign", "dataMacro", qsTr("%1 · %2").arg(map.label).arg(map.table))
            host.designHistory = true
        })
    }
    // What Open Hyperlink would open for a stored hyperlink text ("" when
    // it is no address QindaBase opens: web, mail, FTP and files only).
    function hyperlinkUrl(text) {
        const target = base.hyperlinkParts(text || "").target
        if (/^(https?|mailto|ftp|file):/i.test(target)) return target
        if (/^www\./i.test(target)) return "https://" + target
        return ""
    }
    function openHyperlink() {
        const url = hyperlinkUrl(host.hyperlinkTarget.text)
        if (url.length === 0) {
            window.notify(qsTr("Not opened"), qsTr("QindaBase opens web, e-mail, FTP and file addresses."), "warning")
            return
        }
        Qt.openUrlExternally(url)
    }
    function runAutoExec() {
        if (base === null || !base.hasDatabase) return
        const name = base.autoExecMacro()
        if (name.length === 0) return
        if (host.shiftHeld()) {
            window.notify(qsTr("AutoExec skipped"), qsTr("Shift was held as the database opened."), "info")
            return
        }
        commands.macroRunner.runNamed(name)
    }
    function refreshAll() {
        const r = base.refreshAllLinks()
        window.notify(r.ok ? qsTr("Links refreshed") : qsTr("Some links were not refreshed"),
                      r.ok ? qsTr("%n linked table(s) read again from their sources.", "", r.refreshed) : r.error,
                      r.ok ? "success" : "warning")
    }

    function handle(id) {
        if (wizards[id] !== undefined) {
            window.queryWizard.openFor(wizards[id], host.selectedKind === "table" ? host.selectedName : "")
            return
        }
        if (events[id] !== undefined) {
            if (host.currentView === "tableDesign" && host.currentName.length > 0) editDataMacro(host.currentName, events[id])
            return
        }
        switch (id) {
        case "baseEditHyperlinkAction": window.hyperlinkDialog.openFor(host.hyperlinkTarget); break
        case "baseOpenHyperlinkAction": openHyperlink(); break
        case "baseImportXmlAction":
            window.importDialog.ask([qsTr("Access XML data (*.xml)"), qsTr("All files (*)")],
                                    function(url) { window.accessImportDialog.openFor(url, "xml") })
            break
        case "baseLinkTableAction":
            window.importDialog.ask([qsTr("Databases, text files and workbooks (*.qbase *.sqlite *.db *.csv *.tsv *.txt *.xlsx *.xlsm)"),
                                     qsTr("All files (*)")], function(url) { window.linkTableDialog.openFor(url) })
            break
        case "baseLinkedTableManagerAction": window.linkedTableManager.openFor(); break
        case "baseRefreshLinksAction": refreshAll(); break
        case "baseApplicationLogAction":
            commands.leave(function() {
                const r = base.openApplicationLog()
                if (!r.ok) { window.notify(qsTr("Application log"), r.error, "info"); return }
                commands.show("datasheet", "query", qsTr("Application Log"))
            })
            break
        }
    }
}
