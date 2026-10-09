// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The property sheet of one field in the table designer: caption, rules
// (required, default, unique, indexed), validation (M80: Access's
// Validation Rule and Validation Text, and its Input Mask with the
// wizard's presets), a lookup to another table, and the relationship the
// field belongs to (the parent table, and what happens to this record when
// its parent is deleted or renumbered). Edits go back to the designer as
// `edited(key, value)` / `relationEdited(relationship)`.
Tk.Flex {
    id: sheet

    property var base: null
    property var field: ({})
    property var relationship: null   // this field's relationship, or null
    property string tableName: ""
    readonly property var tables: base !== null ? base.tableNames : []
    // Masks shape what is typed, so a field that takes no typing — or takes
    // a related record's name — has none (base_rules.h validateRules).
    readonly property bool maskable: ["text", "longText", "integer", "number", "currency", "date", "dateTime"]
                                         .indexOf(field.type) >= 0 && !field.lookupTable
    readonly property bool ruled: field.type !== "autoNumber" && field.type !== "file"
    readonly property var refActions: [
        { id: "noAction", label: qsTr("Refuse while related records exist") },
        { id: "cascade", label: qsTr("Delete the related records too") },
        { id: "setNull", label: qsTr("Leave them without a parent") },
        { id: "restrict", label: qsTr("Refuse (checked immediately)") }]
    signal edited(string key, var value)
    signal relationEdited(var relationship)

    function keyField(table) {
        const fields = base.tableFields(table)
        const key = fields.find(function(f) { return f.primaryKey }) || fields.find(function(f) { return f.unique })
        return key ? key.name : ""
    }
    function displayField(table) {
        const fields = base.tableFields(table)
        const text = fields.find(function(f) { return f.type === "text" && !f.primaryKey })
        return text ? text.name : ""
    }
    function fieldNames(table) { return table ? base.tableFields(table).map(function(f) { return f.name }) : [] }

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Tk.SectionHeader { title: qsTr("Field properties · %1").arg(sheet.field.name || "") }
    Tk.PropertyRow {
        label: qsTr("Caption")
        hint: qsTr("Shown as the column heading and on forms")
        Tk.TextField {
            objectName: "fieldCaption"
            text: sheet.field.caption || ""
            placeholderText: sheet.field.name || ""
            onEditingFinished: if (text !== (sheet.field.caption || "")) sheet.edited("caption", text)
        }
    }
    Tk.PropertyRow {
        label: qsTr("Required")
        Tk.Switch {
            objectName: "fieldRequired"
            checked: sheet.field.required === true
            enabled: sheet.field.type !== "autoNumber"
            tooltip: qsTr("A record cannot be saved without it")
            onToggled: sheet.edited("required", checked)
        }
    }
    Tk.PropertyRow {
        label: qsTr("Default value")
        hint: sheet.field.type === "date" || sheet.field.type === "dateTime" ? qsTr("A date, or Date() / Now() for today") : ""
        Tk.TextField {
            objectName: "fieldDefault"
            text: sheet.field.defaultValue || ""
            enabled: sheet.field.type !== "autoNumber"
            mono: true
            onEditingFinished: if (text !== (sheet.field.defaultValue || "")) sheet.edited("defaultValue", text)
        }
    }
    Tk.PropertyRow {
        label: qsTr("Indexed")
        Tk.Segmented {
            objectName: "fieldIndexed"
            small: true
            enabled: !sheet.field.primaryKey
            model: [qsTr("No"), qsTr("Yes"), qsTr("Yes, unique")]
            currentIndex: sheet.field.unique ? 2 : sheet.field.indexed ? 1 : 0
            tooltip: qsTr("An index makes searching and sorting fast; unique refuses duplicates")
            onActivated: function(index) {
                sheet.edited("indexed", index === 1)
                sheet.edited("unique", index === 2)
            }
        }
    }
    Tk.PropertyRow {
        label: qsTr("Description")
        Tk.TextField {
            text: sheet.field.description || ""
            onEditingFinished: if (text !== (sheet.field.description || "")) sheet.edited("description", text)
        }
    }

    Tk.SectionHeader { title: qsTr("Validation") }
    Tk.PropertyRow {
        label: qsTr("Validation rule")
        hint: qsTr("Criteria every value must meet: > 0, Between 1 And 5, Like \"*@*.*\", <= Date()")
        Tk.TextField {
            objectName: "fieldValidationRule"
            text: sheet.field.validationRule || ""
            enabled: sheet.ruled
            mono: true
            placeholderText: "> 0"
            onEditingFinished: if (text !== (sheet.field.validationRule || "")) sheet.edited("validationRule", text)
        }
    }
    Tk.PropertyRow {
        label: qsTr("Validation text")
        hint: qsTr("The message shown when a value breaks the rule")
        Tk.TextField {
            objectName: "fieldValidationText"
            text: sheet.field.validationText || ""
            enabled: sheet.ruled
            onEditingFinished: if (text !== (sheet.field.validationText || "")) sheet.edited("validationText", text)
        }
    }
    Tk.PropertyRow {
        visible: sheet.maskable
        label: qsTr("Input mask")
        hint: qsTr("0 digit · 9 optional digit · L letter · ? optional letter · A letter or digit · > upper case · ;0 keeps the literals")
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.TextField {
                objectName: "fieldInputMask"
                text: sheet.field.inputMask || ""
                mono: true
                placeholderText: "(999) 000-0000"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onEditingFinished: if (text !== (sheet.field.inputMask || "")) sheet.edited("inputMask", text)
            }
            Tk.IconButton {
                id: presetButton
                objectName: "fieldInputMaskPresets"
                iconName: "wand-sparkles"
                small: true
                tooltip: qsTr("Input Mask Wizard: phone number, ZIP code, date…")
                onClicked: presetMenu.popup(presetButton, 0, presetButton.height)
            }
        }
    }

    Tk.SectionHeader { title: qsTr("Relationship") }
    Tk.PropertyRow {
        label: qsTr("Related table")
        hint: sheet.relationship !== null ? qsTr("Each %1 record points to one %2 record by %3.")
                                                .arg(sheet.tableName).arg(sheet.relationship.parentTable).arg(sheet.relationship.parentField)
                                          : qsTr("Link this field to the key of another table")
        Tk.ComboBox {
            objectName: "fieldRelatedTable"
            small: true
            enabled: sheet.field.type === "integer" || sheet.field.type === "text"
            model: [qsTr("None")].concat(sheet.tables.filter(function(t) { return t !== sheet.tableName }))
            currentIndex: sheet.relationship === null ? 0 : Math.max(0, model.indexOf(sheet.relationship.parentTable))
            onActivated: function(index) {
                if (index === 0) { sheet.relationEdited(null); return }
                const parent = model[index]
                sheet.relationEdited({ childTable: sheet.tableName, childField: sheet.field.name, parentTable: parent,
                                       parentField: sheet.keyField(parent),
                                       onDelete: sheet.relationship !== null ? sheet.relationship.onDelete : "noAction",
                                       onUpdate: "cascade" })
                if (!sheet.field.lookupTable) {
                    sheet.edited("lookupTable", parent)
                    sheet.edited("lookupField", sheet.keyField(parent))
                    sheet.edited("lookupDisplay", sheet.displayField(parent))
                }
            }
        }
    }
    Tk.PropertyRow {
        visible: sheet.relationship !== null
        label: qsTr("When deleted")
        Tk.ComboBox {
            objectName: "fieldOnDelete"
            small: true
            textRole: "label"
            model: sheet.refActions
            currentIndex: sheet.relationship === null ? 0
                          : Math.max(0, sheet.refActions.findIndex(function(a) { return a.id === sheet.relationship.onDelete }))
            onActivated: function(index) {
                sheet.relationEdited(Object.assign({}, sheet.relationship, { onDelete: sheet.refActions[index].id }))
            }
        }
    }

    Tk.SectionHeader { title: qsTr("Lookup") }
    Tk.PropertyRow {
        label: qsTr("Show values from")
        hint: qsTr("The datasheet and forms show and accept the related record's name")
        Tk.ComboBox {
            objectName: "fieldLookupTable"
            small: true
            model: [qsTr("None")].concat(sheet.tables)
            currentIndex: sheet.field.lookupTable ? Math.max(0, model.indexOf(sheet.field.lookupTable)) : 0
            onActivated: function(index) {
                const table = index === 0 ? "" : model[index]
                sheet.edited("lookupTable", table)
                sheet.edited("lookupField", table ? sheet.keyField(table) : "")
                sheet.edited("lookupDisplay", table ? sheet.displayField(table) : "")
            }
        }
    }
    Tk.PropertyRow {
        visible: !!sheet.field.lookupTable
        label: qsTr("Display")
        Tk.ComboBox {
            objectName: "fieldLookupDisplay"
            small: true
            model: sheet.fieldNames(sheet.field.lookupTable)
            currentIndex: Math.max(0, model.indexOf(sheet.field.lookupDisplay || ""))
            onActivated: function(index) { sheet.edited("lookupDisplay", model[index]) }
        }
    }

    Tk.Menu {
        id: presetMenu
        Repeater {
            model: sheet.base !== null ? sheet.base.inputMaskPresets : []
            delegate: Tk.MenuItem {
                required property var modelData
                text: modelData.label + " · " + modelData.sample
                onTriggered: sheet.edited("inputMask", modelData.mask)
            }
        }
        Tk.MenuSeparator { }
        Tk.MenuItem { text: qsTr("No input mask"); onTriggered: sheet.edited("inputMask", "") }
    }
}
