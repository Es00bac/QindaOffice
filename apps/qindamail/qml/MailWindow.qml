// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Mail
import QindaOffice.Mail.App

// The QindaMail main window (D-042): the shared OfficeWindow over a
// MailHost (no document), a navigation rail choosing the workspace — Mail
// (folders · messages · reading pane), Calendar (day/week/month grid) or
// Tasks (a board) — the ribbon for that workspace, and the windows and
// dialogs the host asks for: composers (one draft per ordinary top-level
// window), the account dialog, the event dialog, the calendar files.
//
// AGENT-NOTE: the service is per PROCESS (every main window shows the same
// store); everything per window — the workspace, selections, the calendar
// range — lives here or in the workspaces.
OfficeWindow {
    id: window

    readonly property MailHost mail: host as MailHost
    readonly property MailService service: mail !== null ? mail.service : null
    readonly property string view: mail !== null ? mail.view : "mail"

    width: 1280
    height: 820

    function trigger(id) { window.host.actions.trigger(id) }

    readonly property MailRibbonLayout ribbonLayout: MailRibbonLayout { view: window.view }
    ribbonTabs: ribbonLayout.tabs
    // A workspace switch lands on that workspace's Home tab.
    onViewChanged: Qt.callLater(function() { window.ribbon.activateTab("home") })

    // ---- content: rail | workspace ----
    // AGENT-GUARD: the window seats ONE Item and fills it; a Flex seated
    // directly keeps its own implicit size (QindaTK rule 5), so the row
    // anchors inside a plain Item.
    Item {
      objectName: "mailContent"
      // Inside the content item: a Toast is an Item, and a second window
      // child would stop the window from seating the content.
      Tk.Toast {
          id: toast
          objectName: "mailToast"
          z: 10
          anchors.right: parent.right
          anchors.bottom: parent.bottom
          anchors.margins: Tk.Theme.space.lg
      }
      Tk.Flex {
        objectName: "mailRoot"
        anchors.fill: parent
        direction: Tk.Flex.Row
        gap: 0
        Tk.NavigationRail {
            id: rail
            objectName: "mailRail"
            Tk.Flex.shrink: 0
            expanded: false
            currentId: window.view
            tooltip: qsTr("Workspaces")
            groups: [{ id: "workspaces", label: qsTr("Workspaces"), entries: [
                { id: "mail", label: qsTr("Mail"), destination: "mailViewMailAction", iconName: "mail",
                  badge: window.service !== null && window.service.unreadTotal > 0 ? String(window.service.unreadTotal) : "" },
                { id: "calendar", label: qsTr("Calendar"), destination: "mailViewCalendarAction", iconName: "calendar-days" },
                { id: "tasks", label: qsTr("Tasks"), destination: "mailViewTasksAction", iconName: "list-checks" }] }]
            onDestinationRequested: function(id, destination) { window.trigger(destination) }
        }
        Tk.Divider { vertical: true }
        MailWorkspace {
            id: mailWorkspace
            visible: window.view === "mail"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            service: window.service
            host: window.mail
        }
        CalendarWorkspace {
            id: calendarWorkspace
            visible: window.view === "calendar"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            service: window.service
            host: window.mail
            onEventOpenRequested: function(eventId, occurrenceId) { eventDialog.openExisting(eventId, occurrenceId) }
            onEventCreateRequested: function(startLocal, endLocal, allDay) { eventDialog.openNew(startLocal, endLocal, allDay) }
        }
        TasksWorkspace {
            id: tasksWorkspace
            visible: window.view === "tasks"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            service: window.service
        }
      }
    }

    // ---- windows and dialogs the host asks for ----
    Component { id: composerComponent; ComposerWindow { } }
    property var composers: []
    function openComposer(draft) {
        const composer = composerComponent.createObject(null, { service: window.service, draft: draft })
        if (composer === null) return
        composer.closing.connect(function() { window.composers = window.composers.filter(function(c) { return c !== composer }) })
        window.composers = window.composers.concat([composer])
        composer.show()
        composer.raise()
        composer.requestActivate()
    }
    AccountDialog { id: accountDialog; service: window.service }
    EventDialog { id: eventDialog; service: window.service }

    Dialogs.FileDialog {
        id: icsOpenDialog
        title: qsTr("Import Calendar")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("iCalendar (*.ics *.ical *.ifb)"), qsTr("All files (*)")]
        onAccepted: window.service.importIcs(selectedFile, calendarWorkspace.targetCalendarId)
    }
    Dialogs.FileDialog {
        id: icsSaveDialog
        title: qsTr("Export Calendar")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [qsTr("iCalendar (*.ics)")]
        onAccepted: window.service.exportIcs(selectedFile, calendarWorkspace.targetCalendarId)
    }
    Tk.PromptDialog {
        id: subscribeDialog
        objectName: "subscribeDialog"
        title: qsTr("Subscribe to a calendar")
        label: qsTr("Calendar address (https:// or webcal://) — it stays read-only and refreshes on every sync")
        placeholderText: "webcal://example.com/calendar.ics"
        primaryText: qsTr("Subscribe")
        onAccepted: window.service.subscribeFeed(text, "", "")
    }

    Connections {
        target: window.mail
        function onComposeRequested(draft) { window.openComposer(draft) }
        function onAccountDialogRequested(accountId) { accountDialog.openFor(accountId) }
        function onEventDialogRequested() { eventDialog.openNew("", "", false) }
        function onCalendarImportRequested() { icsOpenDialog.open() }
        function onCalendarExportRequested() { icsSaveDialog.open() }
        function onCalendarSubscribeRequested() { subscribeDialog.text = ""; subscribeDialog.open() }
        function onSearchRequested() { if (window.view !== "mail") window.trigger("mailViewMailAction"); mailWorkspace.focusSearch() }
        function onMoveRequested() { mailWorkspace.openMoveMenu() }
        function onTaskNewRequested() { window.trigger("mailViewTasksAction"); tasksWorkspace.focusNewTask() }
    }
    Connections {
        target: window.service
        function onNotify(title, text, variant) { toast.title = title; toast.message = text; toast.variant = variant; toast.show() }
    }
    Component.onCompleted: window.mail.preferencesPageUrl = Qt.resolvedUrl("MailPreferencesPage.qml")
}
