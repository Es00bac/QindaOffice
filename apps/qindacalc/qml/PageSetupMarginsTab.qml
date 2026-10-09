// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Page Setup ▸ Margins (M78, D-087): top, bottom, left, right, header and
// footer in inches (Excel's unit), and Center on page horizontally /
// vertically. Edits the dialog's draft; built afresh on every open.
Loader {
    id: tab

    required property var dialog

    // One margin in inches: `owner.draft.margins[key]` (an inline component
    // does not see this file's ids, hence `owner`).
    component MarginRow: Tk.PropertyRow {
        id: marginRow
        required property var owner
        required property string key
        Tk.NumberField {
            objectName: "margin_" + marginRow.key
            from: 0
            to: 20
            stepSize: 0.05
            decimals: 2
            suffix: qsTr(" in")
            value: (marginRow.owner.draft.margins || {})[marginRow.key] || 0
            onValueModified: function(v) { marginRow.owner.setIn("margins", marginRow.key, v) }
        }
    }

    active: tab.dialog.visible
    sourceComponent: Component {
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            // AGENT-GUARD: explicit rows, not a Repeater over a JS array of
            // objects — in a delegate made from such a model, a NumberField's
            // first bindings (its `decimals` handler, this `value`) ran while
            // the model's accessor still answered every name (`Math`, `tab`)
            // with undefined, so the fields stayed at 0.00 (Qt 6.11).
            Tk.Grid {
                columns: "1fr 1fr"
                gap: Tk.Theme.space.sm
                columnGap: Tk.Theme.space.md
                MarginRow { owner: tab.dialog; key: "top"; label: qsTr("Top") }
                MarginRow { owner: tab.dialog; key: "header"; label: qsTr("Header") }
                MarginRow { owner: tab.dialog; key: "left"; label: qsTr("Left") }
                MarginRow { owner: tab.dialog; key: "right"; label: qsTr("Right") }
                MarginRow { owner: tab.dialog; key: "bottom"; label: qsTr("Bottom") }
                MarginRow { owner: tab.dialog; key: "footer"; label: qsTr("Footer") }
            }
            Tk.SectionHeader { title: qsTr("Center on page") }
            Tk.CheckBox {
                objectName: "marginCenterHorizontally"
                text: qsTr("Horizontally")
                checked: tab.dialog.draft.centerHorizontally === true
                onToggled: tab.dialog.set("centerHorizontally", checked)
            }
            Tk.CheckBox {
                objectName: "marginCenterVertically"
                text: qsTr("Vertically")
                checked: tab.dialog.draft.centerVertically === true
                onToggled: tab.dialog.set("centerVertically", checked)
            }
        }
    }
}
