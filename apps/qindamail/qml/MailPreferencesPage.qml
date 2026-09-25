// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// Settings ▸ Preferences ▸ QindaMail: the accounts, each with its state and
// Edit (the account dialog) — account changes apply at once through the
// service, so apply() has nothing to commit (D-014: no control here is a
// stub; each row acts on the real account).
Tk.Flex {
    id: page

    property var host: null
    readonly property var service: host !== null && host.service !== undefined ? host.service : null

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    function apply() { }

    Tk.SectionHeader { title: qsTr("Accounts") }
    Repeater {
        model: page.service !== null ? page.service.accounts : []
        delegate: Tk.ListRow {
            required property var modelData
            objectName: "prefAccount_" + String(modelData.id)
            iconName: modelData.status === "needsSignIn" ? "circle-alert" : "user"
            text: String(modelData.displayName || modelData.emailAddress || modelData.id)
            secondaryText: String(modelData.emailAddress || "") + " · " + String(modelData.kind || "")
                           + (modelData.status && modelData.status !== "idle" ? " · " + modelData.status : "")
            tooltip: qsTr("Edit %1").arg(text)
            onClicked: page.host.accountDialogRequested(String(modelData.id))
        }
    }
    Tk.Button {
        objectName: "prefAddAccount"
        variant: "outline"
        iconName: "user-plus"
        text: qsTr("Add account…")
        onClicked: page.host.actions.trigger("mailAddAccountAction")
    }
}
