// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

Tk.Dialog {
    id: dialog
    required property EquationSession session
    property bool latex: false
    objectName: "equationDialog"
    title: qsTr("Equation")
    subtitle: qsTr("Type in the equation. Arrow keys move; Tab moves between slots.")
    primaryText: qsTr("Apply")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6
    onAccepted: session.accept()
    onRejected: session.cancel()
    onOpened: canvas.forceActiveFocus()
    Connections {
        target: dialog.session
        function onActiveChanged() { if (!dialog.session.active) dialog.close() }
        function onRequested() { dialog.open() }
        function onBuildRequested() { dialog.session.convert(source.text, dialog.latex) }
        function onFocusRequested() { canvas.forceActiveFocus() }
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Scroll {
            objectName: "equationViewport"
            implicitHeight: Tk.Theme.size.control * 5
            implicitWidth: dialog.availableWidth
            EquationCanvas {
                id: canvas
                objectName: "equationCanvas"
                session: dialog.session
                width: Math.max(implicitWidth, dialog.availableWidth - Tk.Theme.space.lg)
                height: Math.max(implicitHeight, Tk.Theme.size.control * 4)
                selectionColor: Tk.Theme.color.selection
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Equation, professional form")
                Accessible.description: dialog.session.linearText
            }
        }
        Tk.Flex {
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Repeater {
                model: ["equationUndo", "equationRedo", "equationPreviousSlot", "equationNextSlot"]
                Tk.Button {
                    required property string modelData
                    readonly property var command: dialog.session.action(modelData)
                    objectName: modelData
                    text: command.plainText
                    available: command.enabled
                    small: true
                    onClicked: command.trigger()
                }
            }
        }
        Tk.Overline { text: qsTr("Structures") }
        Tk.Flex {
            objectName: "equationGallery"
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Repeater {
                model: dialog.session.templateIds
                Tk.Button {
                    required property string modelData
                    readonly property var command: dialog.session.action(modelData)
                    objectName: modelData
                    text: command.plainText
                    available: command.enabled
                    small: true
                    onClicked: command.trigger()
                }
            }
        }
        Tk.Overline { text: qsTr("Linear input") }
        Tk.Segmented {
            objectName: "equationInputMode"
            model: [qsTr("UnicodeMath"), qsTr("LaTeX")]
            currentIndex: dialog.latex ? 1 : 0
            onActivated: function(index) { dialog.latex = index === 1 }
        }
        Tk.TextArea {
            id: source
            objectName: "equationSource"
            text: dialog.latex ? dialog.session.latexText : dialog.session.linearText
            wrapMode: TextEdit.Wrap
            implicitHeight: Tk.Theme.size.control * 2
            Accessible.name: qsTr("Equation linear input")
        }
        Tk.Caption {
            text: qsTr("Build replaces the draft from this text. Some Office properties have no linear spelling; Undo restores them.")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
        Tk.Button {
            objectName: "equationBuild"
            text: qsTr("Build professional equation")
            onClicked: dialog.session.action("equationBuild").trigger()
        }
        Tk.Caption {
            objectName: "equationError"
            visible: text.length > 0
            text: dialog.session.error
            wrapMode: Text.Wrap
        }
    }
}
