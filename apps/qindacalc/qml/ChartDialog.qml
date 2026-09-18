// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Chart creation: kind, title, data range, header toggles, a live preview
// (QtCharts, when the suite was built with it), then Insert. The chart is
// anchored on the cursor cell as a floating object.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property alias rangeText: range.text

    objectName: "chartDialog"
    title: qsTr("Insert chart")
    dialogWidth: 640
    primaryText: qsTr("Insert")
    primaryEnabled: range.text.trim().length > 0
    onAccepted: dialog.host.insertChartSpec(dialog.spec())

    function spec() {
        return { kind: kind.currentIndex, title: chartTitle.text, rangeText: range.text, headerH: headerH.checked, headerV: headerV.checked }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow { label: qsTr("Kind"); Tk.ComboBox { id: kind; objectName: "chartKind"; model: [qsTr("Bar"), qsTr("Stacked bar"), qsTr("Line"), qsTr("Area"), qsTr("Pie"), qsTr("Scatter")] } }
        Tk.PropertyRow { label: qsTr("Title"); Tk.TextField { id: chartTitle; objectName: "chartTitle" } }
        Tk.PropertyRow { label: qsTr("Data range"); Tk.TextField { id: range; objectName: "chartRange"; mono: true } }
        Tk.PropertyRow {
            label: qsTr("Headers")
            Tk.Flex {
                direction: Tk.Flex.Row; gap: Tk.Theme.space.md
                Tk.CheckBox { id: headerH; objectName: "chartHeaderH"; text: qsTr("First row names the series") }
                Tk.CheckBox { id: headerV; objectName: "chartHeaderV"; text: qsTr("First column names the categories") }
            }
        }
        Loader {
            objectName: "chartPreview"
            active: dialog.host.chartsAvailable && dialog.visible
            width: parent.width
            height: 260
            source: dialog.host.chartsAvailable ? "ChartCanvas.qml" : ""
            onLoaded: item.data = Qt.binding(function() { return dialog.host.chartSeries(dialog.spec()) })
        }
    }
}
