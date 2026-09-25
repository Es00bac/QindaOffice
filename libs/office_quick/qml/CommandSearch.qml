// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// The ribbon's command search field (D-041). It is a launcher, not a
// filter: typing or clicking runs Help ▸ Search Commands… (Alt+Q) through
// the ActionModel — the same entry the menu, the panel and the keyboard
// reach — and hands the typed text over, so the window's CommandPalette
// opens already filtered. The field itself never keeps text.
Tk.SearchField {
    id: field

    property ActionModel actions: null
    // The text typed before the palette took over; OfficeWindow reads it.
    property string handoffText: ""

    implicitWidth: Tk.Theme.size.fieldWidth * 2
    small: true
    placeholderText: qsTr("Search commands  (Alt+Q)")
    Accessible.name: qsTr("Search commands")

    function launch(seed) {
        if (field.actions === null) return
        field.handoffText = seed
        field.text = ""
        field.actions.trigger("helpSearchCommandsAction")
    }
    onTextEdited: if (text.length > 0) launch(text)
    onActiveFocusChanged: if (activeFocus && text.length === 0) launch("")
}
