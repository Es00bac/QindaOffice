// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Group, Sort and Total (M80, D-089 §6): Access's pane for a report's
// grouping — up to three group levels (QindaTK's report design holds
// three), each with a new page when it changes and the totals its footer
// prints; the sort order inside the groups (four keys in all, groups
// included); and the report's own totals. Every change is one step of the
// design document's history (a field not yet bound is bound first), and
// the pages rebuild from it with the engine's exact totals.
Tk.Flex {
    id: panel

    property var designDocument: null
    property var provider: null
    property string table: ""
    readonly property var report: designDocument !== null ? designDocument.report : ({})
    readonly property var groups: report.groups || []
    readonly property var sort: (report.query || ({})).sort || []
    readonly property var totals: report.totals || []
    readonly property var fields: provider !== null && table.length > 0 && provider.connected
                                  ? (provider.schema(table).fields || []).map(function(f) { return String(f.name) }) : []
    readonly property var functions: [
        { id: "sum", label: qsTr("Sum") }, { id: "average", label: qsTr("Average") },
        { id: "countValues", label: qsTr("Count") }, { id: "minimum", label: qsTr("Lowest") },
        { id: "maximum", label: qsTr("Highest") }]
    signal failed(string error)

    function copy(value) { return JSON.parse(JSON.stringify(value)) }
    // Binds `names` to the design when they are not yet (one history step).
    function bound(names) {
        const have = designDocument.boundFields
        const missing = names.filter(function(n) { return n && have.indexOf(n) < 0 })
        return missing.length === 0 || designDocument.bindTable(provider, table, have.concat(missing))
    }
    function apply(ok) { if (!ok) failed(designDocument.lastError); return ok }
    function setGroups(next) {
        const names = []
        next.forEach(function(g) { names.push(g.field); (g.totals || []).forEach(function(t) { names.push(t.field) }) })
        return apply(bound(names) && designDocument.setReportGroups(next))
    }
    function setReport(key, value, names) {
        if (!apply(bound(names))) return false
        const design = copy(designDocument.design)
        const r = design.report || ({})
        if (key === "sort") r.query = Object.assign({}, r.query || ({}), { sort: value })
        else r[key] = value
        design.report = r
        return apply(designDocument.setDesign(design))
    }
    function setGroup(index, patch) { const next = copy(groups); Object.assign(next[index], patch); setGroups(next) }
    function addGroup() {
        const used = groups.map(function(g) { return g.field })
        const field = fields.find(function(f) { return used.indexOf(f) < 0 })
        if (field) setGroups(copy(groups).concat([{ field: field, pageBreakBefore: false, totals: [] }]))
    }

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Tk.SectionHeader { title: qsTr("Group, sort and total") }
    Repeater {
        model: panel.groups
        delegate: Tk.Card {
            id: level
            required property var modelData
            required property int index
            objectName: "reportGroup_" + index
            padding: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    wrap: Tk.Flex.Wrap
                    Tk.Caption { text: level.index === 0 ? qsTr("Group on") : qsTr("then on"); Tk.Flex.shrink: 0 }
                    Tk.ComboBox {
                        objectName: "reportGroupField_" + level.index
                        small: true
                        model: panel.fields
                        currentIndex: model.indexOf(level.modelData.field)
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.6
                        onActivated: function(i) { panel.setGroup(level.index, { field: panel.fields[i] }) }
                    }
                    Tk.IconButton {
                        iconName: "x"; small: true; tooltip: qsTr("Remove this group level")
                        onClicked: { const next = panel.copy(panel.groups); next.splice(level.index, 1); panel.setGroups(next) }
                    }
                }
                Tk.CheckBox {
                    text: qsTr("Start a new page")
                    checked: level.modelData.pageBreakBefore === true
                    tooltip: qsTr("Start each group after the first on a new page")
                    onToggled: panel.setGroup(level.index, { pageBreakBefore: checked })
                }
                ReportTotalsEditor {
                    objectPrefix: "reportGroupTotal_" + level.index
                    totals: level.modelData.totals || []
                    fields: panel.fields
                    functions: panel.functions
                    onEdited: function(next) { panel.setGroup(level.index, { totals: next }) }
                }
            }
        }
    }
    Tk.Button {
        objectName: "reportAddGroup"
        visible: panel.groups.length < 3
        text: qsTr("Add a group")
        iconName: "list-tree"
        small: true
        enabled: panel.designDocument !== null && panel.fields.length > 0
        onClicked: panel.addGroup()
    }
    Tk.Overline { title: qsTr("SORT WITHIN THE GROUPS") }
    Repeater {
        model: panel.sort
        delegate: Tk.Flex {
            id: key
            required property var modelData
            required property int index
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.ComboBox {
                objectName: "reportSortField_" + key.index
                small: true
                model: panel.fields
                currentIndex: model.indexOf(key.modelData.field)
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onActivated: function(i) {
                    const next = panel.copy(panel.sort); next[key.index].field = panel.fields[i]
                    panel.setReport("sort", next, [panel.fields[i]])
                }
            }
            Tk.Segmented {
                small: true
                model: [{ text: qsTr("A → Z"), iconName: "arrow-up-a-z" }, { text: qsTr("Z → A"), iconName: "arrow-down-z-a" }]
                currentIndex: key.modelData.ascending === false ? 1 : 0
                onActivated: function(i) {
                    const next = panel.copy(panel.sort); next[key.index].ascending = i === 0
                    panel.setReport("sort", next, [])
                }
            }
            Tk.IconButton {
                iconName: "x"; small: true; tooltip: qsTr("Remove this sort")
                onClicked: { const next = panel.copy(panel.sort); next.splice(key.index, 1); panel.setReport("sort", next, []) }
            }
        }
    }
    Tk.Button {
        objectName: "reportAddSort"
        visible: panel.groups.length + panel.sort.length < 4
        text: qsTr("Add a sort")
        iconName: "arrow-up-a-z"
        small: true
        enabled: panel.designDocument !== null && panel.fields.length > 0
        onClicked: panel.setReport("sort", panel.copy(panel.sort).concat([{ field: panel.fields[0], ascending: true }]), [panel.fields[0]])
    }
    Tk.Overline { title: qsTr("REPORT TOTALS") }
    ReportTotalsEditor {
        objectPrefix: "reportTotal"
        totals: panel.totals
        fields: panel.fields
        functions: panel.functions
        onEdited: function(next) { panel.setReport("totals", next, next.map(function(t) { return t.field })) }
    }
}
