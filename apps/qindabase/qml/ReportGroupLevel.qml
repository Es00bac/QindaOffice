// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// One level of Group, Sort and Total (M86, D-094, base_report.h): the field
// and Access's Group On interval (each value, the first characters of a
// text, a date's year, quarter, month, week, day, hour or minute, a numeric
// range), the order, Keep Together, Force New Page and Keep Together of
// its header and footer, and the totals its footer prints (a sum or count
// may run). Every change goes to the pane (ReportGroupsPanel.setLevel).
Tk.Card {
    id: card

    required property var panel
    required property var level
    required property int index
    readonly property string type: panel.fieldTypes[level.field] || ""
    readonly property var interval: level.interval || ({ by: "value", size: 1 })
    readonly property var intervals: {
        const out = [{ id: "value", label: qsTr("each value") }]
        if (["text", "longText", "hyperlink"].indexOf(type) >= 0 || panel.lookups.indexOf(level.field) >= 0)
            out.push({ id: "prefix", label: qsTr("first characters") })
        if (type === "date" || type === "dateTime")
            out.push({ id: "year", label: qsTr("year") }, { id: "quarter", label: qsTr("quarter") }, { id: "month", label: qsTr("month") },
                     { id: "week", label: qsTr("week") }, { id: "day", label: qsTr("day") })
        if (type === "dateTime")
            out.push({ id: "hour", label: qsTr("hour") }, { id: "minute", label: qsTr("minute") })
        if (["integer", "largeNumber", "number", "currency"].indexOf(type) >= 0)
            out.push({ id: "range", label: qsTr("ranges of") })
        return out
    }
    readonly property var newPages: [{ id: "none", label: qsTr("no new page") }, { id: "before", label: qsTr("new page before") },
                                     { id: "after", label: qsTr("new page after") }, { id: "both", label: qsTr("before and after") }]
    function section(which) { return level[which] || ({ newPage: which === "header" && level.pageBreakBefore ? "before" : "none", keepTogether: false }) }
    function setSection(which, patch) {
        const next = Object.assign({}, section(which), patch)
        const change = {}
        change[which] = next
        if (which === "header") change.pageBreakBefore = next.newPage === "before" || next.newPage === "both"
        panel.setLevel(index, change)
    }

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
            Tk.Caption { text: card.index === 0 ? qsTr("Group on") : qsTr("then on"); Tk.Flex.shrink: 0 }
            Tk.ComboBox {
                objectName: "reportGroupField_" + card.index
                small: true
                model: card.panel.fields
                currentIndex: model.indexOf(card.level.field)
                Tk.Flex.grow: 1
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.5
                onActivated: function(i) { card.panel.setLevel(card.index, { field: card.panel.fields[i], interval: { by: "value", size: 1 } }) }
            }
            Tk.ComboBox {
                objectName: "reportGroupInterval_" + card.index
                small: true
                textRole: "label"
                model: card.intervals
                currentIndex: Math.max(0, card.intervals.findIndex(function(i) { return i.id === card.interval.by }))
                tooltip: qsTr("Group On: what makes a group")
                Tk.Flex.grow: 1
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.5
                onActivated: function(i) {
                    const by = card.intervals[i].id
                    card.panel.setLevel(card.index, { interval: { by: by, size: by === "range" ? 100 : 1 } })
                }
            }
            Tk.NumberField {
                objectName: "reportGroupSize_" + card.index
                visible: card.interval.by === "prefix" || card.interval.by === "range"
                from: 1
                to: card.interval.by === "prefix" ? 10 : 1000000
                value: card.interval.size || 1
                tooltip: card.interval.by === "prefix" ? qsTr("Characters") : qsTr("Width of each range")
                onValueModified: card.panel.setLevel(card.index, { interval: { by: card.interval.by, size: value } })
            }
            Tk.Segmented {
                small: true
                model: [{ text: qsTr("A → Z"), iconName: "arrow-up-a-z" }, { text: qsTr("Z → A"), iconName: "arrow-down-z-a" }]
                currentIndex: card.level.descending ? 1 : 0
                onActivated: function(i) { card.panel.setLevel(card.index, { descending: i === 1 }) }
            }
            Tk.IconButton {
                iconName: "x"; small: true; tooltip: qsTr("Remove this group level")
                onClicked: card.panel.removeLevel(card.index)
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            wrap: Tk.Flex.Wrap
            Tk.Caption { text: qsTr("Keep together"); Tk.Flex.shrink: 0 }
            Tk.ComboBox {
                objectName: "reportGroupKeep_" + card.index
                readonly property var choices: [{ id: "no", label: qsTr("no") }, { id: "wholeGroup", label: qsTr("whole group on one page") },
                                                { id: "withFirstDetail", label: qsTr("header with first record") }]
                small: true
                textRole: "label"
                model: choices
                currentIndex: Math.max(0, choices.findIndex(function(c) { return c.id === (card.level.keepTogether || "no") }))
                Tk.Flex.grow: 1
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.6
                onActivated: function(i) { card.panel.setLevel(card.index, { keepTogether: choices[i].id }) }
            }
        }
        Repeater {
            model: [{ id: "header", label: qsTr("Header") }, { id: "footer", label: qsTr("Footer") }]
            delegate: Tk.Flex {
                id: part
                required property var modelData
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                wrap: Tk.Flex.Wrap
                Tk.Caption { text: part.modelData.label; Tk.Flex.basis: Tk.Theme.size.control * 2; Tk.Flex.shrink: 0 }
                Tk.ComboBox {
                    objectName: "reportGroup" + (part.modelData.id === "header" ? "Header" : "Footer") + "Page_" + card.index
                    small: true
                    textRole: "label"
                    model: card.newPages
                    currentIndex: Math.max(0, card.newPages.findIndex(function(p) { return p.id === card.section(part.modelData.id).newPage }))
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.5 // Keep Together wraps under it when narrow
                    onActivated: function(i) { card.setSection(part.modelData.id, { newPage: card.newPages[i].id }) }
                }
                Tk.CheckBox {
                    text: qsTr("Keep together")
                    checked: card.section(part.modelData.id).keepTogether === true
                    tooltip: qsTr("Never split this section across pages")
                    onToggled: card.setSection(part.modelData.id, { keepTogether: checked })
                }
            }
        }
        ReportTotalsEditor {
            objectPrefix: "reportGroupTotal_" + card.index
            totals: card.level.totals || []
            fields: card.panel.fields
            functions: card.panel.functions
            allowRunning: true
            onEdited: function(next) { card.panel.setLevel(card.index, { totals: next }) }
        }
    }
}
