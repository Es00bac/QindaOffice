// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Insert ▸ Table ▸ Convert Text to Table (M70, D-079): Word's dialog — the
// number of columns (proposed from the separator) and where the text is
// separated: tabs, commas, paragraphs (each paragraph a cell) or another
// character. The selected paragraphs become a Table Grid table.
Tk.Dialog {
    id: dialog
    signal convertApplied(var answer)
    signal separatorPicked(string separator)
    readonly property var separators: ["tab", "comma", "paragraph", "other"]
    readonly property string separator: separators[kind.currentIndex] === "other"
                                         ? (other.text.length > 0 ? other.text.charAt(0) : "-")
                                         : separators[kind.currentIndex]
    property int columns: 1
    objectName: "textToTableDialog"
    title: qsTr("Convert Text to Table")
    dialogWidth: 400
    primaryText: qsTr("OK")

    function load(info) {
        kind.currentIndex = Math.max(0, separators.indexOf(info.separator))
        columns = info.columns
    }
    onAccepted: dialog.convertApplied({ columns: dialog.columns, separator: dialog.separator })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Separate text at")
            Tk.Segmented {
                id: kind
                objectName: "textToTableSeparator"
                model: [qsTr("Tabs"), qsTr("Commas"), qsTr("Paragraphs"), qsTr("Other")]
                onActivated: dialog.separatorPicked(dialog.separator)
            }
        }
        Tk.PropertyRow {
            label: qsTr("Other")
            visible: kind.currentIndex === 3
            Tk.TextField {
                id: other
                objectName: "textToTableOther"
                width: 60
                text: "-"
                onTextEdited: dialog.separatorPicked(dialog.separator)
            }
        }
        Tk.PropertyRow {
            label: qsTr("Number of columns")
            Tk.NumberField {
                objectName: "textToTableColumns"
                from: 1; to: 63; stepSize: 1; decimals: 0
                value: dialog.columns
                onValueModified: function(v) { dialog.columns = v }
            }
        }
    }
}
