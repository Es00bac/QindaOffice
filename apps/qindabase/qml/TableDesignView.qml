// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The table designer: the field grid (key, name, type, description), the
// selected field's properties and relationship, the table's own
// properties (M80: the record's validation rule), and — always visible — the
// change it amounts to: what it does, what it risks, and the exact SQL,
// previewed against the real data before anything runs. Apply runs it as
// ONE undoable step (a backup first when it drops or narrows data).
// Designer edits have their own undo while the view is open.
Item {
    id: view

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var design: ({ name: "", fields: [], relationships: [] })
    property string original: "{}"
    property int current: 0
    property var history: []
    property var future: []
    property var plan: ({})
    readonly property bool dirty: JSON.stringify(design) !== original
    readonly property bool isNew: !design.originalName
    readonly property bool wide: width > Tk.Theme.size.panelMinWidth * 2.8
    readonly property var field: (design.fields || [])[current] || ({})

    function load(map) {
        if (!map || map.ok === false) { window.notify(qsTr("Cannot design"), map ? map.error : "", "danger"); return }
        design = map
        original = JSON.stringify(map)
        current = Math.min(1, (map.fields || []).length - 1)
        history = []
        future = []
        plan = ({})
        syncHost()
        replan.restart()
    }
    function syncHost() {
        if (window === null || window.baseHost === null || window.view !== "tableDesign") return
        window.baseHost.designDirty = dirty
        window.baseHost.designCanUndo = history.length > 0
        window.baseHost.designCanRedo = future.length > 0
    }
    onDirtyChanged: syncHost()
    onVisibleChanged: if (visible) syncHost()

    function edit(mutate) {
        const next = JSON.parse(JSON.stringify(design))
        mutate(next)
        history = history.concat([JSON.stringify(design)]).slice(-100)
        future = []
        design = next
        syncHost()
        replan.restart()
    }
    function undo() {
        if (history.length === 0) return
        future = future.concat([JSON.stringify(design)])
        design = JSON.parse(history[history.length - 1])
        history = history.slice(0, -1)
        syncHost()
        replan.restart()
    }
    function redo() {
        if (future.length === 0) return
        history = history.concat([JSON.stringify(design)])
        design = JSON.parse(future[future.length - 1])
        future = future.slice(0, -1)
        syncHost()
        replan.restart()
    }
    function setField(key, value) {
        const i = current
        edit(function(d) {
            const f = d.fields[i]
            if (key === "name") {
                for (const r of d.relationships) if (r.childField === f.name) r.childField = value
            }
            f[key] = value
            if (key === "type" && value === "autoNumber") { f.primaryKey = true; f.required = false }
        })
    }
    function setRelation(rel) {
        const name = field.name
        edit(function(d) {
            d.relationships = d.relationships.filter(function(r) { return r.childField !== name })
            if (rel !== null) d.relationships.push(rel)
        })
    }
    // M80: the table's own properties (its record validation rule and text).
    function setTableProperty(key, value) {
        if ((design[key] || "") === value) return
        edit(function(d) { d[key] = value })
    }
    function relationFor(name) {
        return (design.relationships || []).find(function(r) { return r.childField === name }) || null
    }
    function command(id) {
        switch (id) {
        case "baseDesignAddFieldAction":
            edit(function(d) {
                let n = d.fields.length + 1
                while (d.fields.some(function(f) { return f.name === qsTr("Field%1").arg(n) })) ++n
                d.fields.push({ name: qsTr("Field%1").arg(n), originalName: "", type: "text", primaryKey: false, required: false,
                                defaultValue: "", unique: false, indexed: false, caption: "", description: "",
                                lookupTable: "", lookupField: "", lookupDisplay: "",
                                validationRule: "", validationText: "", inputMask: "" })
            })
            current = design.fields.length - 1
            break
        case "baseDesignDeleteFieldAction":
            if (design.fields.length <= 1) break
            const gone = field.name
            edit(function(d) {
                d.fields.splice(view.current, 1)
                d.relationships = d.relationships.filter(function(r) { return r.childField !== gone })
            })
            current = Math.min(current, design.fields.length - 1)
            break
        case "baseDesignMoveUpAction":
        case "baseDesignMoveDownAction":
            const to = current + (id === "baseDesignMoveUpAction" ? -1 : 1)
            if (to < 0 || to >= design.fields.length) break
            const from = current
            edit(function(d) { const f = d.fields.splice(from, 1)[0]; d.fields.splice(to, 0, f) })
            current = to
            break
        case "baseDesignPrimaryKeyAction":
            const i = current
            edit(function(d) {
                const on = !d.fields[i].primaryKey
                for (let k = 0; k < d.fields.length; ++k) if (d.fields[k].type !== "autoNumber" || k === i) d.fields[k].primaryKey = k === i ? on : false
            })
            break
        case "baseDesignApplyAction": apply(); break
        case "baseDesignDiscardAction": discard(); break
        case "baseDesignTestRulesAction": testRules(); break
        }
    }
    function discard() { load(JSON.parse(original)) }
    // Test Validation Rules (M80): the saved table's records against its rules.
    function testRules() {
        if (dirty) {
            window.notify(qsTr("Apply the design first"), qsTr("Rules are tested against the records of the saved table."), "info")
            return
        }
        const r = base.testValidationRules(design.name)
        if (!r.ok) { window.notify(qsTr("Rules not tested"), r.error, "danger"); return }
        window.confirmDialog.ask(qsTr("Test validation rules"), r.lines.join("\n"), r.broken > 0 ? "warning" : "info", qsTr("OK"), null)
    }
    // Apply through the plan dialog (the ribbon's Apply).
    function apply() {
        const p = base.planTableDesign(design)
        plan = p
        if (p.needsSystemTables) {
            window.confirmDialog.ask(qsTr("Add QindaBase's tables?"), p.error, "question", qsTr("Add them"), function() {
                base.addSystemTables(); apply()
            })
            return
        }
        if (!p.ok) { window.notify(qsTr("This design cannot be applied"), p.error, "danger"); return }
        if (p.empty) { window.notify(qsTr("Nothing to change"), qsTr("The table already looks like this."), "info"); return }
        window.planDialog.show(p, function() { save() })
    }
    // Apply directly (Save, or leaving the designer with "Save").
    function save() {
        if (!dirty) return true
        const r = base.applyTableDesign(design)
        if (!r.ok) { window.notify(qsTr("Not applied"), r.error, "danger"); return false }
        const fresh = base.tableDesign(r.name)
        load(fresh)
        window.baseHost.currentName = r.name
        window.baseHost.selectedKind = "table"
        window.baseHost.selectedName = r.name
        return true
    }

    Timer {
        id: replan
        interval: Tk.Theme.motion.slow
        onTriggered: view.plan = view.dirty && view.base !== null ? view.base.planTableDesign(view.design) : ({})
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Box {
            objectName: "tableDesignHeader"
            Tk.Flex.shrink: 0
            color: Tk.Theme.color.surface
            padding: Tk.Theme.space.sm
            paddingLeft: Tk.Theme.space.md
            borderBottom: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Icon { name: "table-2"; size: Tk.Theme.size.icon; color: Tk.Theme.color.accent }
                Tk.Label { text: qsTr("Table"); muted: true; Tk.Flex.shrink: 0 }
                Tk.TextField {
                    objectName: "tableDesignName"
                    text: view.design.name || ""
                    tooltip: qsTr("Table name")
                    Tk.Flex.basis: Tk.Theme.size.fieldWidth
                    onEditingFinished: if (text !== view.design.name) view.edit(function(d) { d.name = text.trim() })
                }
                Tk.Badge { visible: view.isNew; text: qsTr("new") }
                Tk.Badge { visible: view.dirty && !view.isNew; text: qsTr("changed"); variant: "warning" }
                Tk.Spacer { }
                Tk.Button { text: qsTr("Discard"); visible: view.dirty; onClicked: view.discard() }
                Tk.Button {
                    objectName: "tableDesignApply"
                    text: view.isNew ? qsTr("Create table") : qsTr("Apply changes")
                    variant: "accent"
                    iconName: "check-check"
                    enabled: view.dirty
                    onClicked: view.apply()
                }
            }
        }
        Tk.Scroll {
            id: body
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            accessibleName: qsTr("Table design")
            Tk.Flex {
                width: body.width
                direction: view.wide ? Tk.Flex.Row : Tk.Flex.Column
                gap: Tk.Theme.space.lg
                padding: Tk.Theme.space.md
                align: Tk.Flex.Start
                Tk.Flex {
                    // Side by side when wide (shares by 3:2); stacked at their
                    // own heights when narrow (basis auto, no grow).
                    Tk.Flex.grow: view.wide ? 3 : 0
                    Tk.Flex.basis: view.wide ? 0 : -1
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    Tk.Card {
                        objectName: "fieldGrid"
                        padding: 0
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: 0
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                gap: Tk.Theme.space.sm
                                padding: Tk.Theme.space.sm
                                Item { width: Tk.Theme.size.control; height: Tk.Theme.size.border }
                                Tk.Overline { title: qsTr("FIELD NAME"); Tk.Flex.grow: 2; Tk.Flex.basis: 0 }
                                Tk.Overline { title: qsTr("DATA TYPE"); Tk.Flex.grow: 1.4; Tk.Flex.basis: 0 }
                                Tk.Overline { visible: view.width > Tk.Theme.size.panelMinWidth * 2; title: qsTr("DESCRIPTION"); Tk.Flex.grow: 2; Tk.Flex.basis: 0 }
                            }
                            Repeater {
                                model: view.design.fields || []
                                delegate: Tk.Box {
                                    id: fieldRow
                                    required property var modelData
                                    required property int index
                                    objectName: "designField_" + modelData.name
                                    color: fieldRow.index === view.current ? Tk.Theme.color.selection : Tk.Theme.color.panelAlt
                                    borderTop: Tk.Theme.size.border
                                    borderColor: Tk.Theme.color.divider
                                    paddingLeft: Tk.Theme.space.sm
                                    paddingRight: Tk.Theme.space.sm
                                    interactive: true
                                    tooltip: modelData.name
                                    onClicked: view.current = fieldRow.index
                                    Tk.Flex {
                                        direction: Tk.Flex.Row
                                        align: Tk.Flex.Center
                                        gap: Tk.Theme.space.sm
                                        Tk.IconButton {
                                            objectName: "designKey_" + fieldRow.modelData.name
                                            iconName: fieldRow.modelData.primaryKey ? "key-round" : (view.relationFor(fieldRow.modelData.name) !== null ? "link" : "dot")
                                            small: true
                                            tooltip: fieldRow.modelData.primaryKey ? qsTr("Primary key") : view.relationFor(fieldRow.modelData.name) !== null ? qsTr("Related to %1").arg(view.relationFor(fieldRow.modelData.name).parentTable) : qsTr("Make the primary key")
                                            onClicked: { view.current = fieldRow.index; view.command("baseDesignPrimaryKeyAction") }
                                        }
                                        Tk.TextField {
                                            text: fieldRow.modelData.name
                                            small: true
                                            mono: true
                                            Tk.Flex.grow: 2
                                            Tk.Flex.basis: 0
                                            onActiveFocusChanged: if (activeFocus) view.current = fieldRow.index
                                            onEditingFinished: if (text.trim() !== fieldRow.modelData.name) { view.current = fieldRow.index; view.setField("name", text.trim()) }
                                        }
                                        Tk.ComboBox {
                                            small: true
                                            textRole: "label"
                                            model: view.base !== null ? view.base.fieldTypes : []
                                            currentIndex: model.findIndex(function(t) { return t.id === fieldRow.modelData.type })
                                            Tk.Flex.grow: 1.4
                                            Tk.Flex.basis: 0
                                            tooltip: qsTr("Data type")
                                            onActivated: function(index) { view.current = fieldRow.index; view.setField("type", model[index].id) }
                                        }
                                        Tk.TextField {
                                            visible: view.width > Tk.Theme.size.panelMinWidth * 2
                                            text: fieldRow.modelData.description || ""
                                            small: true
                                            placeholderText: qsTr("What it holds")
                                            Tk.Flex.grow: 2
                                            Tk.Flex.basis: 0
                                            onEditingFinished: if (text !== (fieldRow.modelData.description || "")) { view.current = fieldRow.index; view.setField("description", text) }
                                        }
                                    }
                                }
                            }
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                gap: Tk.Theme.space.sm
                                padding: Tk.Theme.space.sm
                                Tk.Button { objectName: "designAddField"; text: qsTr("Add field"); iconName: "plus"; small: true; onClicked: view.command("baseDesignAddFieldAction") }
                                Tk.Button { text: qsTr("Delete field"); iconName: "minus"; small: true; enabled: (view.design.fields || []).length > 1; onClicked: view.command("baseDesignDeleteFieldAction") }
                            }
                        }
                    }
                    Tk.Card {
                        FieldProperties {
                            base: view.base
                            field: view.field
                            tableName: view.design.name || ""
                            relationship: view.relationFor(view.field.name || "")
                            onEdited: function(key, value) { view.setField(key, value) }
                            onRelationEdited: function(rel) { view.setRelation(rel) }
                        }
                    }
                    Tk.Card {
                        objectName: "tableProperties"
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            Tk.SectionHeader { title: qsTr("Table properties") }
                            Tk.PropertyRow {
                                label: qsTr("Validation rule")
                                hint: qsTr("Compares the fields of a record: [ends] >= [starts]")
                                Tk.TextField {
                                    objectName: "tableValidationRule"
                                    text: view.design.validationRule || ""
                                    mono: true
                                    placeholderText: "[ends] >= [starts]"
                                    onEditingFinished: view.setTableProperty("validationRule", text)
                                }
                            }
                            Tk.PropertyRow {
                                label: qsTr("Validation text")
                                hint: qsTr("The message shown when a record breaks the rule")
                                Tk.TextField {
                                    objectName: "tableValidationText"
                                    text: view.design.validationText || ""
                                    onEditingFinished: view.setTableProperty("validationText", text)
                                }
                            }
                        }
                    }
                }
                Tk.Card {
                    objectName: "designChanges"
                    Tk.Flex.grow: view.wide ? 2 : 0
                    Tk.Flex.basis: view.wide ? 0 : -1
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.sm
                        Tk.Overline { title: qsTr("WHAT APPLY WILL DO") }
                        Tk.Label {
                            visible: !view.dirty
                            text: qsTr("No changes yet. Edit the fields; the change is checked against your data as you go.")
                            muted: true
                            wrapMode: Text.Wrap
                        }
                        Tk.Notice { visible: view.dirty && view.plan.ok === false && !!view.plan.error; variant: "danger"; text: view.plan.error || "" }
                        Repeater {
                            model: view.dirty ? (view.plan.notes || []) : []
                            delegate: Tk.Label { required property string modelData; text: "• " + modelData; wrapMode: Text.Wrap }
                        }
                        Tk.Notice {
                            visible: view.dirty && (view.plan.warnings || []).length > 0
                            variant: "warning"
                            text: (view.plan.warnings || []).join("\n")
                        }
                        Tk.Notice {
                            visible: view.dirty && view.plan.destructive === true
                            variant: "info"
                            iconName: "archive"
                            text: qsTr("A backup copy of the database is saved before this runs.")
                        }
                        Tk.Overline { visible: view.dirty && !!view.plan.sql; title: qsTr("SQL") }
                        Tk.TextArea {
                            objectName: "designSql"
                            visible: view.dirty && !!view.plan.sql
                            readOnly: true
                            mono: true
                            wrapMode: TextEdit.Wrap
                            text: view.plan.sql || ""
                        }
                    }
                }
            }
        }
    }
}
