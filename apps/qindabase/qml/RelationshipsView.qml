// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Database Tools ▸ Relationships: every table as a card on QindaTK's
// DiagramCanvas (key fields are outputs, the other fields inputs) and every
// relationship as a wire from a key to the field that points at it. Drag
// from a key to a field to relate them, delete a wire to end a
// relationship — both go through the relationship dialog / plan preview,
// then the diagram is rebuilt from the database. The list beside it says
// the same in words.
Item {
    id: view

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var relationships: []
    property var ports: ({})      // "nodeId/portId" -> {table, field}
    property var known: []        // edge ids the database holds
    property var edgeRelation: ({}) // edge id -> relationship
    property bool building: false
    property int selected: -1
    readonly property bool wide: width > Tk.Theme.size.panelMinWidth * 3

    function actionText(id) {
        return ({ cascade: qsTr("deleting cascades"), setNull: qsTr("deleting leaves them unlinked"),
                  restrict: qsTr("deleting is refused"), noAction: qsTr("deleting is refused") })[id] || ""
    }
    function reload() {
        if (base === null) return
        building = true
        diagram.removeNodes(diagram.nodes.map(function(n) { return n.nodeId }))
        const nodeOf = ({}), portOf = ({}), map = ({})
        const tables = base.tableNames
        for (let t = 0; t < tables.length; ++t) {
            const node = diagram.addNode(tables[t], 0, 0, "table")
            nodeOf[tables[t]] = node
            for (const f of base.tableFields(tables[t])) {
                const key = f.primaryKey || f.unique
                const port = diagram.addPort(node, (key ? "◆ " : "") + f.name, key ? "output" : "input", "any")
                portOf[tables[t] + "." + f.name] = port
                map[node + "/" + port] = { table: tables[t], field: f.name }
            }
        }
        relationships = base.relationships()
        const byEdge = ({})
        for (const r of relationships) {
            const from = portOf[r.parentTable + "." + r.parentField]
            const to = portOf[r.childTable + "." + r.childField]
            if (from && to) byEdge[diagram.connectPorts(nodeOf[r.parentTable], from, nodeOf[r.childTable], to)] = r
        }
        edgeRelation = byEdge
        ports = map
        canvas.autoLayout()
        known = diagram.edges.map(function(e) { return e.edgeId })
        building = false
    }
    function endpoint(nodeId, portId) { return ports[nodeId + "/" + portId] || null }
    // A wire drawn or deleted on the canvas becomes a relationship request.
    function reconcile() {
        if (building) return
        const edges = diagram.edges
        const added = edges.filter(function(e) { return known.indexOf(e.edgeId) < 0 })
        if (added.length > 0) {
            const e = added[0]
            const parent = endpoint(e.fromNodeId, e.fromPortId), child = endpoint(e.toNodeId, e.toPortId)
            Qt.callLater(reload)
            if (parent && child) {
                window.relationshipDialog.openFor({ childTable: child.table, childField: child.field, parentTable: parent.table,
                                                    parentField: parent.field, onDelete: "noAction", onUpdate: "cascade" },
                                                  function() { reload() })
            }
            return
        }
        const ids = edges.map(function(e) { return e.edgeId })
        const gone = known.filter(function(id) { return ids.indexOf(id) < 0 })
        if (gone.length > 0) {
            const r = edgeRelation[gone[0]]
            Qt.callLater(reload)
            if (r) removeRelationship(r)
        }
    }
    function removeRelationship(r) {
        const plan = base.planRelationship(r, false)
        if (!plan.ok) { window.notify(qsTr("Cannot remove"), plan.error, "danger"); return }
        window.planDialog.show(plan, function() {
            const done = base.applyRelationship(r, false)
            if (!done.ok) window.notify(qsTr("Not removed"), done.error, "danger")
            reload()
        })
    }
    onVisibleChanged: if (visible) reload()

    Tk.DiagramDocument {
        id: diagram
        onChanged: view.reconcile()
    }

    Tk.Flex {
        anchors.fill: parent
        direction: view.wide ? Tk.Flex.Row : Tk.Flex.Column
        gap: 0
        Tk.DiagramCanvas {
            id: canvas
            objectName: "relationshipsCanvas"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            document: diagram
            inspectorVisible: false
            tooltip: qsTr("Relationships between tables")
        }
        Tk.Divider { vertical: view.wide }
        Tk.Box {
            objectName: "relationshipsList"
            Tk.Flex.shrink: 0
            Tk.Flex.basis: view.wide ? Tk.Theme.size.panelMinWidth * 1.4 : view.height * 0.38
            color: Tk.Theme.color.surface
            padding: Tk.Theme.space.md
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    Tk.Overline { title: qsTr("RELATIONSHIPS"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                    Tk.Button {
                        objectName: "relationshipAdd"
                        text: qsTr("Add…")
                        iconName: "plus"
                        small: true
                        onClicked: view.window.relationshipDialog.openFor({}, function() { view.reload() })
                    }
                }
                Tk.Caption {
                    wrapMode: Text.Wrap
                    text: qsTr("Drag from a key (◆) to the field that refers to it. Each relationship keeps records consistent: no record points at a missing one.")
                }
                Repeater {
                    model: view.relationships
                    delegate: Tk.ListRow {
                        id: relationRow
                        required property var modelData
                        required property int index
                        objectName: "relationship_" + index
                        text: modelData.childTable + "." + modelData.childField + " → " + modelData.parentTable
                        secondaryText: view.actionText(modelData.onDelete)
                        iconName: "git-branch"
                        selected: view.selected === index
                        tooltip: modelData.label
                        onClicked: view.selected = index
                        trailing: Tk.IconButton {
                            iconName: "trash-2"
                            small: true
                            tooltip: qsTr("Remove this relationship")
                            onClicked: view.removeRelationship(relationRow.modelData)
                        }
                    }
                }
                Tk.EmptyState {
                    visible: view.relationships.length === 0
                    iconName: "git-branch"
                    title: qsTr("No relationships yet")
                    text: qsTr("Relate tables so a donation always belongs to a donor, a task to a project.")
                }
            }
        }
    }
}
