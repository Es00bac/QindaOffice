// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Insert ▸ Header & Footer ▸ Format Page Numbers (M88, D-096): Word's
// dialog for the section of the caret (or of the open header): the number
// format (1, 2, 3 / - 1 - / a, b, c / A, B, C / i, ii, iii / I, II, III)
// and whether its numbers continue from the previous section or start at
// a number. One undo step (SectionsController, write_section_commands.h).
Tk.Dialog {
    id: dialog
    required property QtObject sections
    property var info: ({ formats: [], format: "", start: -1, section: 1, sections: 1 })
    objectName: "pageNumbersDialog"
    title: qsTr("Page Number Format")
    dialogWidth: 400
    primaryText: qsTr("OK")

    Connections {
        target: dialog.sections
        function onPageNumbersRequested() {
            dialog.info = dialog.sections.pageNumberInfo()
            format.currentIndex = Math.max(0, dialog.info.formats.map(function(f) { return f.format }).indexOf(dialog.info.format))
            numbering.currentIndex = dialog.info.start >= 0 ? 1 : 0
            startAt.value = dialog.info.start >= 0 ? dialog.info.start : 1
            dialog.open()
        }
    }
    onAccepted: dialog.sections.applyPageNumbers({ format: info.formats[format.currentIndex].format,
                                                   start: numbering.currentIndex === 1 ? startAt.value : -1 })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Label {
            visible: dialog.info.sections > 1
            text: qsTr("Section %1 of %2").arg(dialog.info.section).arg(dialog.info.sections)
        }
        Tk.PropertyRow {
            label: qsTr("Number format")
            Tk.ComboBox {
                id: format
                objectName: "pageNumberFormat"
                model: dialog.info.formats.map(function(f) { return f.label })
            }
        }
        Tk.PropertyRow {
            label: qsTr("Page numbering")
            Tk.Segmented {
                id: numbering
                objectName: "pageNumberContinue"
                model: [qsTr("Continue from previous section"), qsTr("Start at")]
            }
        }
        Tk.PropertyRow {
            label: qsTr("Start at")
            Tk.NumberField {
                id: startAt
                objectName: "pageNumberStart"
                enabled: numbering.currentIndex === 1
                from: 0
                to: 9999
                decimals: 0
                onValueModified: function(v) { value = v }
            }
        }
    }
}
