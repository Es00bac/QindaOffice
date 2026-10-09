// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// The Network Diagram (M79, D-088): a box per task in columns by dependency,
// links routed between them, critical ones red — plan_engine's layout of
// the view's rows painted by plan_output's painter (PlanNetwork), the same
// picture Print and Export PDF make. A click selects a task, a double click
// opens Task Information; View ▸ Zoom scales it. The canvas is paper.
Item {
    id: view

    property PlanService plan: null
    property PlanHost host: null
    readonly property real zoom: host !== null ? host.zoom : 1
    readonly property size layoutSize: plan !== null ? plan.networkSize : Qt.size(0, 0)

    signal openTask(string taskId)

    // Scrolls a task's box into view.
    function reveal(taskId) {
        if (plan === null || taskId === "") return
        const box = plan.networkBoxOf(taskId)
        if (box.width <= 0) return
        const left = box.x * zoom
        const top = box.y * zoom
        if (left < scroll.contentX || left + box.width * zoom > scroll.contentX + scroll.viewportWidth)
            scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.viewportWidth, left - Tk.Theme.space.xl))
        if (top < scroll.contentY || top + box.height * zoom > scroll.contentY + scroll.viewportHeight)
            scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.viewportHeight, top - Tk.Theme.space.xl))
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Flex {
            Tk.Flex.shrink: 0
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            padding: Tk.Theme.space.md
            Tk.Heading { level: 2; text: qsTr("Network diagram"); Tk.Flex.shrink: 0 }
            Tk.Caption {
                objectName: "planNetworkCaption"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                elide: Text.ElideRight
                text: view.plan === null ? "" : qsTr("%1 · %2 tasks · click a box to select it, double-click to open it")
                    .arg(view.plan.filterTitle).arg(view.plan.revision >= 0 ? view.plan.visibleRowCount() : 0)
            }
        }
        Tk.Divider { }
        Tk.Scroll {
            id: scroll
            objectName: "planNetworkScroll"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            overflowX: Tk.Scroll.Auto
            overflowY: Tk.Scroll.Auto
            accessibleName: qsTr("Network diagram")
            Item {
                id: canvas
                implicitWidth: view.layoutSize.width * view.zoom
                implicitHeight: view.layoutSize.height * view.zoom
                PlanNetwork {
                    id: network
                    x: scroll.contentX
                    y: scroll.contentY
                    width: scroll.viewportWidth
                    height: scroll.viewportHeight
                    originX: x
                    originY: y
                    zoom: view.zoom
                    service: view.plan
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Network diagram of the project's tasks and links")
                }
                TapHandler {
                    onTapped: function(eventPoint) {
                        const id = view.plan.networkTaskAt(eventPoint.position.x / view.zoom, eventPoint.position.y / view.zoom)
                        if (id !== "") view.plan.currentTaskId = id
                    }
                    onDoubleTapped: function(eventPoint) {
                        const id = view.plan.networkTaskAt(eventPoint.position.x / view.zoom, eventPoint.position.y / view.zoom)
                        if (id !== "") view.openTask(id)
                    }
                }
            }
        }
    }
}
