// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Access's Edit Hyperlink (M86, D-094, base_hyperlink.h): a Hyperlink field
// stores "text to display#address#subaddress#screen tip"; this dialog edits
// the four parts of the datasheet's current hyperlink cell
// (BaseHost.hyperlinkTarget {rowId, field, label, text, editable}) and
// writes the joined text back — one undo step, like typing in the cell.
// Remove Link empties the cell.
//
// AGENT-NOTE: the address is stored as typed; Open Hyperlink
// (AccessToolsCommands.hyperlinkUrl) decides what it may open.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var target: ({})
    property string display: ""
    property string address: ""
    property string subAddress: ""
    property string screenTip: ""
    readonly property string stored: base !== null
        ? base.hyperlinkText({ display: display, address: address, subAddress: subAddress, screenTip: screenTip }) : ""

    title: qsTr("Edit hyperlink")
    subtitle: target.label || target.field || ""
    primaryText: qsTr("OK")
    tertiaryText: (target.text || "").length > 0 ? qsTr("Remove link") : ""
    primaryEnabled: address.trim().length > 0 || subAddress.trim().length > 0

    function openFor(t) {
        if (!t || !t.field) return
        target = t
        const parts = base.hyperlinkParts(t.text || "")
        display = parts.display
        address = parts.address
        subAddress = parts.subAddress
        screenTip = parts.screenTip
        open()
    }
    function write(text) {
        const r = window.base.datasheet.setCell(target.rowId, target.field, text)
        if (!r.ok) window.notify(qsTr("Hyperlink not saved"), r.error, "danger")
    }
    onAccepted: write(stored)
    onTertiary: { close(); write("") }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Text to display")
            Tk.TextField {
                objectName: "hyperlinkDisplay"
                text: dialog.display
                placeholderText: dialog.address
                onTextEdited: dialog.display = text
            }
        }
        Tk.PropertyRow {
            label: qsTr("Address")
            hint: qsTr("A web page, an e-mail address (mailto:), or a file")
            Tk.TextField {
                objectName: "hyperlinkAddress"
                text: dialog.address
                placeholderText: qsTr("https://example.org")
                onTextEdited: dialog.address = text
            }
        }
        Tk.PropertyRow {
            label: qsTr("Bookmark")
            hint: qsTr("A place in the page or file (optional)")
            Tk.TextField {
                objectName: "hyperlinkSubAddress"
                text: dialog.subAddress
                onTextEdited: dialog.subAddress = text
            }
        }
        Tk.PropertyRow {
            label: qsTr("ScreenTip")
            Tk.TextField {
                objectName: "hyperlinkScreenTip"
                text: dialog.screenTip
                onTextEdited: dialog.screenTip = text
            }
        }
        Tk.Caption {
            objectName: "hyperlinkStored"
            text: qsTr("Stored as: %1").arg(dialog.stored)
            wrapMode: Text.WrapAnywhere
        }
    }
}
