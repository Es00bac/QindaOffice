// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Mail.App

// Outlook's Rules and Alerts (M81, D-090): the rules in the order they run,
// each with its on/off box and its sentence; New Rule, Change Rule, Copy,
// Delete, Move Up / Down, Run Rules Now, Import and Export (a JSON file).
// Below, read-only, the rules Exchange or Microsoft 365 runs on the server
// for the accounts that have them.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property string current: ""
    property string message: ""
    readonly property var rules: service !== null && service.organizer.revision >= 0 ? service.organizer.rules : []
    readonly property int currentIndex: rules.findIndex(function(r) { return r.id === dialog.current })

    signal editRequested(var rule)
    signal runRequested()

    objectName: "rulesDialog"
    title: qsTr("Rules and Alerts")
    subtitle: qsTr("Rules run in this order on new mail as it arrives, in every account.")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6
    primaryText: qsTr("Close")
    secondaryText: ""                     // every change is saved as it is made

    function openDialog() {
        message = ""
        if (rules.length > 0 && currentIndex < 0) current = rules[0].id
        service.organizer.refreshServerRules()
        open()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            objectName: "rulesToolbar"
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "rulesNew"; small: true; variant: "outline"; iconName: "plus"; text: qsTr("New Rule…")
                tooltip: qsTr("Make a rule from scratch")
                onClicked: dialog.editRequested(dialog.service.organizer.newRule())
            }
            Tk.Button {
                objectName: "rulesChange"; small: true; iconName: "pencil"; text: qsTr("Change Rule…")
                available: dialog.currentIndex >= 0
                tooltip: qsTr("Edit the selected rule")
                onClicked: dialog.editRequested(dialog.rules[dialog.currentIndex])
            }
            Tk.Button {
                objectName: "rulesCopy"; small: true; iconName: "copy"; text: qsTr("Copy")
                available: dialog.currentIndex >= 0
                tooltip: qsTr("Copy the selected rule")
                onClicked: dialog.current = dialog.service.organizer.copyRule(dialog.current)
            }
            Tk.Button {
                objectName: "rulesDelete"; small: true; iconName: "trash-2"; text: qsTr("Delete")
                available: dialog.currentIndex >= 0
                tooltip: qsTr("Delete the selected rule")
                onClicked: { dialog.service.organizer.deleteRule(dialog.current); dialog.current = "" }
            }
            Tk.IconButton {
                objectName: "rulesUp"; small: true; iconName: "arrow-up"; tooltip: qsTr("Run earlier")
                enabled: dialog.currentIndex > 0
                onClicked: dialog.service.organizer.moveRule(dialog.current, dialog.currentIndex - 1)
            }
            Tk.IconButton {
                objectName: "rulesDown"; small: true; iconName: "arrow-down"; tooltip: qsTr("Run later")
                enabled: dialog.currentIndex >= 0 && dialog.currentIndex < dialog.rules.length - 1
                onClicked: dialog.service.organizer.moveRule(dialog.current, dialog.currentIndex + 1)
            }
            Tk.Button {
                objectName: "rulesRunNow"; small: true; iconName: "play"; text: qsTr("Run Rules Now…")
                available: dialog.rules.length > 0
                tooltip: qsTr("Run rules on mail already in a folder")
                onClicked: dialog.runRequested()
            }
            Tk.Button {
                objectName: "rulesImport"; small: true; iconName: "import"; text: qsTr("Import…")
                tooltip: qsTr("Add rules from a QindaMail rules file")
                onClicked: importDialog.open()
            }
            Tk.Button {
                objectName: "rulesExport"; small: true; iconName: "file-output"; text: qsTr("Export…")
                available: dialog.rules.length > 0
                tooltip: qsTr("Save the rules to a file")
                onClicked: exportDialog.open()
            }
        }
        Tk.Box {
            objectName: "rulesList"
            padding: 0
            color: Tk.Theme.color.surface
            implicitHeight: Math.max(Tk.Theme.size.row * 4, Math.min(Tk.Theme.size.row * 12, rulesColumn.implicitHeight + Tk.Theme.space.xs * 2))
            Tk.Scroll {
                anchors.fill: parent
                Tk.Flex {
                    id: rulesColumn
                    width: parent.width
                    direction: Tk.Flex.Column
                    gap: 0
                    padding: Tk.Theme.space.xs
                    Tk.Caption {
                        objectName: "rulesEmpty"
                        visible: dialog.rules.length === 0
                        text: qsTr("No rules yet. New Rule… makes one; Create Rule on a message starts from its sender.")
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: dialog.rules
                        delegate: Tk.ListRow {
                            id: row
                            required property var modelData
                            required property int index
                            objectName: "rule_" + String(modelData.id)
                            text: (row.index + 1) + ". " + String(modelData.name)
                            secondaryText: String(modelData.summary || "")
                            selected: dialog.current === String(modelData.id)
                            active: modelData.enabled === true
                            tooltip: String(modelData.summary || "")
                            onClicked: dialog.current = String(modelData.id)
                            onDoubleClicked: dialog.editRequested(row.modelData)
                            leading: Tk.CheckBox {
                                objectName: "ruleOn_" + String(row.modelData.id)
                                small: true
                                checked: row.modelData.enabled === true
                                tooltip: qsTr("Turn %1 on or off").arg(String(row.modelData.name))
                                onToggled: {
                                    if (!dialog.service.organizer.setRuleEnabled(String(row.modelData.id), checked)) {
                                        checked = false
                                        dialog.message = qsTr("“%1” needs fixing before it can run.").arg(String(row.modelData.name))
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Tk.Caption { objectName: "rulesMessage"; visible: dialog.message.length > 0; text: dialog.message; wrapMode: Text.Wrap }
        Tk.Overline { visible: dialog.service !== null && dialog.service.organizer.serverRules.length > 0; title: qsTr("On the server (read-only)") }
        Repeater {
            model: dialog.service !== null ? dialog.service.organizer.serverRules : []
            delegate: Tk.ListRow {
                required property var modelData
                objectName: "serverRule_" + String(modelData.name)
                iconName: "cloud"
                text: String(modelData.name) + " · " + String(modelData.accountName) + (modelData.enabled ? "" : " " + qsTr("(off)"))
                secondaryText: String(modelData.summary || "")
                tooltip: String(modelData.summary || "")
            }
        }
        Tk.Caption {
            visible: dialog.service !== null && dialog.service.organizer.serverRules.length > 0
            text: qsTr("These rules run on the Exchange or Microsoft 365 server even when QindaMail is closed; change them in Outlook on the web.")
            wrapMode: Text.Wrap
        }
    }

    Dialogs.FileDialog {
        id: importDialog
        title: qsTr("Import Rules")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("QindaMail rules (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const result = dialog.service.organizer.importRules(selectedFile)
            dialog.message = result.ok ? qsTr("%n rule(s) imported.", "", result.added || 0)
                                         + ((result.problems || []).length > 0 ? " " + qsTr("Turned off until fixed: %1").arg(result.problems.join("; ")) : "")
                                       : String(result.error || "")
        }
    }
    Dialogs.FileDialog {
        id: exportDialog
        title: qsTr("Export Rules")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [qsTr("QindaMail rules (*.json)")]
        onAccepted: {
            const result = dialog.service.organizer.exportRules(selectedFile, [])
            dialog.message = result.ok ? qsTr("%n rule(s) exported.", "", result.count || 0) : String(result.error || "")
        }
    }
}
