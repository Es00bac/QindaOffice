// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// The Timeline strip above the Gantt: phases and milestones at a glance,
// painted by plan_output's timeline painter (the same picture File ▸ Export
// ▸ Timeline Image writes). It is paper, framed by the theme.
Item {
    id: strip

    property PlanService plan: null

    implicitHeight: Tk.Theme.size.row * 7.5

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.panelAlt
        Tk.Divider { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom }
    }
    PlanTimeline {
        objectName: "planTimelinePaper"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.sm
        service: strip.plan
        Accessible.role: Accessible.Graphic
        Accessible.name: qsTr("Timeline of the project's phases and milestones")
    }
}
