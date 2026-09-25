// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Mail.App

// The Calendar workspace (D-042): a side pane (month picker, the calendars
// with their colours and visibility) beside Tk.CalendarGridView. The grid
// never mutates events: its create/open requests become the event dialog,
// its move/resize proposals go to MailService.applyChange and come back
// acknowledged; the service re-projects the visible range.
Item {
    id: workspace

    property MailService service: null
    property MailHost host: null
    // The calendar new events and imports go to: the selected one, else the first writable.
    property string targetCalendarId: service !== null && service.writableCalendars.length > 0
                                      ? String(service.writableCalendars[0].id) : "local"
    readonly property string mode: host !== null ? host.calendarMode : "week"
    signal eventOpenRequested(string eventId, string occurrenceId)
    signal eventCreateRequested(string startLocal, string endLocal, bool allDay)

    function nowLocal() { return Qt.formatDateTime(new Date(), "yyyy-MM-ddTHH:mm:ss") }
    function today() { return Qt.formatDate(new Date(), "yyyy-MM-dd") }

    Tk.LocaleService { id: localeService }
    Timer { id: clock; interval: 30000; running: workspace.visible; repeat: true; triggeredOnStart: true; onTriggered: grid.now = workspace.nowLocal() }

    Tk.Splitter {
        anchors.fill: parent
        orientation: Qt.Horizontal
        tooltip: qsTr("Resize the calendar panes")
        Tk.Box {
            objectName: "calendarSidePane"
            T.SplitView.preferredWidth: Tk.Theme.size.panelMinWidth * 1.25
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
            color: Tk.Theme.color.panel
            padding: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Button {
                    objectName: "calendarNewEvent"
                    variant: "accent"
                    large: true
                    iconName: "calendar-plus"
                    text: qsTr("New event")
                    onClicked: workspace.eventCreateRequested("", "", false)
                }
                Tk.CalendarPicker {
                    objectName: "calendarMiniMonth"
                    localeService: localeService
                    selectedDate: grid.anchorDate
                    tooltip: qsTr("Go to a date")
                    onDateSelected: function(iso) { grid.anchorDate = iso }
                }
                Tk.SectionHeader { title: qsTr("Calendars") }
                Repeater {
                    model: workspace.service !== null ? workspace.service.calendars : []
                    delegate: Tk.Flex {
                        id: calendarRow
                        required property var modelData
                        objectName: "calendarToggle_" + String(modelData.id)
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        Rectangle {
                            width: Tk.Theme.size.iconSm; height: width; radius: Tk.Theme.radius.xs
                            color: String(calendarRow.modelData.color || "").length > 0 ? calendarRow.modelData.color : Tk.Theme.color.accent
                            opacity: calendarRow.modelData.visible === false ? Tk.Theme.opacity.ghost : Tk.Theme.opacity.full
                        }
                        Tk.CheckBox {
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            text: String(calendarRow.modelData.name || calendarRow.modelData.id)
                            checked: calendarRow.modelData.visible !== false
                            tooltip: calendarRow.modelData.readOnly ? qsTr("Read-only calendar") : ""
                            onToggled: workspace.service.setCalendarVisible(String(calendarRow.modelData.id), checked)
                        }
                        Tk.Icon { visible: calendarRow.modelData.readOnly === true; name: "lock"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
                    }
                }
                Tk.Button {
                    objectName: "calendarSubscribe"
                    variant: "ghost"
                    iconName: "calendar-plus"
                    text: qsTr("Add a calendar…")
                    onClicked: workspace.host.actions.trigger("calSubscribeAction")
                }
            }
        }
        Tk.Box {
            T.SplitView.fillWidth: true
            color: Tk.Theme.color.surface
            padding: 0
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Column
                gap: 0
                Tk.Flex {
                    objectName: "calendarHeader"
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    padding: Tk.Theme.space.md
                    Tk.Button { objectName: "calendarToday"; variant: "outline"; text: qsTr("Today"); onClicked: workspace.host.actions.trigger("calTodayAction") }
                    Tk.IconButton { objectName: "calendarPrevious"; iconName: "chevron-left"; tooltip: qsTr("Previous"); onClicked: workspace.host.actions.trigger("calPreviousAction") }
                    Tk.IconButton { objectName: "calendarNext"; iconName: "chevron-right"; tooltip: qsTr("Next"); onClicked: workspace.host.actions.trigger("calNextAction") }
                    Tk.Heading { objectName: "calendarTitle"; level: 3; text: grid.title; Tk.Flex.grow: 1; Tk.Flex.basis: 0; elide: Text.ElideRight }
                    Tk.Segmented {
                        objectName: "calendarMode"
                        small: true
                        model: [{ text: qsTr("Day"), value: "calDayAction" }, { text: qsTr("Work week"), value: "calWorkWeekAction" },
                                { text: qsTr("Week"), value: "calWeekAction" }, { text: qsTr("Month"), value: "calMonthAction" }]
                        currentIndex: ["day", "workweek", "week", "month"].indexOf(workspace.mode)
                        tooltip: qsTr("Calendar view")
                        onActivated: function(index) { workspace.host.actions.trigger(model[index].value) }
                    }
                }
                Tk.Divider { }
                Tk.CalendarGridView {
                    id: grid
                    objectName: "mailCalendarGrid"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    mode: workspace.mode
                    anchorDate: workspace.today()
                    events: workspace.service !== null ? workspace.service.events : []
                    calendars: workspace.service !== null ? workspace.service.calendars : []
                    offline: workspace.service !== null && !workspace.service.online
                    onRangeChanged: function(start, end) { if (workspace.service !== null) workspace.service.projectRange(start, end) }
                    onOpenRequested: function(eventId, occurrenceId) { workspace.eventOpenRequested(eventId, occurrenceId) }
                    onCreateRequested: function(startLocal, endLocal, allDay) { workspace.eventCreateRequested(startLocal, endLocal, allDay) }
                    onEventChangeRequested: function(requestId, eventId, proposal) { workspace.service.applyChange(requestId, eventId, proposal) }
                    onDeleteRequested: function(eventId, occurrenceId) { workspace.service.deleteEvent(eventId, occurrenceId, grid.moveScope) }
                    onMoreRequested: function(dateIso) { grid.anchorDate = dateIso; workspace.host.actions.trigger("calDayAction") }
                    Component.onCompleted: if (workspace.service !== null) workspace.service.projectRange(rangeStart, rangeEnd)
                }
            }
        }
    }
    Connections {
        target: workspace.service
        function onEventChangeFinished(requestId, state, message) { grid.acknowledge(requestId, state, message) }
    }
    Connections {
        target: workspace.host !== null ? workspace.host.actions.entry("calTodayAction") : null
        function onTriggered() { grid.goToday(workspace.today()) }
    }
    Connections {
        target: workspace.host !== null ? workspace.host.actions.entry("calPreviousAction") : null
        function onTriggered() { grid.navigate(-1) }
    }
    Connections {
        target: workspace.host !== null ? workspace.host.actions.entry("calNextAction") : null
        function onTriggered() { grid.navigate(1) }
    }
}
