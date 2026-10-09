// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Show

// The slide sorter: one thumbnail per slide (painted through the shared
// renderer), the current one outlined; click selects, drag reorders
// (moveSlide(from, to) → one undo step), right-click shows the Slide menu.
// With sections (M75, D-084) a header leads each one — its name and slide
// count, a click collapses or expands it (a view state, not saved), a
// double-click renames it, right-click shows the Section menu — and the
// rows come from ShowSections.rows; a slide dragged past a header joins
// that section (show_model's moveSlideInSections).
Tk.Scroll {
    id: sorter
    required property ShowHost showHost
    readonly property var rows: showHost.sections.rows
    // Double-click on a header: the window asks for the new name.
    signal renameSectionRequested(int section, string name)

    objectName: "slideSorter"

    // The slide row a pointer at `y` (in the column) is over: the slide
    // whose card spans it, or the first slide after the header there.
    function slideRowAt(y) {
        let last = -1
        for (let i = 0; i < thumbs.count; ++i) {
            const item = thumbs.itemAt(i)
            if (item === null) continue
            const slide = sorter.rows[i].header ? -1 : sorter.rows[i].slide
            if (y < item.y + item.height) {
                if (slide >= 0) return slide
                // A header: the slot just after the slides above it.
                return last + 1
            }
            if (slide >= 0) last = slide
        }
        return Math.max(0, last)
    }
    function findMenu(nodes, id) {
        for (let i = 0; i < nodes.length; ++i) {
            if (nodes[i].id === id) return nodes[i]
            const found = nodes[i].children ? findMenu(nodes[i].children, id) : null
            if (found !== null) return found
        }
        return null
    }

    Tk.Flex {
        id: column
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.sm
        Repeater {
            id: thumbs
            model: sorter.rows
            Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool header: modelData.header
                // Only what the row is gets made: a header or a slide card.
                implicitWidth: content.implicitWidth
                implicitHeight: content.implicitHeight
                Loader {
                    id: content
                    sourceComponent: row.header ? headerRow : slideCard
                }

                // ---- a section header ----
                Component {
                    id: headerRow
                    Tk.ListRow {
                        objectName: "sectionHeader_" + row.modelData.section
                        width: 176
                        text: row.modelData.name
                        tooltip: qsTr("Section %1: click shows its first slide, double-click renames it").arg(text)
                        leading: Tk.IconButton {
                            objectName: "sectionToggle_" + row.modelData.section
                            small: true
                            iconName: row.modelData.collapsed ? "chevron-right" : "chevron-down"
                            tooltip: row.modelData.collapsed ? qsTr("Expand the section") : qsTr("Collapse the section")
                            onClicked: sorter.showHost.sections.toggleCollapsed(row.modelData.section)
                        }
                        trailing: Tk.Badge { text: String(row.modelData.count) }
                        onClicked: if (row.modelData.first >= 0) sorter.showHost.currentSlide = row.modelData.first
                        onDoubleClicked: sorter.renameSectionRequested(row.modelData.section, row.modelData.name)
                        onRightClicked: function(point) {
                            if (row.modelData.first >= 0) sorter.showHost.currentSlide = row.modelData.first
                            sectionMenu.popup()
                        }
                    }
                }

                // ---- a slide ----
                Component {
                    id: slideCard
                    Tk.Card {
                        id: card
                        readonly property int slideRow: row.modelData.slide
                        objectName: "slideThumb_" + slideRow
                        interactive: true
                        selected: slideRow === sorter.showHost.currentSlide
                        padding: Tk.Theme.space.xs
                        implicitWidth: 176
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            SlideThumbItem {
                                objectName: "slideThumbImage_" + card.slideRow
                                host: sorter.showHost
                                slideIndex: card.slideRow
                                revision: sorter.showHost.deckRevision
                                implicitWidth: 160
                                implicitHeight: 90
                            }
                            Tk.Caption { text: (card.slideRow + 1) + ". " + sorter.showHost.slideTitle(card.slideRow); width: 160 }
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            property int dragTarget: -1
                            onPressed: function(mouse) {
                                sorter.showHost.currentSlide = card.slideRow
                                dragTarget = card.slideRow
                                if (mouse.button === Qt.RightButton) slideMenu.popup()
                            }
                            onPositionChanged: function(mouse) {
                                if (!(mouse.buttons & Qt.LeftButton)) return
                                const y = mapToItem(column, mouse.x, mouse.y).y
                                dragTarget = Math.min(sorter.showHost.slideCount - 1, sorter.slideRowAt(y))
                            }
                            onReleased: function(mouse) {
                                if (mouse.button === Qt.LeftButton && dragTarget >= 0 && dragTarget !== card.slideRow) {
                                    sorter.showHost.moveSlide(card.slideRow, dragTarget)
                                }
                                dragTarget = -1
                            }
                        }
                    }
                }
            }
        }
    }
    MenuFromNode {
        id: slideMenu
        objectName: "slideSorterMenu"
        actions: sorter.showHost.actions
        node: sorter.findMenu(sorter.showHost.menuTree.menus, "showSlideMenu")
    }
    MenuFromNode {
        id: sectionMenu
        objectName: "sectionHeaderMenu"
        actions: sorter.showHost.actions
        node: sorter.findMenu(sorter.showHost.menuTree.menus, "showSectionMenu")
    }
}
