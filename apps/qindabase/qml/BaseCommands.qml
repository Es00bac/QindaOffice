// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Base.App

// The window's answers to BaseHost.actionRequested(id), and the one place
// that opens objects and switches views (so a designer with unsaved edits
// is always asked about first).
//
// AGENT-CONTRACT (with base_host_actions.cpp): every id the host emits is
// handled in `handle`; an id added there must be added here.
QtObject {
    id: commands

    property var window: null
    readonly property var host: window !== null ? window.baseHost : null
    readonly property var base: window !== null ? window.base : null

    // The designer shown now (it has save(), discard(), undo(), redo()).
    function activeDesigner() {
        switch (host.currentView) {
        case "tableDesign": return window.tableDesignView
        case "queryDesign": return window.queryDesignView
        case "form": return window.formView
        case "report": return window.reportView
        }
        return null
    }

    function show(view, kind, name) {
        host.designHistory = false
        host.currentKind = kind || ""
        host.currentName = name || ""
        host.currentView = view
        // A designer brings its contextual tab forward; leaving it returns Home.
        const context = ({ tableDesign: "tableDesign", queryDesign: "queryDesign", form: "formDesign", report: "formDesign" })[view]
        if (window.ribbon !== null) {
            if (context) window.ribbon.currentTabId = context
            else if (["tableDesign", "queryDesign", "formDesign"].indexOf(window.ribbon.currentTabId) >= 0) window.ribbon.currentTabId = "home"
        }
    }

    // Runs `next` once a dirty designer is saved or discarded (or at once).
    function leave(next) {
        if (!host.designDirty) { next(); return }
        const designer = activeDesigner()
        window.confirmDialog.ask(qsTr("Save changes?"),
                                 qsTr("%1 has changes that are not saved yet.").arg(host.currentName || qsTr("The design")),
                                 "question", qsTr("Save"), function(choice) {
            if (choice === "primary" && designer !== null && !designer.save()) return
            if (choice === "tertiary" && designer !== null) designer.discard()
            host.designDirty = false
            next()
        }, qsTr("Discard"))
    }

    function openObject(kind, name) {
        leave(function() {
            if (kind === "table") {
                if (base.datasheet.openTable(name)) show("datasheet", kind, name)
                else window.notify(qsTr("Cannot open %1").arg(name), base.datasheet.error, "danger")
            } else if (kind === "query") {
                runQuery(name, ({}), ({}))
            } else if (kind === "form") {
                if (window.formView.open(name, "edit")) show("form", kind, name)
            } else if (kind === "report") {
                withParameters(base.reportParameters(name), function(values) {
                    if (window.reportView.open(name, values, false)) show("report", kind, name)
                })
            }
        })
    }

    function designObject(kind, name) {
        leave(function() {
            if (kind === "table") {
                window.tableDesignView.load(base.tableDesign(name))
                show("tableDesign", kind, name)
                host.designHistory = true
            } else if (kind === "query") {
                window.queryDesignView.load(base.queryDesign(name))
                show("queryDesign", kind, name)
                host.designHistory = true
            } else if (kind === "form") {
                if (window.formView.open(name, "design")) show("form", kind, name)
                host.designHistory = true
            } else if (kind === "report") {
                withParameters(base.reportParameters(name), function(values) {
                    if (window.reportView.open(name, values, true)) show("report", kind, name)
                    host.designHistory = true
                })
            }
        })
    }

    // Asks for parameter values first when there are any.
    function withParameters(parameters, next) {
        if (!parameters || parameters.length === 0) { next(({})); return }
        window.parametersDialog.ask(parameters, next)
    }

    // A saved query (design empty) or a designer's draft (name may be "").
    function runQuery(name, design, values) {
        const result = base.runQuery(name, design, values)
        if (result.needsParameters) {
            withParameters(result.parameters, function(v) { runQuery(name, design, v) })
            return
        }
        if (!result.ok) { window.notify(qsTr("The query did not run"), result.error, "danger"); return }
        if (!result.action) {
            show("datasheet", "query", name)
            return
        }
        const source = Object.keys(design).length ? design : base.queryDesign(name)
        const verb = ({ UPDATE: qsTr("change"), DELETE: qsTr("delete"), INSERT: qsTr("add") })[result.keyword] || qsTr("change")
        window.confirmDialog.ask(qsTr("Run %1?").arg(name || qsTr("this action query")),
                                 qsTr("This query will %1 %n record(s). Undo can reverse it afterwards.", "", result.affected).arg(verb),
                                 result.keyword === "DELETE" ? "danger" : "warning",
                                 qsTr("Run"), function() {
            const done = base.runAction(source, values, result.affected)
            if (!done.ok) window.notify(qsTr("Nothing was changed"), done.error, "danger")
            else window.datasheetView.refresh()
        })
    }

    function selected() { return { kind: host.selectedKind, name: host.selectedName } }

    function renameObject(kind, name) {
        window.promptDialog.ask(qsTr("Rename"), qsTr("New name for the %1 %2").arg(kind).arg(name), name, qsTr("Rename"),
                                function(text) {
            if (text === name || text.length === 0) return
            const r = base.renameObject(kind, name, text)
            if (!r.ok) { window.notify(qsTr("Not renamed"), r.error, "danger"); return }
            if (host.currentName === name && host.currentKind === kind) host.currentName = r.name
            host.selectedName = r.name
        })
    }

    function deleteObject(kind, name) {
        const impact = base.deleteObjectImpact(kind, name)
        if (impact.blocked) { window.notify(qsTr("Cannot delete %1").arg(name), impact.lines.join(" "), "warning"); return }
        window.confirmDialog.ask(qsTr("Delete %1?").arg(name), impact.lines.join("\n"), "danger", qsTr("Delete"), function() {
            if (host.currentName === name && host.currentKind === kind) { host.designDirty = false; show("start", "", "") }
            const r = base.deleteObject(kind, name)
            if (!r.ok) window.notify(qsTr("Not deleted"), r.error, "danger")
            else window.notify(qsTr("Deleted"), r.backupPath ? qsTr("A backup was saved first: %1").arg(r.backupPath)
                                                          : qsTr("Undo brings it back."), "success")
        })
    }

    // Where record commands go: the datasheet or the form.
    function recordTarget() { return host.currentView === "form" ? window.formView : window.datasheetView }

    function exportData(format) {
        const kind = host.currentView === "datasheet" ? base.datasheet.sourceKind : host.selectedKind
        const name = host.currentView === "datasheet" ? base.datasheet.sourceName : host.selectedName
        if (!name) return
        const filters = format === "xlsx" ? [qsTr("Excel workbook (*.xlsx)")] : [qsTr("CSV (*.csv)")]
        window.exportDialog.ask(filters, name + (format === "xlsx" ? ".xlsx" : ".csv"), function(url) {
            const r = base.exportObject(kind, name, url, format)
            if (!r.ok) window.notify(qsTr("Not exported"), r.error, "danger")
        })
    }

    function handle(id) {
        const sel = selected()
        switch (id) {
        case "baseOpenObjectAction": if (sel.name) openObject(sel.kind, sel.name); break
        case "baseViewDesignAction": if (sel.name) designObject(sel.kind, sel.name); break
        case "baseViewSqlAction":
            if (host.currentView === "queryDesign") window.queryDesignView.showSql()
            else if (sel.kind === "query") { designObject("query", sel.name); Qt.callLater(window.queryDesignView.showSql) }
            break
        case "baseRenameObjectAction": if (sel.name) renameObject(sel.kind, sel.name); break
        case "baseDeleteObjectAction": if (sel.name) deleteObject(sel.kind, sel.name); break
        case "baseNewRecordAction": recordTarget().newRecord(); break
        case "baseSaveRecordAction": recordTarget().saveRecord(); break
        case "baseDeleteRecordAction": recordTarget().deleteRecord(); break
        case "baseRefreshAction":
            if (host.currentView === "report") window.reportView.rebuild()
            else recordTarget().refresh()
            break
        case "baseSortAscendingAction": window.datasheetView.sortActive(false); break
        case "baseSortDescendingAction": window.datasheetView.sortActive(true); break
        case "baseFilterBySelectionAction": window.datasheetView.filterBySelection(false); break
        case "baseFilterExcludingAction": window.datasheetView.filterBySelection(true); break
        case "baseClearFiltersAction": base.datasheet.clearFilters(); break
        case "baseFilterAction": window.datasheetView.filterBarVisible = !window.datasheetView.filterBarVisible; break
        case "baseTotalsRowAction": base.datasheet.totalsVisible = !base.datasheet.totalsVisible; break
        case "editCopyAction": window.datasheetView.copySelection(false); break
        case "editCutAction": window.datasheetView.copySelection(true); break
        case "editPasteAction": window.datasheetView.paste(); break
        case "editSelectAllAction": window.datasheetView.selectAll(); break
        case "editFindAction": window.datasheetView.startFind(); break
        case "editFindNextAction": window.datasheetView.findNext(true); break
        case "editFindPreviousAction": window.datasheetView.findNext(false); break
        case "editUndoAction": if (activeDesigner() !== null) activeDesigner().undo(); break
        case "editRedoAction": if (activeDesigner() !== null) activeDesigner().redo(); break
        case "baseNewTableAction":
            leave(function() {
                window.promptDialog.ask(qsTr("New table"), qsTr("Name of the table"), "", qsTr("Create"), function(text) {
                    const r = base.quickTable(text)
                    if (!r.ok) { window.notify(qsTr("No table created"), r.error, "danger"); return }
                    host.selectedKind = "table"; host.selectedName = r.name
                    openObject("table", r.name)
                })
            })
            break
        case "baseTableDesignAction":
            leave(function() {
                window.tableDesignView.load(base.newTableDesign(qsTr("Table%1").arg(base.tableNames.length + 1)))
                show("tableDesign", "table", "")
                host.designHistory = true
            })
            break
        case "baseQueryDesignAction":
            leave(function() {
                window.queryDesignView.load(base.newQueryDesign(sel.kind === "table" && sel.name ? [sel.name] : []))
                show("queryDesign", "query", "")
                host.designHistory = true
            })
            break
        case "baseNewFormAction":
            if (sel.kind !== "table") { window.notify(qsTr("Choose a table"), qsTr("A form edits the records of one table."), "info"); break }
            const form = base.createForm("", sel.name, sel.name, [])
            if (!form.ok) { window.notify(qsTr("No form created"), form.error, "danger"); break }
            openObject("form", form.name)
            break
        case "baseFormWizardAction": window.formWizard.openFor(sel.kind === "table" ? sel.name : ""); break
        case "baseNewReportAction":
            const fields = base.sourceFields(sel.kind, sel.name)
            const report = base.createReport("", { source: { kind: sel.kind, name: sel.name }, title: sel.name,
                columns: fields.slice(0, 6).map(function(f) { return { field: f.name, label: "" } }), groups: [], totals: [] })
            if (!report.ok) { window.notify(qsTr("No report created"), report.error, "danger"); break }
            openObject("report", report.name)
            break
        case "baseReportWizardAction": window.reportWizard.openFor(sel.kind, sel.name); break
        case "baseImportCsvAction":
            window.importDialog.ask([qsTr("Text and CSV (*.csv *.tsv *.txt)"), qsTr("All files (*)")],
                                    function(url) { window.importCsvDialog.openFor(url) })
            break
        case "baseImportAccessAction":
            const tools = base.accessTools()
            if (!tools.available) { window.confirmDialog.ask(qsTr("Access import"), tools.message, "info", qsTr("OK"), null); break }
            window.importDialog.ask([qsTr("Access database (*.accdb *.mdb)")], function(url) { window.accessImportDialog.openFor(url) })
            break
        case "baseExportCsvAction": exportData("csv"); break
        case "baseExportXlsxAction": exportData("xlsx"); break
        case "baseExportPdfAction":
            window.exportDialog.ask([qsTr("PDF (*.pdf)")], (host.currentName || qsTr("Report")) + ".pdf", function(url) {
                const r = host.exportReportPdf(url)
                window.notify(r.ok ? qsTr("Exported") : qsTr("Not exported"), r.ok ? r.path : r.error, r.ok ? "success" : "danger")
            })
            break
        case "baseRelationshipsAction": leave(function() { window.relationshipsView.reload(); show("relationships", "", qsTr("Relationships")) }); break
        case "baseSqlConsoleAction": leave(function() { show("sql", "", qsTr("SQL")) }); break
        case "baseCompactRepairAction":
            window.confirmDialog.ask(qsTr("Compact and repair"), qsTr("Rebuilds the file to reclaim space, then checks every page and relationship. The undo history ends here."),
                                     "question", qsTr("Compact"), function() {
                const r = base.compactAndRepair()
                if (!r.ok) { window.notify(qsTr("Not compacted"), r.error, "danger"); return }
                window.confirmDialog.ask(qsTr("Compact and repair"),
                    (r.problems.length ? qsTr("Problems found:\n%1").arg(r.problems.join("\n")) : qsTr("No problems found."))
                    + "\n" + qsTr("Size: %1 KB → %2 KB").arg(Math.round(r.before / 1024)).arg(Math.round(r.after / 1024)),
                    r.problems.length ? "warning" : "info", qsTr("OK"), null)
            })
            break
        case "baseCheckIntegrityAction":
            const check = base.checkIntegrity()
            window.confirmDialog.ask(qsTr("Integrity check"), check.problems.length ? check.problems.join("\n")
                                                                                    : qsTr("Every page, index and relationship is consistent."),
                                     check.problems.length ? "warning" : "info", qsTr("OK"), null)
            break
        case "baseBackupAction": base.backupNow(); break
        case "baseDatabasePropertiesAction": window.trigger("settingsPreferencesAction"); break
        case "baseShowNavigationAction": break // the host toggles the pane itself
        default:
            if (id.indexOf("baseDesign") === 0) window.tableDesignView.command(id)
            else if (id.indexOf("baseQuery") === 0) window.queryDesignView.command(id)
            else if (id === "baseFormPreviewAction") window.formView.togglePreview()
            else if (id === "baseReportRefreshAction") window.reportView.rebuild()
        }
    }

    property Connections hostConnection: Connections {
        target: commands.host
        function onActionRequested(id) { commands.handle(id) }
        function onDesignSaveRequested() {
            const designer = commands.activeDesigner()
            if (designer !== null && designer.save()) commands.host.designDirty = false
        }
    }
}
