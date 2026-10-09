// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// One rule (M81, D-090), Outlook's Rules Wizard on one page: its name and
// whether it is on, then — one at a time — the conditions, what to do and
// the exceptions, with the rule's sentence below ("Apply this rule after a
// message arrives from …"). Create Rule opens it with the message's sender
// checked and its subject, its account and you offered. Save checks the
// rule as it is edited (organizer.validateRule; Save waits for it) and can
// run it on the current folder.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property var rule: ({})
    property int revision: 0              // bumps the sentence and the checks after an edit
    property string folderKey: ""
    // What keeps Save off, worded without the field paths.
    readonly property var problems: revision >= 0 && service !== null && opened
        ? service.organizer.validateRule(collect()).map(function(p) { return String(p).replace(/^[^:]*: /, "") }) : []

    // Create Rule's rule has a name already: "Edit" only for a saved one.
    readonly property bool existing: service !== null && service.organizer.revision >= 0
        && service.organizer.rules.some(function(r) { return r.id === dialog.rule.id })

    objectName: "ruleEditor"
    title: existing ? qsTr("Edit rule") : qsTr("New rule")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.45
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")
    primaryEnabled: nameField.text.trim().length > 0 && problems.length === 0

    function openFor(rule, suggestions, folderKey) {
        dialog.rule = rule || service.organizer.newRule()
        dialog.folderKey = folderKey || (service !== null ? service.currentFolderKey : "")
        nameField.text = String(dialog.rule.name || "")
        enabledBox.checked = dialog.rule.enabled !== false
        runNowBox.checked = false
        conditions.load(dialog.rule.conditions || [], suggestions || {})
        exceptions.load(dialog.rule.exceptions || [], {})
        actions.load(dialog.rule.actions || [])
        pages.currentIndex = (dialog.rule.conditions || []).length > 0 && (dialog.rule.actions || []).length === 0 ? 1 : 0
        open()
        nameField.forceActiveFocus()
    }
    function collect() {
        return Object.assign({}, rule, { name: nameField.text.trim(), enabled: enabledBox.checked,
                                         conditions: conditions.collect(), exceptions: exceptions.collect(), actions: actions.collect() })
    }
    function save() {
        const result = service.organizer.saveRule(collect())
        if (result.ok && runNowBox.checked && folderKey.length > 0)
            service.organizer.runRulesNow([result.id], folderKey, false, "all")
        return result
    }
    onAccepted: save()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Name")
            Tk.TextField { id: nameField; objectName: "ruleName"; placeholderText: qsTr("Name the rule"); onTextEdited: dialog.revision++ }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.md
            Tk.CheckBox { id: enabledBox; objectName: "ruleEnabled"; text: qsTr("Turn on this rule"); checked: true }
            Tk.CheckBox { id: runNowBox; objectName: "ruleRunNow"; text: qsTr("Run it now on the current folder") }
        }
        Tk.Segmented {
            id: pages
            objectName: "rulePages"
            small: true
            stretch: true
            model: [{ text: qsTr("Conditions"), value: "conditions" }, { text: qsTr("Actions"), value: "actions" },
                    { text: qsTr("Exceptions"), value: "exceptions" }]
            tooltip: qsTr("Which part of the rule to edit")
            onActivated: function(index) { pages.currentIndex = index; dialog.revision++ }
        }
        Tk.Caption {
            text: pages.currentIndex === 0 ? qsTr("Apply the rule to a message that matches every checked condition (none: every message):")
                : pages.currentIndex === 1 ? qsTr("Then do every checked action, in this order:")
                                           : qsTr("Except when any checked exception matches:")
            wrapMode: Text.Wrap
        }
        RuleConditionList {
            id: conditions
            objectName: "ruleConditions"
            visible: pages.currentIndex === 0
            prefix: "condition"
            service: dialog.service
            onEntriesChanged: dialog.revision++
        }
        RuleActionList {
            id: actions
            objectName: "ruleActions"
            visible: pages.currentIndex === 1
            service: dialog.service
            onEntriesChanged: dialog.revision++
        }
        RuleConditionList {
            id: exceptions
            objectName: "ruleExceptions"
            visible: pages.currentIndex === 2
            prefix: "exception"
            service: dialog.service
            onEntriesChanged: dialog.revision++
        }
        Tk.Divider { }
        Tk.Caption {
            objectName: "ruleSummary"
            text: dialog.revision >= 0 && dialog.service !== null && dialog.opened ? dialog.service.organizer.describeRule(dialog.collect()) : ""
            wrapMode: Text.Wrap
        }
        Tk.Notice {
            objectName: "ruleProblems"
            visible: dialog.problems.length > 0
            variant: "info"
            title: qsTr("To save the rule")
            text: dialog.problems.join("\n")
        }
    }
}
