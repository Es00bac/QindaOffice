// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// A union query in the designer (M86, D-094): its parts — each a select
// query with a design grid of its own, shown here as its Access SQL — the
// UNION or UNION ALL between them, and the union's own order (Access sorts
// a union only at its end, by the first part's columns). Design this part
// opens a part in the grid (QueryDesignView.enterPart); Back to the union
// puts it back as one step of the designer's history.
Tk.Flex {
    id: pane

    required property var view
    readonly property var design: view.design
    readonly property var parts: design.parts || []
    readonly property var columns: view.base !== null && parts.length > 0
                                   ? (view.base.compileQuery(parts[0]).columns || []) : []

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    Tk.Caption {
        wrapMode: Text.Wrap
        text: qsTr("A union query puts the records of its parts one after the other. Every part shows as many columns, in the same order; UNION leaves out records that repeat, UNION ALL keeps them.")
    }
    Repeater {
        model: pane.parts
        delegate: Tk.Flex {
            id: part
            required property var modelData
            required property int index
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.CheckBox {
                objectName: "unionAll_" + (part.index - 1)
                visible: part.index > 0
                text: qsTr("UNION ALL — keep records that repeat")
                checked: (pane.design.unionAll || [])[part.index - 1] === true
                onToggled: {
                    const on = checked
                    const at = part.index - 1
                    pane.view.edit(function(d) {
                        const all = (d.unionAll || []).slice()
                        while (all.length < d.parts.length - 1) all.push(false)
                        all[at] = on
                        d.unionAll = all
                    })
                }
            }
            Tk.Card {
                objectName: "unionPart_" + part.index
                padding: Tk.Theme.space.sm
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        Tk.Badge { text: qsTr("Part %1").arg(part.index + 1) }
                        Tk.Caption {
                            text: (part.modelData.tables || []).map(function(t) { return t.name }).join(", ")
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                        }
                        Tk.Button {
                            objectName: "unionDesignPart_" + part.index
                            text: qsTr("Design this part")
                            iconName: "table-2"
                            small: true
                            onClicked: pane.view.enterPart(part.index)
                        }
                        Tk.IconButton {
                            iconName: "x"
                            small: true
                            enabled: pane.parts.length > 2
                            tooltip: qsTr("Leave this part out of the union")
                            onClicked: {
                                const at = part.index
                                pane.view.edit(function(d) {
                                    d.parts.splice(at, 1)
                                    d.unionAll = (d.unionAll || []).filter(function(a, i) { return i !== Math.max(0, at - 1) })
                                })
                            }
                        }
                    }
                    Tk.TextArea {
                        readOnly: true
                        mono: true
                        rows: 3
                        wrapMode: TextEdit.Wrap
                        text: pane.view.base !== null ? pane.view.base.accessSql(part.modelData).sql : ""
                        tooltip: qsTr("Part %1 in Access SQL").arg(part.index + 1)
                    }
                }
            }
        }
    }
    Tk.Button {
        objectName: "unionAddPart"
        text: qsTr("Add a part")
        iconName: "plus"
        small: true
        onClicked: pane.view.edit(function(d) {
            d.parts.push(JSON.parse(JSON.stringify(d.parts[d.parts.length - 1])))
            d.unionAll = (d.unionAll || []).concat([false])
        })
    }
    Tk.Overline { title: qsTr("SORT THE UNION BY") }
    Repeater {
        model: pane.design.unionOrder || []
        delegate: Tk.Flex {
            id: key
            required property var modelData
            required property int index
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.ComboBox {
                objectName: "unionOrder_" + key.index
                small: true
                model: pane.columns
                currentIndex: model.indexOf(key.modelData.column)
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onActivated: function(i) {
                    const column = pane.columns[i]
                    const at = key.index
                    pane.view.edit(function(d) { d.unionOrder[at].column = column })
                }
            }
            Tk.Segmented {
                small: true
                model: [{ text: qsTr("A → Z"), iconName: "arrow-up-a-z" }, { text: qsTr("Z → A"), iconName: "arrow-down-z-a" }]
                currentIndex: key.modelData.descending ? 1 : 0
                onActivated: function(i) { const at = key.index; pane.view.edit(function(d) { d.unionOrder[at].descending = i === 1 }) }
            }
            Tk.IconButton {
                iconName: "x"
                small: true
                tooltip: qsTr("Remove this sort")
                onClicked: { const at = key.index; pane.view.edit(function(d) { d.unionOrder.splice(at, 1) }) }
            }
        }
    }
    Tk.Button {
        objectName: "unionAddSort"
        text: qsTr("Add a sort")
        iconName: "arrow-up-a-z"
        small: true
        enabled: pane.columns.length > 0
        onClicked: pane.view.edit(function(d) {
            d.unionOrder = (d.unionOrder || []).concat([{ column: pane.columns[0], descending: false }])
        })
    }
}
