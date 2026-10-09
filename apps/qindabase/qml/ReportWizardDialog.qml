// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The report wizard: a table or saved query, the columns to print, up to
// two grouping levels (M80; a third is added in Group, Sort and Total) with
// a total (sum, average, count, minimum, maximum of a number or money
// field) in every group footer and the report's, a sort order, a title and
// the paper orientation. The report opens in Preview; Design refines it
// with QindaTK's ReportDesigner and Group, Sort and Total.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property string sourceKind: "table"
    property string sourceName: ""
    property var fields: []
    property var chosen: []
    property string groupField: ""
    property string groupField2: ""   // M80: a second level
    property string sortField: ""     // M80: the order inside the groups
    property bool sortDescending: false
    property string totalField: ""
    property string totalFunction: "sum"
    property bool landscape: false
    readonly property var sources: base === null ? []
        : base.tableNames.map(function(t) { return { kind: "table", name: t, label: t } })
            .concat(base.queryNames.map(function(q) { return { kind: "query", name: q, label: qsTr("%1 (query)").arg(q) } }))
    readonly property var numeric: fields.filter(function(f) {
        return ["integer", "number", "currency"].indexOf(f.type) >= 0 && f.name !== groupField
    }).map(function(f) { return f.name })
    readonly property var functions: [{ id: "sum", label: qsTr("Sum") }, { id: "average", label: qsTr("Average") },
                                      { id: "countValues", label: qsTr("Count") }, { id: "minimum", label: qsTr("Minimum") },
                                      { id: "maximum", label: qsTr("Maximum") }]

    title: qsTr("Report wizard")
    subtitle: qsTr("A printed list, grouped and totalled.")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("Create report")
    secondaryText: qsTr("Cancel")
    primaryEnabled: sourceName.length > 0 && chosen.length > 0 && nameField.text.trim().length > 0

    function openFor(kind, name) {
        const match = sources.find(function(s) { return s.kind === kind && s.name === name })
        const first = match || sources[0]
        if (first) choose(first.kind, first.name)
        open()
    }
    function choose(kind, name) {
        sourceKind = kind
        sourceName = name
        fields = base.sourceFields(kind, name)
        chosen = fields.filter(function(f) { return f.type !== "autoNumber" }).slice(0, 6).map(function(f) { return f.name })
        groupField = ""
        groupField2 = ""
        sortField = ""
        totalField = ""
        titleField.text = name.charAt(0).toUpperCase() + name.slice(1)
        nameField.text = qsTr("%1 Report").arg(name.charAt(0).toUpperCase() + name.slice(1))
    }
    function toggle(name, on) {
        chosen = fields.map(function(f) { return f.name }).filter(function(n) { return n === name ? on : chosen.indexOf(n) >= 0 })
    }
    onAccepted: {
        const totals = totalField ? [{ field: totalField, function: totalFunction }] : []
        const spec = {
            source: { kind: sourceKind, name: sourceName },
            title: titleField.text.trim(),
            columns: chosen.filter(function(n) { return n !== groupField && n !== groupField2 })
                           .map(function(n) { return { field: n, label: "" } }),
            groups: [groupField, groupField2].filter(function(g, i, all) { return g && all.indexOf(g) === i })
                        .map(function(g) { return { field: g, label: "", pageBreakBefore: false, totals: totals } }),
            totals: totals,
            sort: sortField ? [{ field: sortField, descending: sortDescending }] : [],
            detail: true,
            orientation: landscape ? "landscape" : "portrait"
        }
        const r = base.createReport(nameField.text.trim(), spec)
        if (!r.ok) { window.notify(qsTr("No report created"), r.error, "danger"); return }
        window.baseHost.selectedKind = "report"
        window.baseHost.selectedName = r.name
        window.commands.openObject("report", r.name)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Records from")
            Tk.ComboBox {
                objectName: "reportWizardSource"
                small: true
                textRole: "label"
                model: dialog.sources
                currentIndex: dialog.sources.findIndex(function(s) { return s.kind === dialog.sourceKind && s.name === dialog.sourceName })
                onActivated: function(i) { dialog.choose(dialog.sources[i].kind, dialog.sources[i].name) }
            }
        }
        Tk.PropertyRow { label: qsTr("Title"); Tk.TextField { id: titleField; small: true } }
        Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "reportWizardName"; small: true } }
        Tk.Overline { title: qsTr("COLUMNS") }
        Tk.Grid {
            columns: "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.md
            Repeater {
                model: dialog.fields
                delegate: Tk.CheckBox {
                    required property var modelData
                    text: modelData.caption || modelData.name
                    checked: dialog.chosen.indexOf(modelData.name) >= 0
                    onToggled: dialog.toggle(modelData.name, checked)
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Group by")
            Tk.ComboBox {
                objectName: "reportWizardGroup"
                small: true
                model: [qsTr("(no grouping)")].concat(dialog.fields.map(function(f) { return f.name }))
                currentIndex: Math.max(0, model.indexOf(dialog.groupField))
                onActivated: function(i) { dialog.groupField = i === 0 ? "" : model[i] }
            }
        }
        Tk.PropertyRow {
            visible: dialog.groupField.length > 0
            label: qsTr("Then by")
            Tk.ComboBox {
                objectName: "reportWizardGroup2"
                small: true
                model: [qsTr("(no second level)")].concat(dialog.fields.map(function(f) { return f.name })
                                                               .filter(function(n) { return n !== dialog.groupField }))
                currentIndex: Math.max(0, model.indexOf(dialog.groupField2))
                onActivated: function(i) { dialog.groupField2 = i === 0 ? "" : model[i] }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Sort by")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                align: Tk.Flex.Center
                Tk.ComboBox {
                    objectName: "reportWizardSort"
                    small: true
                    model: [qsTr("(as stored)")].concat(dialog.fields.map(function(f) { return f.name }))
                    currentIndex: Math.max(0, model.indexOf(dialog.sortField))
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.sortField = i === 0 ? "" : model[i] }
                }
                Tk.CheckBox { text: qsTr("Z → A"); checked: dialog.sortDescending; onToggled: dialog.sortDescending = checked }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Total")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.ComboBox {
                    objectName: "reportWizardTotal"
                    small: true
                    model: [qsTr("(none)")].concat(dialog.numeric)
                    currentIndex: Math.max(0, model.indexOf(dialog.totalField))
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.totalField = i === 0 ? "" : model[i] }
                }
                Tk.ComboBox {
                    small: true
                    enabled: dialog.totalField.length > 0
                    textRole: "label"
                    model: dialog.functions
                    currentIndex: Math.max(0, dialog.functions.findIndex(function(f) { return f.id === dialog.totalFunction }))
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.totalFunction = dialog.functions[i].id }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Paper")
            Tk.Segmented {
                small: true
                model: [qsTr("Portrait"), qsTr("Landscape")]
                currentIndex: dialog.landscape ? 1 : 0
                onActivated: function(i) { dialog.landscape = i === 1 }
            }
        }
    }
}
