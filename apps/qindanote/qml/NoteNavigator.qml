// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note
import QindaOffice.Shell

// Sections and pages: a filter field over Tk.TreeRows from the host's
// NavigatorModel. Click opens a page (or folds a section), double-click
// renames a page inline, right-click shows the Page menu; New Page sits
// under the list.
Tk.Flex {
    id: navigator

    required property NoteHost note
    property var pageMenuNode: null

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Tk.SearchField {
        objectName: "navigatorFilter"
        placeholderText: qsTr("Filter pages")
        debounce: 100
        onSearched: function(text) { navigator.note.navigator.filter = text }
    }
    Tk.Scroll {
        objectName: "navigatorScroll"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 0

        Tk.Flex {
            direction: Tk.Flex.Column
            gap: 0
            Repeater {
                model: navigator.note.navigator
                // AGENT-GUARD: the roles are read through `model`, never as
                // required properties: `depth` and `expanded` are TreeRow's
                // own properties, and a same-named required property shadows
                // them (the row then draws with depth 0, never indents).
                Tk.TreeRow {
                    id: row
                    required property var model
                    required property int index
                    objectName: model.isSection ? "navSection_" + model.sectionId : "navPage_" + model.pageId
                    text: model.title
                    iconName: model.isSection ? "" : "file-text"
                    depth: model.depth
                    expandable: model.isSection
                    expanded: model.expanded
                    selected: model.current
                    active: model.current
                    editable: !model.isSection
                    onClicked: navigator.note.selectRow(row.index)
                    onExpansionToggled: navigator.note.navigator.toggleSection(row.model.sectionId)
                    onRenamed: function(text) { navigator.note.renamePage(row.model.sectionId, row.model.pageId, text) }
                    onRightClicked: function(point) {
                        navigator.note.selectRow(row.index)
                        contextMenu.popup()
                    }
                }
            }
        }
    }

    // The obvious way to grow the notebook, where the pages are.
    Tk.Button {
        objectName: "navigatorNewPage"
        small: true
        iconName: "plus"
        text: qsTr("New Page")
        tooltip: qsTr("Add a page to this section")
        onClicked: navigator.note.actions.trigger("notePageNew")
    }

    MenuFromNode {
        id: contextMenu
        objectName: "navigatorMenu"
        node: navigator.pageMenuNode
        actions: navigator.note.actions
    }
}
