// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// The Tasks workspace: an "add a task" field over Tk.TaskBoard (to do ·
// doing · done). The board proposes moves; the service applies them to the
// store (and to CalDAV for account tasks) and acknowledges them.
Tk.Box {
    id: workspace

    property MailService service: null

    color: Tk.Theme.color.surface
    padding: Tk.Theme.space.lg

    function focusNewTask() { newTask.forceActiveFocus() }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Heading { level: 2; text: qsTr("Tasks") }
            Tk.TextField {
                id: newTask
                objectName: "taskNewField"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                iconName: "plus"
                placeholderText: qsTr("Add a task and press Enter")
                onAccepted: {
                    if (text.trim().length === 0 || workspace.service === null) return
                    workspace.service.addTask(text.trim())
                    text = ""
                }
            }
        }
        Tk.TaskBoard {
            id: board
            objectName: "mailTaskBoard"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: workspace.service !== null ? workspace.service.taskColumns : []
            onMoveRequested: function(requestId, taskId, fromStatus, toStatus, targetIndex) {
                workspace.service.moveTask(requestId, taskId, toStatus, targetIndex)
            }
        }
    }
    Connections {
        target: workspace.service
        function onOperationFinished(requestId, state, message) { board.acknowledge(requestId, state, message) }
    }
}
