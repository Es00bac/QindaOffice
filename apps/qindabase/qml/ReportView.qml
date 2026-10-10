// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Base.App

// A report: its pages as they print, and in Design the toolkit's
// ReportDesigner beside them — header and footer, page bands, grouping,
// detail rows — with Group, Sort and Total (M80: three group levels with
// their totals, the sort order and the report's own totals), every change
// one step of the design's own history, the pages rebuilt as you go.
// M80 (D-089 §6): the pages are the engine's evaluation of the design over
// the report's records (BaseService.buildReport — exact money in every
// total) laid out by QindaTK. A label report (§7) shows its sheets instead,
// painted as they print. The host prints and exports exactly what shows
// (BaseHost::setReportOutput).
Item {
    id: view

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property string reportName: ""
    property string table: ""
    property var parameters: ({})
    property bool designing: false
    property bool labels: false        // M80: a label report
    property bool groupSortVisible: false
    property var session: null
    property string savedRevision: ""
    property string notice: ""
    property int rowCount: 0
    property bool truncated: false
    readonly property var designDocument: session !== null ? session.model : null
    // M86: the source's fields with their QindaBase types, for Group On.
    readonly property var sourceInfo: reportName.length > 0 && base !== null && !labels ? (base.reportInfo(reportName).source || ({})) : ({})
    readonly property var sourceFields: sourceInfo.name ? base.sourceFields(sourceInfo.kind, sourceInfo.name) : []
    readonly property bool designDirty: designDocument !== null && designDocument.revision !== savedRevision
    readonly property alias output: output
    readonly property alias pages: pages
    readonly property int pageCount: labels ? (base !== null ? base.labels.pageCount : 0) : reportPages.pageCount

    function open(name, values, design) {
        const r = base.openReport(name, values, false)
        if (!r.ok) { window.notify(qsTr("Cannot open %1").arg(name), r.error, "danger"); return false }
        closeSession()
        labels = false
        snapshot.open(r.snapshotPath)
        session = manager.openDocument(r.designPath)
        if (session === null) { window.notify(qsTr("Cannot open %1").arg(name), manager.lastError, "danger"); return false }
        savedRevision = designDocument.revision
        reportName = name
        table = r.table
        parameters = values
        designing = design
        rowCount = r.rowCount
        truncated = r.truncated
        notice = r.notice || ""
        rebuild()
        toTop.restart()
        window.baseHost.setReportOutput(pages)
        syncHost()
        return true
    }
    // M80: a label report — its sheets, from the engine's placements.
    function openLabels(name, values) {
        const r = base.openLabels(name, values)
        if (!r.ok) { window.notify(qsTr("Cannot open %1").arg(name), r.error, "danger"); return false }
        closeSession()
        labels = true
        designing = false
        reportName = name
        table = r.table
        parameters = values
        rowCount = r.records
        truncated = r.truncated
        notice = ""
        window.baseHost.setReportOutput(base.labels)
        syncHost()
        return true
    }
    function closeSession() {
        if (session !== null) manager.closeDocument(session.documentId, Tk.DocumentManager.Discard)
        session = null
    }
    function rebuild() {
        if (labels) { refreshData(); return }
        if (designDocument === null) return
        const built = base.buildReport(pages, designDocument.report)
        output.built = output.built || built.ok
        if (!built.ok) { output.errorText = built.error; notice = qsTr("The last good pages are shown: %1").arg(built.error); return }
        output.errorText = ""
        output.rowCount = built.rowCount
        output.groupCount = built.groupCount
        output.bands = built.bands
        output.pageBreaks = built.pageBreaks || 0   // M86: Force New Page and Keep Together
        output.groupsMoved = built.groupsMoved || 0
    }
    // Fresh data from the database, the design as it is now.
    function refreshData() {
        if (labels) { openLabels(reportName, parameters); return }
        const r = base.openReport(reportName, parameters, true)
        if (!r.ok) { window.notify(qsTr("Not refreshed"), r.error, "danger"); return }
        snapshot.open(r.snapshotPath)
        rowCount = r.rowCount
        truncated = r.truncated
        rebuild()
    }
    function syncHost() {
        if (window === null || window.view !== "report") return
        window.baseHost.designDirty = designDirty
        window.baseHost.designHistory = designing
        window.baseHost.designCanUndo = designDocument !== null && designDocument.canUndo
        window.baseHost.designCanRedo = designDocument !== null && designDocument.canRedo
        window.baseHost.setChecked("baseReportGroupSortAction", groupSortVisible)
    }
    function toggleGroupSort() {
        if (labels) return
        groupSortVisible = !groupSortVisible
        syncHost()
    }
    onDesignDirtyChanged: syncHost()
    onDesigningChanged: syncHost()
    onVisibleChanged: if (visible) { syncHost(); window.baseHost.setReportOutput(labels ? base.labels : pages) }

    function save() {
        if (designDocument === null || !designDirty) return true
        const r = base.saveReportDesign(reportName, designDocument.design)
        if (!r.ok) { window.notify(qsTr("Report not saved"), r.error, "danger"); return false }
        savedRevision = designDocument.revision
        return true
    }
    function discard() { if (reportName.length && !labels) open(reportName, parameters, designing) }
    function undo() { if (designDocument !== null) designDocument.undo() }
    function redo() { if (designDocument !== null) designDocument.redo() }

    // AGENT-NOTE (D-004, AGENTS.md rule 3): the one literal colour allowed —
    // a report page is paper, white with black ink, on every theme, exactly
    // as it prints and exports.
    Binding { target: reportPages.area; property: "pageColor"; value: "#ffffff" }
    Binding { target: reportPages.area; property: "inkColor"; value: "#000000" }
    Binding { target: reportPages.area; property: "borderColor"; value: "#8c8c8c" }

    // AGENT-NOTE: the pages fit the pane once it has its width, and a zoom
    // change keeps the view's centre (RichTextArea) — so a freshly opened
    // report is sent to its first page after that settles.
    Timer { id: toTop; interval: Tk.Theme.motion.slow; onTriggered: reportPages.goToPage(0) }

    Tk.DocumentManager { id: manager }
    Tk.LocalDatabaseProvider { id: snapshot }
    Tk.RichTextDocument { id: pages; objectName: "reportDocument" }
    // What the last build gave (the tests and the header read it).
    QtObject {
        id: output
        objectName: "reportOutput"
        property bool built: false
        property int rowCount: 0
        property int groupCount: 0
        property string errorText: ""
        property var bands: []
        property int pageBreaks: 0  // M86 (base_report_layout.h)
        property int groupsMoved: 0
    }
    Connections {
        target: view.designDocument
        function onChanged() { Qt.callLater(view.rebuild) }
        function onHistoryChanged() { view.syncHost() }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        ReportHeaderBar { Tk.Flex.shrink: 0; view: view }
        Tk.Notice {
            visible: view.truncated
            variant: "info"
            text: qsTr("The report shows the first %1 records.").arg(view.rowCount)
        }
        Tk.Notice { visible: view.notice.length > 0; variant: "warning"; text: view.notice; dismissible: true; onDismissed: view.notice = "" }
        Tk.Splitter {
            visible: !view.labels
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            orientation: Qt.Horizontal
            Item {
                T.SplitView.fillWidth: true
                // M86: a narrow window gives the Group, Sort and Total pane its room.
                T.SplitView.minimumWidth: view.width < Tk.Theme.size.panelMinWidth * 2.5 ? Tk.Theme.size.panelMinWidth * 0.6
                                                                                         : Tk.Theme.size.panelMinWidth
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Row
                    gap: 0
                    Tk.PageNavigator {
                        visible: view.width > Tk.Theme.size.panelMinWidth * 3
                        // (no report yet: no thumbnails, whose page ids are random)
                        view: view.reportName.length > 0 && !view.labels ? reportPages : null
                        Tk.Flex.shrink: 0
                    }
                    Item {
                        id: pagesHost
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Tk.RichDocumentView {
                            id: reportPages
                            objectName: "reportPages"
                            anchors.fill: parent
                            document: pages
                            readOnly: true
                            // Fit the page's width to the pane (never above 100 %), then
                            // apply View ▸ Zoom; pageSize is in unzoomed units.
                            zoom: view.window !== null ? view.window.baseHost.zoom * Math.min(1, Math.max(0.3,
                                (pagesHost.width - reportPages.area.padding * 2 - Tk.Theme.size.scrollbar)
                                / Math.max(1, reportPages.area.pageSize.width))) : 1
                            tooltip: qsTr("Report pages")
                        }
                    }
                }
            }
            Tk.Scroll {
                id: designScroll
                visible: view.designing || view.groupSortVisible
                T.SplitView.preferredWidth: Math.min(view.width * 0.45, Tk.Theme.size.panelMinWidth * 2)
                T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
                accessibleName: qsTr("Report design")
                Tk.Flex {
                    width: designScroll.width
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    padding: Tk.Theme.space.md
                    ReportGroupsPanel {
                        objectName: "reportGroupsPanel"
                        visible: view.groupSortVisible || view.designing
                        width: designScroll.width - Tk.Theme.space.md * 2
                        base: view.base
                        designDocument: view.designDocument
                        provider: snapshot
                        table: view.table
                        fieldTypes: {
                            const out = ({})
                            for (const f of view.sourceFields) out[f.name] = f.type === "calculated" ? f.resultType : f.type
                            return out
                        }
                        lookups: view.sourceFields.filter(function(f) { return !!f.lookupTable }).map(function(f) { return f.name })
                    }
                    Tk.ReportDesigner {
                        objectName: "baseReportDesigner"
                        visible: view.designing
                        width: designScroll.width - Tk.Theme.space.md * 2
                        provider: snapshot
                        designDocument: view.designDocument
                        onExportRequested: function(requestId, reportDesign) { view.window.trigger("baseExportPdfAction") }
                    }
                }
            }
        }
        LabelPages {
            visible: view.labels
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            view: view
        }
    }
}
