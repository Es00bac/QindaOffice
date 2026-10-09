// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Books.App

// Reports (D-045/D-047): the catalog grouped as the engine groups it
// (business overview, who owes you, what you owe, sales, expenses, taxes,
// accountant), the options a report takes (period or as-of date, basis,
// comparison, columns), the rendered table, drill-down, and PDF/CSV/Excel/
// print of exactly what is on screen (BooksHost.setPrintSource).
Item {
    id: view

    property BooksService books: null
    property BooksHost host: null
    property var catalog: []
    property string reportId: "profit_and_loss"
    property var report: ({})
    property string from: ""
    property string to: ""
    property string asOf: ""
    readonly property var entry: catalog.find(function(c) { return c.id === view.reportId }) || ({ options: [] })
    readonly property var groups: {
        const out = []
        for (const c of catalog) if (out.indexOf(c.group) < 0) out.push(c.group)
        return out
    }
    readonly property var presets: [
        { id: "thisMonth", text: qsTr("This month") }, { id: "lastMonth", text: qsTr("Last month") },
        { id: "thisQuarter", text: qsTr("This quarter") }, { id: "lastQuarter", text: qsTr("Last quarter") },
        { id: "yearToDate", text: qsTr("Year to date") }, { id: "thisYear", text: qsTr("This fiscal year") },
        { id: "lastYear", text: qsTr("Last fiscal year") }, { id: "last12Months", text: qsTr("Last 12 months") }]

    function has(option) { return (entry.options || []).indexOf(option) >= 0 }
    function options() {
        return { from: from, to: to, asOf: asOf, basis: basisBox.currentIndex === 1 ? "cash" : "accrual",
                 comparison: ["none", "priorPeriod", "priorYear"][compareBox.currentIndex],
                 columns: ["total", "months", "classes"][columnsBox.currentIndex] }
    }
    function run() {
        if (books === null || !books.hasCompany) return
        report = books.runReport(reportId, options())
        host.setPrintSource("report", reportId, options())
    }
    property string preset: "thisMonth"
    // M71: a narrow window gives the report the width and picks it from a list.
    readonly property bool narrow: width < Tk.Theme.size.panelMinWidth * 3
    function applyPreset(id) { const r = books.rangePreset(id); preset = id; from = r.from; to = r.to; asOf = r.to; run() }
    function exportTo(url, format) { books.exportReport(reportId, options(), url, format) }
    onVisibleChanged: if (visible && catalog.length === 0 && books !== null && books.hasCompany) {
        catalog = books.reportCatalog()
        applyPreset("thisMonth")
    }
    onReportIdChanged: run()
    Connections { target: view.books; function onChanged() { if (view.visible) view.run() } }
    Connections {
        target: view.host !== null ? view.host.actions.entry("booksRunReportAction") : null
        function onTriggered() { view.run() }
    }

    Tk.Splitter {
        anchors.fill: parent
        orientation: Qt.Horizontal
        Tk.Box {
            objectName: "reportCatalog"
            visible: !view.narrow
            T.SplitView.preferredWidth: Tk.Theme.size.panelMinWidth * 1.3
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
            color: Tk.Theme.color.panel
            padding: 0
            Tk.Scroll {
                id: catalogScroll
                anchors.fill: parent
                Tk.Flex {
                    width: catalogScroll.viewportWidth
                    direction: Tk.Flex.Column
                    padding: Tk.Theme.space.sm
                    gap: Tk.Theme.space.xs
                    Repeater {
                        model: view.groups
                        delegate: Tk.Flex {
                            id: group
                            required property var modelData
                            direction: Tk.Flex.Column
                            Tk.SectionHeader { title: String(group.modelData) }
                            Repeater {
                                model: view.catalog.filter(function(c) { return c.group === group.modelData })
                                delegate: Tk.ListRow {
                                    required property var modelData
                                    objectName: "reportEntry_" + modelData.id
                                    text: String(modelData.title)
                                    tooltip: String(modelData.definition || "")
                                    selected: view.reportId === modelData.id
                                    onClicked: view.reportId = modelData.id
                                }
                            }
                        }
                    }
                }
            }
        }
        Tk.Box {
            T.SplitView.fillWidth: true
            color: Tk.Theme.color.surface
            padding: Tk.Theme.space.lg
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Flex {
                    objectName: "reportOptions"
                    direction: Tk.Flex.Row
                    wrap: Tk.Flex.Wrap
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    Tk.ComboBox {
                        objectName: "reportPicker"
                        visible: view.narrow
                        small: true
                        textRole: "title"
                        model: view.catalog
                        currentIndex: view.catalog.findIndex(function(c) { return c.id === view.reportId })
                        onActivated: function(i) { view.reportId = view.catalog[i].id }
                    }
                    Tk.ComboBox {
                        objectName: "reportPreset"
                        small: true
                        textRole: "text"
                        model: view.presets
                        // M71: a preset chosen elsewhere (Payroll Tax Forms) shows here too.
                        currentIndex: Math.max(0, view.presets.map(function(p) { return p.id }).indexOf(view.preset))
                        onActivated: function(i) { view.applyPreset(view.presets[i].id) }
                    }
                    Tk.TextField { visible: view.has("from"); small: true; mono: true; implicitWidth: Tk.Theme.size.fieldWidth; text: view.from; onEditingFinished: { view.from = text; view.run() } }
                    Tk.Caption { visible: view.has("from"); text: qsTr("to") }
                    Tk.TextField { visible: view.has("to"); small: true; mono: true; implicitWidth: Tk.Theme.size.fieldWidth; text: view.to; onEditingFinished: { view.to = text; view.run() } }
                    Tk.Caption { visible: view.has("asOf"); text: qsTr("as of") }
                    Tk.TextField { visible: view.has("asOf"); small: true; mono: true; implicitWidth: Tk.Theme.size.fieldWidth; text: view.asOf; onEditingFinished: { view.asOf = text; view.run() } }
                    Tk.Segmented { id: basisBox; visible: view.has("basis"); small: true; model: [qsTr("Accrual"), qsTr("Cash")]; onActivated: view.run() }
                    Tk.ComboBox { id: compareBox; visible: view.has("comparison"); small: true; model: [qsTr("No comparison"), qsTr("Previous period"), qsTr("Previous year")]; onActivated: view.run() }
                    Tk.ComboBox { id: columnsBox; visible: view.has("columns"); small: true; model: [qsTr("Total only"), qsTr("By month"), qsTr("By class")]; onActivated: view.run() }
                    Tk.Spacer { }
                    Tk.IconButton { iconName: "printer"; tooltip: qsTr("Print this report"); onClicked: view.host.actions.trigger("filePrintAction") }
                    Tk.Button { small: true; variant: "ghost"; iconName: "file-down"; text: "PDF"; onClicked: view.host.actions.trigger("booksExportReportPdfAction") }
                    Tk.Button { small: true; variant: "ghost"; iconName: "file-spreadsheet"; text: "CSV"; onClicked: view.host.actions.trigger("booksExportReportCsvAction") }
                    Tk.Button { objectName: "reportExcel"; small: true; variant: "ghost"; iconName: "sheet"; text: "Excel"; onClicked: view.host.actions.trigger("booksExportReportXlsxAction") }
                }
                ReportTableView {
                    objectName: "reportTable"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    report: view.report
                    onDrillRequested: function(drill, label) { drillDialog.openFor(drill, label) }
                }
            }
        }
    }
    Tk.Dialog {
        id: drillDialog
        property var rows: []
        title: qsTr("Transactions")
        dialogWidth: Tk.Theme.size.dialogWidth * 2
        primaryText: qsTr("Close")
        secondaryText: ""
        function openFor(drill, label) { subtitle = label; rows = view.books.drillDown(drill); open() }
        Tk.DataTable {
            implicitHeight: Tk.Theme.size.panelMinHeight * 3
            model: drillDialog.rows
            alternatingRows: true
            onActivated: function(index) { const r = drillDialog.rows[index]; if (r.documentId) view.host.documentEditorRequested(String(r.kind), r.documentId) }
            columns: [
                Tk.TableColumn { key: "date"; title: qsTr("Date"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
                Tk.TableColumn { key: "kindLabel"; title: qsTr("Type"); width: Tk.Theme.size.tableColumn * 1.3 },
                Tk.TableColumn { key: "number"; title: qsTr("No."); width: Tk.Theme.size.tableColumn; mono: true },
                Tk.TableColumn { key: "name"; title: qsTr("Name"); flex: 1 },
                Tk.TableColumn { key: "memo"; title: qsTr("Memo"); flex: 1; muted: true },
                Tk.TableColumn { key: "amountText"; title: qsTr("Amount"); width: Tk.Theme.size.tableColumn * 1.4; align: Qt.AlignRight; mono: true }
            ]
        }
    }
}
