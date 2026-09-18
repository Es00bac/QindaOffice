// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The find band under the content: a search field, Next/Previous, a case
// toggle and Close. Escape closes; Enter finds the next match. Apps put it
// in OfficeWindow's `findBar` slot and answer findNext/findPrevious.
Tk.Box {
    id: bar

    property alias text: field.text
    property alias caseSensitive: caseBox.checked
    property alias placeholderText: field.placeholderText

    signal findNext(string text, bool caseSensitive)
    signal findPrevious(string text, bool caseSensitive)
    signal closed()

    objectName: "findBar"
    visible: false
    color: Tk.Theme.color.surface
    borderTop: 1
    borderColor: Tk.Theme.color.divider
    padding: Tk.Theme.space.xs

    function open() {
        bar.visible = true
        field.forceActiveFocus(Qt.TabFocusReason)
        field.selectAll()
    }
    function close() {
        bar.visible = false
        bar.closed()
    }

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Caption { text: qsTr("Find") }
        Tk.TextField {
            id: field
            objectName: "findField"
            placeholderText: qsTr("Search")
            clearable: true
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minWidth: 120
            onAccepted: bar.findNext(field.text, caseBox.checked)
            Keys.onEscapePressed: bar.close()
        }
        Tk.IconButton { objectName: "findNext"; iconName: "chevron-down"; tooltip: qsTr("Next"); onClicked: bar.findNext(field.text, caseBox.checked) }
        Tk.IconButton { objectName: "findPrevious"; iconName: "chevron-up"; tooltip: qsTr("Previous"); onClicked: bar.findPrevious(field.text, caseBox.checked) }
        Tk.CheckBox { id: caseBox; objectName: "findCase"; text: qsTr("Case sensitive") }
        Tk.IconButton { objectName: "findClose"; iconName: "x"; tooltip: qsTr("Close"); onClicked: bar.close() }
    }
}
