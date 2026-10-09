// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// The conditions of a rule, its exceptions, or a custom search folder
// (M81, D-090; mail_criteria.h): every condition Outlook offers as a check
// box with its value beside it. load() takes condition maps (and Create
// Rule's suggestions for the unchecked ones); collect() gives the checked
// ones back as maps. `forSearch` adds the conditions only search folders
// use (flagged, unread, older than).
Tk.Flex {
    id: list

    property MailService service: null
    property bool forSearch: false
    property string prefix: "condition"
    // type → {on, text, value, min, max, after, before, days}
    property var entries: ({})

    readonly property var accounts: service !== null ? service.accounts.filter(function(a) { return a.hasMail }) : []
    readonly property var importanceChoices: [{ text: qsTr("High"), value: "high" }, { text: qsTr("Normal"), value: "normal" },
                                              { text: qsTr("Low"), value: "low" }]
    readonly property var rows: {
        const all = [
            { type: "from", label: qsTr("From"), editor: "words", hint: qsTr("Names or addresses, separated by commas") },
            { type: "sentTo", label: qsTr("Sent to (To or Cc)"), editor: "words", hint: qsTr("Names or addresses") },
            { type: "sentOnlyToMe", label: qsTr("Sent only to me"), editor: "" },
            { type: "toMe", label: qsTr("My name is in the To box"), editor: "" },
            { type: "toOrCcMe", label: qsTr("My name is in the To or Cc box"), editor: "" },
            { type: "subject", label: qsTr("Subject contains"), editor: "words", hint: qsTr("Words, separated by commas") },
            { type: "body", label: qsTr("Body contains"), editor: "words", hint: qsTr("Words") },
            { type: "subjectOrBody", label: qsTr("Subject or body contains"), editor: "words", hint: qsTr("Words") },
            { type: "header", label: qsTr("Message header contains"), editor: "words", hint: qsTr("Words, e.g. List-Id: news") },
            { type: "hasAttachment", label: qsTr("Has an attachment"), editor: "" },
            { type: "importance", label: qsTr("Importance is"), editor: "importance" },
            { type: "size", label: qsTr("Size (KB) between"), editor: "size" },
            { type: "received", label: qsTr("Received between"), editor: "dates" },
            { type: "account", label: qsTr("Through the account"), editor: "account" },
            { type: "category", label: qsTr("Assigned to category"), editor: "words", hint: qsTr("Category names (empty: any)") }]
        return forSearch ? all.concat([
            { type: "flagged", label: qsTr("Flagged for follow up"), editor: "" },
            { type: "unread", label: qsTr("Unread"), editor: "" },
            { type: "olderThan", label: qsTr("Older than (days)"), editor: "days" }]) : all
    }

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    function load(conditions, suggestions) {
        const next = {}
        const extra = suggestions || {}
        for (const key in extra) {
            if (!rows.some(function(r) { return r.type === key })) continue      // e.g. Create Rule's folderKey
            const values = typeof extra[key] === "string" ? [extra[key]] : Array.from(extra[key] || [])
            next[key] = { on: false, text: values.join(", "), value: String(values[0] || "") }
        }
        for (const c of (conditions || [])) {
            next[c.type] = { on: true, text: (c.values || []).join(", "), value: String(c.value || (c.values || [""])[0] || ""),
                             min: c.min || 0, max: c.max || 0, after: String(c.after || ""), before: String(c.before || ""),
                             days: c.days || 0 }
        }
        entries = next
    }
    function entry(type) { return entries[type] || ({ on: false, text: "", value: "", min: 0, max: 0, after: "", before: "", days: 0 }) }
    function update(type, key, value) {
        const next = Object.assign({}, entries)
        const current = Object.assign({}, entry(type))
        current[key] = value
        if (key !== "on") current.on = true            // typing a value checks the box
        next[type] = current
        entries = next
    }
    function words(text) { return String(text || "").split(/[,;]/).map(function(w) { return w.trim() }).filter(Boolean) }
    function collect() {
        const out = []
        for (const row of rows) {
            const e = entry(row.type)
            if (!e.on) continue
            const c = { type: row.type }
            if (row.editor === "words") c.values = words(e.text)
            else if (row.editor === "importance") c.value = e.value || "high"
            else if (row.editor === "account") c.values = [e.value || (accounts.length > 0 ? String(accounts[0].id) : "")]
            else if (row.editor === "size") { c.min = Number(e.min || 0); c.max = Number(e.max || 0) }
            else if (row.editor === "dates") { if (e.after) c.after = e.after; if (e.before) c.before = e.before }
            else if (row.editor === "days") c.days = Number(e.days || 7)
            out.push(c)
        }
        return out
    }

    Repeater {
        model: list.rows
        delegate: Tk.Flex {
            id: conditionRow
            required property var modelData
            readonly property var current: list.entry(modelData.type)
            objectName: list.prefix + "_" + modelData.type
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.CheckBox {
                objectName: list.prefix + "Check_" + conditionRow.modelData.type
                text: conditionRow.modelData.label
                checked: conditionRow.current.on === true
                tooltip: conditionRow.modelData.label
                onToggled: list.update(conditionRow.modelData.type, "on", checked)
            }
            Tk.TextField {
                visible: conditionRow.modelData.editor === "words"
                objectName: list.prefix + "Text_" + conditionRow.modelData.type
                small: true
                Tk.Flex.grow: 1
                Tk.Flex.basis: Tk.Theme.size.fieldWidth
                text: String(conditionRow.current.text || "")
                placeholderText: String(conditionRow.modelData.hint || "")
                onTextEdited: list.update(conditionRow.modelData.type, "text", text)
            }
            Tk.ComboBox {
                visible: conditionRow.modelData.editor === "importance"
                small: true
                textRole: "text"
                model: list.importanceChoices
                currentIndex: Math.max(0, list.importanceChoices.map(function(c) { return c.value }).indexOf(String(conditionRow.current.value || "high")))
                onActivated: function(index) { list.update(conditionRow.modelData.type, "value", list.importanceChoices[index].value) }
            }
            Tk.ComboBox {
                visible: conditionRow.modelData.editor === "account"
                small: true
                textRole: "displayName"
                model: list.accounts
                currentIndex: Math.max(0, list.accounts.map(function(a) { return String(a.id) }).indexOf(String(conditionRow.current.value || "")))
                onActivated: function(index) { list.update(conditionRow.modelData.type, "value", String(list.accounts[index].id)) }
            }
            Tk.TextField {
                visible: conditionRow.modelData.editor === "size" || conditionRow.modelData.editor === "days"
                objectName: list.prefix + "Number_" + conditionRow.modelData.type
                small: true
                mono: true
                implicitWidth: Tk.Theme.size.fieldWidth / 2
                text: String(conditionRow.modelData.editor === "days" ? (conditionRow.current.days || "") : (conditionRow.current.min || ""))
                placeholderText: conditionRow.modelData.editor === "days" ? "7" : qsTr("from")
                onTextEdited: list.update(conditionRow.modelData.type, conditionRow.modelData.editor === "days" ? "days" : "min", text)
            }
            Tk.TextField {
                visible: conditionRow.modelData.editor === "size"
                small: true
                mono: true
                implicitWidth: Tk.Theme.size.fieldWidth / 2
                text: String(conditionRow.current.max || "")
                placeholderText: qsTr("to")
                onTextEdited: list.update(conditionRow.modelData.type, "max", text)
            }
            MailDateField {
                visible: conditionRow.modelData.editor === "dates"
                objectName: list.prefix + "After"
                text: String(conditionRow.current.after || "")
                placeholder: qsTr("from")
                onTextChanged: if (text !== String(conditionRow.current.after || "")) list.update(conditionRow.modelData.type, "after", text)
            }
            MailDateField {
                visible: conditionRow.modelData.editor === "dates"
                objectName: list.prefix + "Before"
                text: String(conditionRow.current.before || "")
                placeholder: qsTr("to")
                onTextChanged: if (text !== String(conditionRow.current.before || "")) list.update(conditionRow.modelData.type, "before", text)
            }
        }
    }
}
