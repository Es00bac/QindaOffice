// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data ▸ Data Validation for the selection (M78, D-087: Excel's dialog;
// M21's grew into it): Settings — Allow (any value, whole number, decimal,
// list, date, time, text length, custom, and QindaCalc's not empty), the
// operator, the bounds (numbers, dates, or =formulas such as =$C$1),
// the list's source (items, or =range), the custom formula, Ignore blank,
// In-cell dropdown —; Input Message (shown in the status line while the
// cell is selected); Error Alert — Stop refuses, Warning and Information
// ask —, its title and message. Apply is one undo step.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property string range: ""
    property int tab: 0
    // Allow's entries → ValidationRule::Kind (-1: no rule).
    readonly property var kinds: [-1, 0, 1, 2, 5, 6, 3, 7, 4]
    readonly property int kindValue: dialog.kinds[allow.currentIndex]
    readonly property bool bounded: [0, 1, 3, 5, 6].indexOf(dialog.kindValue) >= 0
    readonly property bool twoBounds: dialog.bounded && operator.currentIndex <= 1

    objectName: "validationDialog"
    title: qsTr("Data Validation")
    subtitle: qsTr("Applies to %1").arg(range)
    dialogWidth: Tk.Theme.size.dialogWidth * 1.1
    primaryText: qsTr("Apply")

    function load() {
        const rule = dialog.host.validationAtCursor()
        dialog.range = rule.range
        dialog.tab = 0
        allow.currentIndex = Math.max(0, dialog.kinds.indexOf(rule.kind))
        operator.currentIndex = rule.op || 0
        first.text = rule.operand1 || ""
        second.text = rule.operand2 || ""
        list.text = rule.list || ""
        ignoreBlank.checked = rule.allowBlank !== false
        dropdown.checked = rule.inCellDropdown !== false
        showInput.checked = rule.showInput !== false
        promptTitle.text = rule.promptTitle || ""
        prompt.text = rule.prompt || ""
        showError.checked = rule.showError !== false
        style.currentIndex = rule.style || 0
        errorTitle.text = rule.errorTitle || ""
        message.text = rule.message || ""
    }
    onAccepted: {
        // "Any value" keeps a rule only for its input message, as in Excel.
        const kind = dialog.kindValue === -1 && prompt.text.trim().length > 0 ? 8 : dialog.kindValue
        dialog.host.setValidationRule({
            kind: kind, op: dialog.bounded ? operator.currentIndex : 0,
            operand1: dialog.kindValue === 7 ? (first.text.startsWith("=") ? first.text : "=" + first.text) : first.text,
            operand2: dialog.twoBounds ? second.text : "", list: list.text,
            allowBlank: ignoreBlank.checked, inCellDropdown: dropdown.checked,
            showInput: showInput.checked, promptTitle: promptTitle.text, prompt: prompt.text,
            showError: showError.checked, style: style.currentIndex, errorTitle: errorTitle.text, message: message.text
        })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.TabStrip {
            objectName: "validationTabs"
            model: [qsTr("Settings"), qsTr("Input Message"), qsTr("Error Alert")]
            currentIndex: dialog.tab
            onTabActivated: function(index) { dialog.tab = index }
        }
        Tk.Flex {
            visible: dialog.tab === 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Allow")
                Tk.ComboBox {
                    id: allow; objectName: "validationKind"
                    model: [qsTr("Any value"), qsTr("Whole number"), qsTr("Decimal"), qsTr("List"), qsTr("Date"), qsTr("Time"),
                            qsTr("Text length"), qsTr("Custom"), qsTr("Not empty")]
                }
            }
            Tk.PropertyRow {
                visible: dialog.bounded
                label: qsTr("Data")
                Tk.ComboBox {
                    id: operator; objectName: "validationOperator"
                    model: [qsTr("between"), qsTr("not between"), qsTr("equal to"), qsTr("not equal to"), qsTr("greater than"),
                            qsTr("less than"), qsTr("greater than or equal to"), qsTr("less than or equal to")]
                }
            }
            Tk.PropertyRow {
                visible: dialog.bounded || dialog.kindValue === 7
                label: dialog.kindValue === 7 ? qsTr("Formula") : (dialog.twoBounds ? qsTr("Minimum") : qsTr("Value"))
                Tk.TextField { id: first; objectName: "validationMin"; mono: true; placeholderText: qsTr("A number, a date or =formula") }
            }
            Tk.PropertyRow {
                visible: dialog.twoBounds
                label: qsTr("Maximum")
                Tk.TextField { id: second; objectName: "validationMax"; mono: true }
            }
            Tk.PropertyRow {
                visible: dialog.kindValue === 2
                label: qsTr("Source")
                Tk.TextField { id: list; objectName: "validationList"; placeholderText: qsTr("Comma-separated values, or =range") }
            }
            Tk.CheckBox { id: ignoreBlank; objectName: "validationIgnoreBlank"; visible: dialog.kindValue >= 0; text: qsTr("Ignore blank") }
            Tk.CheckBox { id: dropdown; objectName: "validationDropdown"; visible: dialog.kindValue === 2; text: qsTr("In-cell dropdown") }
        }
        Tk.Flex {
            visible: dialog.tab === 1
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.CheckBox { id: showInput; objectName: "validationShowInput"; text: qsTr("Show input message when cell is selected") }
            Tk.PropertyRow { label: qsTr("Title"); Tk.TextField { id: promptTitle; objectName: "validationPromptTitle" } }
            Tk.PropertyRow {
                label: qsTr("Input message")
                alignTop: true
                Tk.TextArea { id: prompt; objectName: "validationPrompt"; implicitHeight: Tk.Theme.size.row * 3 }
            }
        }
        Tk.Flex {
            visible: dialog.tab === 2
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.CheckBox { id: showError; objectName: "validationShowError"; text: qsTr("Show error alert after invalid data is entered") }
            Tk.PropertyRow {
                label: qsTr("Style")
                Tk.ComboBox { id: style; objectName: "validationStyle"; model: [qsTr("Stop"), qsTr("Warning"), qsTr("Information")] }
            }
            Tk.PropertyRow { label: qsTr("Title"); Tk.TextField { id: errorTitle; objectName: "validationErrorTitle" } }
            Tk.PropertyRow {
                label: qsTr("Error message")
                alignTop: true
                Tk.TextArea { id: message; objectName: "validationMessage"; implicitHeight: Tk.Theme.size.row * 3 }
            }
        }
    }
}
