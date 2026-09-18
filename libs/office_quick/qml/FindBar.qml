// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The find band under the content: a search field, Next/Previous, a case
// toggle and Close — and, when `replaceEnabled`, a second row Ctrl+H
// reveals (Replace / Replace All). Escape closes; Enter finds the next
// match. Apps put it in OfficeWindow's `findBar` slot and answer the
// signals.
Tk.Box {
    id: bar

    property alias text: field.text
    property alias caseSensitive: caseBox.checked
    property alias placeholderText: field.placeholderText
    // The app answers replaceOne/replaceAll; the row shows on openReplace().
    property bool replaceEnabled: false
    property bool replaceVisible: false
    property alias replaceText: replaceField.text

    signal findNext(string text, bool caseSensitive)
    signal findPrevious(string text, bool caseSensitive)
    signal replaceOne(string text, string replacement, bool caseSensitive)
    signal replaceAll(string text, string replacement, bool caseSensitive)
    signal closed()

    objectName: "findBar"
    visible: false
    color: Tk.Theme.color.surface
    borderTop: 1
    borderColor: Tk.Theme.color.divider
    padding: Tk.Theme.space.xs

    function open() {
        bar.replaceVisible = false
        bar.visible = true
        field.forceActiveFocus(Qt.TabFocusReason)
        field.selectAll()
    }
    function openReplace() {
        bar.replaceVisible = bar.replaceEnabled
        bar.visible = true
        field.forceActiveFocus(Qt.TabFocusReason)
        field.selectAll()
    }
    function close() {
        bar.visible = false
        bar.closed()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
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
        Tk.Flex {
            objectName: "replaceRow"
            visible: bar.replaceEnabled && bar.replaceVisible
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Replace") }
            Tk.TextField {
                id: replaceField
                objectName: "replaceField"
                placeholderText: qsTr("Replacement")
                clearable: true
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minWidth: 120
                onAccepted: bar.replaceOne(field.text, replaceField.text, caseBox.checked)
                Keys.onEscapePressed: bar.close()
            }
            Tk.Button { objectName: "replaceOne"; text: qsTr("Replace"); small: true; onClicked: bar.replaceOne(field.text, replaceField.text, caseBox.checked) }
            Tk.Button { objectName: "replaceAll"; text: qsTr("Replace All"); small: true; onClicked: bar.replaceAll(field.text, replaceField.text, caseBox.checked) }
        }
    }
}
