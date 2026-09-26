// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Base.App

// A PivotTable of the datasheet's rows: QindaTK's QueryController over the
// model's typed pivotGrid (pivot_source.h), edited with Tk.QueryBuilder
// (rows, columns, measures, filter) and shown by Tk.PivotView.
//
// AGENT-NOTE: QindaTK's query engine sums in double, so money totals here
// are exact only to double precision (docs/qindabase.md); the datasheet's
// totals row and saved queries total in integer cents.
Item {
    id: panel

    property DatasheetModel sheet: null

    // A useful first layout: group by the first text column, count rows and
    // sum the first money/number column.
    function seed() {
        const fields = controller.schema.fields || []
        const group = fields.find(function(f) { return f.type === "text" })
        const amount = fields.find(function(f) { return f.type === "number" })
        builder.rowFields = group ? [group.id] : []
        builder.columnFields = []
        const measures = [{ id: "count", field: fields.length ? fields[0].id : "", function: "countRows" }]
        if (amount) measures.push({ id: "total", field: amount.id, function: "sum" })
        builder.measures = measures
        builder.requestPreview()
    }

    Tk.QueryController {
        id: controller
        source: panel.sheet !== null ? panel.sheet.pivotGrid : null
        schema: panel.sheet !== null ? panel.sheet.pivotSchema : ({})
        onSchemaChanged: if (panel.visible) Qt.callLater(panel.seed)
    }
    onVisibleChanged: if (visible && (controller.schema.fields || []).length > 0) Qt.callLater(seed)

    Tk.Splitter {
        anchors.fill: parent
        orientation: Qt.Horizontal
        Tk.Scroll {
            id: fieldsScroll
            T.SplitView.preferredWidth: Math.min(panel.width * 0.42, Tk.Theme.size.panelMinWidth * 1.7)
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
            accessibleName: qsTr("PivotTable fields")
            Tk.Flex {
                width: fieldsScroll.width
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                padding: Tk.Theme.space.md
                Tk.Overline { title: qsTr("PIVOTTABLE FIELDS") }
                Tk.Caption {
                    wrapMode: Text.Wrap
                    text: qsTr("Put fields on rows or columns and add measures. Money totals here use decimal arithmetic good to about 15 digits; the totals row is exact to the cent.")
                }
                Tk.QueryBuilder {
                    id: builder
                    objectName: "pivotBuilder"
                    width: fieldsScroll.width - Tk.Theme.space.md * 2
                    controller: controller
                }
            }
        }
        Tk.PivotView {
            objectName: "pivotView"
            T.SplitView.fillWidth: true
            controller: controller
            tooltip: qsTr("PivotTable of %1").arg(panel.sheet !== null ? panel.sheet.sourceName : "")
            // AGENT-NOTE: the engine's double sums (2752.9700000000003) are
            // shown to the cent; the value itself is the engine's.
            function valueText(value) {
                const v = value !== null && typeof value === "object" && value.value !== undefined ? value.value : value
                if (v === undefined || v === null || (typeof value === "object" && value !== null && value.type === "null")) return "—"
                if (typeof value === "object" && value !== null && value.type === "error") return value.message || qsTr("Error")
                if (typeof v === "number") return v.toLocaleString(Qt.locale(), "f", Number.isInteger(v) ? 0 : 2)
                return String(v)
            }
        }
    }
}
