// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Design ▸ Page Setup (D-050): paper, orientation or an exact size for the
// current page. Sizes are typed in inches, or millimetres where the locale
// is metric; the page stores CSS pixels (1/96 in). One undo step on OK.
Tk.Dialog {
    id: dialog
    objectName: "diagramPageSetup"

    required property DiagramService diagram
    readonly property bool metric: Qt.locale().measurementSystem === Locale.MetricSystem
    readonly property real unitPixels: metric ? 96 / 25.4 : 96
    readonly property var papers: diagram.paperSizes
    property int paperIndex: 0
    property bool landscape: false

    title: qsTr("Page setup")
    subtitle: qsTr("Applies to the current page")
    primaryText: qsTr("Apply")

    function openFor() {
        const page = diagram.page
        paperIndex = papers.length
        for (let i = 0; i < papers.length; ++i) if (papers[i].id === page.paper) paperIndex = i
        landscape = page.landscape
        widthField.value = page.width / unitPixels
        heightField.value = page.height / unitPixels
        open()
    }
    onAccepted: {
        if (paperIndex < papers.length) {
            diagram.setPaper(papers[paperIndex].id)
            diagram.setLandscape(landscape)
        } else {
            diagram.setPageSize(widthField.value * unitPixels, heightField.value * unitPixels)
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Paper")
            Tk.ComboBox {
                objectName: "pageSetupPaper"
                model: dialog.papers.map(function(p) { return p.label }).concat([qsTr("Custom size")])
                currentIndex: dialog.paperIndex
                tooltip: qsTr("Paper size")
                onActivated: function(index) { dialog.paperIndex = index }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Orientation")
            Tk.Segmented {
                objectName: "pageSetupOrientation"
                stretch: true
                enabled: dialog.paperIndex < dialog.papers.length
                model: [qsTr("Portrait"), qsTr("Landscape")]
                currentIndex: dialog.landscape ? 1 : 0
                tooltip: qsTr("Page orientation")
                onActivated: function(index) { dialog.landscape = index === 1 }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Width")
            Tk.NumberField {
                id: widthField
                objectName: "pageSetupWidth"
                enabled: dialog.paperIndex >= dialog.papers.length
                from: dialog.metric ? 13 : 0.5
                to: dialog.metric ? 5000 : 200
                stepSize: dialog.metric ? 5 : 0.25
                decimals: dialog.metric ? 0 : 2
                suffix: dialog.metric ? " mm" : " in"
            }
        }
        Tk.PropertyRow {
            label: qsTr("Height")
            Tk.NumberField {
                id: heightField
                objectName: "pageSetupHeight"
                enabled: dialog.paperIndex >= dialog.papers.length
                from: dialog.metric ? 13 : 0.5
                to: dialog.metric ? 5000 : 200
                stepSize: dialog.metric ? 5 : 0.25
                decimals: dialog.metric ? 0 : 2
                suffix: dialog.metric ? " mm" : " in"
            }
        }
        Tk.Caption {
            text: qsTr("Printing fits each page on one sheet; Fit Page to Drawing sizes the page to what is on it.")
            wrapMode: Text.WordWrap
        }
    }
}
