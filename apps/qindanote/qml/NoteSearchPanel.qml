// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note

// Search across notebooks (M84, D-093; OneNote's Ctrl+E): typed text,
// live notes, checklists, tag names and page titles in this page, section,
// notebook or every notebook; results grouped by section and page; a click
// opens the page (and its notebook) with the caret in the block.
Tk.Flex {
    id: panel

    required property NoteHost note
    readonly property NoteSearchModel search: panel.note.search

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm

    function focusField() { field.forceActiveFocus(Qt.ShortcutFocusReason) }

    Tk.SearchField {
        id: field
        objectName: "noteSearchField"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        placeholderText: qsTr("Search notes, tags and titles")
        debounce: 250
        onSearched: function(text) { panel.search.query = text }
        // A query set from elsewhere (a test, a later scope) shows here too.
        Connections {
            target: panel.search
            function onQueryChanged() { if (field.text !== panel.search.query) field.text = panel.search.query }
        }
    }
    Tk.ComboBox {
        objectName: "noteSearchScope"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        small: true
        tooltip: qsTr("Search in")
        model: [qsTr("This page"), qsTr("This section"), qsTr("This notebook"), qsTr("All notebooks")]
        currentIndex: ["page", "section", "notebook", "all"].indexOf(panel.search.scope)
        onActivated: function(index) { panel.search.scope = ["page", "section", "notebook", "all"][index] }
    }
    Tk.Caption {
        objectName: "noteSearchSummary"
        visible: panel.search.query.trim().length > 0
        text: panel.search.hitCount === 0 ? qsTr("No matches")
              : qsTr("%n match(es)", "", panel.search.hitCount) + " · " + qsTr("%n page(s)", "", panel.search.pageCount)
    }
    Tk.EmptyState {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: panel.search.query.trim().length === 0
        iconName: "search"
        title: qsTr("Search your notes")
        text: qsTr("Every word you type must be on the page — in its title, its notes, its checklists or its tags. Handwriting and pictures are not searched.")
    }
    Tk.Scroll {
        id: scroll
        objectName: "noteSearchScroll"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 60
        overflowX: Tk.Scroll.Hidden
        visible: panel.search.hitCount > 0
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: 0
            width: scroll.width - Tk.Theme.space.sm
            Repeater {
                model: panel.search
                Item {
                    id: row
                    required property var model
                    required property int index
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    implicitHeight: row.model.kind === "section" ? header.implicitHeight : result.implicitHeight
                    Tk.SectionHeader {
                        id: header
                        objectName: "searchSection_" + row.index
                        visible: row.model.kind === "section"
                        width: parent.width
                        title: row.model.text
                    }
                    Tk.ListRow {
                        id: result
                        objectName: "searchResult_" + row.index
                        visible: row.model.kind !== "section"
                        width: parent.width
                        small: true
                        indent: row.model.kind === "hit" ? Tk.Theme.space.lg : 0
                        iconName: row.model.icon
                        text: row.model.text
                        secondaryText: row.model.detail
                        onClicked: panel.search.activate(row.index)
                    }
                }
            }
        }
    }
}
