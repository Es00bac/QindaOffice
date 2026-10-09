// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Rules (M77, D-086): If…Then…Else (Word's IF with the text to
// insert either way), Skip Record If (SKIPIF: the recipient gets no copy)
// and Next Record If (NEXTIF) — a field, a comparison and a value.
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property string rule: "if"
    readonly property var ops: ["=", "<>", "<", ">", "<=", ">=", "=", "<>"]
    objectName: "mergeRuleDialog"
    title: rule === "if" ? qsTr("Insert Word Field: IF") : rule === "skipif" ? qsTr("Insert Word Field: Skip Record If")
                                                                            : qsTr("Insert Word Field: Next Record If")
    dialogWidth: 520
    primaryText: qsTr("Insert")

    Connections {
        target: dialog.mailMerge
        function onRuleRequested(rule) {
            dialog.rule = rule
            field.currentIndex = 0
            comparison.currentIndex = 0
            value.text = ""
            thenText.text = ""
            otherwise.text = ""
            dialog.open()
        }
    }
    onAccepted: {
        const blank = comparison.currentIndex >= 6
        dialog.mailMerge.insertRule({ rule: rule, field: dialog.mailMerge.fields[field.currentIndex] || "",
                                      op: ops[comparison.currentIndex], value: blank ? "" : value.text,
                                      then: thenText.text, otherwise: otherwise.text })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            gap: Tk.Theme.space.xs
            Tk.ComboBox { id: field; objectName: "ruleField"; implicitWidth: 170; model: dialog.mailMerge.fields }
            Tk.ComboBox {
                id: comparison
                objectName: "ruleComparison"
                implicitWidth: 160
                model: [qsTr("Equal to"), qsTr("Not equal to"), qsTr("Less than"), qsTr("Greater than"),
                        qsTr("Less than or equal"), qsTr("Greater than or equal"), qsTr("Is blank"), qsTr("Is not blank")]
            }
            Tk.TextField { id: value; objectName: "ruleValue"; implicitWidth: 140; enabled: comparison.currentIndex < 6; placeholderText: qsTr("Compare to") }
        }
        Tk.Caption { visible: dialog.rule === "if"; text: qsTr("Insert this text:") }
        Tk.TextArea { id: thenText; objectName: "ruleThen"; visible: dialog.rule === "if"; width: 470; rows: 2 }
        Tk.Caption { visible: dialog.rule === "if"; text: qsTr("Otherwise insert this text:") }
        Tk.TextArea { id: otherwise; objectName: "ruleOtherwise"; visible: dialog.rule === "if"; width: 470; rows: 2 }
    }
}
