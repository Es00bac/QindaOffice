// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Insert ▸ Special Character: Word's Symbol dialog on QindaTK's CharacterMap
// (M89, D-097; QindaTK D-327) — the Unicode blocks a document reaches for,
// the characters a block holds in the document's font, the selected one
// large with its code point, and the characters used lately. Insert, a
// double-click or Return inserts at the caret and the dialog stays open for
// the next one, as Word's does. Recent characters start from Word's special
// characters (WriteHost::symbols) and follow what this window inserted.
// (Until M85 this was a grid of those special characters only.)
Tk.Dialog {
    id: dialog
    property var symbols: []
    // Word's special characters first; then what was inserted (assigned).
    property var recent: symbols.slice(0, 16)
    property string fontFamily: ""
    signal symbolPicked(string text)
    objectName: "symbolDialog"
    title: qsTr("Symbol")
    dialogWidth: Tk.Theme.size.panelMinWidth * 2 + Tk.Theme.space.xl * 2
    primaryText: ""
    secondaryText: qsTr("Close")

    Tk.CharacterMap {
        objectName: "symbolCharacterMap"
        fontFamily: dialog.fontFamily.length > 0 ? dialog.fontFamily : Tk.Theme.font.family
        recent: dialog.recent
        tooltip: qsTr("Symbols")
        onCharacterChosen: function(text) {
            dialog.symbolPicked(text)
            dialog.recent = [text].concat(dialog.recent.filter(function(c) { return c !== text })).slice(0, 16)
        }
    }
}
