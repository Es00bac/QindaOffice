// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// The sparkline and Table Design dialogs (M90, D-098) and the host requests
// that open them: Create / Edit Sparklines, Hidden and Empty Cell Settings,
// Sparkline Colors, the axis's Custom Value and Date Axis Type; Rename
// Table and Resize Table.
// AGENT-GUARD: a QtObject, not an Item — the window seats ONE content child
// (Tk.AppWindow); the dialogs reach the window's overlay through hostItem.
QtObject {
    id: root

    required property CalcHost host
    property Item hostItem: null

    property PivotCalculatedDialog calculated: PivotCalculatedDialog { host: root.host; hostItem: root.hostItem }
    property SparklineCreateDialog create: SparklineCreateDialog { host: root.host; hostItem: root.hostItem }
    property SparklineHiddenDialog hidden: SparklineHiddenDialog { host: root.host; hostItem: root.hostItem }
    property SparklineColorsDialog colors: SparklineColorsDialog { host: root.host; hostItem: root.hostItem }
    property Tk.PromptDialog axisValue: Tk.PromptDialog {
        property bool max: false
        objectName: "sparklineAxisValueDialog"
        hostItem: root.hostItem
        title: max ? qsTr("Sparkline Vertical Axis Settings — Maximum") : qsTr("Sparkline Vertical Axis Settings — Minimum")
        label: max ? qsTr("Enter the maximum value") : qsTr("Enter the minimum value")
        onAccepted: {
            const problem = root.host.sparklines.applyAxisValue(max, text)
            if (problem.length > 0) root.host.showStatusMessage(problem, 6000)
        }
    }
    property Tk.PromptDialog dateAxis: Tk.PromptDialog {
        objectName: "sparklineDateAxisDialog"
        hostItem: root.hostItem
        title: qsTr("Sparkline Date Range")
        label: qsTr("Select the range that contains the date values")
        onAccepted: {
            const problem = root.host.sparklines.applyDateAxis(text)
            if (problem.length > 0) root.host.showStatusMessage(problem, 6000)
        }
    }
    property Tk.PromptDialog tableName: Tk.PromptDialog {
        objectName: "tableRenameDialog"
        hostItem: root.hostItem
        title: qsTr("Table Name")
        label: qsTr("Table Name")
        onAccepted: {
            const problem = root.host.renameTableTo(text)
            if (problem.length > 0) root.host.showStatusMessage(problem, 8000)
        }
    }
    property Tk.PromptDialog tableResize: Tk.PromptDialog {
        objectName: "tableResizeDialog"
        hostItem: root.hostItem
        title: qsTr("Resize Table")
        label: qsTr("Select the new data range for your table")
        onAccepted: {
            const problem = root.host.resizeTableTo(text)
            if (problem.length > 0) root.host.showStatusMessage(problem, 8000)
        }
    }

    property Connections sparklineRequests: Connections {
        target: root.host.sparklines
        function onCreateRequested(type) { root.create.load("create", type); root.create.open() }
        function onEditRequested(single) { root.create.load(single ? "single" : "edit", 0); root.create.open() }
        function onHiddenEmptyRequested() { root.hidden.load(); root.hidden.open() }
        function onColorsRequested() { root.colors.load(); root.colors.open() }
        function onAxisValueRequested(max) {
            root.axisValue.max = max
            root.axisValue.text = root.host.sparklines.axisValue(max)
            root.axisValue.open()
        }
        function onDateAxisRequested() { root.dateAxis.text = root.host.sparklines.dateRange(); root.dateAxis.open() }
    }
    property Connections tableRequests: Connections {
        target: root.host
        function onPivotCalculatedRequested() { root.calculated.load(); root.calculated.open() }
        function onTableRenameRequested() { root.tableName.text = root.host.tableName; root.tableName.open() }
        function onTableResizeRequested() { root.tableResize.text = root.host.tableRangeText(); root.tableResize.open() }
    }
}
