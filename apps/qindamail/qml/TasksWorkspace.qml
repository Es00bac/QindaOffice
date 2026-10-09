// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// The Tasks workspace: an "add a task" field over Tk.TaskBoard (to do ·
// doing · done). The board proposes moves; the service applies them to the
// store (and to CalDAV for account tasks) and acknowledges them. M81
// (D-090): flagged mail is on the board as in Outlook's To-Do (Done marks
// the flag complete), a card's Edit opens its task or follow-up, and the
// board can show one category only.
Tk.Box {
    id: workspace

    property MailService service: null
    property string category: ""
    readonly property var categoryChoices: [qsTr("All categories")].concat(
        service !== null && service.organizer.revision >= 0 ? service.organizer.categoriesFor("local").map(function(c) { return String(c.name) }) : [])
    readonly property var columns: {
        const all = service !== null ? service.taskColumns : []
        if (category.length === 0) return all
        return all.map(function(column) {
            return Object.assign({}, column, { tasks: column.tasks.filter(function(task) {
                return (task.categories || []).some(function(n) { return String(n).toLowerCase() === workspace.category.toLowerCase() })
            }) })
        })
    }

    signal openMessageRequested(string messageId)

    color: Tk.Theme.color.surface
    padding: Tk.Theme.space.lg

    function focusNewTask() { newTask.forceActiveFocus() }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
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
            Tk.ComboBox {
                objectName: "taskCategoryFilter"
                small: true
                model: workspace.categoryChoices
                tooltip: qsTr("Show the tasks of one category")
                onActivated: function(index) { workspace.category = index === 0 ? "" : String(workspace.categoryChoices[index]) }
            }
        }
        Tk.TaskBoard {
            id: board
            objectName: "mailTaskBoard"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: workspace.columns
            onMoveRequested: function(requestId, taskId, fromStatus, toStatus, targetIndex) {
                workspace.service.moveTask(requestId, taskId, toStatus, targetIndex)
            }
            onEditRequested: function(requestId, taskId) {
                board.acknowledge(requestId, "accepted", "")
                taskDialog.openFor(taskId)
            }
        }
    }
    TaskDialog {
        id: taskDialog
        service: workspace.service
        onOpenMessageRequested: function(messageId) { workspace.openMessageRequested(messageId) }
    }
    Connections {
        target: workspace.service
        function onOperationFinished(requestId, state, message) { board.acknowledge(requestId, state, message) }
    }
}
