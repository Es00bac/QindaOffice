// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// A report: its pages as they print (QindaTK's LocalReportDocumentModel
// over a snapshot of the report's table or query, shown in a
// RichDocumentView), and in Design the toolkit's ReportDesigner beside
// them — header and footer, grouping with totals, page bands, detail rows
// — every change one step of the design's own history, the pages rebuilt
// as you go. The host prints and exports exactly these pages
// (BaseHost::setReportOutput).
Item {
    id: view

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property string reportName: ""
    property string table: ""
    property var parameters: ({})
    property bool designing: false
    property var session: null
    property string savedRevision: ""
    property string notice: ""
    property int rowCount: 0
    property bool truncated: false
    readonly property var designDocument: session !== null ? session.model : null
    readonly property bool designDirty: designDocument !== null && designDocument.revision !== savedRevision
    readonly property alias output: output

    function open(name, values, design) {
        const r = base.openReport(name, values, false)
        if (!r.ok) { window.notify(qsTr("Cannot open %1").arg(name), r.error, "danger"); return false }
        closeSession()
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
        Qt.callLater(function() { reportPages.goToPage(0) })
        window.baseHost.setReportOutput(output)
        syncHost()
        return true
    }
    function closeSession() {
        if (session !== null) manager.closeDocument(session.documentId, Tk.DocumentManager.Discard)
        session = null
    }
    function rebuild() {
        if (designDocument === null) return
        const built = output.build(snapshot, designDocument.report)
        if (!built.ok) notice = qsTr("The last good pages are shown: %1").arg(built.error)
    }
    // Fresh data from the database, the design as it is now.
    function refreshData() {
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
    }
    onDesignDirtyChanged: syncHost()
    onDesigningChanged: syncHost()
    onVisibleChanged: if (visible) { syncHost(); window.baseHost.setReportOutput(output) }

    function save() {
        if (designDocument === null || !designDirty) return true
        const r = base.saveReportDesign(reportName, designDocument.design)
        if (!r.ok) { window.notify(qsTr("Report not saved"), r.error, "danger"); return false }
        savedRevision = designDocument.revision
        return true
    }
    function discard() { if (reportName.length) open(reportName, parameters, designing) }
    function undo() { if (designDocument !== null) designDocument.undo() }
    function redo() { if (designDocument !== null) designDocument.redo() }

    // AGENT-NOTE (D-004, AGENTS.md rule 3): the one literal colour allowed —
    // a report page is paper, white with black ink, on every theme, exactly
    // as it prints and exports.
    Binding { target: reportPages.area; property: "pageColor"; value: "#ffffff" }
    Binding { target: reportPages.area; property: "inkColor"; value: "#000000" }
    Binding { target: reportPages.area; property: "borderColor"; value: "#8c8c8c" }

    Tk.DocumentManager { id: manager }
    Tk.LocalDatabaseProvider { id: snapshot }
    Tk.LocalReportDocumentModel { id: output; objectName: "reportOutput" }
    Connections {
        target: view.designDocument
        function onChanged() { Qt.callLater(view.rebuild) }
        function onHistoryChanged() { view.syncHost() }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Box {
            objectName: "reportHeader"
            Tk.Flex.shrink: 0
            color: Tk.Theme.color.surface
            padding: Tk.Theme.space.sm
            paddingLeft: Tk.Theme.space.md
            borderBottom: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                wrap: Tk.Flex.Wrap
                Tk.Icon { name: "scroll-text"; size: Tk.Theme.size.icon; color: Tk.Theme.color.accent }
                Tk.Heading { objectName: "reportName"; level: 3; text: view.reportName; Tk.Flex.shrink: 0 }
                Tk.Caption {
                    objectName: "reportStats"
                    text: qsTr("%1 · %n record(s) · %2 page(s)", "", view.rowCount).arg(view.table).arg(reportPages.pageCount)
                    Tk.Flex.shrink: 0
                }
                Tk.Spacer { }
                Tk.Segmented {
                    objectName: "reportMode"
                    small: true
                    model: [{ text: qsTr("Preview"), iconName: "eye" }, { text: qsTr("Design"), iconName: "scroll-text" }]
                    currentIndex: view.designing ? 1 : 0
                    onActivated: function(i) { view.designing = i === 1 }
                }
                Tk.Button { text: qsTr("Save design"); iconName: "save"; enabled: view.designDirty; onClicked: view.save() }
                Tk.IconButton { iconName: "refresh-ccw"; tooltip: qsTr("Refresh the data"); onClicked: view.refreshData() }
                Tk.Button { objectName: "reportPrint"; text: qsTr("Print…"); iconName: "printer"; variant: "accent"; onClicked: view.window.trigger("filePrintAction") }
            }
        }
        Tk.Notice {
            visible: view.truncated
            variant: "info"
            text: qsTr("The report shows the first %1 records.").arg(view.rowCount)
        }
        Tk.Notice { visible: view.notice.length > 0; variant: "warning"; text: view.notice; dismissible: true; onDismissed: view.notice = "" }
        Tk.Splitter {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            orientation: Qt.Horizontal
            Item {
                T.SplitView.fillWidth: true
                T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Row
                    gap: 0
                    Tk.PageNavigator {
                        visible: view.width > Tk.Theme.size.panelMinWidth * 3
                        // (no report yet: no thumbnails, whose page ids are random)
                        view: view.reportName.length > 0 ? reportPages : null
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
                            document: output.document
                            readOnly: true
                            zoom: view.window !== null ? view.window.baseHost.zoom * Math.min(1, Math.max(0.45, (pagesHost.width - Tk.Theme.space.xl * 2) / (Tk.Theme.size.panelMinWidth * 3.3))) : 1
                            tooltip: qsTr("Report pages")
                        }
                    }
                }
            }
            Tk.Scroll {
                id: designScroll
                visible: view.designing
                T.SplitView.preferredWidth: Math.min(view.width * 0.45, Tk.Theme.size.panelMinWidth * 2)
                T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
                accessibleName: qsTr("Report design")
                Tk.Flex {
                    width: designScroll.width
                    direction: Tk.Flex.Column
                    padding: Tk.Theme.space.md
                    Tk.ReportDesigner {
                        objectName: "baseReportDesigner"
                        width: designScroll.width - Tk.Theme.space.md * 2
                        provider: snapshot
                        designDocument: view.designDocument
                        onExportRequested: function(requestId, reportDesign) { view.window.trigger("baseExportPdfAction") }
                    }
                }
            }
        }
    }
}
