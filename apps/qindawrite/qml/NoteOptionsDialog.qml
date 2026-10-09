// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// References ▸ Footnote and Endnote (M77, D-086): Word's dialog — for
// footnotes or endnotes, where they go, the number format (1 2 3, a b c,
// A B C, i ii iii, I II III, * † ‡ §), the first number and whether the
// numbering restarts; Convert opens Convert Notes. Each answer is one undo
// step (NotesController, write_notes.h).
Tk.Dialog {
    id: dialog
    required property QtObject notes
    property var info: ({ footnote: {}, endnote: {}, formats: [] })
    readonly property bool endnote: kind.currentIndex === 1
    readonly property var positions: endnote ? ["docEnd", "sectEnd"] : ["pageBottom", "beneathText"]
    readonly property var restarts: endnote ? ["continuous", "eachSect"] : ["continuous", "eachSect", "eachPage"]
    objectName: "noteOptionsDialog"
    title: qsTr("Footnote and Endnote")
    dialogWidth: 440
    primaryText: qsTr("Apply")
    tertiaryText: qsTr("Convert…")

    function show(kindIndex) {
        const o = kindIndex === 1 ? info.endnote : info.footnote
        location.currentIndex = Math.max(0, positions.indexOf(o.position))
        format.currentIndex = Math.max(0, info.formats.map(function(f) { return f.format }).indexOf(o.format))
        start.value = o.start
        restart.currentIndex = Math.max(0, restarts.indexOf(o.restart))
    }
    Connections {
        target: dialog.notes
        function onOptionsRequested() {
            dialog.info = dialog.notes.noteOptions()
            kind.currentIndex = dialog.info.endnotes > 0 && dialog.info.footnotes === 0 ? 1 : 0
            dialog.show(kind.currentIndex)
            dialog.open()
        }
    }
    onAccepted: dialog.notes.applyNoteOptions({ kind: endnote ? "endnote" : "footnote", position: positions[location.currentIndex],
                                                format: info.formats[format.currentIndex].format, start: start.value,
                                                restart: restarts[restart.currentIndex] })
    onTertiary: dialog.notes.convertRequested()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Segmented {
            id: kind
            objectName: "noteKind"
            model: [qsTr("Footnotes"), qsTr("Endnotes")]
            onActivated: function(i) { dialog.show(i) }
        }
        Tk.PropertyRow {
            label: qsTr("Location")
            Tk.ComboBox {
                id: location
                objectName: "noteLocation"
                model: dialog.endnote ? [qsTr("End of document"), qsTr("End of section")]
                                      : [qsTr("Bottom of page"), qsTr("Below text")]
            }
        }
        Tk.PropertyRow {
            label: qsTr("Number format")
            Tk.ComboBox { id: format; objectName: "noteFormat"; model: dialog.info.formats.map(function(f) { return f.label }) }
        }
        Tk.PropertyRow {
            label: qsTr("Start at")
            Tk.NumberField { id: start; objectName: "noteStart"; from: 1; to: 9999; decimals: 0; onValueModified: function(v) { value = v } }
        }
        Tk.PropertyRow {
            label: qsTr("Numbering")
            Tk.ComboBox {
                id: restart
                objectName: "noteRestart"
                model: dialog.endnote ? [qsTr("Continuous"), qsTr("Restart each section")]
                                      : [qsTr("Continuous"), qsTr("Restart each section"), qsTr("Restart each page")]
            }
        }
    }
}
