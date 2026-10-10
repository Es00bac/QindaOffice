// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// External Data ▸ Linked Table Manager (M86, D-094, base_links.h): every
// linked table with its source, when it was last read and what went wrong
// if the last read failed — refreshed (one undo step each), pointed at a
// moved source (Relink: the file is asked for, then the copy refreshes)
// or converted to an ordinary, editable table (undo links it again).
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var links: []

    title: qsTr("Linked table manager")
    subtitle: links.length === 0 ? qsTr("This database has no linked tables.")
                                 : qsTr("%n linked table(s), read again from their sources as the database opens.", "", links.length)
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: qsTr("Close")
    secondaryText: ""

    function openFor() {
        reload()
        open()
    }
    function reload() { links = base.linkedTables() }
    function refresh(table) {
        const r = base.refreshLink(table)
        if (!r.ok) window.notify(qsTr("%1 was not refreshed").arg(table), r.error, "warning")
        reload()
    }
    function relink(link) {
        close() // the file dialog comes up on its own; the manager comes back after it
        const filters = link.kind === "xlsx" ? [qsTr("Excel workbook (*.xlsx *.xlsm)")]
                      : link.kind === "csv" ? [qsTr("Text and CSV (*.csv *.tsv *.txt)")]
                      : [qsTr("QindaBase database (*.qbase *.sqlite *.db)")]
        window.importDialog.ask(filters.concat([qsTr("All files (*)")]), function(url) {
            const r = base.relinkTable(link.table, url)
            if (!r.ok) window.notify(qsTr("%1 was not relinked").arg(link.table), r.error, "danger")
            dialog.openFor()
        })
    }
    function unlink(table) {
        const r = base.unlinkTable(table)
        if (!r.ok) window.notify(qsTr("%1 is still linked").arg(table), r.error, "danger")
        else window.notify(qsTr("%1 is a local table now").arg(table), qsTr("Its records stay and can be edited; undo links it again."), "success")
        reload()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Repeater {
            model: dialog.links
            delegate: Tk.Card {
                id: row
                required property var modelData
                required property int index
                objectName: "linkedTable_" + modelData.table
                padding: Tk.Theme.space.sm
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        wrap: Tk.Flex.Wrap
                        Tk.Icon {
                            name: row.modelData.problem ? "triangle-alert" : "link-2"
                            size: Tk.Theme.size.icon
                            color: row.modelData.problem ? Tk.Theme.color.warning : Tk.Theme.color.accent
                            Tk.Flex.shrink: 0
                        }
                        Tk.Heading { level: 3; text: row.modelData.table; Tk.Flex.shrink: 0 }
                        Tk.Caption {
                            text: qsTr("← %1").arg(row.modelData.source)
                            elide: Text.ElideMiddle
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            Tk.Flex.minWidth: Tk.Theme.size.control * 3
                        }
                    }
                    Tk.Caption {
                        objectName: "linkedTableStatus_" + row.modelData.table
                        wrapMode: Text.Wrap
                        text: row.modelData.problem
                              ? qsTr("Not read: %1 (it shows its copy from %2)").arg(row.modelData.problem).arg(row.modelData.refreshed || qsTr("never"))
                              : qsTr("%n record(s), read %1", "", row.modelData.rows).arg(row.modelData.refreshed || qsTr("never"))
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.sm
                        wrap: Tk.Flex.Wrap
                        Tk.Button {
                            objectName: "linkRefresh_" + row.modelData.table
                            text: qsTr("Refresh")
                            iconName: "rotate-cw"
                            small: true
                            enabled: !dialog.base.readOnly
                            onClicked: dialog.refresh(row.modelData.table)
                        }
                        Tk.Button {
                            objectName: "linkRelink_" + row.modelData.table
                            text: qsTr("Relink…")
                            iconName: "link"
                            small: true
                            enabled: !dialog.base.readOnly
                            onClicked: dialog.relink(row.modelData)
                        }
                        Tk.Button {
                            objectName: "linkUnlink_" + row.modelData.table
                            text: qsTr("Convert to a local table")
                            iconName: "unlink"
                            small: true
                            enabled: !dialog.base.readOnly
                            onClicked: dialog.unlink(row.modelData.table)
                        }
                    }
                }
            }
        }
        Tk.EmptyState {
            visible: dialog.links.length === 0
            iconName: "link-2"
            title: qsTr("No linked tables")
            text: qsTr("External Data ▸ Link Table reads a table of another database, a text file or an Excel sheet.")
        }
    }
}
