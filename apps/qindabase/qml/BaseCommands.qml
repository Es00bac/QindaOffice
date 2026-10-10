// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Base.App

// The window's answers to BaseHost.actionRequested(id), and the one place
// that opens objects and switches views (so a designer with unsaved edits
// is always asked about first).
//
// AGENT-CONTRACT (with base_host_actions.cpp): every id the host emits is
// handled in `handle`; an id added there must be added here. M86's ids
// (base_host_actions_access.cpp) go to AccessToolsCommands.qml, except
// Indexes and the Union query type, which their designers answer.
QtObject {
    id: commands

    property var window: null
    readonly property var host: window !== null ? window.baseHost : null
    readonly property var base: window !== null ? window.base : null
    // M80: runs macros and form buttons through the commands below.
    property MacroRunner macroRunner: MacroRunner { window: commands.window }

    // The designer shown now (it has save(), discard(), undo(), redo()).
    function activeDesigner() {
        switch (host.currentView) {
        case "tableDesign": return window.tableDesignView
        case "queryDesign": return window.queryDesignView
        case "form": return window.formView
        case "report": return window.reportView
        case "macroDesign": return window.macroDesignView
        }
        return null
    }

    function show(view, kind, name) {
        // AGENT-NOTE: read before the view changes — the ribbon hides a
        // contextual tab as soon as its context goes and selects its first
        // tab (File) on its own, so "was a designer's tab" is known only now.
        const leaving = window.ribbon !== null
                        && ["tableDesign", "queryDesign", "formDesign", "macroDesign"].indexOf(window.ribbon.currentTabId) >= 0
        host.designHistory = false
        host.currentKind = kind || ""
        host.currentName = name || ""
        host.currentView = view
        // A designer brings its contextual tab forward; leaving it returns Home.
        const context = ({ tableDesign: "tableDesign", queryDesign: "queryDesign", form: "formDesign", report: "formDesign",
                           macroDesign: "macroDesign" })[view]
        if (window.ribbon !== null) {
            if (context) window.ribbon.currentTabId = context
            else if (leaving) window.ribbon.currentTabId = "home"
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
                openReportFor(name, "preview", null, null)
            } else if (kind === "macro") {
                macroRunner.runNamed(name) // M80: Open on a macro runs it, as in Access
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
                if (base.reportInfo(name).type === "labels") { openReportFor(name, "preview", null, null); return }
                withParameters(base.reportParameters(name), function(values) {
                    if (window.reportView.open(name, values, true)) show("report", kind, name)
                    host.designHistory = true
                })
            } else if (kind === "macro") {
                window.macroDesignView.load(base.macroDesign(name))
                show("macroDesign", kind, name)
                host.designHistory = true
            }
        })
    }

    // ---- M80: what macros and form buttons do (MacroRunner.qml) ----
    // Each calls done() when it has done its part, or failed(problem).
    function openFormFor(name, where, dataMode, done, failed) {
        leave(function() {
            if (!window.formView.open(name, "edit")) { if (failed) failed(qsTr("The form %1 did not open.").arg(name)); return }
            show("form", "form", name)
            if (where.length > 0) {
                const r = window.formView.applyFilter(where)
                if (!r.ok) { if (failed) failed(r.error); return }
            }
            if (dataMode === "add") window.formView.newRecord()
            if (done) done()
        })
    }
    function openReportFor(name, view, done, failed) {
        leave(function() {
            const labels = base.reportInfo(name).type === "labels"
            withParameters(base.reportParameters(name), function(values) {
                const shown = labels ? window.reportView.openLabels(name, values) : window.reportView.open(name, values, false)
                if (!shown) { if (failed) failed(qsTr("The report %1 did not open.").arg(name)); return }
                show("report", "report", name)
                if (view === "print") window.trigger("filePrintAction")
                if (done) done()
            })
        })
    }
    function openTableFor(name, done, failed) {
        leave(function() {
            if (!base.datasheet.openTable(name)) { if (failed) failed(base.datasheet.error); return }
            show("datasheet", "table", name)
            if (done) done()
        })
    }
    function applyFilter(where) {
        if (host.currentView === "form") return window.formView.applyFilter(where)
        if (host.currentView === "datasheet" && base.datasheet.sourceKind === "table") {
            if (where.length === 0) { base.datasheet.clearFilters(); return { ok: true } }
            return base.datasheet.applyWhere(where)
        }
        return { ok: false, error: qsTr("Open a form or a table first; the filter applies to it.") }
    }
    function goToRecord(which) {
        if (host.currentView === "form") window.formView.goToRecord(which)
        else if (host.currentView === "datasheet") window.datasheetView.goToRecord(which)
    }
    function findRecord(text) {
        if (host.currentView !== "datasheet") return { ok: false, error: qsTr("FindRecord searches an open table or query.") }
        window.datasheetView.findText(text)
        return { ok: true }
    }
    function closeObject(done) {
        leave(function() { show("start", "", ""); if (done) done() })
    }

    // Asks for parameter values first when there are any.
    function withParameters(parameters, next) {
        if (!parameters || parameters.length === 0) { next(({})); return }
        window.parametersDialog.ask(parameters, next)
    }

    // A saved query (design empty) or a designer's draft (name may be "").
    // `done`/`failed` (M80, a macro's OpenQuery) follow it to its end.
    function runQuery(name, design, values, done, failed) {
        const result = base.runQuery(name, design, values)
        if (result.needsParameters) {
            withParameters(result.parameters, function(v) { runQuery(name, design, v, done, failed) })
            return
        }
        if (!result.ok) {
            window.notify(qsTr("The query did not run"), result.error, "danger")
            if (failed) failed(result.error)
            return
        }
        if (!result.action) {
            show("datasheet", "query", name)
            if (done) done()
            return
        }
        const source = Object.keys(design).length ? design : base.queryDesign(name)
        if (result.makeTable) {
            // M80: a make-table query shows its plan; Apply is one undo step.
            window.planDialog.show(result.plan, function() {
                const made = base.runMakeTable(source, values)
                if (!made.ok) { window.notify(qsTr("No table made"), made.error, "danger"); if (failed) failed(made.error); return }
                if (done) done()
            })
            return
        }
        const verb = ({ UPDATE: qsTr("change"), DELETE: qsTr("delete"), INSERT: qsTr("add") })[result.keyword] || qsTr("change")
        window.confirmDialog.ask(qsTr("Run %1?").arg(name || qsTr("this action query")),
                                 qsTr("This query will %1 %n record(s). Undo can reverse it afterwards.", "", result.affected).arg(verb),
                                 result.keyword === "DELETE" ? "danger" : "warning",
                                 qsTr("Run"), function() {
            const ran = base.runAction(source, values, result.affected)
            if (!ran.ok) {
                window.notify(qsTr("Nothing was changed"), ran.error, "danger")
                if (failed) failed(ran.error)
                return
            }
            window.datasheetView.refresh()
            if (done) done()
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
        const filters = format === "xlsx" ? [qsTr("Excel workbook (*.xlsx)")]
                      : format === "xml" ? [qsTr("XML data for Access (*.xml)")] : [qsTr("CSV (*.csv)")]
        window.exportDialog.ask(filters, name + "." + format, function(url) {
            const r = base.exportObject(kind, name, url, format)
            if (!r.ok) window.notify(qsTr("Not exported"), r.error, "danger")
        })
    }

    // File fields (M65, D-074): `t` = {table, rowid, field, hasFile}, the
    // host's fileTarget or a FileFieldPanel's own field.
    function insertFile(t) {
        if (!t || !t.field) return
        window.importDialog.ask([qsTr("All files (*)"), qsTr("Pictures (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg)"),
                                 qsTr("PDF documents (*.pdf)")], function(url) {
            const r = base.insertFile(t.table, t.rowid, t.field, url)
            if (!r.ok) window.notify(qsTr("No file inserted"), r.error, "danger")
        })
    }
    function saveFile(t) {
        if (!t || !t.hasFile) return
        const info = base.fileInfo(t.table, t.rowid, t.field)
        window.exportDialog.ask([qsTr("All files (*)")], info.suggestedName || t.field, function(url) {
            const r = base.saveFile(t.table, t.rowid, t.field, url)
            window.notify(r.ok ? qsTr("Saved") : qsTr("Not saved"), r.ok ? r.path : r.error, r.ok ? "success" : "danger")
        })
    }
    function removeFile(t) {
        if (!t || !t.hasFile) return
        window.confirmDialog.ask(qsTr("Remove the file?"), qsTr("Undo brings it back."), "warning", qsTr("Remove"), function() {
            const r = base.removeFile(t.table, t.rowid, t.field)
            if (!r.ok) window.notify(qsTr("Not removed"), r.error, "danger")
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
                                    function(url) { window.importCsvDialog.openFor(url, "csv") })
            break
        case "baseImportExcelAction":
            window.importDialog.ask([qsTr("Excel workbook (*.xlsx *.xlsm)"), qsTr("All files (*)")],
                                    function(url) { window.importCsvDialog.openFor(url, "xlsx") })
            break
        case "baseFileInsertAction": insertFile(host.fileTarget); break
        case "baseFileSaveAction": saveFile(host.fileTarget); break
        case "baseFileRemoveAction": removeFile(host.fileTarget); break
        case "baseImportAccessAction":
            const tools = base.accessTools()
            if (!tools.available) { window.confirmDialog.ask(qsTr("Access import"), tools.message, "info", qsTr("OK"), null); break }
            window.importDialog.ask([qsTr("Access database (*.accdb *.mdb)")], function(url) { window.accessImportDialog.openFor(url) })
            break
        case "baseExportCsvAction": exportData("csv"); break
        case "baseExportXlsxAction": exportData("xlsx"); break
        case "baseExportXmlAction": exportData("xml"); break
        case "baseNewMacroAction":
            leave(function() {
                window.macroDesignView.load(base.newMacroDesign())
                show("macroDesign", "macro", "")
                host.designHistory = true
            })
            break
        case "baseLabelsWizardAction": window.labelWizard.openFor(sel.kind, sel.name); break
        case "baseFormAddSubformAction": window.formControlDialog.openFor("subform", window.formView); break
        case "baseFormAddButtonAction": window.formControlDialog.openFor("button", window.formView); break
        case "baseReportGroupSortAction": window.reportView.toggleGroupSort(); break
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
            if (window.accessCommands.handles(id)) window.accessCommands.handle(id) // M86
            else if (id.indexOf("baseDesign") === 0) window.tableDesignView.command(id)
            else if (id.indexOf("baseQuery") === 0) window.queryDesignView.command(id)
            else if (id.indexOf("baseMacro") === 0) window.macroDesignView.command(id)
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
