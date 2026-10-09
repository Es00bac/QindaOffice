// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data ▸ Text to Columns (M78, D-087): Excel's Convert Text to Columns
// Wizard as three steps — the data type (Delimited / Fixed width), the
// delimiters or the column breaks (typed as character positions, guessed
// the way Excel guesses them) with a preview, and each column's data
// format (General, Text, Date MDY…YDM, Do not import) with the
// destination. Finish converts as one undo step; when cells other than the
// source would be overwritten Excel's question comes first.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var state: ({})
    property int step: 0
    property var formats: []
    property string error: ""
    readonly property var options: ({
        mode: kind.currentIndex === 1 ? "fixed" : "delimited",
        tab: tabBox.checked, semicolon: semicolon.checked, comma: comma.checked, space: space.checked,
        other: otherOn.checked ? other.text : "", consecutive: consecutive.checked,
        qualifier: ["\"", "'", ""][qualifier.currentIndex],
        breaks: breaks.text.split(/[ ,;]+/).filter(function(t) { return t.length > 0 }).map(Number),
        formats: dialog.formats, destination: destination.text
    })
    readonly property var preview: dialog.visible ? dialog.host.textToColumnsPreview(dialog.options) : []
    readonly property int width_: dialog.preview.reduce(function(m, row) { return Math.max(m, row.length) }, 1)

    signal overwriteAsked()

    // A refusal shows its reason in the dialog again, once the closing
    // transition has run (open() during it would be lost).
    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    objectName: "textToColumnsDialog"
    title: qsTr("Convert Text to Columns Wizard")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: qsTr("Finish")

    function load() {
        dialog.state = dialog.host.textToColumnsState()
        dialog.step = 0
        dialog.formats = []
        dialog.error = dialog.state.oneColumn === false ? qsTr("Text to Columns can convert only one column at a time.") : ""
        breaks.text = (dialog.state.breaks || []).join(", ")
        destination.text = dialog.state.destination || ""
    }
    function finish(overwrite) {
        const problem = dialog.host.applyTextToColumns(dialog.options, overwrite)
        if (problem === "confirm") {
            dialog.overwriteAsked()
        } else if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
        }
    }
    function setFormat(column, format) {
        const next = dialog.formats.slice()
        while (next.length <= column) next.push(0)
        next[column] = format
        dialog.formats = next
    }
    onAccepted: dialog.finish(false)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "textToColumnsError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.TabStrip {
            objectName: "textToColumnsSteps"
            model: [qsTr("1. Data type"), qsTr("2. Delimiters"), qsTr("3. Column format")]
            currentIndex: dialog.step
            onTabActivated: function(index) { dialog.step = index }
        }
        Tk.Flex {
            visible: dialog.step === 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Choose the file type that best describes your data:") }
            Tk.Segmented {
                id: kind
                objectName: "textToColumnsType"
                model: [qsTr("Delimited"), qsTr("Fixed width")]
            }
            Tk.Caption {
                muted: true
                text: kind.currentIndex === 0 ? qsTr("Characters such as commas or tabs separate each field.")
                                              : qsTr("Fields are aligned in columns with spaces between each field.")
            }
        }
        Tk.Flex {
            visible: dialog.step === 1 && kind.currentIndex === 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.md
                wrap: Tk.Flex.Wrap
                Tk.CheckBox { id: tabBox; objectName: "textToColumnsTab"; text: qsTr("Tab"); checked: true }
                Tk.CheckBox { id: semicolon; objectName: "textToColumnsSemicolon"; text: qsTr("Semicolon") }
                Tk.CheckBox { id: comma; objectName: "textToColumnsComma"; text: qsTr("Comma") }
                Tk.CheckBox { id: space; objectName: "textToColumnsSpace"; text: qsTr("Space") }
                Tk.CheckBox { id: otherOn; objectName: "textToColumnsOtherOn"; text: qsTr("Other") }
                Tk.TextField { id: other; objectName: "textToColumnsOther"; implicitWidth: Tk.Theme.size.control * 2; maximumLength: 1 }
            }
            Tk.CheckBox { id: consecutive; objectName: "textToColumnsConsecutive"; text: qsTr("Treat consecutive delimiters as one") }
            Tk.PropertyRow {
                label: qsTr("Text qualifier")
                Tk.ComboBox { id: qualifier; objectName: "textToColumnsQualifier"; model: ["\"", "'", qsTr("{none}")] }
            }
        }
        Tk.PropertyRow {
            visible: dialog.step === 1 && kind.currentIndex === 1
            label: qsTr("Column breaks")
            hint: qsTr("Character positions where a new column starts")
            Tk.TextField { id: breaks; objectName: "textToColumnsBreaks"; mono: true }
        }
        Tk.Flex {
            visible: dialog.step === 2
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            wrap: Tk.Flex.Wrap
            Repeater {
                model: dialog.width_
                Tk.ComboBox {
                    required property int index
                    objectName: "textToColumnsFormat_" + index
                    small: true
                    implicitWidth: Tk.Theme.size.fieldWidth
                    model: [qsTr("General"), qsTr("Text"), qsTr("Date: MDY"), qsTr("Date: DMY"), qsTr("Date: YMD"), qsTr("Date: MYD"),
                            qsTr("Date: DYM"), qsTr("Date: YDM"), qsTr("Do not import")]
                    currentIndex: dialog.formats[index] || 0
                    onActivated: function(choice) { dialog.setFormat(index, choice) }
                }
            }
        }
        Tk.PropertyRow {
            visible: dialog.step === 2
            label: qsTr("Destination")
            Tk.TextField { id: destination; objectName: "textToColumnsDestination"; mono: true }
        }
        Tk.SectionHeader { title: qsTr("Data preview") }
        Tk.Scroll {
            objectName: "textToColumnsPreview"
            implicitHeight: Tk.Theme.size.row * 5
            Tk.Grid {
                columns: Array(dialog.width_).fill("1fr").join(" ")
                columnGap: Tk.Theme.space.sm
                Repeater {
                    model: dialog.preview.length * dialog.width_
                    Tk.Mono {
                        required property int index
                        text: (dialog.preview[Math.floor(index / dialog.width_)] || [])[index % dialog.width_] || ""
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
