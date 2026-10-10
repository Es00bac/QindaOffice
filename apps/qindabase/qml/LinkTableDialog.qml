// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// External Data ▸ Link Table (M86, D-094, base_links.h): a read-only table
// over a table of another QindaBase (or SQLite) file, a CSV / text file or
// an Excel sheet. The file is chosen first (AccessToolsCommands asks for
// it); the kind follows its extension and can be changed here. QindaBase
// keeps a copy of the source's records and reads it again each time the
// database opens and on Refresh All Links. Linking is one undo step.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property url source: ""
    property string kind: "qbase" // qbase | csv | xlsx
    property var choices: []      // qbase: tables; xlsx: sheets
    property string choice: ""
    property string name: ""
    property string delimiter: "" // csv: "" = detected
    property bool headerRow: true
    property string error: ""
    readonly property string fileName: String(source).replace(/^.*\//, "")
    readonly property var kinds: [{ id: "qbase", text: qsTr("Database"), iconName: "database" },
                                  { id: "csv", text: qsTr("Text"), iconName: "file-text" },
                                  { id: "xlsx", text: qsTr("Excel"), iconName: "sheet" }]
    readonly property var delimiters: [{ id: "", label: qsTr("Detect") }, { id: ",", label: qsTr("Comma") },
                                       { id: ";", label: qsTr("Semicolon") }, { id: "\t", label: qsTr("Tab") },
                                       { id: "|", label: qsTr("Bar ( | )") }]

    title: qsTr("Link a table")
    subtitle: fileName
    dialogWidth: Tk.Theme.size.dialogWidth * 1.2
    primaryText: qsTr("Link")
    primaryEnabled: fileName.length > 0 && name.trim().length > 0 && error.length === 0
                    && (kind === "csv" || choice.length > 0)

    // The kind a file's extension suggests.
    function kindOf(url) {
        const ext = String(url).toLowerCase().replace(/^.*\./, "")
        return ["xlsx", "xlsm"].indexOf(ext) >= 0 ? "xlsx" : ["csv", "tsv", "txt", "tab"].indexOf(ext) >= 0 ? "csv" : "qbase"
    }
    // A name no table has yet ("Members", "Members1", …).
    function freeName(wanted) {
        const taken = (base.tableNames || []).map(function(t) { return t.toLowerCase() })
        // (the characters Access refuses in a name: . ! ` [ ] ")
        const stem = (wanted || qsTr("Linked")).replace(/[.!`\[\]"]+/g, " ").trim() || qsTr("Linked")
        let n = 0
        let next = stem
        while (taken.indexOf(next.toLowerCase()) >= 0) next = stem + (++n)
        return next
    }
    function openFor(url) {
        source = url
        delimiter = ""
        headerRow = true
        setKind(kindOf(url))
        open()
    }
    function setKind(next) {
        kind = next
        const r = base.linkChoices(source, kind)
        error = r.ok ? "" : r.error
        choices = r.ok ? (kind === "xlsx" ? r.sheets || [] : r.tables || []) : []
        setChoice(choices.length > 0 ? choices[0] : "")
    }
    function setChoice(next) {
        choice = next
        name = freeName(kind === "csv" || next.length === 0 ? fileName.replace(/\.[^.]*$/, "") : next)
    }
    function link() {
        const r = base.linkTable(name.trim(), { kind: kind, url: source, table: choice, sheet: choice,
                                                delimiter: delimiter, headerRow: headerRow })
        if (!r.ok) { window.notify(qsTr("Not linked"), r.error, "danger"); return false }
        window.notify(qsTr("Linked"), qsTr("%1: %n record(s), read-only, refreshed as the database opens.", "", r.rows).arg(r.name),
                      "success")
        window.baseHost.selectedKind = "table"
        window.baseHost.selectedName = r.name
        return true
    }
    onAccepted: link()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "linkError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow {
            label: qsTr("Link to")
            Tk.Segmented {
                objectName: "linkKind"
                small: true
                model: dialog.kinds
                currentIndex: Math.max(0, dialog.kinds.findIndex(function(k) { return k.id === dialog.kind }))
                onActivated: function(i) { dialog.setKind(dialog.kinds[i].id) }
            }
        }
        Tk.PropertyRow {
            visible: dialog.kind !== "csv"
            label: dialog.kind === "xlsx" ? qsTr("Sheet") : qsTr("Table")
            Tk.ComboBox {
                objectName: "linkSourceTable"
                model: dialog.choices
                currentIndex: dialog.choices.indexOf(dialog.choice)
                onActivated: function(i) { dialog.setChoice(dialog.choices[i]) }
            }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "csv"
            label: qsTr("Separator")
            Tk.ComboBox {
                objectName: "linkDelimiter"
                textRole: "label"
                model: dialog.delimiters
                currentIndex: Math.max(0, dialog.delimiters.findIndex(function(d) { return d.id === dialog.delimiter }))
                onActivated: function(i) { dialog.delimiter = dialog.delimiters[i].id }
            }
        }
        Tk.CheckBox {
            objectName: "linkHeaderRow"
            visible: dialog.kind !== "qbase"
            text: qsTr("The first row holds the field names")
            checked: dialog.headerRow
            onToggled: dialog.headerRow = checked
        }
        Tk.PropertyRow {
            label: qsTr("Name here")
            Tk.TextField {
                objectName: "linkName"
                text: dialog.name
                onTextEdited: dialog.name = text
            }
        }
        Tk.Caption {
            wrapMode: Text.Wrap
            text: qsTr("The linked table is read-only here: change the source, then refresh. Its path is kept relative to this database's folder, so the two can move together.")
        }
    }
}
