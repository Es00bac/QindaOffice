// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The start page. Without a database it is the template gallery (a blank
// database or one of the five starters, File ▸ Open for anything else);
// with one it is the database's home: what it holds, integrity notices,
// and the everyday starting points.
Item {
    id: start

    property var window: null
    readonly property var base: window !== null ? window.base : null
    readonly property bool hasDatabase: base !== null && base.hasDatabase
    readonly property var objects: base !== null ? base.objects : []

    function countOf(kind) { return objects.filter(function(o) { return o.kind === kind }).length }

    Tk.Scroll {
        anchors.fill: parent
        accessibleName: qsTr("Start")
        Tk.Flex {
            width: start.width
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.lg
            padding: Tk.Theme.space.xl

            // ---- no database: the gallery ----
            Tk.Flex {
                visible: !start.hasDatabase
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.md
                    Tk.Icon { name: "database"; size: Tk.Theme.size.iconLg * 1.5; color: Tk.Theme.color.accent }
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Tk.Heading { level: 1; text: qsTr("Start a database") }
                        Tk.Label {
                            muted: true
                            wrapMode: Text.Wrap
                            text: qsTr("Keep your records — people, gifts, shifts, stock, projects — in one file you own. "
                                       + "Pick a starter or begin blank; any SQLite file opens too.")
                        }
                    }
                    Tk.Button {
                        objectName: "startOpen"
                        visible: !start.window.narrow
                        text: qsTr("Open…")
                        iconName: "folder-open"
                        onClicked: start.window.trigger("fileOpenAction")
                    }
                }
                // AGENT-NOTE: a wrapping Flex with computed card widths, not
                // a Tk.Grid: wrapped descriptions fed back into Grid tracks
                // (a polish loop).
                Tk.Flex {
                    id: gallery
                    objectName: "templateGallery"
                    readonly property real available: start.width - Tk.Theme.space.xl * 2
                    readonly property int perRow: Math.max(1, Math.floor((available + Tk.Theme.space.md)
                                                                         / (Tk.Theme.size.panelMinWidth * 0.95 + Tk.Theme.space.md)))
                    readonly property real cardWidth: Math.floor((available - Tk.Theme.space.md * (perRow - 1)) / perRow)
                    direction: Tk.Flex.Row
                    wrap: Tk.Flex.Wrap
                    gap: Tk.Theme.space.md
                    Repeater {
                        model: start.base !== null ? start.base.templates : []
                        delegate: Tk.Card {
                            id: templateCard
                            required property var modelData
                            objectName: "template_" + (modelData.id || "blank")
                            width: gallery.cardWidth
                            Tk.Flex.basis: gallery.cardWidth
                            Tk.Flex.shrink: 0
                            interactive: true
                            tooltip: modelData.name
                            onClicked: {
                                const r = start.base.createUntitled(templateCard.modelData.id)
                                if (!r.ok) start.window.notify(qsTr("Not created"), r.error, "danger")
                            }
                            Tk.Flex {
                                direction: Tk.Flex.Column
                                gap: Tk.Theme.space.sm
                                Tk.Flex {
                                    direction: Tk.Flex.Row
                                    align: Tk.Flex.Center
                                    gap: Tk.Theme.space.sm
                                    Tk.Box {
                                        color: Tk.Theme.color.accentSubtle
                                        radius: Tk.Theme.radius.md
                                        padding: Tk.Theme.space.sm
                                        Tk.Icon { name: templateCard.modelData.iconName; size: Tk.Theme.size.iconLg; color: Tk.Theme.color.accent }
                                    }
                                    Tk.Heading { level: 3; text: templateCard.modelData.name; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                                }
                                Tk.Label {
                                    width: gallery.cardWidth - Tk.Theme.space.md * 2
                                    text: templateCard.modelData.description
                                    muted: true
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 3
                                }
                            }
                        }
                    }
                }
                Tk.Button {
                    visible: start.window.narrow
                    text: qsTr("Open a database…")
                    iconName: "folder-open"
                    onClicked: start.window.trigger("fileOpenAction")
                }
            }

            // ---- a database: its home ----
            Tk.Flex {
                visible: start.hasDatabase
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Overline { title: qsTr("DATABASE") }
                    Tk.Heading { objectName: "startDatabaseName"; level: 1; text: start.base !== null ? start.base.databaseName : "" }
                }
                Tk.Notice {
                    visible: start.base !== null && start.base.openProblems.length > 0
                    variant: "warning"
                    title: qsTr("The integrity check found problems")
                    text: start.base !== null ? start.base.openProblems.join("\n") : ""
                }
                Tk.Notice {
                    objectName: "startSystemTables"
                    visible: start.base !== null && start.hasDatabase && !start.base.hasSystemTables && !start.base.readOnly
                    variant: "info"
                    title: qsTr("A plain SQLite database")
                    text: qsTr("Your tables open and edit as they are. To save queries, forms, reports, captions and lookups in this file, QindaBase adds three small tables whose names start with qb_.")
                    Tk.Button {
                        text: qsTr("Add them")
                        onClicked: start.base.addSystemTables()
                    }
                }
                Tk.Grid {
                    columns: "repeat(auto-fill, minmax(" + Math.round(Tk.Theme.size.panelMinWidth * 0.6) + ", 1fr))"
                    gap: Tk.Theme.space.md
                    Repeater {
                        model: [
                            { kind: "table", label: qsTr("Tables"), iconName: "table", create: "baseTableDesignAction" },
                            { kind: "query", label: qsTr("Queries"), iconName: "funnel", create: "baseQueryDesignAction" },
                            { kind: "form", label: qsTr("Forms"), iconName: "layout-template", create: "baseFormWizardAction" },
                            { kind: "report", label: qsTr("Reports"), iconName: "scroll-text", create: "baseReportWizardAction" }]
                        delegate: Tk.Card {
                            id: statCard
                            required property var modelData
                            objectName: "startCount_" + modelData.kind
                            interactive: true
                            tooltip: qsTr("Create: %1").arg(modelData.label)
                            onClicked: start.window.trigger(statCard.modelData.create)
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.sm
                                Tk.Icon { name: statCard.modelData.iconName; size: Tk.Theme.size.iconLg; color: Tk.Theme.color.accent }
                                Tk.Flex {
                                    direction: Tk.Flex.Column
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                    Tk.Heading { level: 1; text: String(start.countOf(statCard.modelData.kind)) }
                                    Tk.Caption { text: statCard.modelData.label }
                                }
                                Tk.Icon { name: "plus"; size: Tk.Theme.size.icon; color: Tk.Theme.color.textMuted }
                            }
                        }
                    }
                }
                Tk.SectionHeader { title: qsTr("Tables"); count: String(start.countOf("table")) }
                Tk.Grid {
                    columns: "repeat(auto-fill, minmax(" + Math.round(Tk.Theme.size.panelMinWidth * 0.8) + ", 1fr))"
                    gap: Tk.Theme.space.sm
                    Repeater {
                        model: start.objects.filter(function(o) { return o.kind === "table" })
                        delegate: Tk.ListRow {
                            required property var modelData
                            text: modelData.name
                            secondaryText: modelData.description
                            iconName: "table"
                            tooltip: qsTr("Open %1").arg(modelData.name)
                            onClicked: start.window.commands.openObject("table", modelData.name)
                        }
                    }
                }
                Tk.EmptyState {
                    visible: start.countOf("table") === 0
                    iconName: "table"
                    title: qsTr("No tables yet")
                    text: qsTr("Design a table, or import one from a CSV file or an Access database.")
                    Tk.Button { variant: "accent"; text: qsTr("Table Design"); onClicked: start.window.trigger("baseTableDesignAction") }
                    Tk.Button { variant: "outline"; text: qsTr("Import CSV…"); onClicked: start.window.trigger("baseImportCsvAction") }
                }
            }
        }
    }
}
