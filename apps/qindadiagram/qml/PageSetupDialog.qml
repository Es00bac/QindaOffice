// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Design ▸ Page Setup (D-050; M87 adds Visio's Print Setup and Page
// Properties): paper, orientation or an exact size for the current page;
// the printer paper it prints on and how — fitted to sheets across and
// down, or at a print zoom across as many sheets as it takes; whether it
// is a background page, which background page shows behind it, and Auto
// Size. Sizes are typed in inches, or millimetres where the locale is
// metric; the page stores CSS pixels (1/96 in). Applied on OK.
Tk.Dialog {
    id: dialog
    objectName: "diagramPageSetup"

    required property DiagramService diagram
    readonly property bool metric: Qt.locale().measurementSystem === Locale.MetricSystem
    readonly property real unitPixels: metric ? 96 / 25.4 : 96
    readonly property var papers: diagram.paperSizes
    readonly property var backgrounds: diagram.backgroundPages.filter(function(p) { return p.id !== diagram.currentPageId })
    property int paperIndex: 0
    property bool landscape: false
    // Print setup: printer paper (0 the printer's, 1 the page's size, 2… papers)
    property int printPaperIndex: 0
    property bool printLandscape: false
    property bool fitMode: true
    property bool isBackground: false
    property int backIndex: 0

    title: qsTr("Page setup")
    subtitle: qsTr("Applies to the current page")
    primaryText: qsTr("Apply")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.15

    function openFor() {
        const page = diagram.page
        paperIndex = papers.length
        for (let i = 0; i < papers.length; ++i) if (papers[i].id === page.paper) paperIndex = i
        landscape = page.landscape
        widthField.value = page.width / unitPixels
        heightField.value = page.height / unitPixels
        const print = page.print || {}
        printPaperIndex = 0
        for (let j = 0; j < papers.length; ++j) if (papers[j].id === print.paper) printPaperIndex = j + 2
        printLandscape = print.paper ? print.landscape : page.landscape
        fitMode = !(print.zoom > 0)
        zoomField.value = print.zoom > 0 ? print.zoom * 100 : 100
        acrossField.value = print.across || 1
        downField.value = print.down || 1
        centerSwitch.checked = print.center !== false
        isBackground = !!page.backgroundPage
        backIndex = 0
        for (let k = 0; k < backgrounds.length; ++k) if (backgrounds[k].id === page.backPageId) backIndex = k + 1
        autoSizeSwitch.checked = !!page.autoSize
        open()
    }
    onAccepted: {
        if (paperIndex < papers.length) {
            diagram.setPaper(papers[paperIndex].id)
            diagram.setLandscape(landscape)
        } else {
            diagram.setPageSize(widthField.value * unitPixels, heightField.value * unitPixels)
        }
        diagram.setPrintSetup({ paper: printPaperIndex === 0 ? "" : printPaperIndex === 1 ? "page" : papers[printPaperIndex - 2].id,
                                landscape: printLandscape, zoom: fitMode ? 0 : zoomField.value / 100,
                                across: acrossField.value, down: downField.value, center: centerSwitch.checked })
        diagram.setBackgroundPage(isBackground)
        if (!isBackground) diagram.setBackPage(backIndex > 0 ? backgrounds[backIndex - 1].id : "")
        diagram.setAutoSize(autoSizeSwitch.checked)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Overline { title: qsTr("PAGE SIZE") }
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
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.PropertyRow {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
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
                    onValueModified: function(v) { value = v } // no model: the field echoes (D-198)
                }
            }
            Tk.PropertyRow {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
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
                    onValueModified: function(v) { value = v } // no model: the field echoes (D-198)
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Auto Size")
            Tk.Switch {
                id: autoSizeSwitch
                objectName: "pageSetupAutoSize"
                tooltip: qsTr("The page grows by whole sheets as shapes go past its edge")
            }
        }

        Tk.Overline { title: qsTr("PRINT") }
        Tk.PropertyRow {
            label: qsTr("Printer paper")
            Tk.ComboBox {
                objectName: "pageSetupPrintPaper"
                model: [qsTr("The printer's paper"), qsTr("Same as the page")].concat(dialog.papers.map(function(p) { return p.label }))
                currentIndex: dialog.printPaperIndex
                tooltip: qsTr("The paper this page prints on")
                onActivated: function(index) { dialog.printPaperIndex = index }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Paper orientation")
            visible: dialog.printPaperIndex >= 2
            Tk.Segmented {
                objectName: "pageSetupPrintOrientation"
                stretch: true
                model: [qsTr("Portrait"), qsTr("Landscape")]
                currentIndex: dialog.printLandscape ? 1 : 0
                onActivated: function(index) { dialog.printLandscape = index === 1 }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Print zoom")
            Tk.Segmented {
                objectName: "pageSetupPrintMode"
                stretch: true
                model: [qsTr("Fit to sheets"), qsTr("Adjust to")]
                currentIndex: dialog.fitMode ? 0 : 1
                tooltip: qsTr("Fit the page on sheets across and down, or print it at a zoom across as many sheets as it takes")
                onActivated: function(index) { dialog.fitMode = index === 0 }
            }
        }
        Tk.Flex {
            visible: dialog.fitMode
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.PropertyRow {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                label: qsTr("Sheets across")
                Tk.NumberField {
                    id: acrossField
                    objectName: "pageSetupAcross"
                    from: 1
                    to: 50
                    stepSize: 1
                    decimals: 0
                    onValueModified: function(v) { value = v }
                }
            }
            Tk.PropertyRow {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                label: qsTr("Down")
                Tk.NumberField {
                    id: downField
                    objectName: "pageSetupDown"
                    from: 1
                    to: 50
                    stepSize: 1
                    decimals: 0
                    onValueModified: function(v) { value = v }
                }
            }
        }
        Tk.PropertyRow {
            visible: !dialog.fitMode
            label: qsTr("Zoom")
            Tk.NumberField {
                id: zoomField
                objectName: "pageSetupZoom"
                from: 5
                to: 1000
                stepSize: 5
                decimals: 0
                suffix: " %"
                onValueModified: function(v) { value = v }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Centre on paper")
            Tk.Switch { id: centerSwitch; objectName: "pageSetupCenter" }
        }

        Tk.Overline { title: qsTr("PAGE PROPERTIES") }
        Tk.PropertyRow {
            label: qsTr("Background page")
            Tk.Switch {
                objectName: "pageSetupBackground"
                checked: dialog.isBackground
                tooltip: qsTr("A background page shows behind the pages that choose it, and prints with them")
                onToggled: dialog.isBackground = checked
            }
        }
        Tk.PropertyRow {
            label: qsTr("Background")
            visible: !dialog.isBackground
            Tk.ComboBox {
                objectName: "pageSetupBackPage"
                model: [qsTr("None")].concat(dialog.backgrounds.map(function(p) { return p.name }))
                currentIndex: dialog.backIndex
                tooltip: qsTr("The background page shown behind this page")
                onActivated: function(index) { dialog.backIndex = index }
            }
        }
        Tk.Caption {
            text: qsTr("View ▸ Page Breaks shows where the sheets meet; Fit Page to Drawing sizes the page to what is on it.")
            wrapMode: Text.WordWrap
        }
    }
}
