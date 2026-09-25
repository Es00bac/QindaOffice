// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Renders one ReportTable map from BooksService.runReport (D-045): the
// company, title and period; the column header; rows styled by kind —
// section headings, indented account lines, ruled subtotals, a doubly
// ruled grand total. The numbers are the engine's display text; a row that
// carries a drill-down opens the transactions behind it.
Tk.Box {
    id: view

    property var report: ({})
    readonly property var columns: report.columns || []
    readonly property real valueWidth: Tk.Theme.size.tableColumn * 1.5
    signal drillRequested(var drill, string label)

    color: Tk.Theme.color.panelAlt
    radius: Tk.Theme.radius.lg
    padding: Tk.Theme.space.xl

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Caption { objectName: "reportCompany"; text: String(view.report.company || ""); horizontalAlignment: Text.AlignHCenter; width: parent.width }
        Tk.Heading { objectName: "reportTitle"; level: 2; text: String(view.report.title || ""); horizontalAlignment: Text.AlignHCenter; width: parent.width }
        Tk.Caption { objectName: "reportSubtitle"; text: String(view.report.subtitle || ""); horizontalAlignment: Text.AlignHCenter; width: parent.width }
        Item { implicitHeight: Tk.Theme.space.md }
        Tk.Flex {
            objectName: "reportHeader"
            direction: Tk.Flex.Row
            Repeater {
                model: view.columns
                delegate: Tk.Overline {
                    required property int index
                    required property var modelData
                    title: String(modelData.title || "")
                    horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                    Tk.Flex.grow: index === 0 ? 1 : 0
                    Tk.Flex.shrink: index === 0 ? 1 : 0
                    Tk.Flex.basis: index === 0 ? 0 : view.valueWidth
                    rightPadding: index === 0 ? 0 : Tk.Theme.space.sm
                }
            }
        }
        Rectangle { implicitHeight: Tk.Theme.size.focusRing; color: Tk.Theme.color.accent; implicitWidth: parent.width }
        ListView {
            id: rows
            objectName: "reportRows"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            clip: true
            model: view.report.rows || []
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.Table
            Accessible.name: String(view.report.title || qsTr("Report"))
            Tk.ScrollBar.vertical: Tk.ScrollBar { }
            delegate: Item {
                id: row
                required property int index
                required property var modelData
                readonly property string kind: String(modelData.kind || "account")
                readonly property bool strong: kind === "section" || kind === "subtotal" || kind === "total"
                readonly property bool hasDrill: modelData.drill && (modelData.drill.accountId || modelData.drill.partyId || modelData.drill.itemId
                                                                      || (modelData.drill.documentIds || []).length)
                objectName: "reportRow_" + String(modelData.key || index)
                width: rows.width
                implicitHeight: kind === "spacer" ? Tk.Theme.space.md : Tk.Theme.size.rowLg
                Rectangle {
                    anchors.fill: parent
                    color: hover.hovered && row.hasDrill ? Tk.Theme.color.hover : Tk.Theme.alpha(Tk.Theme.color.hover, Tk.Theme.opacity.hidden)
                    radius: Tk.Theme.radius.sm
                }
                HoverHandler { id: hover; cursorShape: row.hasDrill ? Qt.PointingHandCursor : Qt.ArrowCursor }
                TapHandler {
                    enabled: row.hasDrill
                    onDoubleTapped: view.drillRequested(row.modelData.drill, String((row.modelData.cells[0] || {}).text || ""))
                }
                Tk.Flex {
                    anchors.fill: parent
                    visible: row.kind !== "spacer"
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    Repeater {
                        model: row.modelData.cells || []
                        delegate: Item {
                            id: cell
                            required property int index
                            required property var modelData
                            Tk.Flex.grow: index === 0 ? 1 : 0
                            Tk.Flex.basis: index === 0 ? 0 : -1
                            implicitWidth: index === 0 ? Tk.Theme.size.labelWidth : view.valueWidth
                            implicitHeight: Tk.Theme.size.rowLg
                            Tk.Label {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: cell.index === 0 ? parent.left : undefined
                                anchors.right: cell.index === 0 ? undefined : parent.right
                                anchors.leftMargin: cell.index === 0 ? Tk.Theme.space.lg * (row.modelData.depth || 0) + Tk.Theme.space.sm : 0
                                anchors.rightMargin: Tk.Theme.space.sm
                                width: cell.index === 0 ? parent.width - anchors.leftMargin : implicitWidth
                                text: String(cell.modelData.text || "")
                                mono: cell.index > 0
                                elide: Text.ElideRight
                                font.weight: row.strong ? Font.DemiBold : Font.Normal
                                color: cell.modelData.negative ? Tk.Theme.color.danger : Tk.Theme.color.text
                            }
                            // Subtotals get a rule above the figure; the grand total two below.
                            Rectangle {
                                visible: cell.index > 0 && (row.kind === "subtotal" || row.kind === "total") && String(cell.modelData.text || "").length > 0
                                anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: Tk.Theme.space.sm
                                width: parent.width * 0.7; height: Tk.Theme.size.border
                                color: Tk.Theme.color.borderStrong
                            }
                            Rectangle {
                                visible: cell.index > 0 && row.kind === "total" && String(cell.modelData.text || "").length > 0
                                anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.rightMargin: Tk.Theme.space.sm
                                width: parent.width * 0.7; height: Tk.Theme.size.border * 3
                                color: Tk.Theme.alpha(Tk.Theme.color.bg, Tk.Theme.opacity.hidden)
                                border.width: Tk.Theme.size.border; border.color: Tk.Theme.color.borderStrong
                            }
                        }
                    }
                }
            }
        }
        Repeater {
            model: view.report.notes || []
            delegate: Tk.Caption { required property var modelData; text: String(modelData); wrapMode: Text.Wrap; width: parent ? parent.width : 0 }
        }
    }
}
