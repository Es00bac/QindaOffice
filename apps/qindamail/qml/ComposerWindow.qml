// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Mail.App

// One draft = one ordinary top-level window (D-042, ADR-0110 in spirit:
// the desktop groups composers with the main window by app id, nothing
// app-side). Tk.RichMailComposer edits; this window owns From, Send,
// attachments and the draft's lifetime: closing keeps the draft (saved
// locally), Discard removes it, a delivered message closes the window.
//
// AGENT-CONTRACT (MailService): `send(draft)` returns the service request
// id and later emits `sendFinished(requestId, result)`; the composer's own
// request id is acknowledged with the result's state.
Tk.AppWindow {
    id: composerWindow

    property MailService service: null
    property var draft: ({})
    property string fromAccountId: String(draft.accountId || (service !== null ? service.defaultAccountId : "local"))
    property string pendingComposerRequest: ""
    property string pendingServiceRequest: ""
    property bool finished: false

    title: (String(composer.workingDraft.subject || "").length > 0 ? composer.workingDraft.subject : qsTr("New message")) + " — QindaMail"
    width: Tk.Theme.size.dialogWidth * 1.9
    height: Tk.Theme.size.dialogWidth * 1.6

    function merged(snapshot) {
        return Object.assign({}, composerWindow.draft, snapshot, { accountId: composerWindow.fromAccountId })
    }
    function localPath(url) { return decodeURIComponent(String(url).replace(/^file:\/\//, "")) }
    function uuid() {
        return "att-xxxxxxxx-xxxx".replace(/x/g, function() { return (Math.random() * 16 | 0).toString(16) })
    }

    toolBars: [
        Tk.ToolBar {
            objectName: "composerToolbar"
            Tk.Button {
                objectName: "composerWindowSend"
                variant: "accent"
                iconName: "send"
                text: qsTr("Send")
                tooltip: composer.recipientError.length > 0 ? composer.recipientError : qsTr("Send (Ctrl+Enter)")
                available: composer.validDraft && composerWindow.service !== null && composerWindow.service.online
                           && composerWindow.pendingServiceRequest.length === 0
                onClicked: composer.requestSend()
            }
            Tk.ToolSeparator { }
            Tk.Label { text: qsTr("From"); muted: true }
            Tk.ComboBox {
                objectName: "composerFrom"
                small: true
                implicitWidth: Tk.Theme.size.fieldWidth * 2.4
                textRole: "label"
                placeholderText: qsTr("Add an account to send mail")
                model: (composerWindow.service !== null ? composerWindow.service.sendableAccounts : []).map(function(a) {
                    return { id: a.id, label: a.displayName + " <" + a.emailAddress + ">" } })
                currentIndex: Math.max(0, model.map(function(a) { return a.id }).indexOf(composerWindow.fromAccountId))
                onActivated: function(index) { composerWindow.fromAccountId = model[index].id }
            }
            Tk.ToolSeparator { }
            Tk.Button { objectName: "composerWindowAttach"; variant: "ghost"; iconName: "paperclip"; text: qsTr("Attach"); tooltip: qsTr("Attach a file"); onClicked: composer.requestAttachmentAdd() }
            Tk.Button { objectName: "composerWindowBcc"; variant: "ghost"; checkable: true; checked: composer.bccVisible; text: qsTr("Bcc"); tooltip: qsTr("Show the Bcc field"); onClicked: composer.bccVisible = !composer.bccVisible }
            Tk.Spacer { }
            Tk.Caption { objectName: "composerWindowStatus"; text: composer.statusMessage }
            Tk.Button { objectName: "composerWindowSave"; variant: "ghost"; iconName: "save"; text: qsTr("Save draft"); onClicked: composer.requestSave() }
            Tk.Button {
                objectName: "composerWindowDiscard"
                variant: "ghost"
                iconName: "trash-2"
                text: qsTr("Discard")
                tooltip: qsTr("Delete this draft")
                onClicked: {
                    composerWindow.finished = true
                    if (composerWindow.service !== null && composerWindow.draft.id) composerWindow.service.discardDraft(String(composerWindow.draft.id))
                    composerWindow.close()
                }
            }
        }
    ]

    Tk.Scroll {
        id: scroll
        objectName: "composerScroll"
        Tk.Box {
            width: scroll.viewportWidth
            padding: Tk.Theme.space.lg
            Tk.RichMailComposer {
                id: composer
                objectName: "mailComposer"
                headerVisible: false
                actionsVisible: false
                bodyRows: 18
                draft: composerWindow.draft
                online: composerWindow.service !== null && composerWindow.service.online
                onSendRequested: function(requestId, snapshot) {
                    composerWindow.pendingComposerRequest = requestId
                    composerWindow.pendingServiceRequest = composerWindow.service.send(composerWindow.merged(snapshot))
                }
                onSaveDraftRequested: function(requestId, snapshot) {
                    const saved = composerWindow.service.saveDraft(composerWindow.merged(snapshot))
                    if (saved && saved.id) composerWindow.draft = Object.assign({}, composerWindow.draft, { id: saved.id })
                    composer.acknowledge(requestId, saved && saved.ok !== false ? "accepted" : "rejected",
                                         saved && saved.ok !== false ? qsTr("Draft saved") : String(saved ? saved.error : ""))
                }
                onAttachmentAddRequested: function(requestId) { attachDialog.requestId = requestId; attachDialog.open() }
                onAttachmentRemoveRequested: function(requestId, attachmentId) {
                    const result = composerWindow.service.attachmentStore.remove(attachmentId)
                    composer.acknowledgeAttachment(requestId, result.ok ? "accepted" : "rejected", String(result.error || ""), { id: attachmentId })
                }
            }
        }
    }

    Dialogs.FileDialog {
        id: attachDialog
        property string requestId: ""
        title: qsTr("Attach a file")
        fileMode: Dialogs.FileDialog.OpenFile
        onAccepted: {
            const id = composerWindow.uuid()
            const result = composerWindow.service.attachmentStore.importFile(id, composerWindow.localPath(selectedFile))
            composer.acknowledgeAttachment(requestId, result.ok ? "accepted" : "rejected", String(result.error || ""),
                                           { id: id, name: result.name, mimeType: result.mimeType, size: result.size, state: "local" })
        }
        onRejected: composer.acknowledgeAttachment(requestId, "rejected", qsTr("No file chosen"), {})
    }
    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; onActivated: composer.requestSend() }
    Connections {
        target: composerWindow.service
        function onSendFinished(requestId, result) {
            if (requestId !== composerWindow.pendingServiceRequest) return
            composerWindow.pendingServiceRequest = ""
            composer.acknowledge(composerWindow.pendingComposerRequest, result.state, String(result.error || ""))
            if (result.state === "accepted") {
                composerWindow.finished = true
                composerWindow.close()
            }
        }
    }
    // Closing keeps the draft: nothing typed is ever lost to a stray Ctrl+W.
    onClosing: {
        if (!composerWindow.finished && composerWindow.service !== null) composerWindow.service.saveDraft(composerWindow.merged(composer.snapshot()))
        Qt.callLater(composerWindow.destroy)
    }
}
