// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Reports: Project Overview, Critical Tasks, Resource Overview, Late Tasks
// and (M79) Project's Cost Overview and Work Overview dashboards, built by
// the engine (PlanService.report) — the same data the printed/PDF report
// paints. Headline figures as cards, then each table; a task row opens the
// task in the Gantt.
Tk.Scroll {
    id: view

    property PlanService plan: null
    property PlanHost host: null
    readonly property var report: plan !== null && plan.revision >= 0 && host !== null && visible
                                  ? plan.report(host.reportId) : ({})
    readonly property var reportIds: ["overview", "critical", "resources", "late", "cost", "work"]

    objectName: "planReports"
    accessibleName: String(report.title || qsTr("Report"))

    Tk.Flex {
        width: view.viewportWidth
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.lg
        padding: Tk.Theme.space.xl

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Flex.grow: 1
                Tk.Flex.basis: Tk.Theme.size.panelMinWidth
                Tk.Heading { objectName: "planReportTitle"; level: 1; text: String(view.report.title || "") }
                Tk.Caption { objectName: "planReportSubtitle"; text: String(view.report.subtitle || ""); elide: Text.ElideRight }
            }
            Tk.ComboBox {
                objectName: "planReportPicker"
                small: true
                tooltip: qsTr("Report")
                model: [qsTr("Project Overview"), qsTr("Critical Tasks"), qsTr("Resource Overview"), qsTr("Late Tasks"),
                        qsTr("Cost Overview"), qsTr("Work Overview")]
                currentIndex: view.host !== null ? Math.max(0, view.reportIds.indexOf(view.host.reportId)) : 0
                onActivated: function(index) { view.host.reportId = view.reportIds[index] }
            }
            Tk.Button {
                objectName: "planReportPdf"
                small: true
                variant: "outline"
                iconName: "file-output"
                text: qsTr("PDF…")
                onClicked: view.host.actions.trigger("planExportPdfAction")
            }
            Tk.Button {
                small: true
                variant: "outline"
                iconName: "printer"
                text: qsTr("Print…")
                onClicked: view.host.actions.trigger("filePrintAction")
            }
        }

        Tk.Grid {
            objectName: "planReportKpis"
            columns: "repeat(auto-fill, minmax(" + Tk.Theme.size.panelMinWidth * 0.85 + ", 1fr))"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.md
            Repeater {
                model: view.report.kpis || []
                delegate: ReportKpi {
                    required property var modelData
                    kpi: modelData
                }
            }
        }

        Repeater {
            model: view.report.tables || []
            delegate: Tk.Card {
                id: tableCard
                required property var modelData
                readonly property var numeric: modelData.numericColumns || []
                objectName: "planReportTable_" + String(modelData.id)
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        Tk.Heading { level: 3; text: String(tableCard.modelData.title); Tk.Flex.shrink: 0 }
                        Tk.Badge { text: String((tableCard.modelData.rows || []).length); variant: "default" }
                    }
                    Tk.Caption {
                        visible: (tableCard.modelData.rows || []).length === 0
                        text: String(tableCard.modelData.emptyText || "")
                    }
                    Tk.DataTable {
                        id: table
                        visible: (tableCard.modelData.rows || []).length > 0
                        implicitHeight: Tk.Theme.size.row * (Math.min(14, (tableCard.modelData.rows || []).length) + 1.2)
                        tooltip: String(tableCard.modelData.title)
                        model: tableCard.modelData.rows || []
                        alternatingRows: true
                        columns: []
                        Component.onCompleted: {
                            const cols = []
                            const titles = tableCard.modelData.columns || []
                            // The name column stretches; an ID column before it is narrow.
                            const byId = titles.length > 0 && titles[0] === "ID"
                            for (let c = 0; c < titles.length; ++c) {
                                cols.push(columnComponent.createObject(table, {
                                    key: "c" + c, title: titles[c], sortable: false,
                                    flex: c === (byId ? 1 : 0) ? 1 : 0,
                                    minWidth: Tk.Theme.size.tableColumn * 1.6,
                                    width: Tk.Theme.size.tableColumn * (byId && c === 0 ? 0.6 : 1.3),
                                    align: tableCard.numeric.indexOf(c) >= 0 ? Qt.AlignRight : Qt.AlignLeft }))
                            }
                            table.columns = cols
                        }
                        onActivated: function(index) {
                            const row = (tableCard.modelData.rows || [])[index]
                            if (row && row.taskId) {
                                view.plan.currentTaskId = row.taskId
                                view.host.actions.trigger("planViewGanttAction")
                                view.plan.revealTask(row.taskId)
                            }
                        }
                    }
                }
            }
        }
    }
    Component {
        id: columnComponent
        Tk.TableColumn { }
    }
}
