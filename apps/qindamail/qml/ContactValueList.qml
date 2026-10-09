// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// One repeated, typed contact field of the People editor (M64, D-072) —
// e-mail addresses, phone numbers or web pages: a type, the value, remove;
// "Add" below. The editor loads `values` and reads them back on save.
//
// AGENT-CONTRACT (ContactEditor.qml, office_mail contact.h): a value is
// {type, value, preferred}; `types` are [{value, text}] with "" meaning no type.
// AGENT-GUARD: typing edits the entries IN PLACE (no reassignment), so the
// Repeater keeps its delegates and the field being typed in keeps focus;
// only Add and Remove replace the array.
Tk.Flex {
    id: list

    property string title: ""
    property string fieldName: ""
    property var values: []
    property var types: []
    property string placeholderText: ""
    property string addText: qsTr("Add")
    signal modified()

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    function typeIndex(type) {
        const index = list.types.map(function(t) { return t.value }).indexOf(String(type || ""))
        return Math.max(0, index)
    }
    function changed(index, key, value) {
        list.values[index][key] = value
        list.modified()
    }
    function removeAt(index) {
        const next = list.values.slice()
        next.splice(index, 1)
        list.values = next
        list.modified()
    }
    function add() {
        list.values = list.values.concat([{ type: list.types.length > 0 ? list.types[0].value : "", value: "",
                                            preferred: list.values.length === 0 }])
        list.modified()
    }
    // A copy without the empty rows, for saving.
    function collected() {
        return list.values.filter(function(entry) { return String(entry.value || "").trim().length > 0 })
                          .map(function(entry) { return Object.assign({}, entry, { value: String(entry.value).trim() }) })
    }

    Tk.Label { text: list.title; muted: true; visible: list.title.length > 0 }
    Repeater {
        model: list.values
        delegate: Tk.Flex {
            id: entryRow
            required property var modelData
            required property int index
            objectName: "contact_" + list.fieldName + "_" + index
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            width: list.width
            Tk.ComboBox {
                objectName: "contact_" + list.fieldName + "_type_" + entryRow.index
                small: true
                implicitWidth: Tk.Theme.size.fieldWidth
                model: list.types
                textRole: "text"
                currentIndex: list.typeIndex(entryRow.modelData.type)
                tooltip: qsTr("Kind")
                onActivated: function(index) { list.changed(entryRow.index, "type", list.types[index].value) }
            }
            Tk.TextField {
                objectName: "contact_" + list.fieldName + "_value_" + entryRow.index
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                text: String(entryRow.modelData.value || "")
                placeholderText: list.placeholderText
                tooltip: list.title
                onTextEdited: list.changed(entryRow.index, "value", text)
            }
            Tk.IconButton {
                objectName: "contact_" + list.fieldName + "_remove_" + entryRow.index
                small: true
                iconName: "x"
                tooltip: qsTr("Remove")
                onClicked: list.removeAt(entryRow.index)
            }
        }
    }
    Tk.Button {
        objectName: "contact_" + list.fieldName + "_add"
        // The column stretches its rows; the button keeps its own width.
        Tk.Flex.alignSelf: Tk.Flex.Start
        small: true
        variant: "ghost"
        iconName: "plus"
        text: list.addText
        tooltip: list.addText
        onClicked: list.add()
    }
}
