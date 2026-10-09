// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Page Setup ▸ Sheet (M78, D-087): the print area ($A$1:$D$20), Print
// titles — rows to repeat at top ($1:$2) and columns to repeat at left
// ($A:$A) —, Print (gridlines, black and white, draft quality, row and
// column headings, comments, cell errors as) and the page order. Edits the
// dialog's draft; built afresh on every open.
Loader {
    id: tab

    required property var dialog

    active: tab.dialog.visible
    sourceComponent: Component {
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Print area")
                Tk.TextField {
                    objectName: "sheetPrintArea"
                    width: Tk.Theme.size.fieldWidth * 2
                    mono: true
                    placeholderText: qsTr("The used range")
                    text: tab.dialog.draft.printArea || ""
                    onTextEdited: tab.dialog.set("printArea", text)
                }
            }
            Tk.SectionHeader { title: qsTr("Print titles") }
            Tk.PropertyRow {
                label: qsTr("Rows at top")
                Tk.TextField {
                    objectName: "sheetTitleRows"
                    width: Tk.Theme.size.fieldWidth * 2
                    mono: true
                    placeholderText: qsTr("$1:$1")
                    text: tab.dialog.draft.titleRows || ""
                    onTextEdited: tab.dialog.set("titleRows", text)
                }
            }
            Tk.PropertyRow {
                label: qsTr("Columns at left")
                Tk.TextField {
                    objectName: "sheetTitleCols"
                    width: Tk.Theme.size.fieldWidth * 2
                    mono: true
                    placeholderText: qsTr("$A:$A")
                    text: tab.dialog.draft.titleCols || ""
                    onTextEdited: tab.dialog.set("titleCols", text)
                }
            }
            Tk.SectionHeader { title: qsTr("Print") }
            Tk.Grid {
                columns: "1fr 1fr"
                columnGap: Tk.Theme.space.md
                Repeater {
                    model: [{ key: "gridLines", text: qsTr("Gridlines") }, { key: "headings", text: qsTr("Row and column headings") },
                            { key: "blackAndWhite", text: qsTr("Black and white") }, { key: "draft", text: qsTr("Draft quality") }]
                    Tk.CheckBox {
                        required property var modelData
                        objectName: "sheetPrint_" + modelData.key
                        text: modelData.text
                        checked: tab.dialog.draft[modelData.key] === true
                        onToggled: tab.dialog.set(modelData.key, checked)
                    }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Comments")
                Tk.ComboBox {
                    objectName: "sheetComments"
                    width: Tk.Theme.size.fieldWidth * 2
                    model: [qsTr("(None)"), qsTr("At end of sheet"), qsTr("As displayed on sheet")]
                    currentIndex: tab.dialog.draft.cellComments || 0
                    onActivated: function(index) { tab.dialog.set("cellComments", index) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Cell errors as")
                Tk.ComboBox {
                    objectName: "sheetErrors"
                    width: Tk.Theme.size.fieldWidth * 2
                    model: [qsTr("displayed"), qsTr("<blank>"), qsTr("--"), qsTr("#N/A")]
                    currentIndex: tab.dialog.draft.errors || 0
                    onActivated: function(index) { tab.dialog.set("errors", index) }
                }
            }
            Tk.SectionHeader { title: qsTr("Page order") }
            Tk.Segmented {
                objectName: "sheetPageOrder"
                model: [qsTr("Down, then over"), qsTr("Over, then down")]
                currentIndex: tab.dialog.draft.overThenDown === true ? 1 : 0
                onActivated: function(index) { tab.dialog.set("overThenDown", index === 1) }
            }
        }
    }
}
