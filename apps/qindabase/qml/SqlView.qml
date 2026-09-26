// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Database Tools ▸ SQL. Above: QindaTK's DatabaseSchemaView over the live
// file — columns, keys and relationships of any table, and schema SQL
// (CREATE / ALTER / DROP) that it validates and the host then applies
// through BaseService as ONE previewed, backed-up, undoable step. Below: a
// read-only SQL console whose first 500 rows show under it (changing data
// is done with a saved action query, which counts first).
Item {
    id: view

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var result: ({})

    function run(values) {
        const r = base.previewSql(editor.text, values, 500)
        if (r.needsParameters) {
            window.parametersDialog.ask(r.parameters, function(v) { view.run(v) })
            return
        }
        result = r
        const cols = []
        for (let i = 0; i < (r.columns || []).length; ++i)
            cols.push(columnComponent.createObject(results, { key: "c" + i, title: r.columns[i], flex: 1 }))
        results.columns = cols
    }
    function reopenProvider() {
        if (base !== null && base.workingPath.length > 0) live.open(base.workingPath)
    }
    onVisibleChanged: if (visible) reopenProvider()

    Tk.LocalDatabaseProvider { id: live }
    Component { id: columnComponent; Tk.TableColumn { } }

    // AGENT-NOTE: stacked, not side by side: DatabaseSchemaView's header
    // carries the database path, which crowds out its table picker in a
    // narrow pane.
    Tk.Splitter {
        anchors.fill: parent
        orientation: Qt.Vertical
        Tk.Scroll {
            id: schemaScroll
            T.SplitView.preferredHeight: view.height * 0.46
            T.SplitView.minimumHeight: Tk.Theme.size.panelMinHeight
            accessibleName: qsTr("Schema")
            Tk.Flex {
                width: schemaScroll.width
                direction: Tk.Flex.Column
                padding: Tk.Theme.space.md
                Tk.DatabaseSchemaView {
                    id: schema
                    objectName: "baseSchemaView"
                    width: schemaScroll.width - Tk.Theme.space.md * 2
                    provider: live
                    selectedTable: view.base !== null && view.base.tableNames.length > 0 ? view.base.tableNames[0] : ""
                    onMigrationCommitRequested: function(requestId, plan) {
                        const statements = plan.statements ? plan.statements : [plan.sql]
                        const planned = view.base.planSql(statements)
                        if (!planned.ok) { schema.acknowledge(requestId, "rejected", planned.error); return }
                        view.window.planDialog.show(planned, function() {
                            const r = view.base.applySql(statements)
                            view.reopenProvider()
                            schema.acknowledge(requestId, r.ok ? "accepted" : "failed",
                                               r.ok ? qsTr("Applied. Undo reverses it.") : r.error)
                        })
                    }
                }
            }
        }
        Tk.Flex {
            T.SplitView.fillWidth: true
            T.SplitView.fillHeight: true
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            padding: Tk.Theme.space.md
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Overline { title: qsTr("SQL CONSOLE — READ ONLY"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                Tk.Button { objectName: "sqlRun"; text: qsTr("Run"); iconName: "play"; variant: "accent"; small: true; onClicked: view.run(({})) }
            }
            Tk.TextArea {
                id: editor
                objectName: "sqlEditor"
                mono: true
                rows: 6
                wrapMode: TextEdit.Wrap
                placeholderText: "SELECT name, email FROM donors WHERE name LIKE :pattern"
                tooltip: qsTr("One SELECT statement; :name asks for a value")
                Keys.onPressed: function(event) {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        view.run(({}))
                        event.accepted = true
                    }
                }
            }
            Tk.Notice { visible: view.result.ok === false && !!view.result.error; variant: "danger"; text: view.result.error || "" }
            Tk.Caption {
                visible: view.result.ok === true
                text: view.result.truncated ? qsTr("The first 500 rows") : qsTr("%n row(s)", "", (view.result.rows || []).length)
            }
            Tk.DataTable {
                id: results
                objectName: "sqlResults"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                model: view.result.rows || []
                tooltip: qsTr("Query results")
                emptyText: qsTr("Run a query to see its rows")
            }
        }
    }
}
