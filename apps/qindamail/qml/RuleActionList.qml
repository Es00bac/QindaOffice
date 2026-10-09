// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// What a rule does (M81, D-090; mail_rules.h): every action Outlook offers
// as a check box with its value beside it — a folder for move and copy,
// when to follow up, the categories, the importance, the people a message
// is forwarded or redirected to (the composer's address rules apply when
// the rule is saved), the alert's text. load() / collect() as
// RuleConditionList.
Tk.Flex {
    id: list

    property MailService service: null
    property string accountId: ""
    // type → {on, value, folderKey, text}
    property var entries: ({})

    property var folders: []              // read by load() (folders come and go)
    readonly property var folderNames: folders.map(function(f) {
        const account = service.accounts.find(function(a) { return a.id === f.accountId }) || ({})
        return "  ".repeat(Number(f.depth || 0)) + String(f.name) + (accountId.length > 0 ? "" : " · " + String(account.displayName || f.accountId)) })
    readonly property var flagChoices: [{ text: qsTr("Today"), value: "today" }, { text: qsTr("Tomorrow"), value: "tomorrow" },
                                        { text: qsTr("This week"), value: "thisWeek" }, { text: qsTr("Next week"), value: "nextWeek" },
                                        { text: qsTr("No date"), value: "noDate" }]
    readonly property var importanceChoices: [{ text: qsTr("High"), value: "high" }, { text: qsTr("Normal"), value: "normal" },
                                              { text: qsTr("Low"), value: "low" }]
    readonly property var rows: [
        { type: "moveTo", label: qsTr("Move it to"), editor: "folder" },
        { type: "copyTo", label: qsTr("Copy it to"), editor: "folder" },
        { type: "delete", label: qsTr("Delete it"), editor: "" },
        { type: "deletePermanently", label: qsTr("Permanently delete it"), editor: "" },
        { type: "markRead", label: qsTr("Mark it as read"), editor: "" },
        { type: "flag", label: qsTr("Flag it for follow up"), editor: "flag" },
        { type: "clearFlag", label: qsTr("Clear its flag"), editor: "" },
        { type: "assignCategory", label: qsTr("Assign it to category"), editor: "words", hint: qsTr("Category names, separated by commas") },
        { type: "clearCategories", label: qsTr("Clear its categories"), editor: "" },
        { type: "setImportance", label: qsTr("Mark it as importance"), editor: "importance" },
        { type: "forward", label: qsTr("Forward it to"), editor: "text", hint: qsTr("Addresses, separated by commas") },
        { type: "redirect", label: qsTr("Redirect it to"), editor: "text", hint: qsTr("Addresses (the sender stays theirs)") },
        { type: "notify", label: qsTr("Display a desktop alert"), editor: "text", hint: qsTr("Alert text (empty: the rule's name)") },
        { type: "stop", label: qsTr("Stop processing more rules"), editor: "" }]

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    function load(actions) {
        folders = service !== null ? service.folderTargets(accountId) : []
        const next = {}
        for (const a of (actions || []))
            next[a.type] = { on: true, value: String(a.value || ""), folderKey: String(a.folderKey || ""), text: (a.values || []).join(", ") || String(a.value || "") }
        entries = next
    }
    function entry(type) { return entries[type] || ({ on: false, value: "", folderKey: "", text: "" }) }
    function update(type, key, value) {
        const next = Object.assign({}, entries)
        const current = Object.assign({}, entry(type))
        current[key] = value
        if (key !== "on") current.on = true
        next[type] = current
        entries = next
    }
    function collect() {
        const out = []
        for (const row of rows) {
            const e = entry(row.type)
            if (!e.on) continue
            const a = { type: row.type }
            if (row.editor === "folder") a.folderKey = e.folderKey || (folders.length > 0 ? String(folders[0].key) : "")
            else if (row.editor === "flag") a.value = e.value || "today"
            else if (row.editor === "importance") a.value = e.value || "high"
            else if (row.editor === "words") a.values = String(e.text || "").split(/[,;]/).map(function(w) { return w.trim() }).filter(Boolean)
            else if (row.editor === "text") a.value = String(e.text || "").trim()
            out.push(a)
        }
        return out
    }

    Repeater {
        model: list.rows
        delegate: Tk.Flex {
            id: actionRow
            required property var modelData
            readonly property var current: list.entry(modelData.type)
            objectName: "action_" + modelData.type
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.CheckBox {
                objectName: "actionCheck_" + actionRow.modelData.type
                text: actionRow.modelData.label
                checked: actionRow.current.on === true
                tooltip: actionRow.modelData.label
                onToggled: list.update(actionRow.modelData.type, "on", checked)
            }
            Tk.ComboBox {
                visible: actionRow.modelData.editor === "folder"
                objectName: "actionFolder_" + actionRow.modelData.type
                small: true
                Tk.Flex.grow: 1
                Tk.Flex.basis: Tk.Theme.size.fieldWidth
                model: list.folderNames
                currentIndex: Math.max(0, list.folders.map(function(f) { return String(f.key) }).indexOf(String(actionRow.current.folderKey || "")))
                onActivated: function(index) { list.update(actionRow.modelData.type, "folderKey", String(list.folders[index].key)) }
            }
            Tk.ComboBox {
                visible: actionRow.modelData.editor === "flag" || actionRow.modelData.editor === "importance"
                small: true
                textRole: "text"
                model: actionRow.modelData.editor === "flag" ? list.flagChoices : list.importanceChoices
                currentIndex: Math.max(0, (actionRow.modelData.editor === "flag" ? list.flagChoices : list.importanceChoices)
                                          .map(function(c) { return c.value }).indexOf(String(actionRow.current.value || "")))
                onActivated: function(index) {
                    const choices = actionRow.modelData.editor === "flag" ? list.flagChoices : list.importanceChoices
                    list.update(actionRow.modelData.type, "value", choices[index].value)
                }
            }
            Tk.TextField {
                visible: actionRow.modelData.editor === "words" || actionRow.modelData.editor === "text"
                objectName: "actionText_" + actionRow.modelData.type
                small: true
                Tk.Flex.grow: 1
                Tk.Flex.basis: Tk.Theme.size.fieldWidth
                text: String(actionRow.current.text || "")
                placeholderText: String(actionRow.modelData.hint || "")
                onTextEdited: list.update(actionRow.modelData.type, "text", text)
            }
        }
    }
}
