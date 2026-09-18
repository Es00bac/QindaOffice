// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Slide ▸ Apply Master to Slide: the deck's masters, or plain white.
Tk.Dialog {
    id: dialog
    signal masterChosen(int index)
    objectName: "showApplyMasterDialog"
    title: qsTr("Apply Master")
    dialogWidth: 360
    primaryText: qsTr("Apply")
    function load(names) { combo.model = [qsTr("None (plain white)")].concat(names); combo.currentIndex = 0 }
    onAccepted: dialog.masterChosen(combo.currentIndex - 1)
    Tk.PropertyRow { label: qsTr("Master"); Tk.ComboBox { id: combo; objectName: "showApplyMasterCombo"; implicitWidth: 220 } }
}
