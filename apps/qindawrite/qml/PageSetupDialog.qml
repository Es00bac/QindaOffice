// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Page ▸ Page Setup: paper, margin, header and footer ({page} / {pages}
// expand at render time on screen, print and PDF alike).
Tk.Dialog {
    id: dialog
    signal setupApplied(real width, real height, real margin, string header, string footer)
    readonly property var papers: [{ name: "A4", w: 794, h: 1123 }, { name: "Letter", w: 816, h: 1056 }]
    objectName: "pageSetupDialog"
    title: qsTr("Page Setup")
    dialogWidth: 420
    primaryText: qsTr("Apply")
    function load(setup) {
        let index = 0
        for (let i = 0; i < papers.length; ++i) if (Math.abs(papers[i].w - setup.width) < 1 && Math.abs(papers[i].h - setup.height) < 1) index = i
        paper.currentIndex = index
        margin.value = setup.margin
        header.text = setup.header
        footer.text = setup.footer
    }
    onAccepted: dialog.setupApplied(papers[paper.currentIndex].w, papers[paper.currentIndex].h, margin.value, header.text, footer.text)
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow { label: qsTr("Paper"); Tk.ComboBox { id: paper; objectName: "pagePaper"; model: dialog.papers.map(function(p) { return p.name }) } }
        Tk.PropertyRow { label: qsTr("Margin"); Tk.NumberField { id: margin; objectName: "pageMargin"; from: 0; to: 200; decimals: 0; suffix: " px" } }
        Tk.PropertyRow { label: qsTr("Header"); Tk.TextField { id: header; objectName: "pageHeader"; width: 260; placeholderText: qsTr("Header ({page} of {pages})") } }
        Tk.PropertyRow { label: qsTr("Footer"); Tk.TextField { id: footer; objectName: "pageFooter"; width: 260; placeholderText: qsTr("Footer ({page} of {pages})") } }
    }
}
