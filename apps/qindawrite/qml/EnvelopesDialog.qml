// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Envelopes (M77, D-086): Word's Envelopes and Labels ▸
// Envelopes — the delivery and return address (remembered, or omitted),
// the envelope size (Size 10, DL, C5, C6, … or a custom size), where the
// delivery address goes (Word's Auto, or from the left and top edge). Add
// to Document makes it the document's first section; New Document opens
// it in a window; Print prints it. For Start Mail Merge ▸ Envelopes
// (`merge`) the document becomes the envelope with an «AddressBlock».
// Answered through EnvelopesController (write_envelopes.h).
Tk.Dialog {
    id: dialog
    required property QtObject envelopes
    property bool merge: false
    property var sizes: []
    readonly property bool custom: sizeCombo.currentIndex === sizes.length
    objectName: "envelopesDialog"
    title: merge ? qsTr("Envelope Options") : qsTr("Envelopes")
    dialogWidth: 520
    primaryText: merge ? qsTr("OK") : qsTr("Add to Document")
    secondaryText: qsTr("Cancel")
    tertiaryText: merge ? "" : qsTr("New Document")

    function answer(mode) {
        const custom = dialog.custom
        return { mode: mode, delivery: delivery.text, returnAddress: returnAddress.text, omitReturn: omit.checked,
                 size: custom ? "custom" : sizes[sizeCombo.currentIndex].id,
                 width: customWidth.value * 96, height: customHeight.value * 96,
                 deliveryLeft: autoPosition.checked ? -1 : leftField.value * 96, deliveryTop: autoPosition.checked ? -1 : topField.value * 96 }
    }
    Connections {
        target: dialog.envelopes
        function onEnvelopeDialogRequested(merge) {
            const info = dialog.envelopes.envelopeInfo()
            dialog.merge = merge
            dialog.sizes = info.sizes
            let at = 0
            for (let i = 0; i < info.sizes.length; ++i) if (info.sizes[i].id === info.size) at = i
            sizeCombo.currentIndex = at
            delivery.text = info.delivery
            returnAddress.text = info.returnAddress
            omit.checked = info.omitReturn
            autoPosition.checked = true
            dialog.open()
        }
    }
    onAccepted: dialog.envelopes.applyEnvelope(answer(merge ? "merge" : "add"))
    onTertiary: dialog.envelopes.applyEnvelope(answer("new"))

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption { visible: !dialog.merge; text: qsTr("Delivery address") }
        Tk.TextArea { id: delivery; objectName: "envelopeDelivery"; visible: !dialog.merge; width: 470; rows: 4 }
        Tk.Flex {
            gap: Tk.Theme.space.md
            Tk.Caption { text: qsTr("Return address") }
            Tk.CheckBox { id: omit; objectName: "envelopeOmit"; text: qsTr("Omit") }
        }
        Tk.TextArea { id: returnAddress; objectName: "envelopeReturn"; width: 470; rows: 3; enabled: !omit.checked }
        Tk.PropertyRow {
            label: qsTr("Envelope size")
            Tk.ComboBox {
                id: sizeCombo
                objectName: "envelopeSize"
                model: dialog.sizes.map(function(s) { return s.name }).concat([qsTr("Custom size")])
            }
        }
        Tk.Flex {
            visible: dialog.custom
            gap: Tk.Theme.space.xs
            Tk.Label { text: qsTr("Width") }
            Tk.NumberField { id: customWidth; objectName: "envelopeWidth"; implicitWidth: 80; from: 1; to: 20; decimals: 2; suffix: qsTr(" in"); value: 9.5; onValueModified: function(v) { value = v } }
            Tk.Label { text: qsTr("Height") }
            Tk.NumberField { id: customHeight; objectName: "envelopeHeight"; implicitWidth: 80; from: 1; to: 20; decimals: 2; suffix: qsTr(" in"); value: 4.13; onValueModified: function(v) { value = v } }
        }
        Tk.Overline { text: qsTr("Delivery address position") }
        Tk.Flex {
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Tk.CheckBox { id: autoPosition; objectName: "envelopeAuto"; text: qsTr("Auto"); checked: true }
            Tk.Label { text: qsTr("From left") }
            Tk.NumberField { id: leftField; objectName: "envelopeLeft"; enabled: !autoPosition.checked; implicitWidth: 80; from: 0; to: 20; decimals: 2; suffix: qsTr(" in"); value: 4; onValueModified: function(v) { value = v } }
            Tk.Label { text: qsTr("From top") }
            Tk.NumberField { id: topField; objectName: "envelopeTop"; enabled: !autoPosition.checked; implicitWidth: 80; from: 0; to: 20; decimals: 2; suffix: qsTr(" in"); value: 2; onValueModified: function(v) { value = v } }
        }
        Tk.Button {
            objectName: "envelopePrint"
            Tk.Flex.alignSelf: Tk.Flex.End
            visible: !dialog.merge
            text: qsTr("Print…")
            iconName: "printer"
            onClicked: { dialog.close(); dialog.envelopes.applyEnvelope(dialog.answer("print")) }
        }
    }
}
