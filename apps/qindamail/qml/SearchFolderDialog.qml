// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Outlook's New Search Folder (M81, D-090; Outlook's Ctrl+Shift+P is
// Print Preview here): pick what the
// folder shows — Reading Mail, Mail from People and Lists, Organizing Mail,
// or a custom one built from the rules' conditions — give the preset its
// people, words, size, days or categories, name it and choose the
// accounts it searches. "Customize This Search Folder" opens a saved one
// as a custom folder. The folder appears under Search Folders and stays live.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property string editingKey: ""
    property var folder: ({})
    property string preset: "unread"
    property string matchMode: "all"
    readonly property var presets: service !== null ? service.organizer.searchFolderPresets() : []
    readonly property var presetEntry: presets.find(function(p) { return p.preset === dialog.preset }) || ({})
    readonly property var accountChoices: [{ id: "", displayName: qsTr("All accounts") }].concat(
        service !== null ? service.accounts.filter(function(a) { return a.hasMail }) : [])

    signal saved(string key)

    objectName: "searchFolderDialog"
    title: editingKey.length > 0 ? qsTr("Customize Search Folder") : qsTr("New Search Folder")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")
    primaryEnabled: nameField.text.trim().length > 0

    function openFor(key) {
        editingKey = key || ""
        const saved = editingKey.length > 0 ? service.organizer.searchFolderByKey(editingKey) : ({})
        folder = saved
        preset = saved.preset !== undefined ? "custom" : "unread"
        matchMode = String(saved.match || "all")
        argumentField.text = ""
        nameField.text = String(saved.name || service.organizer.newSearchFolder("unread").name)
        accountBox.currentIndex = Math.max(0, accountChoices.map(function(a) { return String(a.id) })
                                                 .indexOf(String((saved.accounts || [""])[0] || "")))
        criteria.load(saved.conditions || [], {})
        open()
    }
    function choose(name) {
        const wasDefault = nameField.text === String(service.organizer.newSearchFolder(preset).name)
        preset = name
        if (wasDefault || nameField.text.length === 0) nameField.text = String(service.organizer.newSearchFolder(name).name)
    }
    function argument() {
        const kind = String(presetEntry.argument || "")
        if (kind === "size" || kind === "days") return Number(argumentField.text || 0)
        return argumentField.text
    }
    onAccepted: {
        let made = preset === "custom" ? Object.assign({}, folder, { preset: "custom", match: matchMode, conditions: criteria.collect() })
                                       : service.organizer.newSearchFolder(preset, argument())
        if (folder.id !== undefined) made.id = folder.id
        made.name = nameField.text.trim()
        const account = String(accountChoices[accountBox.currentIndex].id || "")
        made.accounts = account.length > 0 ? [account] : []
        const result = service.organizer.saveSearchFolder(made)
        if (result.ok) dialog.saved(String(result.key))
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption { text: qsTr("Select a search folder:"); visible: dialog.editingKey.length === 0 }
        Tk.Box {
            objectName: "searchFolderPresets"
            visible: dialog.editingKey.length === 0
            padding: 0
            color: Tk.Theme.color.surface
            implicitHeight: Tk.Theme.size.row * 8
            Tk.Scroll {
                anchors.fill: parent
                Tk.Flex {
                    width: parent.width
                    direction: Tk.Flex.Column
                    gap: 0
                    padding: Tk.Theme.space.xs
                    Repeater {
                        model: dialog.presets
                        delegate: Tk.Flex {
                            id: presetRow
                            required property var modelData
                            required property int index
                            direction: Tk.Flex.Column
                            gap: 0
                            Tk.Flex.alignSelf: Tk.Flex.Stretch
                            Tk.Overline {
                                visible: presetRow.index === 0 || dialog.presets[presetRow.index - 1].group !== presetRow.modelData.group
                                title: String(presetRow.modelData.group)
                            }
                            Tk.ListRow {
                                objectName: "searchPreset_" + String(presetRow.modelData.preset)
                                text: String(presetRow.modelData.name)
                                selected: dialog.preset === presetRow.modelData.preset
                                indent: Tk.Theme.space.md
                                tooltip: String(presetRow.modelData.name)
                                onClicked: dialog.choose(String(presetRow.modelData.preset))
                            }
                        }
                    }
                }
            }
        }
        Tk.PropertyRow {
            visible: String(dialog.presetEntry.argument || "").length > 0 && dialog.preset !== "custom"
            label: dialog.presetEntry.argument === "people" ? qsTr("From")
                 : dialog.presetEntry.argument === "words" ? qsTr("Words")
                 : dialog.presetEntry.argument === "size" ? qsTr("Larger than (KB)")
                 : dialog.presetEntry.argument === "days" ? qsTr("Older than (days)") : qsTr("Categories")
            Tk.TextField {
                id: argumentField
                objectName: "searchFolderArgument"
                placeholderText: dialog.presetEntry.argument === "size" ? "100"
                               : dialog.presetEntry.argument === "days" ? "7"
                               : dialog.presetEntry.argument === "categories" ? qsTr("Empty: any category") : qsTr("Separated by commas")
            }
        }
        Tk.Segmented {
            visible: dialog.preset === "custom"
            objectName: "searchFolderMatch"
            small: true
            model: [{ text: qsTr("Every checked condition"), value: "all" }, { text: qsTr("Any checked condition"), value: "any" }]
            currentIndex: dialog.matchMode === "any" ? 1 : 0
            onActivated: function(index) { dialog.matchMode = index === 1 ? "any" : "all" }
        }
        RuleConditionList {
            id: criteria
            objectName: "searchFolderCriteria"
            visible: dialog.preset === "custom"
            prefix: "criterion"
            forSearch: true
            service: dialog.service
        }
        Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "searchFolderName" } }
        Tk.PropertyRow {
            label: qsTr("Search mail in")
            Tk.ComboBox { id: accountBox; objectName: "searchFolderAccount"; small: true; textRole: "displayName"; model: dialog.accountChoices }
        }
    }
}
