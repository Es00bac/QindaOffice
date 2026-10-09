// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Greeting Line (M77, D-086): Word's Insert Greeting Line — the
// salutation, the name format and the punctuation, and the greeting for
// a recipient without a name; the preview shows the record being
// previewed. Inserts Word's GREETINGLINE field.
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property var info: ({ formats: [], salutations: [], punctuations: [], fallbacks: [] })
    property string preview: ""
    objectName: "greetingLineDialog"
    title: qsTr("Insert Greeting Line")
    dialogWidth: 480
    primaryText: qsTr("Insert")

    function label(text) { return text.length > 0 ? text : qsTr("(none)") }
    function options() {
        return { salutation: info.salutations[salutation.currentIndex] || "",
                 nameFormat: info.formats.length > 0 ? info.formats[nameFormat.currentIndex].format : "",
                 punctuation: info.punctuations[punctuation.currentIndex] || "",
                 fallback: info.fallbacks[fallback.currentIndex] || "" }
    }
    function refresh() { preview = dialog.mailMerge.greetingLinePreview(options()) }

    Connections {
        target: dialog.mailMerge
        function onGreetingLineRequested() {
            dialog.info = dialog.mailMerge.greetingLineInfo()
            salutation.currentIndex = 0
            nameFormat.currentIndex = 0
            punctuation.currentIndex = 0
            fallback.currentIndex = 0
            dialog.refresh()
            dialog.open()
        }
    }
    onAccepted: dialog.mailMerge.insertGreetingLine(options())

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Overline { text: qsTr("Greeting line format") }
        Tk.Flex {
            gap: Tk.Theme.space.xs
            Tk.ComboBox { id: salutation; objectName: "greetingSalutation"; implicitWidth: 90; model: dialog.info.salutations.map(dialog.label); onActivated: dialog.refresh() }
            Tk.ComboBox { id: nameFormat; objectName: "greetingName"; implicitWidth: 200; model: dialog.info.formats.map(function(f) { return f.sample }); onActivated: dialog.refresh() }
            Tk.ComboBox { id: punctuation; objectName: "greetingPunctuation"; implicitWidth: 80; model: dialog.info.punctuations.map(dialog.label); onActivated: dialog.refresh() }
        }
        Tk.PropertyRow {
            label: qsTr("Invalid recipient names")
            Tk.ComboBox { id: fallback; objectName: "greetingFallback"; model: dialog.info.fallbacks.map(dialog.label); onActivated: dialog.refresh() }
        }
        Tk.Overline { text: qsTr("Preview") }
        Tk.Label { objectName: "greetingPreview"; text: dialog.preview.length > 0 ? dialog.preview : qsTr("(empty)") }
    }
}
