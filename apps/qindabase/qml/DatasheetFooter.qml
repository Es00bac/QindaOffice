// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Base.App

// Under the datasheet grid: the totals row (a cell per column, aligned to
// the grid's own column geometry; click one to choose its function) and the
// record navigator — first / previous / "Record n of N" / next / last /
// new — with the filter state, like Access's record bar.
Tk.Flex {
    id: footer

    property DatasheetModel sheet: null
    property var grid: null
    property var window: null
    readonly property int current: grid !== null && grid.activeRowId !== "new" ? grid.activeRow : -1
    readonly property int count: sheet !== null ? sheet.recordCount : 0

    direction: Tk.Flex.Column
    gap: 0

    function goTo(row) {
        if (grid === null || grid.model === null || grid.model.rowCount === 0) return
        const column = Math.max(0, grid.activeColumn)
        grid.setCurrent(Math.max(0, Math.min(row, grid.model.rowCount - 1)), column, false)
        grid.ensureCurrentVisible()
        grid.forceActiveFocus()
    }

    // ---- totals row ----
    Item {
        objectName: "datasheetTotals"
        visible: footer.sheet !== null && footer.sheet.totalsVisible
        width: footer.width
        implicitHeight: Tk.Theme.size.row + Tk.Theme.size.border
        clip: true
        Rectangle { anchors.fill: parent; color: Tk.Theme.color.headerBg }
        Rectangle { width: parent.width; height: Tk.Theme.size.border; color: Tk.Theme.color.borderStrong }
        Tk.Label {
            x: Tk.Theme.space.xs
            width: footer.grid !== null ? footer.grid.rowHeaderWidth - Tk.Theme.space.xs : 0
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: "Σ"
            font.weight: Font.DemiBold
        }
        Repeater {
            model: footer.sheet !== null && footer.grid !== null && footer.grid.model !== null ? footer.sheet.columns : []
            delegate: Tk.Box {
                id: totalCell
                required property var modelData
                required property int index
                readonly property rect cell: footer.grid.dataEpoch >= 0
                                             ? footer.grid.model.cellRect(0, index, footer.grid.scrollX, 0) : Qt.rect(0, 0, 0, 0)
                readonly property var total: footer.sheet.totals[modelData.name]
                objectName: "datasheetTotal_" + modelData.name
                x: footer.grid.rowHeaderWidth + cell.x
                y: Tk.Theme.size.border
                width: cell.width
                height: Tk.Theme.size.row
                interactive: true
                tooltip: qsTr("Total for %1").arg(modelData.label)
                paddingLeft: Tk.Theme.space.sm
                paddingRight: Tk.Theme.space.sm
                onClicked: totalMenu.openFor(modelData, totalCell)
                Tk.Label {
                    width: parent.width
                    horizontalAlignment: totalCell.modelData.numeric && !totalCell.modelData.lookup ? Text.AlignRight : Text.AlignLeft
                    text: totalCell.total === undefined ? "" : (totalCell.total.function === "count" ? qsTr("Count ") : "")
                                                               + totalCell.total.text
                    font.weight: Font.DemiBold
                    mono: totalCell.modelData.numeric
                }
            }
        }
    }

    // ---- record navigator ----
    Tk.Box {
        objectName: "recordNavigator"
        width: footer.width
        color: Tk.Theme.color.surface
        borderTop: Tk.Theme.size.border
        borderColor: Tk.Theme.color.divider
        paddingLeft: Tk.Theme.space.sm
        paddingRight: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Record"); Tk.Flex.shrink: 0 }
            Tk.IconButton { objectName: "recordFirst"; iconName: "chevrons-left"; small: true; tooltip: qsTr("First record"); enabled: footer.count > 0; onClicked: footer.goTo(0) }
            Tk.IconButton { objectName: "recordPrevious"; iconName: "chevron-left"; small: true; tooltip: qsTr("Previous record"); enabled: footer.current > 0; onClicked: footer.goTo(footer.current - 1) }
            Tk.Label {
                objectName: "recordPosition"
                text: footer.current >= 0 ? qsTr("%1 of %2").arg(footer.current + 1).arg(footer.count)
                                          : (footer.grid !== null && footer.grid.activeRowId === "new" ? qsTr("New of %1").arg(footer.count)
                                                                                                       : qsTr("%n record(s)", "", footer.count))
                mono: true
                Tk.Flex.shrink: 0
            }
            Tk.IconButton { objectName: "recordNext"; iconName: "chevron-right"; small: true; tooltip: qsTr("Next record"); enabled: footer.current + 1 < footer.count; onClicked: footer.goTo(footer.current + 1) }
            Tk.IconButton { objectName: "recordLast"; iconName: "chevrons-right"; small: true; tooltip: qsTr("Last record"); enabled: footer.count > 0; onClicked: footer.goTo(footer.count - 1) }
            Tk.IconButton {
                objectName: "recordNew"
                iconName: "list-plus"
                small: true
                tooltip: qsTr("New record")
                enabled: footer.sheet !== null && footer.sheet.editable
                onClicked: footer.window.trigger("baseNewRecordAction")
            }
            Tk.Divider { vertical: true; height: Tk.Theme.size.iconSm }
            Tk.Caption {
                text: footer.sheet === null ? "" : footer.sheet.filters.length > 0 ? qsTr("Filtered")
                                                  : footer.sheet.truncated ? qsTr("First %1 of %2").arg(footer.count).arg(footer.sheet.totalRows)
                                                  : qsTr("No filter")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Caption {
                visible: footer.grid !== null && footer.grid.activeColumnId.length > 0 && footer.sheet !== null
                text: footer.sheet !== null && footer.grid.activeColumnId.length > 0
                      ? (footer.sheet.columns.find(function(c) { return c.name === footer.grid.activeColumnId }) || { typeLabel: "" }).typeLabel : ""
                Tk.Flex.shrink: 0
            }
        }
    }

    Tk.Menu {
        id: totalMenu
        property var column: ({})
        function openFor(column, anchor) {
            totalMenu.column = column
            totalMenu.popup(anchor, 0, -totalMenu.implicitHeight)
        }
        Tk.MenuItem { text: qsTr("None"); onTriggered: footer.sheet.setTotal(totalMenu.column.name, "") }
        Tk.MenuItem { text: qsTr("Sum"); enabled: totalMenu.column.numeric === true; onTriggered: footer.sheet.setTotal(totalMenu.column.name, "sum") }
        Tk.MenuItem { text: qsTr("Average"); enabled: totalMenu.column.numeric === true; onTriggered: footer.sheet.setTotal(totalMenu.column.name, "average") }
        Tk.MenuItem { text: qsTr("Count"); onTriggered: footer.sheet.setTotal(totalMenu.column.name, "count") }
        Tk.MenuItem { text: qsTr("Minimum"); onTriggered: footer.sheet.setTotal(totalMenu.column.name, "min") }
        Tk.MenuItem { text: qsTr("Maximum"); onTriggered: footer.sheet.setTotal(totalMenu.column.name, "max") }
    }
}
