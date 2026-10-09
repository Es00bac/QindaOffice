// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The navigation pane: every table, query, form, report and (M80) macro of
// the database, grouped by kind, with search. Click selects (the ribbon's
// Open/Design/Rename/Delete act on the selection), double-click opens,
// F2 renames in place, the context menu offers the rest.
Item {
    id: pane

    property var window: null
    readonly property var base: window !== null ? window.base : null
    readonly property var host: window !== null ? window.baseHost : null
    property string filterText: ""
    property var collapsed: ({})
    readonly property var kinds: [
        { kind: "table", title: qsTr("Tables") }, { kind: "query", title: qsTr("Queries") },
        { kind: "form", title: qsTr("Forms") }, { kind: "report", title: qsTr("Reports") },
        { kind: "macro", title: qsTr("Macros") }]

    function objectsOf(kind) {
        const all = base !== null ? base.objects : []
        const needle = filterText.toLowerCase()
        return all.filter(function(o) {
            return o.kind === kind && (needle.length === 0 || o.name.toLowerCase().indexOf(needle) >= 0)
        })
    }
    function select(kind, name) {
        host.selectedKind = kind
        host.selectedName = name
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Box {
            Tk.Flex.shrink: 0
            color: Tk.Theme.color.surface
            padding: Tk.Theme.space.sm
            borderBottom: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    Tk.Icon { name: "database"; size: Tk.Theme.size.icon; color: Tk.Theme.color.accent }
                    Tk.Label {
                        objectName: "navigationTitle"
                        text: pane.base !== null ? pane.base.databaseName : ""
                        font.weight: Font.DemiBold
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                    }
                    Tk.IconButton {
                        objectName: "navigationCreate"
                        iconName: "plus"
                        small: true
                        tooltip: qsTr("Create")
                        onClicked: createMenu.popup(this, 0, height)
                    }
                }
                Tk.SearchField {
                    objectName: "navigationSearch"
                    placeholderText: qsTr("Search objects")
                    small: true
                    tooltip: qsTr("Search tables, queries, forms and reports")
                    onSearched: function(text) { pane.filterText = text }
                }
            }
        }
        Tk.Scroll {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            accessibleName: qsTr("Database objects")
            Tk.Flex {
                width: pane.width
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: pane.kinds
                    delegate: Tk.Flex {
                        id: section
                        required property var modelData
                        readonly property var items: pane.objectsOf(modelData.kind)
                        width: pane.width
                        direction: Tk.Flex.Column
                        gap: 0
                        Tk.SectionHeader {
                            objectName: "navigationSection_" + section.modelData.kind
                            title: section.modelData.title
                            count: String(section.items.length)
                            collapsible: true
                            collapsed: pane.collapsed[section.modelData.kind] === true
                            // AGENT-NOTE: SectionHeader toggles `collapsed` itself.
                            onCollapsedChanged: {
                                if (collapsed === (pane.collapsed[section.modelData.kind] === true)) return
                                const next = Object.assign({}, pane.collapsed)
                                next[section.modelData.kind] = collapsed
                                pane.collapsed = next
                            }
                        }
                        Repeater {
                            model: pane.collapsed[section.modelData.kind] === true ? [] : section.items
                            delegate: Tk.TreeRow {
                                id: row
                                required property var modelData
                                objectName: "navigationItem_" + modelData.kind + "_" + modelData.name
                                width: pane.width
                                text: modelData.name
                                secondaryText: pane.width > Tk.Theme.size.panelMinWidth * 0.9 ? modelData.description : ""
                                iconName: modelData.iconName
                                editable: pane.base !== null && !pane.base.readOnly
                                selected: pane.host.selectedKind === modelData.kind && pane.host.selectedName === modelData.name
                                active: pane.host.currentKind === modelData.kind && pane.host.currentName === modelData.name
                                tooltip: modelData.name
                                onClicked: pane.select(modelData.kind, modelData.name)
                                onDoubleClicked: {
                                    pane.select(modelData.kind, modelData.name)
                                    pane.window.commands.openObject(modelData.kind, modelData.name)
                                }
                                onRightClicked: function(point) {
                                    pane.select(modelData.kind, modelData.name)
                                    objectMenu.popup(row, point.x, point.y)
                                }
                                onRenamed: function(text) {
                                    const r = pane.base.renameObject(modelData.kind, modelData.name, text)
                                    if (!r.ok) pane.window.notify(qsTr("Not renamed"), r.error, "danger")
                                    else pane.select(modelData.kind, r.name)
                                }
                            }
                        }
                        Tk.Caption {
                            visible: section.items.length === 0 && pane.collapsed[section.modelData.kind] !== true
                            text: pane.filterText.length ? qsTr("No match") : qsTr("None yet")
                            leftPadding: Tk.Theme.space.xl
                            topPadding: Tk.Theme.space.xs
                            bottomPadding: Tk.Theme.space.sm
                        }
                    }
                }
            }
        }
    }

    Tk.Menu {
        id: objectMenu
        Tk.MenuItem {
            text: pane.host.selectedKind === "macro" ? qsTr("Run") : qsTr("Open")
            iconName: pane.host.selectedKind === "macro" ? "play" : "external-link"
            onTriggered: pane.window.trigger("baseOpenObjectAction")
        }
        Tk.MenuItem { text: qsTr("Design View"); iconName: "square-pen"; onTriggered: pane.window.trigger("baseViewDesignAction") }
        Tk.MenuSeparator { }
        Tk.MenuItem {
            text: qsTr("Export as CSV…"); iconName: "file-output"
            enabled: pane.host.selectedKind === "table" || pane.host.selectedKind === "query"
            onTriggered: pane.window.trigger("baseExportCsvAction")
        }
        Tk.MenuItem {
            text: qsTr("New Form"); iconName: "layout-template"; enabled: pane.host.selectedKind === "table"
            onTriggered: pane.window.trigger("baseNewFormAction")
        }
        Tk.MenuItem {
            text: qsTr("New Report"); iconName: "scroll-text"
            enabled: pane.host.selectedKind === "table" || pane.host.selectedKind === "query"
            onTriggered: pane.window.trigger("baseNewReportAction")
        }
        Tk.MenuSeparator { }
        Tk.MenuItem { text: qsTr("Rename…"); iconName: "text-cursor-input"; onTriggered: pane.window.trigger("baseRenameObjectAction") }
        Tk.MenuItem { text: qsTr("Delete…"); iconName: "trash-2"; danger: true; onTriggered: pane.window.trigger("baseDeleteObjectAction") }
    }
    Tk.Menu {
        id: createMenu
        Tk.MenuItem { text: qsTr("Table…"); iconName: "table"; onTriggered: pane.window.trigger("baseNewTableAction") }
        Tk.MenuItem { text: qsTr("Table Design"); iconName: "table-2"; onTriggered: pane.window.trigger("baseTableDesignAction") }
        Tk.MenuItem { text: qsTr("Query Design"); iconName: "funnel"; onTriggered: pane.window.trigger("baseQueryDesignAction") }
        Tk.MenuItem { text: qsTr("Form Wizard…"); iconName: "wand-sparkles"; onTriggered: pane.window.trigger("baseFormWizardAction") }
        Tk.MenuItem { text: qsTr("Report Wizard…"); iconName: "wand"; onTriggered: pane.window.trigger("baseReportWizardAction") }
        Tk.MenuItem { text: qsTr("Labels…"); iconName: "tag"; onTriggered: pane.window.trigger("baseLabelsWizardAction") }
        Tk.MenuItem { text: qsTr("Macro"); iconName: "workflow"; onTriggered: pane.window.trigger("baseNewMacroAction") }
        Tk.MenuSeparator { }
        Tk.MenuItem { text: qsTr("Import Text or CSV…"); iconName: "file-input"; onTriggered: pane.window.trigger("baseImportCsvAction") }
    }
}
