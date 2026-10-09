// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// A form's lookup fields (M80, D-089 §4) as Access shows them: a combo box
// of the related records, or a list box. QindaTK's FormDesigner draws text
// fields and check boxes only, so they live here, above it, in the form's
// field order. On a saved record a choice is saved at once (one undo
// step); on a new record it waits for Save record. In design, each one's
// control (combo or list) and whether it shows are set here.
Tk.Flex {
    id: lookups

    required property var view
    readonly property bool designing: view.designing

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    Repeater {
        model: lookups.view.lookups.filter(function(l) { return lookups.designing || !l.hidden })
        delegate: Tk.Flex {
            id: row
            required property var modelData
            required property int index
            readonly property var choices: lookups.view.lookupChoices[modelData.name] || []
            readonly property var value: lookups.view.lookupValue(modelData.name)
            readonly property int chosen: choices.findIndex(function(c) { return String(c.value) === String(row.value) })
            objectName: "formLookup_" + modelData.name
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: row.modelData.control === "list" && !lookups.designing ? Tk.Flex.Start : Tk.Flex.Center
            opacity: row.modelData.hidden ? Tk.Theme.opacity.disabled : 1
            Tk.Caption {
                text: row.modelData.label + (row.modelData.required ? " *" : "")
                wrapMode: Text.Wrap
                Tk.Flex.basis: Tk.Theme.size.labelWidth
                Tk.Flex.shrink: 0
            }
            Tk.ComboBox {
                objectName: "formCombo_" + row.modelData.name
                visible: row.modelData.control !== "list" || lookups.designing
                enabled: !lookups.designing
                textRole: "text"
                model: [{ value: null, text: qsTr("(none)") }].concat(row.choices)
                currentIndex: row.chosen + 1
                tooltip: qsTr("%1: one of the %2 records").arg(row.modelData.label).arg(row.modelData.related)
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onActivated: function(i) { lookups.view.setLookup(row.modelData.name, i === 0 ? null : row.choices[i - 1].value) }
            }
            Item {
                objectName: "formList_" + row.modelData.name
                visible: row.modelData.control === "list" && !lookups.designing
                implicitHeight: Tk.Theme.size.control * 4.5 // Tk.Flex reads implicit sizes only
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                // AGENT-NOTE: a plain Item holds the anchored Scroll (no
                // anchors inside a layout container, AGENTS.md).
                Tk.Scroll {
                    id: listScroll
                    anchors.fill: parent
                    accessibleName: row.modelData.label
                    Tk.Flex {
                        width: listScroll.width
                        direction: Tk.Flex.Column
                        gap: 0
                        Repeater {
                            model: row.choices
                            delegate: Tk.ListRow {
                                required property var modelData
                                required property int index
                                small: true
                                text: modelData.text
                                selected: index === row.chosen
                                onClicked: lookups.view.setLookup(row.modelData.name, modelData.value)
                            }
                        }
                    }
                }
            }
            Tk.Segmented {
                objectName: "formLookupControl_" + row.modelData.name
                visible: lookups.designing
                small: true
                model: [{ text: qsTr("Combo"), iconName: "chevrons-up-down" }, { text: qsTr("List"), iconName: "layout-list" }]
                currentIndex: row.modelData.control === "list" ? 1 : 0
                onActivated: function(i) { lookups.view.setLookupControl(row.modelData.name, i === 1 ? "list" : "combo", row.modelData.hidden) }
            }
            Tk.CheckBox {
                visible: lookups.designing
                text: qsTr("Shown")
                checked: !row.modelData.hidden
                tooltip: qsTr("Show %1 on the form").arg(row.modelData.label)
                onToggled: lookups.view.setLookupControl(row.modelData.name, row.modelData.control, !checked)
            }
        }
    }
}
