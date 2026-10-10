// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The table's own properties in the table designer: its record validation
// rule and text (M80, D-089 §1); M86 (D-094): its data macros — Access's
// table events, each opened in the macro designer — and, for a linked
// table, where its records come from (its design is the source's).
Tk.Card {
    id: card

    required property var view
    readonly property var design: view.design
    readonly property var link: design.link || ({})
    readonly property bool linked: link.path !== undefined && String(link.path).length > 0
    readonly property var events: view.base !== null ? view.base.dataMacroEvents : []

    objectName: "tableProperties"

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.SectionHeader { title: qsTr("Table properties") }
        Tk.Notice {
            objectName: "tableLinkNotice"
            visible: card.linked
            variant: "info"
            iconName: "link-2"
            title: qsTr("Linked table")
            text: card.linked ? qsTr("Its records are a copy of %1, refreshed %2. Change the design there; Linked Table Manager relinks or converts it to a local table.")
                                    .arg(card.link.path).arg(card.link.refreshed || qsTr("never")) : ""
        }
        Tk.PropertyRow {
            label: qsTr("Validation rule")
            hint: qsTr("Compares the fields of a record: [ends] >= [starts]")
            Tk.TextField {
                objectName: "tableValidationRule"
                text: card.design.validationRule || ""
                mono: true
                placeholderText: "[ends] >= [starts]"
                onEditingFinished: card.view.setTableProperty("validationRule", text)
            }
        }
        Tk.PropertyRow {
            label: qsTr("Validation text")
            hint: qsTr("The message shown when a record breaks the rule")
            Tk.TextField {
                objectName: "tableValidationText"
                text: card.design.validationText || ""
                onEditingFinished: card.view.setTableProperty("validationText", text)
            }
        }
        Tk.Overline { title: qsTr("DATA MACROS (TABLE EVENTS)") }
        Tk.Caption {
            visible: card.view.isNew
            wrapMode: Text.Wrap
            text: qsTr("Create the table first; then its events can run data macros for every writer.")
        }
        Repeater {
            model: card.view.isNew || card.linked ? [] : card.events
            delegate: Tk.Flex {
                id: event
                required property var modelData
                readonly property var macro: (card.design.dataMacros || ({}))[modelData.id] || null
                readonly property int steps: event.macro !== null ? (event.macro.actions || []).length : 0
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Label { text: event.modelData.label; Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.7; Tk.Flex.shrink: 0 }
                Tk.Caption {
                    objectName: "dataMacroSummary_" + event.modelData.id
                    text: event.steps > 0 ? qsTr("%n action(s)", "", event.steps) : qsTr("none")
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Button {
                    objectName: "dataMacroEdit_" + event.modelData.id
                    text: event.steps > 0 ? qsTr("Edit") : qsTr("Create")
                    iconName: "workflow"
                    small: true
                    onClicked: card.view.window.accessCommands.editDataMacro(card.design.name, event.modelData.id)
                }
            }
        }
    }
}
