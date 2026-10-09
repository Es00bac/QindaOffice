// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Labels (M77, D-086): Word's Envelopes and Labels ▸ Labels —
// the address, a full page of the same label or one label at a row and
// column, and the label (Avery's US Letter and A4 sheets with Word's
// dimensions). New Document opens the sheet in a window; Print prints it.
// For Start Mail Merge ▸ Labels (`merge`) the document becomes the sheet,
// «Next Record» before every label but the first. Answered through
// EnvelopesController (write_envelopes.h).
Tk.Dialog {
    id: dialog
    required property QtObject envelopes
    property bool merge: false
    property var vendors: []
    property var products: []
    readonly property var shown: products.filter(function(p) { return p.vendor === vendors[vendor.currentIndex] })
    objectName: "labelsDialog"
    title: merge ? qsTr("Label Options") : qsTr("Labels")
    dialogWidth: 560
    primaryText: merge ? qsTr("OK") : qsTr("New Document")
    secondaryText: qsTr("Cancel")
    tertiaryText: merge ? "" : qsTr("Print…")

    function answer(mode) {
        const p = shown[product.currentIndex]
        return { mode: mode, product: p ? p.id : "", address: address.text, fullPage: fullPage.checked,
                 row: row.value, column: column.value }
    }
    Connections {
        target: dialog.envelopes
        function onLabelsDialogRequested(merge) {
            const info = dialog.envelopes.labelInfo()
            dialog.merge = merge
            dialog.vendors = info.vendors
            dialog.products = info.products
            let v = 0
            for (let i = 0; i < info.products.length; ++i)
                if (info.products[i].id === info.product) v = Math.max(0, info.vendors.indexOf(info.products[i].vendor))
            vendor.currentIndex = v
            const list = dialog.shown
            let at = 0
            for (let k = 0; k < list.length; ++k) if (list[k].id === info.product) at = k
            product.currentIndex = at
            address.text = info.address
            fullPage.checked = true
            single.checked = false
            dialog.open()
        }
    }
    onAccepted: dialog.envelopes.applyLabels(answer(merge ? "merge" : "new"))
    onTertiary: dialog.envelopes.applyLabels(answer("print"))

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption { visible: !dialog.merge; text: qsTr("Address") }
        Tk.TextArea { id: address; objectName: "labelAddress"; visible: !dialog.merge; width: 510; rows: 4 }
        Tk.Flex {
            visible: !dialog.merge
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Tk.RadioButton { id: fullPage; objectName: "labelFullPage"; text: qsTr("Full page of the same label"); checked: true; onClicked: single.checked = false }
            // Single label with its row and column wraps as one piece.
            Tk.Flex {
                gap: Tk.Theme.space.sm
                align: Tk.Flex.Center
                Tk.RadioButton { id: single; objectName: "labelSingle"; text: qsTr("Single label"); onClicked: fullPage.checked = false }
                Tk.Label { text: qsTr("Row") }
                Tk.NumberField { id: row; objectName: "labelRow"; enabled: single.checked; implicitWidth: 60; from: 1; to: 80; decimals: 0; value: 1; onValueModified: function(v) { value = v } }
                Tk.Label { text: qsTr("Column") }
                Tk.NumberField { id: column; objectName: "labelColumn"; enabled: single.checked; implicitWidth: 60; from: 1; to: 10; decimals: 0; value: 1; onValueModified: function(v) { value = v } }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Label vendors")
            Tk.ComboBox { id: vendor; objectName: "labelVendor"; model: dialog.vendors; onActivated: product.currentIndex = 0 }
        }
        Tk.PropertyRow {
            label: qsTr("Product number")
            Tk.ComboBox {
                id: product
                objectName: "labelProduct"
                model: dialog.shown.map(function(p) { return p.product + " — " + p.description })
            }
        }
        Tk.Caption {
            objectName: "labelDetails"
            width: 510
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            text: dialog.shown.length > 0 && product.currentIndex >= 0 && product.currentIndex < dialog.shown.length
                  ? dialog.shown[product.currentIndex].details : ""
        }
    }
}
