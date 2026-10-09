// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A date as yyyy-mm-dd text with a month picker (M81, D-090): the field
// the event dialog uses for its dates, shared by the follow-up, rule,
// search-folder and automatic-reply dialogs. `text` is the ISO date ("" =
// none); `valid` is false for anything else typed.
Tk.Flex {
    id: dateField

    property alias text: field.text
    property bool readOnly: false
    property string placeholder: "yyyy-mm-dd"
    readonly property bool valid: field.text.length === 0 || /^\d{4}-\d{2}-\d{2}$/.test(field.text)

    direction: Tk.Flex.Row
    gap: Tk.Theme.space.xs

    Tk.TextField {
        id: field
        objectName: dateField.objectName + "Text"
        mono: true
        small: true
        implicitWidth: Tk.Theme.size.fieldWidth
        placeholderText: dateField.placeholder
        readOnly: dateField.readOnly
        error: !dateField.valid
    }
    Tk.IconButton {
        id: pickButton
        objectName: dateField.objectName + "Pick"
        small: true
        iconName: "calendar"
        tooltip: qsTr("Pick a date")
        enabled: !dateField.readOnly
        onClicked: picker.open()
    }
    Tk.Popover {
        id: picker
        anchorItem: pickButton
        Tk.CalendarPicker {
            localeService: locale
            selectedDate: field.text
            onDateSelected: function(iso) { field.text = iso; picker.close() }
        }
    }
    Tk.LocaleService { id: locale }
}
