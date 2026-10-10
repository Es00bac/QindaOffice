// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Access's Indexes dialog (M86, D-094, base_indexes.h): the table's indexes
// over one or several fields, each field ascending or descending, Unique and
// Ignore Nulls. A plain one-field index is that field's Indexed property
// (listed here, changed there). OK puts the indexes into the design as one
// step of the designer's history; Apply plans them (a unique index is
// refused while records repeat its values).
Tk.Dialog {
    id: dialog

    property var view: null
    property var indexes: []
    readonly property var fields: view !== null ? (view.design.fields || []).map(function(f) { return f.name }) : []
    readonly property var fieldIndexes: view !== null ? (view.design.fields || []).filter(function(f) {
        return (f.indexed || f.unique) && !f.primaryKey
    }) : []

    title: qsTr("Indexes")
    subtitle: view !== null ? view.design.name || "" : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")

    function openFor(designer) {
        view = designer
        indexes = JSON.parse(JSON.stringify(designer.design.indexes || []))
        open()
    }
    function change(mutate) {
        const next = JSON.parse(JSON.stringify(indexes))
        mutate(next)
        indexes = next
    }
    function addIndex() {
        let n = indexes.length + 1
        while (indexes.some(function(i) { return i.name === qsTr("Index%1").arg(n) })) ++n
        const name = qsTr("Index%1").arg(n)
        change(function(list) {
            list.push({ name: name, originalName: "", unique: false, ignoreNulls: false,
                        fields: [{ field: dialog.fields[0] || "", descending: false }] })
        })
    }
    onAccepted: {
        const next = indexes
        if (JSON.stringify(next) !== JSON.stringify(view.design.indexes || [])) view.edit(function(d) { d.indexes = next })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption {
            visible: dialog.fieldIndexes.length > 0
            wrapMode: Text.Wrap
            text: qsTr("One-field indexes (each field's Indexed property): %1")
                  .arg(dialog.fieldIndexes.map(function(f) { return f.name + (f.unique ? qsTr(" (unique)") : "") }).join(", "))
        }
        Repeater {
            model: dialog.indexes
            delegate: Tk.Card {
                id: indexCard
                required property var modelData
                required property int index
                objectName: "indexCard_" + index
                padding: Tk.Theme.space.sm
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        wrap: Tk.Flex.Wrap
                        Tk.TextField {
                            objectName: "indexName_" + indexCard.index
                            small: true
                            text: indexCard.modelData.name
                            tooltip: qsTr("Index name")
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.6
                            onEditingFinished: { const t = text.trim(); const at = indexCard.index; dialog.change(function(l) { l[at].name = t }) }
                        }
                        Tk.CheckBox {
                            objectName: "indexUnique_" + indexCard.index
                            text: qsTr("Unique")
                            checked: indexCard.modelData.unique === true
                            onToggled: { const on = checked; const at = indexCard.index; dialog.change(function(l) { l[at].unique = on }) }
                        }
                        Tk.CheckBox {
                            objectName: "indexIgnoreNulls_" + indexCard.index
                            text: qsTr("Ignore Nulls")
                            checked: indexCard.modelData.ignoreNulls === true
                            tooltip: qsTr("Records with an empty field stay out of the index")
                            onToggled: { const on = checked; const at = indexCard.index; dialog.change(function(l) { l[at].ignoreNulls = on }) }
                        }
                        Tk.IconButton {
                            iconName: "trash-2"
                            small: true
                            tooltip: qsTr("Delete this index")
                            onClicked: { const at = indexCard.index; dialog.change(function(l) { l.splice(at, 1) }) }
                        }
                    }
                    Repeater {
                        model: indexCard.modelData.fields || []
                        delegate: Tk.Flex {
                            id: part
                            required property var modelData
                            required property int index
                            direction: Tk.Flex.Row
                            align: Tk.Flex.Center
                            gap: Tk.Theme.space.sm
                            Tk.Caption { text: part.index === 0 ? qsTr("Fields") : ""; Tk.Flex.basis: Tk.Theme.size.control * 2; Tk.Flex.shrink: 0 }
                            Tk.ComboBox {
                                objectName: "indexField_" + indexCard.index + "_" + part.index
                                small: true
                                model: dialog.fields
                                currentIndex: model.indexOf(part.modelData.field)
                                Tk.Flex.grow: 1
                                Tk.Flex.basis: 0
                                onActivated: function(i) {
                                    const name = dialog.fields[i]; const at = indexCard.index; const k = part.index
                                    dialog.change(function(l) { l[at].fields[k].field = name })
                                }
                            }
                            Tk.Segmented {
                                small: true
                                model: [qsTr("Ascending"), qsTr("Descending")]
                                currentIndex: part.modelData.descending ? 1 : 0
                                onActivated: function(i) {
                                    const at = indexCard.index; const k = part.index
                                    dialog.change(function(l) { l[at].fields[k].descending = i === 1 })
                                }
                            }
                            Tk.IconButton {
                                iconName: "x"
                                small: true
                                enabled: (indexCard.modelData.fields || []).length > 1
                                tooltip: qsTr("Leave this field out of the index")
                                onClicked: { const at = indexCard.index; const k = part.index; dialog.change(function(l) { l[at].fields.splice(k, 1) }) }
                            }
                        }
                    }
                    Tk.Button {
                        objectName: "indexAddField_" + indexCard.index
                        text: qsTr("Add a field")
                        iconName: "plus"
                        small: true
                        enabled: (indexCard.modelData.fields || []).length < 10
                        onClicked: {
                            const at = indexCard.index
                            const used = (indexCard.modelData.fields || []).map(function(f) { return f.field })
                            const next = dialog.fields.find(function(n) { return used.indexOf(n) < 0 }) || dialog.fields[0]
                            dialog.change(function(l) { l[at].fields.push({ field: next, descending: false }) })
                        }
                    }
                }
            }
        }
        Tk.Button {
            objectName: "indexAdd"
            text: qsTr("Add an index")
            iconName: "list-ordered"
            small: true
            onClicked: dialog.addIndex()
        }
    }
}
