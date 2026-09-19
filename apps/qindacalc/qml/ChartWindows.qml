// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// One floating window per chart in the workbook, its contents re-read on
// every edit while the window keeps its identity and geometry (AGENT-GUARD:
// never destroyed and re-created on a data edit — the user's placement
// would be lost). Sized before show, never resized after (ADR-0117).
// AGENT-GUARD: a QtObject, not an Item — an Item here would be a second
// content child of the window and Tk.AppWindow seats only ONE.
QtObject {
    id: root

    required property CalcHost host
    property Window parentWindow: null

    property Instantiator windows: Instantiator {
        model: root.host.chartsAvailable ? root.host.charts : []
        delegate: Window {
            id: chartWindow
            required property var modelData
            required property int index
            objectName: "chartWindow_" + index
            title: modelData.title.length > 0 ? modelData.title : qsTr("Chart")
            width: modelData.widthPx
            height: modelData.heightPx
            flags: Qt.Tool
            transientParent: root.parentWindow
            color: Tk.Theme.color.panel
            visible: true
            ChartItem {
                id: chartItem
                anchors.fill: parent
                chartData: ({})
                inkColor: Tk.Theme.color.text
                Component.onCompleted: chartItem.chartData = root.host.chartSeriesAt(chartWindow.index)
                Connections {
                    target: root.host
                    function onChartsChanged() { chartItem.chartData = root.host.chartSeriesAt(chartWindow.index) }
                }
            }
        }
    }
}
