// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Insert ▸ Link and Edit Link (M66, D-075), Excel's dialog cut to what a
// link holds: the text to display, where it goes — a web page, a file or an
// e-mail address, or a place in this workbook (a sheet and a cell, or a
// defined name) — and its screen tip.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var sheets: []

    objectName: "linkDialog"
    title: qsTr("Link")
    dialogWidth: 440
    primaryText: qsTr("Apply")
    primaryEnabled: kind.currentIndex === 0 ? address.text.trim().length > 0 : cell.text.trim().length > 0

    function load() {
        const link = dialog.host.linkAtCursor()
        dialog.sheets = link.sheets
        display.text = link.display
        tooltip.text = link.tooltip
        address.text = link.address
        kind.currentIndex = link.address.length === 0 && link.location.length > 0 ? 1 : 0
        sheet.currentIndex = Math.max(0, dialog.sheets.indexOf(link.locationSheet))
        cell.text = link.locationCell.length > 0 ? link.locationCell : "A1"
        dialog.title = link.hasLink ? qsTr("Edit link") : qsTr("Insert link")
    }
    onOpened: (kind.currentIndex === 0 ? address : cell).forceActiveFocus(Qt.TabFocusReason)
    onAccepted: dialog.host.applyLink({
        address: kind.currentIndex === 0 ? address.text : "",
        sheet: kind.currentIndex === 1 ? dialog.sheets[sheet.currentIndex] : "",
        cell: kind.currentIndex === 1 ? cell.text : "",
        display: display.text,
        tooltip: tooltip.text
    })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Text to display")
            Tk.TextField { id: display; objectName: "linkDisplay"; width: 260; placeholderText: qsTr("The address itself") }
        }
        Tk.PropertyRow {
            label: qsTr("Link to")
            Tk.ComboBox {
                id: kind; objectName: "linkKind"; width: 260
                model: [qsTr("Web page, file or e-mail address"), qsTr("Place in this workbook")]
            }
        }
        Tk.PropertyRow {
            visible: kind.currentIndex === 0
            label: qsTr("Address")
            Tk.TextField {
                id: address; objectName: "linkAddress"; width: 260
                placeholderText: qsTr("https://… or someone@example.com")
            }
        }
        Tk.PropertyRow {
            visible: kind.currentIndex === 1
            label: qsTr("Sheet")
            Tk.ComboBox { id: sheet; objectName: "linkSheet"; width: 260; model: dialog.sheets }
        }
        Tk.PropertyRow {
            visible: kind.currentIndex === 1
            label: qsTr("Cell or name")
            Tk.TextField { id: cell; objectName: "linkCell"; width: 260; placeholderText: qsTr("A1, or a defined name") }
        }
        Tk.PropertyRow {
            label: qsTr("Screen tip")
            Tk.TextField { id: tooltip; objectName: "linkTooltip"; width: 260; placeholderText: qsTr("Shown when the pointer rests on the link") }
        }
    }
}
