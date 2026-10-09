// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// A task card's Edit (M81, D-090): a task's title, due date, priority,
// status, categories and notes — or, for flagged mail on the board
// (Outlook's To-Do), the message's follow-up dates and categories with
// Open Message and Mark Complete.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property string itemId: ""
    property bool mail: false
    property var item: ({})
    readonly property var priorities: [{ text: qsTr("Low"), value: "Low" }, { text: qsTr("Normal"), value: "Normal" },
                                       { text: qsTr("High"), value: "High" }]
    readonly property var statuses: [{ text: qsTr("To do"), value: "todo" }, { text: qsTr("In progress"), value: "doing" },
                                     { text: qsTr("Done"), value: "done" }]

    signal openMessageRequested(string messageId)

    objectName: "taskDialog"
    title: mail ? qsTr("Follow up") : qsTr("Task")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.25
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")
    tertiaryText: mail ? qsTr("Mark Complete") : ""
    primaryEnabled: mail || titleField.text.trim().length > 0

    function openFor(taskId) {
        mail = String(taskId).startsWith("mailflag:")
        itemId = mail ? String(taskId).slice(9) : String(taskId)
        if (mail) {
            item = service.messageById(itemId)
            const flag = service.organizer.followUpOf(itemId)
            startField.text = String(flag.start || "")
            dueField.text = String(flag.due || "")
            categories.accountId = String(item.accountId || "local")
            categories.names = item.categories || []
        } else {
            item = (service.store.tasks.find(function(t) { return String(t.id) === dialog.itemId })) || ({})
            titleField.text = String(item.title || "")
            dueField.text = String(item.dueDate || "").slice(0, 10)
            priorityBox.currentIndex = Math.max(0, priorities.map(function(p) { return p.value }).indexOf(String(item.priority || "Normal")))
            statusBox.currentIndex = Math.max(0, statuses.map(function(s) { return s.value }).indexOf(String(item.status || "todo")))
            notesField.text = String(item.description || "")
            categories.accountId = "local"
            categories.names = item.categories || []
        }
        open()
    }
    onAccepted: {
        if (mail) {
            if (dueField.text.length > 0) service.organizer.followUpCustom([itemId], startField.text, dueField.text)
            service.organizer.setCategories("message", [itemId], categories.names)
            return
        }
        service.updateTask({ taskId: itemId, title: titleField.text.trim(), dueDate: dueField.text,
                             priority: priorities[priorityBox.currentIndex].value, status: statuses[statusBox.currentIndex].value,
                             description: notesField.text })
        service.organizer.setCategories("task", [itemId], categories.names)
    }
    onTertiary: service.organizer.followUp([itemId], "complete")

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            visible: dialog.mail
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Label { objectName: "taskMailSubject"; text: String(dialog.item.subject || qsTr("(no subject)")); font.weight: Font.DemiBold; elide: Text.ElideRight }
            Tk.Caption { text: qsTr("From %1").arg(String(dialog.item.sender || "")) }
            Tk.Button {
                objectName: "taskOpenMessage"
                small: true
                variant: "outline"
                iconName: "mail-open"
                text: qsTr("Open Message")
                tooltip: qsTr("Show the message in Mail")
                onClicked: { dialog.close(); dialog.openMessageRequested(dialog.itemId) }
            }
        }
        Tk.PropertyRow { visible: !dialog.mail; label: qsTr("Title"); Tk.TextField { id: titleField; objectName: "taskTitle" } }
        Tk.PropertyRow { visible: dialog.mail; label: qsTr("Start date"); MailDateField { id: startField; objectName: "taskStart" } }
        Tk.PropertyRow { label: qsTr("Due date"); MailDateField { id: dueField; objectName: "taskDue" } }
        Tk.PropertyRow {
            visible: !dialog.mail
            label: qsTr("Priority")
            Tk.ComboBox { id: priorityBox; objectName: "taskPriority"; small: true; textRole: "text"; model: dialog.priorities }
        }
        Tk.PropertyRow {
            visible: !dialog.mail
            label: qsTr("Status")
            Tk.ComboBox { id: statusBox; objectName: "taskStatus"; small: true; textRole: "text"; model: dialog.statuses }
        }
        Tk.PropertyRow {
            label: qsTr("Categories")
            CategoryField { id: categories; objectName: "taskCategories"; service: dialog.service }
        }
        Tk.TextArea { id: notesField; objectName: "taskNotes"; visible: !dialog.mail; rows: 4; placeholderText: qsTr("Notes") }
    }
}
