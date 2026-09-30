// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note

// Every follow-up is a draft first. Persistent commands also serve the
// native menu and command search; generating never changes the notebook.
Tk.Flex {
    id: panel
    required property NoteHost note
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs
    Tk.Overline { title: qsTr("Follow-up drafts") }
    Tk.ComboBox {
        id: kind
        objectName: "sessionDraftKind"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        model: panel.note.live.followUpKinds.map(e => e.name)
        tooltip: qsTr("What to prepare from this page's notes and transcript")
    }
    Connections {
        target: panel.note.live
        function onDraftChanged() {
            const index = panel.note.live.followUpKinds.findIndex(e => e.id === panel.note.live.draftKind)
            if (index >= 0) kind.currentIndex = index
        }
    }
    Tk.Button {
        objectName: "sessionMakeDraft"
        small: true
        iconName: "sparkles"
        text: panel.note.live.working ? qsTr("Preparing…") : qsTr("Prepare draft")
        available: !panel.note.live.working && !panel.note.live.active
        onClicked: panel.note.live.makeDraft(panel.note.live.followUpKinds[kind.currentIndex].id)
    }
    Tk.Label {
        objectName: "sessionDraftStatus"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        wrapMode: Text.Wrap
        muted: panel.note.live.errorText.length === 0
        text: panel.note.live.errorText.length > 0 ? panel.note.live.errorText
            : panel.note.live.working ? panel.note.live.statusText
            : panel.note.live.draftText.length > 0 ? qsTr("Review this draft. Your source notes stay on their page.")
            : panel.note.live.statusText.length > 0 ? panel.note.live.statusText
            : qsTr("Create a recap, next steps, a troubleshooting brief, questions, or study material.")
    }
    Tk.Label {
        visible: panel.note.live.draftText.length > 0
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        wrapMode: Text.Wrap
        text: panel.note.live.draftTitle
    }
    Tk.TextArea {
        objectName: "sessionDraftPreview"
        visible: panel.note.live.draftText.length > 0
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        rows: 8
        readOnly: true
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.MarkdownText
        text: panel.note.live.draftText
        tooltip: qsTr("AI draft for review; select text to copy it")
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.xs
        visible: panel.note.live.draftText.length > 0 || panel.note.live.working
        Tk.Button {
            objectName: "sessionAcceptDraft"
            small: true
            iconName: panel.note.live.draftKind === "tasks" ? "list-todo" : "file-plus"
            text: panel.note.live.draftKind === "tasks" ? qsTr("Send reviewed tasks") : qsTr("Add as new page")
            available: !panel.note.live.working && panel.note.live.draftText.length > 0
            onClicked: panel.note.actions.trigger("noteAcceptDraftAction")
        }
        Tk.Button {
            objectName: "sessionDiscardDraft"
            small: true
            iconName: "x"
            text: qsTr("Discard")
            onClicked: panel.note.actions.trigger("noteDiscardDraftAction")
        }
    }
}
