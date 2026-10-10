// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Arrange ▸ Auto-Layout ▸ Layout Options (M87, D-095; Visio's Re-Layout
// Page dialog): a style — flowcharts in four directions, org chart, compact
// tree, radial, circular — and the spacing between ranks and between
// neighbours, in inches or centimetres. The selection is arranged, or the
// whole page with nothing selected; one undo step.
Tk.Dialog {
    id: dialog
    objectName: "diagramLayoutDialog"

    required property DiagramService diagram
    readonly property bool metric: Qt.locale().measurementSystem === Locale.MetricSystem
    readonly property real unitPixels: metric ? 96 / 2.54 : 96
    readonly property var kinds: ["topDown", "bottomUp", "leftRight", "rightLeft", "tree", "compactTree", "radial", "circular"]
    readonly property var labels: [qsTr("Flowchart, top to bottom"), qsTr("Flowchart, bottom to top"),
                                   qsTr("Flowchart, left to right"), qsTr("Flowchart, right to left"),
                                   qsTr("Org chart (tree)"), qsTr("Compact tree"), qsTr("Radial"), qsTr("Circular")]
    property int kindIndex: 0

    title: qsTr("Layout options")
    subtitle: diagram.selection.length > 0 ? qsTr("Arranges the selected shapes") : qsTr("Arranges the whole page")
    primaryText: qsTr("Arrange")

    function openFor() { open() }
    onAccepted: diagram.relayout({ kind: kinds[kindIndex], rankGap: rankGap.value * unitPixels,
                                   nodeGap: nodeGap.value * unitPixels })

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Style")
            Tk.ComboBox {
                objectName: "layoutStyle"
                model: dialog.labels
                currentIndex: dialog.kindIndex
                tooltip: qsTr("How the shapes are arranged")
                onActivated: function(index) { dialog.kindIndex = index }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Between ranks")
            Tk.NumberField {
                id: rankGap
                objectName: "layoutRankGap"
                // AGENT-NOTE: constant bounds (good for inches and cm): bound
                // `from` and `to` with decimals make Tk.NumberField report a
                // binding loop on clampedValue (a QindaTK gap, D-095).
                from: 0.05
                to: 25
                stepSize: dialog.metric ? 0.25 : 0.125
                decimals: 2
                value: 56 / dialog.unitPixels
                suffix: dialog.metric ? " cm" : " in"
                tooltip: qsTr("Space between levels (rings, for radial)")
                onValueModified: function(v) { value = v }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Between shapes")
            Tk.NumberField {
                id: nodeGap
                objectName: "layoutNodeGap"
                from: 0.05
                to: 25
                stepSize: dialog.metric ? 0.25 : 0.125
                decimals: 2
                value: 36 / dialog.unitPixels
                suffix: dialog.metric ? " cm" : " in"
                tooltip: qsTr("Space between neighbours on one level")
                onValueModified: function(v) { value = v }
            }
        }
        Tk.Caption {
            text: qsTr("Radial and circular layouts draw their connectors straight, as Visio does.")
            wrapMode: Text.WordWrap
        }
    }
}
