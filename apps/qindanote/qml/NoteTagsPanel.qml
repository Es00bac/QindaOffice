// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note

// Find Tags (M84, D-093; OneNote's tag summary pane): the tagged notes of
// this page, section or notebook, grouped by tag or by page. A click opens
// the note with the caret in it; a To Do can be ticked here; Create
// Summary Page collects them on a new page.
Tk.Flex {
    id: panel

    required property NoteHost note
    readonly property NoteTagSummary summary: panel.note.tagSummary

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm

    Tk.Segmented {
        objectName: "tagsScope"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        small: true
        stretch: true
        tooltip: qsTr("Search in")
        model: [{ text: qsTr("Page"), value: "page" }, { text: qsTr("Section"), value: "section" },
                { text: qsTr("Notebook"), value: "notebook" }]
        currentIndex: ["page", "section", "notebook"].indexOf(panel.summary.scope)
        onActivated: function(index) { panel.summary.scope = ["page", "section", "notebook"][index] }
    }
    Tk.Segmented {
        objectName: "tagsGrouping"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        small: true
        stretch: true
        tooltip: qsTr("Group by")
        model: [{ text: qsTr("By tag"), value: "tag" }, { text: qsTr("By page"), value: "page" }]
        currentIndex: panel.summary.grouping === "page" ? 1 : 0
        onActivated: function(index) { panel.summary.grouping = index === 1 ? "page" : "tag" }
    }

    Tk.EmptyState {
        objectName: "tagsEmpty"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: panel.summary.itemCount === 0
        iconName: "tag"
        title: qsTr("No tags here")
        text: qsTr("Tag a line with Tags ▸ To Do (Ctrl+1), Important (Ctrl+2) or any other tag; it shows up here.")
    }

    Tk.Scroll {
        id: scroll
        objectName: "tagsScroll"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 60
        overflowX: Tk.Scroll.Hidden
        visible: panel.summary.itemCount > 0
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: 0
            width: scroll.width - Tk.Theme.space.sm
            Repeater {
                model: panel.summary
                Item {
                    id: row
                    required property var model
                    required property int index
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    implicitHeight: row.model.kind === "group" ? header.implicitHeight : item.implicitHeight
                    Tk.SectionHeader {
                        id: header
                        objectName: "tagGroup_" + row.index
                        visible: row.model.kind === "group"
                        width: parent.width
                        title: row.model.text
                        count: row.model.detail
                    }
                    Tk.ListRow {
                        id: item
                        objectName: "tagItem_" + row.index
                        visible: row.model.kind === "item"
                        width: parent.width
                        small: true
                        text: row.model.text.length > 0 ? row.model.text : qsTr("(empty line)")
                        secondaryText: row.model.detail
                        onClicked: panel.summary.activate(row.index)
                        leading: [
                            Tk.CheckBox {
                                objectName: "tagDone_" + row.index
                                visible: row.model.todo
                                small: true
                                checked: row.model.done
                                tooltip: qsTr("Done")
                                onToggled: panel.summary.setDone(row.index, checked)
                            },
                            Tk.Icon {
                                visible: !row.model.todo
                                name: row.model.icon
                                color: row.model.tagColor
                                size: Tk.Theme.size.iconSm
                            }
                        ]
                    }
                }
            }
        }
    }

    Tk.Button {
        objectName: "tagsSummaryPage"
        small: true
        iconName: "file-plus"
        text: qsTr("Create Summary Page")
        available: panel.summary.itemCount > 0
        onClicked: panel.note.actions.trigger("noteTagSummaryPageAction")
    }
}
