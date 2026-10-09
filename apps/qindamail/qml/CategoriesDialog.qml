// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Outlook's Color Categories dialog (M81, D-090): the master list of the
// item's account — check boxes assign categories to the selected items
// (OK applies them), and New, Rename, Delete and the colour change the
// list. An Exchange or Microsoft 365 account's list belongs to the server:
// new categories are added there, renaming and deleting are not offered.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property string kind: ""
    property var ids: []
    property string accountId: ""
    property var checkedNames: []
    property string current: ""
    property string editMode: ""          // "" | new | rename
    property string problem: ""
    readonly property var categories: service !== null && service.organizer.revision >= 0
                                      ? service.organizer.categoriesFor(accountId) : []
    readonly property bool serverOwned: categories.length > 0 && categories[0].serverOwned === true
    readonly property bool assigning: ids.length > 0
    readonly property var currentEntry: categories.find(function(c) { return String(c.name) === dialog.current }) || null

    objectName: "categoriesDialog"
    title: qsTr("Color Categories")
    subtitle: assigning ? qsTr("Check the categories the selected item has; select a name to change it.")
                        : qsTr("The categories you can give mail, events, tasks and contacts.")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.2
    primaryText: assigning ? qsTr("OK") : qsTr("Close")
    secondaryText: assigning ? qsTr("Cancel") : ""

    function openFor(kind, ids, accountId) {
        dialog.kind = kind || ""
        dialog.ids = ids || []
        dialog.accountId = accountId || "local"
        dialog.checkedNames = dialog.ids.length > 0 ? service.organizer.categoriesOf(dialog.kind, dialog.ids[0]) : []
        dialog.current = ""
        dialog.editMode = ""
        dialog.problem = ""
        open()
    }
    function isChecked(name) {
        return checkedNames.some(function(n) { return String(n).toLowerCase() === String(name).toLowerCase() })
    }
    function setChecked(name, on) {
        const rest = checkedNames.filter(function(n) { return String(n).toLowerCase() !== String(name).toLowerCase() })
        checkedNames = on ? rest.concat([name]) : rest
    }
    function commitEdit() {
        const name = nameField.text.trim()
        let result = { ok: false, error: qsTr("Give the category a name") }
        if (name.length > 0 && editMode === "new") {
            result = service.organizer.addCategory(accountId, name, "")
            if (result.ok && assigning) setChecked(name, true)
        } else if (name.length > 0 && editMode === "rename") {
            result = service.organizer.updateCategory(accountId, current, { name: name })
            if (result.ok && isChecked(current)) { setChecked(current, false); setChecked(name, true) }
        }
        problem = result.ok ? "" : String(result.error || "")
        if (result.ok) { current = name; editMode = "" }
    }
    onAccepted: if (assigning) service.organizer.setCategories(kind, ids, checkedNames)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice {
            visible: dialog.serverOwned
            variant: "info"
            text: qsTr("This account's categories are kept by Exchange: new ones are added there; rename or delete them in Outlook on the web.")
        }
        Tk.Box {
            objectName: "categoriesList"
            padding: 0
            implicitHeight: Math.min(Tk.Theme.size.row * 9, listColumn.implicitHeight) + Tk.Theme.space.xs * 2
            color: Tk.Theme.color.surface
            Tk.Scroll {
                anchors.fill: parent
                Tk.Flex {
                    id: listColumn
                    width: parent.width
                    direction: Tk.Flex.Column
                    gap: 0
                    padding: Tk.Theme.space.xs
                    Repeater {
                        model: dialog.categories
                        delegate: Tk.ListRow {
                            id: row
                            required property var modelData
                            objectName: "category_" + String(modelData.name)
                            text: String(modelData.name)
                            selected: dialog.current === String(modelData.name)
                            tooltip: String(modelData.name)
                            onClicked: { dialog.current = String(modelData.name); dialog.editMode = "" }
                            leading: Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.xs
                                Tk.CheckBox {
                                    objectName: "categoryCheck_" + String(row.modelData.name)
                                    visible: dialog.assigning
                                    small: true
                                    checked: dialog.isChecked(row.modelData.name)
                                    tooltip: qsTr("Give the selected item %1").arg(String(row.modelData.name))
                                    onToggled: dialog.setChecked(String(row.modelData.name), checked)
                                }
                                // A category's colour is user data (AGENTS.md rule 3).
                                Rectangle {
                                    implicitWidth: Tk.Theme.size.iconSm
                                    implicitHeight: implicitWidth
                                    radius: Tk.Theme.radius.xs
                                    color: String(row.modelData.color || "").length > 0 ? String(row.modelData.color) : Tk.Theme.color.textMuted
                                }
                            }
                        }
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "categoryNew"
                small: true
                iconName: "plus"
                text: qsTr("New…")
                tooltip: qsTr("Add a category to the list")
                onClicked: { dialog.editMode = "new"; nameField.text = ""; nameField.forceActiveFocus() }
            }
            Tk.Button {
                objectName: "categoryRename"
                small: true
                iconName: "pencil"
                text: qsTr("Rename")
                tooltip: qsTr("Rename the selected category everywhere it is used")
                available: dialog.current.length > 0 && !dialog.serverOwned
                onClicked: { dialog.editMode = "rename"; nameField.text = dialog.current; nameField.forceActiveFocus() }
            }
            Tk.Button {
                objectName: "categoryDelete"
                small: true
                iconName: "trash-2"
                text: qsTr("Delete")
                tooltip: qsTr("Take the selected category off the list (items keep the name)")
                available: dialog.current.length > 0 && !dialog.serverOwned
                onClicked: { dialog.setChecked(dialog.current, false); dialog.service.organizer.deleteCategory(dialog.accountId, dialog.current); dialog.current = "" }
            }
            Tk.ColorButton {
                objectName: "categoryColor"
                small: true
                enabled: dialog.currentEntry !== null && !dialog.serverOwned
                color: dialog.currentEntry !== null ? String(dialog.currentEntry.color) : Tk.Theme.color.textMuted
                colors: dialog.service !== null ? dialog.service.organizer.categoryPalette().map(function(p) { return { name: p.name, color: p.color } }) : []
                columns: 5
                tooltip: qsTr("The selected category's colour")
                onColorSelected: function(color) { dialog.service.organizer.updateCategory(dialog.accountId, dialog.current, { color: String(color) }) }
            }
        }
        Tk.Flex {
            visible: dialog.editMode.length > 0
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.TextField {
                id: nameField
                objectName: "categoryName"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                placeholderText: dialog.editMode === "new" ? qsTr("New category name") : qsTr("New name")
                // Enter adds or renames and keeps the dialog up (the dialog's
                // own Enter would accept it): taken before the field's handling.
                Keys.onReturnPressed: function(event) { event.accepted = true; dialog.commitEdit() }
                Keys.onEnterPressed: function(event) { event.accepted = true; dialog.commitEdit() }
            }
            Tk.Button {
                objectName: "categoryNameSave"
                small: true
                variant: "outline"
                text: dialog.editMode === "new" ? qsTr("Add") : qsTr("Rename")
                onClicked: dialog.commitEdit()
            }
        }
        Tk.Caption { objectName: "categoryProblem"; visible: dialog.problem.length > 0; text: dialog.problem; color: Tk.Theme.color.danger; wrapMode: Text.Wrap }
    }
}
