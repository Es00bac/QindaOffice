// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaMail's ribbon (D-041, D-042): the Home tab follows the workspace —
// mail commands in Mail, calendar commands in Calendar, tasks in Tasks,
// contacts in People (M64; groups and CardDAV address books, M76) — while
// File (M76: an Import group), Accounts and View stay put. Every
// string is an ActionEntry id MailHost registers (app-scoped literals,
// AGENTS.md rule 5).
QtObject {
    property string view: "mail"

    readonly property var fileTab: {
        const tab = StandardRibbon.fileTab([
            { id: "messageFiles", label: qsTr("Messages"), items: [
                { action: "mailSaveMessageAction", size: "large" }, "mailSaveAttachmentsAction"] },
            { id: "importFiles", label: qsTr("Import"), items: [
                { action: "mailImportPstAction", size: "large" }, "mailImportMboxAction"] },
            { id: "contactFiles", label: qsTr("Contacts"), items: [
                { action: "contactImportAction", size: "large" }, "contactExportAction"] },
            { id: "calendarFiles", label: qsTr("Calendars"), items: [
                { action: "calImportAction", iconName: "file-input" },
                { action: "calExportAction", iconName: "file-output" },
                { action: "calSubscribeAction", iconName: "calendar-plus" }] }], [], [])
        // Mail has no document to save: its File tab creates, opens message
        // files, prints the open message and closes.
        tab.groups[0] = { id: "fileNew", label: qsTr("New"), items: [
            { action: "mailNewMessageAction", size: "large" },
            { action: "mailNewEventAction" }, { action: "taskNewAction" }, { action: "contactNewAction" },
            { action: "contactNewGroupAction" },
            { action: "fileOpenAction" }, { action: "fileNewAction", iconName: "monitor" }, { action: "fileCloseWindowAction" }] }
        tab.groups[1] = { id: "filePrint", label: qsTr("Print"), items: [
            { action: "filePrintAction", size: "large" }, { action: "filePrintPreviewAction", iconName: "eye" },
            "mailExportPdfAction"] }
        return tab
    }
    readonly property var mailHome: { "id": "home", "label": qsTr("Home"), "keyTip": "H", "groups": [
        { id: "new", label: qsTr("New"), items: [
            { action: "mailNewMessageAction", size: "large", split: true,
              menu: ["mailNewMessageAction", "mailNewEventAction", "taskNewAction"] }] },
        { id: "delete", label: qsTr("Delete"), items: [
            { action: "mailDeleteAction", size: "large" }, "mailArchiveAction"] },
        { id: "respond", label: qsTr("Respond"), items: [
            { action: "mailReplyAction", size: "large" }, "mailReplyAllAction", "mailForwardAction"] },
        { id: "tags", label: qsTr("Tags"), items: ["mailToggleReadAction", "mailToggleFlagAction"] },
        { id: "people", label: qsTr("People"), items: ["mailAddSenderAction", "mailShowPicturesAction"] },
        { id: "move", label: qsTr("Move"), items: [{ action: "mailMoveAction", size: "large" }] },
        { id: "find", label: qsTr("Find"), items: [
            { action: "mailSearchAction", size: "large" }, "mailSyncAction"] }
    ] }
    readonly property var calendarHome: { "id": "home", "label": qsTr("Home"), "keyTip": "H", "groups": [
        { id: "new", label: qsTr("New"), items: [
            { action: "mailNewEventAction", size: "large", split: true,
              menu: ["mailNewEventAction", "mailNewMessageAction", "taskNewAction"] }] },
        { id: "goTo", label: qsTr("Go to"), items: [
            { action: "calTodayAction", size: "large" }, "calPreviousAction", "calNextAction"] },
        { id: "arrange", label: qsTr("Arrange"), items: [
            { action: "calDayAction", size: "large" }, { action: "calWorkWeekAction", size: "large" },
            { action: "calWeekAction", size: "large" }, { action: "calMonthAction", size: "large" }] },
        { id: "manage", label: qsTr("Manage calendars"), items: [
            { action: "calSubscribeAction", size: "large" }, "calImportAction", "calExportAction"] },
        { id: "sync", label: qsTr("Sync"), items: ["mailSyncAction"] }
    ] }
    readonly property var tasksHome: { "id": "home", "label": qsTr("Home"), "keyTip": "H", "groups": [
        { id: "new", label: qsTr("New"), items: [{ action: "taskNewAction", size: "large" }] },
        { id: "sync", label: qsTr("Sync"), items: [{ action: "mailSyncAction", size: "large" }] }
    ] }
    readonly property var peopleHome: { "id": "home", "label": qsTr("Home"), "keyTip": "H", "groups": [
        { id: "new", label: qsTr("New"), items: [
            { action: "contactNewAction", size: "large", split: true,
              menu: ["contactNewAction", "contactNewGroupAction", "mailNewMessageAction", "mailNewEventAction"] },
            "contactNewGroupAction"] },
        { id: "manage", label: qsTr("Manage"), items: [{ action: "contactDeleteAction", size: "large" }] },
        { id: "share", label: qsTr("Import & export"), items: [
            { action: "contactImportAction", size: "large" }, "contactExportAction"] },
        { id: "addressBooks", label: qsTr("Address books"), items: ["contactAddressBookAction", "mailSyncAction"] }
    ] }
    readonly property var accountsTab: { "id": "accounts", "label": qsTr("Accounts"), "keyTip": "A", "groups": [
        { id: "accountManage", label: qsTr("Accounts"), items: [
            { action: "mailAddAccountAction", size: "large" }, { action: "mailAccountSettingsAction", size: "large" }] },
        { id: "accountSync", label: qsTr("Send & receive"), items: [{ action: "mailSyncAction", size: "large" }] }
    ] }
    readonly property var viewTab: StandardRibbon.viewTab([
        { id: "workspace", label: qsTr("Workspace"), items: [
            { action: "mailViewMailAction", size: "large" }, { action: "mailViewCalendarAction", size: "large" },
            { action: "mailViewTasksAction", size: "large" }, { action: "mailViewPeopleAction", size: "large" }] },
        { id: "layout", label: qsTr("Layout"), items: ["mailReadingPaneAction", "mailConversationsAction"] },
        StandardRibbon.assistantGroup([])
    ])

    readonly property var tabs: [fileTab, view === "calendar" ? calendarHome : view === "tasks" ? tasksHome
                                          : view === "people" ? peopleHome : mailHome,
                                 accountsTab, viewTab]
}
