// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Insert ▸ Link (M87, D-095; Visio's Hyperlinks dialog, Ctrl+K): the
// selection's links — each a web address or file, or a page of this
// drawing, with a description — added, changed or removed together, one
// undo step on OK. Ctrl+click (or Open Link) follows the first one.
Tk.Dialog {
    id: dialog
    objectName: "diagramLinkDialog"

    required property DiagramService diagram
    property var links: []
    readonly property var pageNames: [qsTr("(no page)")].concat(diagram.pages.map(function(p) { return p.name }))

    title: qsTr("Links")
    subtitle: qsTr("On %n selected item(s)", "", diagram.selection.length)
    primaryText: qsTr("Apply")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.25

    function openFor() {
        const id = diagram.selection.length > 0 ? diagram.selection[0] : ""
        links = diagram.links(id)
        if (links.length === 0) links = [{ description: "", address: "", subAddress: "" }]
        open()
    }
    function update(index, key, value) {
        const next = links.slice()
        next[index] = Object.assign({}, next[index])
        next[index][key] = value
        links = next
    }
    onAccepted: diagram.setSelectionLinks(links)

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.md
        Repeater {
            model: dialog.links.length
            delegate: Tk.Flex {
                id: row
                required property int index
                readonly property var link: dialog.links[index] || ({})
                objectName: "diagramLinkRow_" + index
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Tk.PropertyRow {
                    label: qsTr("Address")
                    Tk.TextField {
                        objectName: "diagramLinkAddress"
                        small: true
                        text: row.link.address || ""
                        placeholderText: qsTr("https://… or a file")
                        tooltip: qsTr("A web address or a file the link opens")
                        onTextEdited: dialog.update(row.index, "address", text)
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Page")
                    Tk.ComboBox {
                        objectName: "diagramLinkPage"
                        small: true
                        model: dialog.pageNames
                        currentIndex: Math.max(0, dialog.pageNames.indexOf(row.link.subAddress || ""))
                        tooltip: qsTr("A page of this drawing the link goes to")
                        onActivated: function(i) { dialog.update(row.index, "subAddress", i === 0 ? "" : dialog.pageNames[i]) }
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.TextField {
                        objectName: "diagramLinkDescription"
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        small: true
                        text: row.link.description || ""
                        placeholderText: qsTr("Description")
                        tooltip: qsTr("What the link is for")
                        onTextEdited: dialog.update(row.index, "description", text)
                    }
                    Tk.IconButton {
                        objectName: "diagramLinkRemove"
                        small: true
                        iconName: "trash-2"
                        tooltip: qsTr("Remove this link")
                        onClicked: dialog.links = dialog.links.filter(function(l, i) { return i !== row.index })
                    }
                }
                Tk.Divider {}
            }
        }
        Tk.Button {
            objectName: "diagramLinkAdd"
            small: true
            iconName: "plus"
            text: qsTr("Add a link")
            onClicked: dialog.links = dialog.links.concat([{ description: "", address: "", subAddress: "" }])
        }
    }
}
