// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Outlook's Run Rules Now (M81, D-090): the chosen rules — turned off ones
// too — on the messages already in a folder (and its subfolders), all of
// them or only the read or unread ones. The result says how many messages
// were looked at and how many a rule acted on.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property var chosen: []
    property string requestId: ""
    property string outcome: ""
    readonly property var rules: service !== null && service.organizer.revision >= 0 ? service.organizer.rules : []
    property var folders: []              // read when the dialog opens (folders come and go)
    readonly property var folderNames: folders.map(function(f) {
        const account = service.accounts.find(function(a) { return a.id === f.accountId }) || ({})
        return "  ".repeat(Number(f.depth || 0)) + String(f.name) + " · " + String(account.displayName || "") })
    readonly property var whichChoices: [{ text: qsTr("All messages"), value: "all" }, { text: qsTr("Read messages"), value: "read" },
                                         { text: qsTr("Unread messages"), value: "unread" }]

    objectName: "runRulesDialog"
    title: qsTr("Run Rules Now")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("Run Now")
    secondaryText: qsTr("Close")
    primaryEnabled: chosen.length > 0 && folders.length > 0 && requestId.length === 0

    function openFor(folderKey) {
        folders = service !== null ? service.folderTargets("") : []
        outcome = ""
        requestId = ""
        chosen = rules.filter(function(r) { return r.enabled }).map(function(r) { return r.id })
        folderBox.currentIndex = Math.max(0, folders.map(function(f) { return String(f.key) }).indexOf(String(folderKey || "")))
        open()
    }
    function toggle(id, on) {
        const rest = chosen.filter(function(c) { return c !== id })
        chosen = on ? rest.concat([id]) : rest
    }
    // Run keeps the dialog up to show the outcome.
    onAccepted: {
        requestId = service.organizer.runRulesNow(chosen, String(folders[folderBox.currentIndex].key), subfolders.checked,
                                                  whichChoices[whichBox.currentIndex].value)
        outcome = qsTr("Running…")
        open()
    }

    Connections {
        target: dialog.service !== null ? dialog.service.organizer : null
        function onRulesRunFinished(requestId, outcome) {
            if (requestId !== dialog.requestId) return
            dialog.requestId = ""
            dialog.outcome = qsTr("%1 message(s) checked, %2 handled by a rule.").arg(outcome.processed || 0).arg(outcome.matched || 0)
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Overline { title: qsTr("Rules to run") }
        Repeater {
            model: dialog.rules
            delegate: Tk.CheckBox {
                required property var modelData
                objectName: "runRule_" + String(modelData.id)
                text: String(modelData.name) + (modelData.enabled ? "" : " " + qsTr("(off)"))
                checked: dialog.chosen.indexOf(modelData.id) >= 0
                tooltip: String(modelData.summary || "")
                onToggled: dialog.toggle(modelData.id, checked)
            }
        }
        Tk.PropertyRow {
            label: qsTr("Run in folder")
            Tk.ComboBox { id: folderBox; objectName: "runRulesFolder"; small: true; model: dialog.folderNames }
        }
        Tk.CheckBox { id: subfolders; objectName: "runRulesSubfolders"; text: qsTr("Include subfolders") }
        Tk.PropertyRow {
            label: qsTr("Apply rules to")
            Tk.ComboBox { id: whichBox; objectName: "runRulesWhich"; small: true; textRole: "text"; model: dialog.whichChoices }
        }
        Tk.Caption { objectName: "runRulesResult"; visible: dialog.outcome.length > 0; text: dialog.outcome; wrapMode: Text.Wrap }
    }
}
