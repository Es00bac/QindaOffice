// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Group, Sort and Total (M80, D-089 §6): Access's pane for a report's
// grouping — M86 (D-094): up to ten group levels (ReportGroupLevel.qml:
// Group On intervals, order, Keep Together, Force New Page of the header
// and footer, running totals), the sort order inside the groups, the
// report's own totals, and its pages and running sums
// (ReportSectionsPanel.qml). The levels live in the report map beside the
// toolkit's own view of them (BaseService.reportLevels / reportWithLevels,
// base_report.h); every change is one step of the design document's
// history (a field not yet bound is bound first), and the pages rebuild
// from it with the engine's exact totals.
Tk.Flex {
    id: panel

    property var base: null
    property var designDocument: null
    property var provider: null
    property string table: ""
    property var fieldTypes: ({}) // M86: field → QindaBase type id (the report's source)
    property var lookups: []      // M86: fields shown by a related record's name
    readonly property var report: designDocument !== null ? designDocument.report : ({})
    readonly property var levels: base !== null && designDocument !== null ? base.reportLevels(report) : []
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
    function setLevels(next) {
        const names = []
        next.forEach(function(g) { names.push(g.field); (g.totals || []).forEach(function(t) { names.push(t.field) }) })
        if (!apply(bound(names))) return false
        const design = copy(designDocument.design)
        design.report = base.reportWithLevels(design.report || ({}), next)
        return apply(designDocument.setDesign(design))
    }
    function setLevel(index, patch) { const next = copy(levels); Object.assign(next[index], patch); setLevels(next) }
    function removeLevel(index) { const next = copy(levels); next.splice(index, 1); setLevels(next) }
    function addGroup() { addLevel() } // M80's name, kept for callers (tst_qindabase_ui_m80)
    function addLevel() {
        const used = levels.map(function(g) { return g.field })
        const field = fields.find(function(f) { return used.indexOf(f) < 0 }) || fields[0]
        if (field) setLevels(copy(levels).concat([{ field: field, pageBreakBefore: false, totals: [] }]))
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

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Tk.SectionHeader { title: qsTr("Group, sort and total") }
    Repeater {
        model: panel.levels
        // (ReportGroupLevel's own required `index` takes the model's index)
        delegate: ReportGroupLevel {
            required property var modelData
            panel: panel
            level: modelData
        }
    }
    Tk.Caption {
        objectName: "reportLevelsNote"
        visible: panel.levels.length > 3
        wrapMode: Text.Wrap
        text: qsTr("The pages print all %1 levels; the report designer beside them shows the first levels it can hold.").arg(panel.levels.length)
    }
    Tk.Button {
        objectName: "reportAddGroup"
        visible: panel.levels.length < 10
        text: qsTr("Add a group")
        iconName: "list-tree"
        small: true
        enabled: panel.designDocument !== null && panel.fields.length > 0
        onClicked: panel.addLevel()
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
        visible: panel.sort.length < 4
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
    ReportSectionsPanel { panel: panel }
}
