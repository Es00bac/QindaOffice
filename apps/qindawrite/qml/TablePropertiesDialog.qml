// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Table ▸ Table Properties (M70, D-079): Word's four pages. Table —
// preferred width (automatic, a length, or a percentage of the text
// column), alignment, indent from the left, fixed or automatic layout, the
// default cell margins; Row — height "at least" or "exactly", allow the row
// to break across pages, repeat as a header row at the top of each page;
// Column — width; Cell — vertical alignment and the cell's own margins.
// Lengths in inches or centimetres (the host speaks twips). Only the pages
// the user touched are applied (`changed`); rows and columns are those the
// selection covers.
Tk.Dialog {
    id: dialog
    signal propertiesApplied(var answer)
    property var info: ({ table: {}, row: {}, column: {}, cell: {} })
    property var changed: ({})
    property bool loading: false
    readonly property real twipsPerUnit: units.currentIndex === 0 ? 1440 : 1440 / 2.54
    readonly property string unitSuffix: units.currentIndex === 0 ? qsTr(" in") : qsTr(" cm")
    objectName: "tablePropertiesDialog"
    title: qsTr("Table Properties")
    dialogWidth: 520
    primaryText: qsTr("OK")

    function mark(page) {
        if (loading) return
        const next = Object.assign({}, changed)
        next[page] = true
        changed = next
    }
    function length(twips) { return twips >= 0 ? twips / twipsPerUnit : 0 }
    function twips(value) { return Math.round(value * twipsPerUnit) }
    function load(i) {
        loading = true
        info = i
        changed = ({})
        pages.currentIndex = 0
        const t = i.table
        widthOn.checked = t.widthType !== "auto"
        widthMeasure.currentIndex = t.widthType === "pct" ? 1 : 0
        widthValue.value = t.widthType === "pct" ? t.width : length(t.width)
        alignment.currentIndex = ["left", "center", "right"].indexOf(t.alignment)
        indent.value = length(t.indent)
        fixedLayout.checked = t.layout === "fixed"
        marginTop.value = length(Math.max(0, t.margins.top))
        marginBottom.value = length(Math.max(0, t.margins.bottom))
        marginLeft.value = length(t.margins.left >= 0 ? t.margins.left : 108)
        marginRight.value = length(t.margins.right >= 0 ? t.margins.right : 108)
        heightOn.checked = i.row.height > 0
        rowHeight.value = length(i.row.height)
        heightRule.currentIndex = i.row.exact ? 1 : 0
        breakAcross.checked = !i.row.cantSplit
        header.checked = i.row.header
        columnWidth.value = length(i.column.width)
        valign.currentIndex = ["top", "center", "bottom"].indexOf(i.cell.valign)
        const own = i.cell.margins
        sameAsTable.checked = own === undefined || own === null
        cellTop.value = own ? length(Math.max(0, own.top)) : marginTop.value
        cellBottom.value = own ? length(Math.max(0, own.bottom)) : marginBottom.value
        cellLeft.value = own ? length(Math.max(0, own.left)) : marginLeft.value
        cellRight.value = own ? length(Math.max(0, own.right)) : marginRight.value
        loading = false
    }
    onAccepted: {
        if (Object.keys(changed).length === 0) return
        dialog.propertiesApplied({
            changed: Object.keys(changed),
            table: {
                widthType: !widthOn.checked ? "auto" : widthMeasure.currentIndex === 1 ? "pct" : "dxa",
                width: widthMeasure.currentIndex === 1 ? widthValue.value : twips(widthValue.value),
                alignment: ["left", "center", "right"][Math.max(0, alignment.currentIndex)],
                indent: twips(indent.value), layout: fixedLayout.checked ? "fixed" : "autofit",
                margins: { top: twips(marginTop.value), left: twips(marginLeft.value),
                           bottom: twips(marginBottom.value), right: twips(marginRight.value) }
            },
            row: { height: heightOn.checked ? twips(rowHeight.value) : 0, exact: heightRule.currentIndex === 1,
                   cantSplit: !breakAcross.checked, header: header.checked },
            column: { width: twips(columnWidth.value) },
            cell: { valign: ["top", "center", "bottom"][Math.max(0, valign.currentIndex)],
                    margins: sameAsTable.checked ? {} : { top: twips(cellTop.value), left: twips(cellLeft.value),
                                                          bottom: twips(cellBottom.value), right: twips(cellRight.value) } }
        })
    }

    component Length: Tk.NumberField {
        property string page: "table"
        from: 0; to: 22; stepSize: 0.05; decimals: 2; suffix: dialog.unitSuffix
        onValueModified: function(v) { value = v; dialog.mark(page) }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.TabStrip {
                id: pages
                objectName: "tablePropertiesPages"
                model: [qsTr("Table"), qsTr("Row"), qsTr("Column"), qsTr("Cell")]
            }
            Tk.Segmented {
                id: units
                objectName: "tablePropertiesUnits"
                model: [qsTr("in"), qsTr("cm")]
                property real previous: 1440
                onCurrentIndexChanged: {
                    const now = currentIndex === 0 ? 1440 : 1440 / 2.54
                    const keep = dialog.loading
                    dialog.loading = true
                    for (const f of [indent, marginTop, marginBottom, marginLeft, marginRight, rowHeight, columnWidth,
                                     cellTop, cellBottom, cellLeft, cellRight])
                        f.value = f.value * previous / now
                    if (widthMeasure.currentIndex === 0) widthValue.value = widthValue.value * previous / now
                    dialog.loading = keep
                    previous = now
                }
            }
        }
        // Table
        Tk.Flex {
            visible: pages.currentIndex === 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.PropertyRow {
                label: qsTr("Preferred width")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    align: Tk.Flex.Center
                    Tk.CheckBox { id: widthOn; objectName: "tableWidthOn"; onToggled: dialog.mark("table") }
                    Tk.NumberField {
                        id: widthValue; objectName: "tableWidth"; enabled: widthOn.checked
                        from: 0; to: widthMeasure.currentIndex === 1 ? 600 : 22; stepSize: widthMeasure.currentIndex === 1 ? 5 : 0.1
                        decimals: widthMeasure.currentIndex === 1 ? 0 : 2
                        suffix: widthMeasure.currentIndex === 1 ? qsTr(" %") : dialog.unitSuffix
                        onValueModified: function(v) { value = v; dialog.mark("table") }
                    }
                    Tk.ComboBox {
                        id: widthMeasure; objectName: "tableWidthMeasure"; enabled: widthOn.checked
                        model: [qsTr("Length"), qsTr("Percent")]
                        onActivated: function(index) { widthValue.value = index === 1 ? 100 : 6.5; dialog.mark("table") }
                    }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Alignment")
                Tk.Segmented {
                    id: alignment
                    objectName: "tableAlignment"
                    model: [qsTr("Left"), qsTr("Center"), qsTr("Right")]
                    onActivated: dialog.mark("table")
                }
            }
            Tk.PropertyRow {
                label: qsTr("Indent from left")
                Length { id: indent; objectName: "tableIndent"; from: -22; enabled: alignment.currentIndex === 0 }
            }
            Tk.CheckBox { id: fixedLayout; objectName: "tableFixedLayout"; text: qsTr("Fixed column widths (no AutoFit)")
                          onToggled: dialog.mark("table") }
            Tk.PropertyRow {
                label: qsTr("Default cell margins")
                Tk.Grid {
                    columns: "auto auto"
                    gap: Tk.Theme.space.xs
                    Length { id: marginTop; objectName: "tableMarginTop"; label: qsTr("T") }
                    Length { id: marginBottom; objectName: "tableMarginBottom"; label: qsTr("B") }
                    Length { id: marginLeft; objectName: "tableMarginLeft"; label: qsTr("L") }
                    Length { id: marginRight; objectName: "tableMarginRight"; label: qsTr("R") }
                }
            }
        }
        // Row
        Tk.Flex {
            visible: pages.currentIndex === 1
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Caption {
                text: dialog.info.row.count > 1 ? qsTr("Rows %1–%2").arg(dialog.info.row.first + 1).arg(dialog.info.row.first + dialog.info.row.count)
                                                : qsTr("Row %1").arg((dialog.info.row.first || 0) + 1)
            }
            Tk.PropertyRow {
                label: qsTr("Specify height")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    align: Tk.Flex.Center
                    Tk.CheckBox { id: heightOn; objectName: "rowHeightOn"; onToggled: dialog.mark("row") }
                    Length { id: rowHeight; objectName: "rowHeight"; page: "row"; enabled: heightOn.checked }
                    Tk.ComboBox { id: heightRule; objectName: "rowHeightRule"; enabled: heightOn.checked
                                  model: [qsTr("At least"), qsTr("Exactly")]; onActivated: dialog.mark("row") }
                }
            }
            Tk.CheckBox { id: breakAcross; objectName: "rowBreakAcross"; text: qsTr("Allow row to break across pages")
                          onToggled: dialog.mark("row") }
            Tk.CheckBox { id: header; objectName: "rowHeader"; text: qsTr("Repeat as header row at the top of each page")
                          onToggled: dialog.mark("row") }
        }
        // Column
        Tk.Flex {
            visible: pages.currentIndex === 2
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.PropertyRow {
                label: qsTr("Preferred width")
                Length { id: columnWidth; objectName: "columnWidth"; page: "column"; from: 0.05 }
            }
        }
        // Cell
        Tk.Flex {
            visible: pages.currentIndex === 3
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.PropertyRow {
                label: qsTr("Vertical alignment")
                Tk.Segmented {
                    id: valign
                    objectName: "cellVerticalAlignment"
                    model: [qsTr("Top"), qsTr("Center"), qsTr("Bottom")]
                    onActivated: dialog.mark("cell")
                }
            }
            Tk.CheckBox { id: sameAsTable; objectName: "cellSameMargins"; text: qsTr("Same margins as the whole table")
                          onToggled: dialog.mark("cell") }
            Tk.PropertyRow {
                label: qsTr("Cell margins")
                enabled: !sameAsTable.checked
                Tk.Grid {
                    columns: "auto auto"
                    gap: Tk.Theme.space.xs
                    Length { id: cellTop; objectName: "cellMarginTop"; page: "cell"; label: qsTr("T") }
                    Length { id: cellBottom; objectName: "cellMarginBottom"; page: "cell"; label: qsTr("B") }
                    Length { id: cellLeft; objectName: "cellMarginLeft"; page: "cell"; label: qsTr("L") }
                    Length { id: cellRight; objectName: "cellMarginRight"; page: "cell"; label: qsTr("R") }
                }
            }
        }
    }
}
