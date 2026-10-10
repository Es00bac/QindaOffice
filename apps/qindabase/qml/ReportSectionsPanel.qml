// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The report's own sections and running sums (M86, D-094, base_report.h):
// a title page (Force New Page after the report header), the detail
// section's Force New Page (each record on a page of its own, …), the
// report's totals on a page of their own, and running-sum columns — a
// number field summed as the records go, or the records numbered — over
// the innermost group or over the report. Changes go to the pane
// (ReportGroupsPanel.setReport) as one design step each.
Tk.Flex {
    id: sections

    required property var panel
    readonly property var report: panel.report
    readonly property var parts: report.sections || ({})
    readonly property var runs: report.runningSums || []
    readonly property var numbers: panel.fields.filter(function(f) {
        return ["integer", "largeNumber", "number", "currency"].indexOf(sections.panel.fieldTypes[f] || "") >= 0
    })
    readonly property var newPages: [{ id: "none", label: qsTr("no new page") }, { id: "before", label: qsTr("new page before each record") },
                                     { id: "after", label: qsTr("new page after each record") }, { id: "both", label: qsTr("before and after") }]
    function setPart(which, patch) {
        const next = JSON.parse(JSON.stringify(parts))
        next[which] = Object.assign({}, next[which] || ({ newPage: "none", keepTogether: false }), patch)
        panel.setReport("sections", next, [])
    }
    function setRuns(next) { panel.setReport("runningSums", next, next.map(function(r) { return r.field }).filter(function(f) { return f })) }

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Tk.Overline { title: qsTr("PAGES") }
    Tk.CheckBox {
        objectName: "reportTitlePage"
        text: qsTr("Title on a page of its own")
        checked: (sections.parts.reportHeader || ({})).newPage === "after"
        onToggled: sections.setPart("reportHeader", { newPage: checked ? "after" : "none" })
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Caption { text: qsTr("Records"); Tk.Flex.shrink: 0 }
        Tk.ComboBox {
            objectName: "reportDetailPage"
            small: true
            textRole: "label"
            model: sections.newPages
            currentIndex: Math.max(0, sections.newPages.findIndex(function(p) { return p.id === ((sections.parts.detail || ({})).newPage || "none") }))
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            onActivated: function(i) { sections.setPart("detail", { newPage: sections.newPages[i].id }) }
        }
    }
    Tk.CheckBox {
        objectName: "reportTotalsPage"
        text: qsTr("Report totals on a page of their own")
        checked: (sections.parts.reportFooter || ({})).newPage === "before"
        onToggled: sections.setPart("reportFooter", { newPage: checked ? "before" : "none" })
    }
    Tk.Overline { title: qsTr("RUNNING SUMS") }
    Repeater {
        model: sections.runs
        delegate: Tk.Flex {
            id: run
            required property var modelData
            required property int index
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            wrap: Tk.Flex.Wrap
            Tk.ComboBox {
                objectName: "reportRunningField_" + run.index
                readonly property var choices: [qsTr("(number the records)")].concat(sections.numbers)
                small: true
                model: choices
                currentIndex: run.modelData.field ? Math.max(0, choices.indexOf(run.modelData.field)) : 0
                Tk.Flex.grow: 1
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.5
                onActivated: function(i) {
                    const next = JSON.parse(JSON.stringify(sections.runs))
                    next[run.index].field = i === 0 ? "" : choices[i]
                    sections.setRuns(next)
                }
            }
            Tk.Segmented {
                objectName: "reportRunningOver_" + run.index
                small: true
                model: [qsTr("Over the group"), qsTr("Over all")]
                currentIndex: run.modelData.over === "group" ? 0 : 1
                onActivated: function(i) {
                    const next = JSON.parse(JSON.stringify(sections.runs))
                    next[run.index].over = i === 0 ? "group" : "all"
                    sections.setRuns(next)
                }
            }
            Tk.TextField {
                small: true
                text: run.modelData.label || ""
                placeholderText: qsTr("Column label")
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.5
                onEditingFinished: {
                    const next = JSON.parse(JSON.stringify(sections.runs))
                    next[run.index].label = text.trim()
                    sections.setRuns(next)
                }
            }
            Tk.IconButton {
                iconName: "x"; small: true; tooltip: qsTr("Remove this running sum")
                onClicked: { const next = sections.runs.slice(); next.splice(run.index, 1); sections.setRuns(next) }
            }
        }
    }
    Tk.Button {
        objectName: "reportAddRunning"
        text: qsTr("Add a running sum")
        iconName: "sigma"
        small: true
        enabled: sections.panel.designDocument !== null
        onClicked: sections.setRuns(sections.runs.concat([{ field: sections.numbers[0] || "", over: "all", label: "" }]))
    }
}
