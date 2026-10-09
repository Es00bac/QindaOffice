// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// The PivotTable Fields pane (M74, D-083), Excel's field list: every source
// field with its checkbox (checking adds it where Excel would — numbers to
// Values, the rest to Rows) and a search, then the four areas — Filters,
// Columns, Rows, Values — that fields are dragged into, between and out of.
// Clicking an area entry opens Excel's menu for it: Move Up / Down / to
// Beginning / to End, Move to Report Filter / Row Labels / Column Labels /
// Values, Remove Field, Field Settings or Value Field Settings.
//
// AGENT-NOTE: QindaTK has no field-well control (D-083 lists the gap), so
// the drag is QtQuick's own: a DragHandler on each field and entry moves a
// ghost chip whose Drag reaches one DropArea per area (and the field list,
// where a drop removes the field, as does a drop outside every area).
// place(), addField() and moveEntry() are the one path the drags, the
// menus and the tests share; every change is one undo step in the host.
Tk.Flex {
    id: pane

    required property CalcHost host
    readonly property var model: host.pivotPane
    readonly property string dragKey: "qindacalc/pivot-field"
    // The drag under way: {field, caption, fromArea ("" from the list), fromPosition}.
    property var dragging: null
    // Where the drop landed: {area, position}; set by a DropArea during drop().
    property var dropTarget: null

    objectName: "pivotFieldsPane"
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    // ---- the one path every change takes ----
    function addField(field, area, position) {
        pane.host.pivotAddField(field, area, position)
    }
    // Moves an entry to its final index in `toArea` (-1: the end).
    function moveEntry(fromArea, fromPosition, toArea, toPosition) {
        if (fromArea === toArea && fromPosition === toPosition) return
        pane.host.pivotMoveField(fromArea, fromPosition, toArea, toPosition)
    }
    // A drop of `drag` before entry `position` of `area` (-1: after the
    // last); "list" takes a field out of the report.
    function place(drag, area, position) {
        if (area === "list") {
            if (drag.fromArea !== "") pane.host.pivotRemoveField(drag.fromArea, drag.fromPosition)
            return
        }
        if (drag.fromArea === "") {
            pane.addField(drag.field, area, position)
            return
        }
        let to = position
        if (drag.fromArea === area && to > drag.fromPosition) to -= 1 // the entry leaves its old place first
        pane.moveEntry(drag.fromArea, drag.fromPosition, area, to)
    }

    // ---- the drag ----
    function beginDrag(source, field, caption, fromArea, fromPosition, point) {
        pane.dragging = { field: field, caption: caption, fromArea: fromArea, fromPosition: fromPosition }
        pane.moveGhost(source, point)
    }
    function moveGhost(source, point) {
        const p = source.mapToItem(pane, point.x, point.y)
        ghost.x = p.x - ghost.width / 2
        ghost.y = p.y - ghost.height / 2
    }
    // AGENT-GUARD: the DropAreas only record where the drop landed; the
    // change happens here, after Drag.drop() returned — changing the
    // pane's model inside the drop event would destroy the drag's source.
    function endDrag() {
        if (pane.dragging === null) return
        pane.dropTarget = null
        ghost.Drag.drop()
        const drag = pane.dragging
        const target = pane.dropTarget
        pane.dragging = null
        pane.dropTarget = null
        if (target !== null) pane.place(drag, target.area, target.position)
        else pane.place(drag, "list", -1) // out of every area: Excel removes the field
    }

    // ---- the field list ----
    Tk.Notice {
        objectName: "pivotFieldsReadOnly"
        visible: pane.model.readOnly === true
        variant: "warning"
        title: qsTr("PivotTable “%1” is read-only").arg(pane.model.name || "")
        text: qsTr("It %1; QindaCalc keeps it exactly as it is and saves it unchanged.").arg(pane.model.note || "")
    }
    Tk.Caption {
        objectName: "pivotFieldsEmpty"
        visible: pane.model.active !== true && pane.model.readOnly !== true
        text: qsTr("To show the PivotTable's fields, click in a PivotTable.")
        wrapMode: Text.Wrap
    }
    Tk.Caption {
        visible: pane.model.active === true
        text: qsTr("Choose fields to add to report:")
    }
    Tk.SearchField {
        id: search
        objectName: "pivotFieldSearch"
        visible: pane.model.active === true
        tooltip: qsTr("Find a field by its name")
    }
    Tk.Box {
        objectName: "pivotFieldList"
        visible: pane.model.active === true
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: Tk.Theme.size.row * 3
        color: listDrop.containsDrag ? Tk.Theme.color.dropZone : Tk.Theme.color.inputBg
        borderWidth: Tk.Theme.size.border
        borderColor: Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
        Item {
            Tk.Scroll {
                anchors.fill: parent
                padding: Tk.Theme.space.xs
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: 0
                    Repeater {
                        model: (pane.model.fields || []).filter(function(f) {
                            return search.text.length === 0 || f.name.toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                        })
                        Tk.CheckBox {
                            id: fieldBox
                            required property var modelData
                            objectName: "pivotField_" + modelData.index
                            text: modelData.name
                            checked: modelData.checked
                            tooltip: qsTr("Check to add the field to the report, or drag it to an area")
                            onToggled: pane.host.pivotToggleField(fieldBox.modelData.index, fieldBox.checked)
                            DragHandler {
                                target: null
                                onActiveChanged: active ? pane.beginDrag(fieldBox, fieldBox.modelData.index, fieldBox.modelData.name,
                                                                         "", -1, centroid.position)
                                                        : pane.endDrag()
                                onCentroidChanged: if (active) pane.moveGhost(fieldBox, centroid.position)
                            }
                        }
                    }
                }
            }
            DropArea {
                id: listDrop
                anchors.fill: parent
                keys: [pane.dragKey]
                onDropped: function(drop) {
                    pane.dropTarget = { area: "list", position: -1 }
                    drop.accept()
                }
            }
        }
    }

    // ---- the four areas ----
    Tk.Caption {
        visible: pane.model.active === true
        text: qsTr("Drag fields between areas below:")
    }
    Tk.Grid {
        objectName: "pivotAreas"
        visible: pane.model.active === true
        columns: "1fr 1fr"
        columnGap: Tk.Theme.space.sm
        rowGap: Tk.Theme.space.sm
        AreaWell { area: "filters"; title: qsTr("Filters") }
        AreaWell { area: "columns"; title: qsTr("Columns") }
        AreaWell { area: "rows"; title: qsTr("Rows") }
        AreaWell { area: "values"; title: qsTr("Values") }
    }

    // The dragged field's chip; QtQuick's Drag follows it into the areas.
    Tk.Box {
        id: ghost
        objectName: "pivotDragGhost"
        Tk.Flex.ignore: true
        visible: pane.dragging !== null
        z: 1
        color: Tk.Theme.color.accentSubtle
        borderWidth: Tk.Theme.size.border
        borderColor: Tk.Theme.color.accent
        radius: Tk.Theme.radius.sm
        padding: Tk.Theme.space.xs
        Drag.active: pane.dragging !== null
        Drag.keys: [pane.dragKey]
        Drag.hotSpot.x: ghost.width / 2
        Drag.hotSpot.y: ghost.height / 2
        Tk.Label { text: pane.dragging !== null ? pane.dragging.caption : "" }
    }

    // One area: its title, its entries in order, a drop anywhere on it.
    component AreaWell: Item {
        id: well

        required property string area
        required property string title
        readonly property var entries: pane.model[well.area] || []

        objectName: "pivotArea_" + area
        implicitWidth: box.implicitWidth
        implicitHeight: Math.max(box.implicitHeight, Tk.Theme.size.row * 5)

        // The entry a drop at `y` goes before (-1: after the last).
        function positionAt(y) {
            for (let i = 0; i < rows.count; ++i) {
                const row = rows.itemAt(i)
                if (row !== null && y < row.mapToItem(well, 0, row.height / 2).y) return i
            }
            return -1
        }

        Tk.Box {
            id: box
            anchors.fill: parent
            color: drop.containsDrag ? Tk.Theme.color.dropZone : Tk.Theme.color.inputBg
            borderWidth: Tk.Theme.size.border
            borderColor: drop.containsDrag ? Tk.Theme.color.accent : Tk.Theme.color.border
            radius: Tk.Theme.radius.sm
            padding: Tk.Theme.space.xs
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Tk.Overline { title: well.title }
                Repeater {
                    id: rows
                    model: well.entries
                    Tk.ListRow {
                        id: entry
                        required property var modelData
                        required property int index
                        objectName: "pivotAreaRow_" + well.area + "_" + index
                        small: true
                        text: modelData.caption
                        tooltip: qsTr("Click for the field's commands, or drag it to another place")
                        onClicked: entryMenu.openFor(well.area, entry.index, entry.modelData.field, well.entries.length, entry)
                        onRightClicked: entryMenu.openFor(well.area, entry.index, entry.modelData.field, well.entries.length, entry)
                        DragHandler {
                            target: null
                            onActiveChanged: active ? pane.beginDrag(entry, entry.modelData.field, entry.modelData.caption,
                                                                     well.area, entry.index, centroid.position)
                                                    : pane.endDrag()
                            onCentroidChanged: if (active) pane.moveGhost(entry, centroid.position)
                        }
                    }
                }
            }
        }
        DropArea {
            id: drop
            anchors.fill: parent
            keys: [pane.dragKey]
            onDropped: function(event) {
                pane.dropTarget = { area: well.area, position: well.positionAt(event.y) }
                event.accept()
            }
        }
    }

    // Excel's menu for an area entry; Σ Values moves between Rows and
    // Columns only and goes with the data fields.
    Tk.Menu {
        id: entryMenu

        // AGENT-GUARD: not `count` / `position`: T.Menu's own (FINAL) names.
        property string entryArea: ""
        property int entryPosition: -1
        property int entryField: -1
        property int entryCount: 0
        readonly property bool sigma: entryField === -2

        function openFor(area, position, field, count, item) {
            entryMenu.entryArea = area
            entryMenu.entryPosition = position
            entryMenu.entryField = field
            entryMenu.entryCount = count
            entryMenu.popup(item, 0, item.height)
        }

        objectName: "pivotAreaMenu"
        Tk.MenuItem {
            objectName: "pivotAreaMoveUp"; text: qsTr("Move &Up"); enabled: entryMenu.entryPosition > 0
            onTriggered: pane.moveEntry(entryMenu.entryArea, entryMenu.entryPosition, entryMenu.entryArea, entryMenu.entryPosition - 1)
        }
        Tk.MenuItem {
            objectName: "pivotAreaMoveDown"; text: qsTr("Move &Down"); enabled: entryMenu.entryPosition < entryMenu.entryCount - 1
            onTriggered: pane.moveEntry(entryMenu.entryArea, entryMenu.entryPosition, entryMenu.entryArea, entryMenu.entryPosition + 1)
        }
        Tk.MenuItem {
            objectName: "pivotAreaMoveFirst"; text: qsTr("Move to &Beginning"); enabled: entryMenu.entryPosition > 0
            onTriggered: pane.moveEntry(entryMenu.entryArea, entryMenu.entryPosition, entryMenu.entryArea, 0)
        }
        Tk.MenuItem {
            objectName: "pivotAreaMoveLast"; text: qsTr("Move to &End"); enabled: entryMenu.entryPosition < entryMenu.entryCount - 1
            onTriggered: pane.moveEntry(entryMenu.entryArea, entryMenu.entryPosition, entryMenu.entryArea, -1)
        }
        Tk.MenuSeparator { }
        Tk.MenuItem {
            objectName: "pivotAreaToFilters"; text: qsTr("Move to Report &Filter")
            enabled: entryMenu.entryArea !== "filters" && !entryMenu.sigma
            onTriggered: pane.moveEntry(entryMenu.entryArea, entryMenu.entryPosition, "filters", -1)
        }
        Tk.MenuItem {
            objectName: "pivotAreaToRows"; text: qsTr("Move to &Row Labels"); enabled: entryMenu.entryArea !== "rows"
            onTriggered: pane.moveEntry(entryMenu.entryArea, entryMenu.entryPosition, "rows", -1)
        }
        Tk.MenuItem {
            objectName: "pivotAreaToColumns"; text: qsTr("Move to &Column Labels"); enabled: entryMenu.entryArea !== "columns"
            onTriggered: pane.moveEntry(entryMenu.entryArea, entryMenu.entryPosition, "columns", -1)
        }
        Tk.MenuItem {
            objectName: "pivotAreaToValues"; text: qsTr("Move to &Values")
            enabled: entryMenu.entryArea !== "values" && !entryMenu.sigma
            onTriggered: pane.moveEntry(entryMenu.entryArea, entryMenu.entryPosition, "values", -1)
        }
        Tk.MenuSeparator { }
        Tk.MenuItem {
            objectName: "pivotAreaRemove"; text: qsTr("Remove Field"); enabled: !entryMenu.sigma
            onTriggered: pane.host.pivotRemoveField(entryMenu.entryArea, entryMenu.entryPosition)
        }
        Tk.MenuSeparator { }
        Tk.MenuItem {
            objectName: "pivotAreaSettings"
            text: entryMenu.entryArea === "values" ? qsTr("Value Field Settings…") : qsTr("Field Settings…")
            enabled: !entryMenu.sigma
            onTriggered: {
                if (entryMenu.entryArea === "values") pane.host.pivotValueFieldRequested(entryMenu.entryPosition)
                else pane.host.pivotFieldRequested(entryMenu.entryField)
            }
        }
    }
}
