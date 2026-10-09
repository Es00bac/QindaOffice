// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The query's SQL. In Design view: the statement in Access SQL as the
// grid stands, the prompts it asks for, and the SQLite SQL it runs as. In
// SQL view: the Access SQL to edit — what the grid can hold goes back into
// it on Design, Run or Save; what it cannot is said, with Keep as SQLite
// SQL — or, for a query written in SQLite SQL, that statement itself.
Tk.Flex {
    id: pane

    required property var view
    readonly property var compiled: view.compiled
    readonly property bool editing: view.mode === "sql"

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    Tk.Notice {
        objectName: "queryProblem"
        visible: pane.compiled.ok === false && !!pane.compiled.error
                 && (pane.view.design.tables || []).length + (pane.view.design.sql || "").length > 0
        variant: "warning"
        text: pane.compiled.error || ""
    }
    Tk.Notice {
        objectName: "querySqlProblem"
        visible: pane.editing && pane.view.sqlError.length > 0
        variant: "warning"
        title: qsTr("The design grid cannot show this")
        text: pane.view.sqlError
        actions: [
            Tk.Button {
                objectName: "queryKeepSqlite"
                text: qsTr("Keep as SQLite SQL")
                small: true
                tooltip: qsTr("Run the statement as it is written, without a design grid")
                onClicked: pane.view.keepAsSqlite()
            }
        ]
    }
    Tk.Flex {
        visible: (pane.compiled.parameters || []).length > 0
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Center
        Tk.Overline { title: qsTr("ASKS FOR") }
        Repeater {
            model: pane.compiled.parameters || []
            delegate: Tk.Chip {
                required property var modelData
                small: true
                iconName: "circle-question-mark"
                text: modelData.prompt + " · " + modelData.typeLabel + (modelData.declared ? qsTr(" (declared)") : "")
            }
        }
    }
    Tk.Overline {
        title: pane.view.sqlOnly ? qsTr("SQLITE SQL — ONE STATEMENT; :name ASKS FOR A VALUE")
               : pane.editing ? qsTr("ACCESS SQL — EDIT, THEN DESIGN, RUN OR SAVE")
                              : qsTr("ACCESS SQL")
    }
    Tk.TextArea {
        objectName: "querySql"
        mono: true
        rows: pane.editing ? 10 : 5
        readOnly: !pane.editing
        wrapMode: TextEdit.Wrap
        text: pane.view.sqlOnly ? (pane.view.design.sql || "") : pane.editing ? pane.view.sqlText : pane.view.accessText
        tooltip: qsTr("The query in SQL")
        onEditingFinished: {
            if (!pane.editing) return
            if (pane.view.sqlOnly) {
                const sql = text
                if (sql !== pane.view.design.sql) pane.view.edit(function(d) { d.sql = sql })
            } else {
                pane.view.sqlText = text
            }
        }
    }
    Tk.Overline { visible: !pane.view.sqlOnly && !!pane.compiled.sql; title: qsTr("RUNS AS (SQLITE)") }
    Tk.TextArea {
        objectName: "queryCompiledSql"
        visible: !pane.view.sqlOnly && !!pane.compiled.sql
        mono: true
        rows: 4
        readOnly: true
        wrapMode: TextEdit.Wrap
        text: pane.compiled.sql || ""
        tooltip: qsTr("The SQLite statement the query runs as")
    }
}
