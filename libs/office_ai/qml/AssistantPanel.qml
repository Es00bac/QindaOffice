// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Ai

// The assistant panel (conversion plan §3.8) over an AssistantController:
// model picker with the off-machine badge, context scope, the conversation
// as markdown, reply actions, and the prompt line with Send / Stop.
// Privacy contract: nothing is sent until Send (or Enter in the field).
// D-058: where the app gave the assistant tools, "Edit the document" lets
// it change the document itself (one undo step, Undo right here); the
// document then always goes with the request, so the scope picker hides.
Tk.Flex {
    id: panel

    required property AssistantController controller

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm

    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Center
        Tk.ComboBox {
            id: modelPicker
            objectName: "assistantModel"
            Tk.Flex.grow: 1
            model: panel.controller.models.map(function(m) { return m.name })
            currentIndex: {
                const models = panel.controller.models
                for (let i = 0; i < models.length; ++i) if (models[i].id === panel.controller.modelId) return i
                return -1
            }
            placeholderText: qsTr("No models configured")
            onActivated: function(index) { panel.controller.modelId = panel.controller.models[index].id }
        }
        Tk.Badge {
            objectName: "assistantOffMachine"
            visible: panel.controller.offMachine
            text: qsTr("Leaves this machine")
            variant: "warning"
        }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Center
        visible: panel.controller.canEdit
        Tk.Switch {
            objectName: "assistantEditMode"
            Tk.Flex.grow: 1
            small: true
            text: qsTr("Edit the document")
            tooltip: qsTr("On: the assistant sees the whole document and makes the changes you ask for (one undo step). Off: it only answers, and Insert pastes its reply.")
            checked: panel.controller.editMode
            onToggled: panel.controller.editMode = checked
        }
        Tk.Button {
            objectName: "assistantUndoEdit"
            small: true
            iconName: "undo-2"
            text: qsTr("Undo change")
            visible: panel.controller.lastEditSummary.length > 0
            tooltip: panel.controller.lastEditSummary
            onClicked: panel.controller.undoLastEdit()
        }
    }
    Tk.Segmented {
        objectName: "assistantScope"
        visible: !panel.controller.editMode
        stretch: true
        small: true
        model: [qsTr("No context"), qsTr("Selection"), panel.controller.documentScopeLabel]
        currentIndex: panel.controller.scope
        onActivated: function(index) { panel.controller.scope = index }
    }
    Tk.Scroll {
        id: transcript
        objectName: "assistantTranscript"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 0
        overflowX: Tk.Scroll.Hidden
        Tk.Label {
            objectName: "assistantConversation"
            width: transcript.width - Tk.Theme.space.sm
            text: panel.controller.conversation
            textFormat: Text.MarkdownText
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            selectable: true
            onTextChanged: Qt.callLater(transcript.scrollToBottom)
        }
        Tk.EmptyState {
            anchors.centerIn: parent
            width: transcript.width - Tk.Theme.space.md
            visible: panel.controller.conversation.length === 0
            iconName: "sparkles"
            title: qsTr("Assistant")
            text: panel.controller.editMode
                  ? qsTr("Ask about the document or tell the assistant what to change — “condense this to key points”, “fix the grammar”, “add a summary”. Changes are one undo step. Nothing is sent until you press Send.")
                  : qsTr("Ask a question, or pick a context and ask about the selection or the document. Nothing is sent until you press Send.")
        }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        Tk.Button { objectName: "assistantInsert"; text: qsTr("Insert"); small: true; available: panel.controller.hasReply; onClicked: panel.controller.insert() }
        Tk.Button { objectName: "assistantReplace"; text: qsTr("Replace selection"); small: true; available: panel.controller.hasReply; onClicked: panel.controller.replaceSelection() }
        Tk.Button { objectName: "assistantCopy"; text: qsTr("Copy"); small: true; available: panel.controller.hasReply; onClicked: panel.controller.copy() }
        Tk.Spacer { }
        Tk.IconButton { objectName: "assistantClear"; iconName: "trash-2"; tooltip: qsTr("Clear conversation"); enabled: panel.controller.conversation.length > 0 && !panel.controller.streaming; onClicked: panel.controller.clearConversation() }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        align: Tk.Flex.Center
        Tk.TextField {
            id: prompt
            objectName: "assistantPrompt"
            Tk.Flex.grow: 1
            placeholderText: panel.controller.editMode ? qsTr("Ask, or tell it what to change…") : qsTr("Ask the assistant…")
            enabled: !panel.controller.streaming
            onAccepted: send.clicked()
        }
        Tk.Button {
            id: send
            objectName: "assistantSend"
            text: qsTr("Send")
            variant: "accent"
            available: prompt.text.trim().length > 0 && !panel.controller.streaming
            onClicked: {
                const text = prompt.text.trim()
                if (text.length > 0) {
                    panel.controller.send(text)
                    prompt.text = ""
                }
            }
        }
        Tk.IconButton {
            objectName: "assistantStop"
            iconName: "square"
            tooltip: qsTr("Stop")
            enabled: panel.controller.streaming
            onClicked: panel.controller.stop()
        }
    }
}
