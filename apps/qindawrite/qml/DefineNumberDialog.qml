// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Home ▸ Numbering ▸ Define New Number Format (M73, D-082): the number
// style (1, I, i, A, a, 1st, One, First, 01, none), the format text around
// the number ("Step %1:" — %1 is where the number goes), the start value
// and the number's alignment. On a level that also shows its parents'
// numbers ("1.1.") the text stays in Word's own %1 … %9 (levelText; the
// preview takes the parents from the caret's list). Answered through
// ListController (write_lists.h): on a numbered list it changes the
// caret's level of that list, on other paragraphs it makes a new list.
Tk.Dialog {
    id: dialog
    required property QtObject lists
    property var formats: []
    property bool levelText: false
    // The caret's list's levels above this one, for the preview (levelText).
    property var parents: []
    readonly property var justifications: ["left", "center", "right"]
    readonly property string format: formats.length > 0 ? formats[style.currentIndex].format : "decimal"
    readonly property var spec: ({ format: format, text: formatText.text, start: startField.value })
    objectName: "defineNumberDialog"
    title: qsTr("Define New Number Format")
    dialogWidth: 440
    primaryText: qsTr("OK")

    function load(info) {
        formats = dialog.lists.numberFormats()
        style.currentIndex = Math.max(0, formats.findIndex(function(f) { return f.format === info.format }))
        formatText.text = info.text
        startField.value = info.start
        alignment.currentIndex = Math.max(0, justifications.indexOf(info.justification))
        levelText = info.levelText === true
        parents = levelText ? dialog.lists.multilevelInfo().levels.slice(0, info.level) : []
    }
    function label(n) {
        return dialog.lists.previewLabel(dialog.parents.concat([dialog.spec]), dialog.parents.length, n)
    }
    onAccepted: lists.applyNumberFormat({ format: format, text: formatText.text, start: startField.value,
                                         justification: justifications[alignment.currentIndex],
                                         levelText: levelText })

    Connections {
        target: dialog.lists
        function onDefineNumberRequested() { dialog.load(dialog.lists.numberFormatInfo()); dialog.open() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Number style")
            Tk.ComboBox {
                id: style
                objectName: "numberStyle"
                implicitWidth: 220
                model: dialog.formats.map(function(f) { return f.label })
            }
        }
        Tk.PropertyRow {
            label: qsTr("Number format")
            hint: dialog.levelText ? qsTr("%1 … %9 stand for the numbers of levels 1 to 9.")
                                   : qsTr("%1 stands for the number; type the text around it.")
            Tk.TextField {
                id: formatText
                objectName: "numberFormatText"
                placeholderText: "%1."
            }
        }
        Tk.PropertyRow {
            label: qsTr("Start at")
            Tk.NumberField {
                id: startField
                objectName: "numberStart"
                from: 0; to: 32767; stepSize: 1; decimals: 0
                onValueModified: function(v) { value = v }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Alignment")
            Tk.Segmented {
                id: alignment
                objectName: "numberAlignment"
                model: [qsTr("Left"), qsTr("Centered"), qsTr("Right")]
            }
        }
        Tk.Overline { text: qsTr("Preview") }
        Tk.Box {
            objectName: "numberPreview"
            borderWidth: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            radius: Tk.Theme.radius.sm
            padding: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Repeater {
                    model: 3
                    Tk.Flex {
                        gap: Tk.Theme.space.md
                        Tk.Label { objectName: "numberPreview_" + index; text: dialog.label(startField.value + index) }
                        Tk.Label { text: qsTr("List item"); muted: true }
                    }
                }
            }
        }
    }
}
