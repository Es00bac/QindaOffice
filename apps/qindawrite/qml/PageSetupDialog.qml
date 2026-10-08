// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Layout ▸ Page Setup: paper, orientation, the four margins and the
// header/footer distances (in inches or centimetres; the host speaks
// 96-dpi document px), a different first page, and plain header/footer
// text ({page} / {pages} become page-number fields). A header read from a
// Word document can be richer than one line — pictures, tables, formatting —
// and the dialog says so before typing would replace it.
Tk.Dialog {
    id: dialog
    signal setupApplied(var setup)
    // Paper sizes in document px (96 per inch).
    readonly property var papers: [
        { name: qsTr("Letter (8.5 × 11 in)"), w: 816, h: 1056 },
        { name: qsTr("Legal (8.5 × 14 in)"), w: 816, h: 1344 },
        { name: qsTr("Tabloid (11 × 17 in)"), w: 1056, h: 1632 },
        { name: qsTr("Executive (7.25 × 10.5 in)"), w: 696, h: 1008 },
        { name: qsTr("A3 (297 × 420 mm)"), w: 1123, h: 1587 },
        { name: qsTr("A4 (210 × 297 mm)"), w: 794, h: 1123 },
        { name: qsTr("A5 (148 × 210 mm)"), w: 559, h: 794 },
        { name: qsTr("B5 (176 × 250 mm)"), w: 665, h: 945 },
        { name: qsTr("Envelope #10 (4.125 × 9.5 in)"), w: 396, h: 912 },
        { name: qsTr("Custom"), w: 0, h: 0 }
    ]
    property real customWidth: 816
    property real customHeight: 1056
    property bool headerRich: false
    property bool footerRich: false
    readonly property real pxPerUnit: units.currentIndex === 0 ? 96 : 96 / 2.54
    readonly property string unitSuffix: units.currentIndex === 0 ? qsTr(" in") : qsTr(" cm")
    objectName: "pageSetupDialog"
    title: qsTr("Page Setup")
    dialogWidth: 460
    primaryText: qsTr("Apply")

    function toUnits(px) { return px / pxPerUnit }
    function toPx(value) { return value * pxPerUnit }
    function load(setup) {
        // Portrait size of the current paper decides the list entry.
        const w = Math.min(setup.width, setup.height)
        const h = Math.max(setup.width, setup.height)
        let index = papers.length - 1
        for (let i = 0; i < papers.length - 1; ++i)
            if (Math.abs(papers[i].w - w) < 2 && Math.abs(papers[i].h - h) < 2) index = i
        customWidth = w
        customHeight = h
        paper.currentIndex = index
        orientation.currentIndex = setup.landscape ? 1 : 0
        top.value = toUnits(setup.top)
        bottom.value = toUnits(setup.bottom)
        left.value = toUnits(setup.left)
        right.value = toUnits(setup.right)
        headerDistance.value = toUnits(setup.headerDistance)
        footerDistance.value = toUnits(setup.footerDistance)
        firstPage.checked = setup.differentFirstPage
        header.text = setup.header
        footer.text = setup.footer
        headerRich = setup.headerRich
        footerRich = setup.footerRich
    }
    onAccepted: {
        const entry = papers[paper.currentIndex]
        const w = entry.w > 0 ? entry.w : customWidth
        const h = entry.h > 0 ? entry.h : customHeight
        const landscape = orientation.currentIndex === 1
        dialog.setupApplied({
            width: landscape ? Math.max(w, h) : Math.min(w, h),
            height: landscape ? Math.min(w, h) : Math.max(w, h),
            top: toPx(top.value), bottom: toPx(bottom.value),
            left: toPx(left.value), right: toPx(right.value),
            headerDistance: toPx(headerDistance.value), footerDistance: toPx(footerDistance.value),
            differentFirstPage: firstPage.checked,
            header: header.text, footer: footer.text
        })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Paper")
            Tk.ComboBox { id: paper; objectName: "pagePaper"; model: dialog.papers.map(function(p) { return p.name }) }
        }
        Tk.PropertyRow {
            label: qsTr("Orientation")
            Tk.Segmented { id: orientation; objectName: "pageOrientation"; model: [qsTr("Portrait"), qsTr("Landscape")] }
        }
        Tk.PropertyRow {
            label: qsTr("Units")
            Tk.Segmented {
                id: units
                objectName: "pageUnits"
                model: [qsTr("Inches"), qsTr("Centimetres")]
                // Re-express the fields when the unit changes.
                property real previous: 96
                onCurrentIndexChanged: {
                    const now = currentIndex === 0 ? 96 : 96 / 2.54
                    for (const f of [top, bottom, left, right, headerDistance, footerDistance]) f.value = f.value * previous / now
                    previous = now
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Top / bottom")
            Tk.Flex {
                gap: Tk.Theme.space.sm
                Tk.NumberField { id: top; objectName: "pageMarginTop"; from: 0; to: 10; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix }
                Tk.NumberField { id: bottom; objectName: "pageMarginBottom"; from: 0; to: 10; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Left / right")
            Tk.Flex {
                gap: Tk.Theme.space.sm
                Tk.NumberField { id: left; objectName: "pageMarginLeft"; from: 0; to: 10; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix }
                Tk.NumberField { id: right; objectName: "pageMarginRight"; from: 0; to: 10; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Header / footer from edge")
            Tk.Flex {
                gap: Tk.Theme.space.sm
                Tk.NumberField { id: headerDistance; objectName: "pageHeaderDistance"; from: 0; to: 10; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix }
                Tk.NumberField { id: footerDistance; objectName: "pageFooterDistance"; from: 0; to: 10; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix }
            }
        }
        Tk.CheckBox { id: firstPage; objectName: "pageFirstDifferent"; text: qsTr("Different first page") }
        Tk.PropertyRow {
            label: qsTr("Header")
            hint: dialog.headerRich ? qsTr("Has pictures or formatting from the document; typing here replaces them.") : ""
            Tk.TextField { id: header; objectName: "pageHeader"; width: 260; placeholderText: qsTr("Header ({page} of {pages})") }
        }
        Tk.PropertyRow {
            label: qsTr("Footer")
            hint: dialog.footerRich ? qsTr("Has pictures or formatting from the document; typing here replaces them.") : ""
            Tk.TextField { id: footer; objectName: "pageFooter"; width: 260; placeholderText: qsTr("Footer ({page} of {pages})") }
        }
    }
}
