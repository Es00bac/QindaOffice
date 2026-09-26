// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Creates one relationship: which field points at which key, and what a
// delete or a renumbering of the key does to the records that point at it.
// The plan is previewed live against the data (records that point at
// nothing are counted and refuse the relationship until fixed), and runs
// as one undoable step.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var callback: null
    property string childTable: ""
    property string childField: ""
    property string parentTable: ""
    property string parentField: ""
    property string onDelete: "noAction"
    property string onUpdate: "cascade"
    readonly property var tables: base !== null ? base.tableNames : []
    readonly property var relation: ({ childTable: childTable, childField: childField, parentTable: parentTable,
                                        parentField: parentField, onDelete: onDelete, onUpdate: onUpdate })
    readonly property var plan: visible && childTable && childField && parentTable && parentField
                                ? base.planRelationship(relation, true) : ({})
    readonly property var actions: [
        { id: "noAction", label: qsTr("Refuse while related records exist") },
        { id: "cascade", label: qsTr("Delete the related records too") },
        { id: "setNull", label: qsTr("Leave them without a parent") }]

    title: qsTr("Relationship")
    subtitle: qsTr("Every record on the many side points at one record on the one side.")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: qsTr("Create")
    secondaryText: qsTr("Cancel")
    primaryEnabled: plan.ok === true

    function fieldsOf(table) { return table ? base.tableFields(table) : [] }
    function keysOf(table) { return fieldsOf(table).filter(function(f) { return f.primaryKey || f.unique }).map(function(f) { return f.name }) }
    function openFor(r, done) {
        callback = done
        parentTable = r.parentTable || (tables.length > 0 ? tables[0] : "")
        parentField = r.parentField || (keysOf(parentTable)[0] || "")
        childTable = r.childTable || (tables.length > 1 ? tables[1] : "")
        childField = r.childField || ""
        onDelete = r.onDelete || "noAction"
        onUpdate = r.onUpdate || "cascade"
        open()
    }
    onAccepted: {
        const r = base.applyRelationship(relation, true)
        if (!r.ok) window.notify(qsTr("No relationship created"), r.error, "danger")
        const f = callback
        callback = null
        if (f) f()
    }
    onRejected: { const f = callback; callback = null; if (f) f() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("One")
            hint: qsTr("The table whose records are pointed at, and its key")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.ComboBox {
                    objectName: "relationParentTable"
                    small: true
                    model: dialog.tables
                    currentIndex: model.indexOf(dialog.parentTable)
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.parentTable = model[i]; dialog.parentField = dialog.keysOf(model[i])[0] || "" }
                }
                Tk.ComboBox {
                    small: true
                    model: dialog.keysOf(dialog.parentTable)
                    currentIndex: model.indexOf(dialog.parentField)
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.parentField = model[i] }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Many")
            hint: qsTr("The table whose records point, and the field that holds the key")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.ComboBox {
                    objectName: "relationChildTable"
                    small: true
                    model: dialog.tables
                    currentIndex: model.indexOf(dialog.childTable)
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.childTable = model[i]; dialog.childField = "" }
                }
                Tk.ComboBox {
                    objectName: "relationChildField"
                    small: true
                    model: dialog.fieldsOf(dialog.childTable).map(function(f) { return f.name })
                    currentIndex: model.indexOf(dialog.childField)
                    placeholderText: qsTr("Field")
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.childField = model[i] }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("When deleted")
            Tk.ComboBox {
                objectName: "relationOnDelete"
                small: true
                textRole: "label"
                model: dialog.actions
                currentIndex: Math.max(0, dialog.actions.findIndex(function(a) { return a.id === dialog.onDelete }))
                onActivated: function(i) { dialog.onDelete = dialog.actions[i].id }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Key changes")
            Tk.Switch {
                text: qsTr("Update the related records")
                checked: dialog.onUpdate === "cascade"
                onToggled: dialog.onUpdate = checked ? "cascade" : "noAction"
            }
        }
        Tk.Notice {
            objectName: "relationProblem"
            visible: dialog.plan.ok === false && !!dialog.plan.error
            variant: "danger"
            text: dialog.plan.error || ""
        }
        Repeater {
            model: dialog.plan.notes || []
            delegate: Tk.Caption { required property string modelData; text: "• " + modelData; wrapMode: Text.Wrap }
        }
    }
}
