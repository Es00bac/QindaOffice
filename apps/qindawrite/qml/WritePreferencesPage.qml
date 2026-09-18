// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// Settings ▸ Preferences ▸ QindaWrite (D-014: every control here is READ
// by the host — the spell checker follows the switch on Apply).
Tk.Flex {
    id: page
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    function apply() {
        Preferences.spellCheckEnabled = spell.checked
    }
    Component.onCompleted: spell.checked = Preferences.spellCheckEnabled
    Tk.PropertyRow {
        label: qsTr("Writing")
        Tk.Switch { id: spell; objectName: "prefSpellCheck"; text: qsTr("Check spelling as you type") }
    }
}
