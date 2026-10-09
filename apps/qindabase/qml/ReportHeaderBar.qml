// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The report view's header: the report, what it reads and how many pages
// it makes, Preview/Design, Save design, refresh and Print. A label report
// counts its labels and sheets and has no design view here (M80).
Tk.Box {
    id: bar

    required property var view

    objectName: "reportHeader"
    color: Tk.Theme.color.surface
    padding: Tk.Theme.space.sm
    paddingLeft: Tk.Theme.space.md
    borderBottom: Tk.Theme.size.border
    borderColor: Tk.Theme.color.divider

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        wrap: Tk.Flex.Wrap
        Tk.Icon { name: bar.view.labels ? "tag" : "scroll-text"; size: Tk.Theme.size.icon; color: Tk.Theme.color.accent }
        Tk.Heading { objectName: "reportName"; level: 3; text: bar.view.reportName; Tk.Flex.shrink: 0 }
        Tk.Caption {
            objectName: "reportStats"
            text: bar.view.labels
                  ? qsTr("%1 · %n label(s) · %2 sheet(s)", "", bar.view.rowCount).arg(bar.view.table).arg(bar.view.pageCount)
                  : qsTr("%1 · %n record(s) · %2 page(s)", "", bar.view.rowCount).arg(bar.view.table).arg(bar.view.pageCount)
            Tk.Flex.shrink: 0
        }
        Tk.Caption {
            visible: bar.view.labels && bar.view.base !== null
            text: bar.view.base !== null ? bar.view.base.labels.productLabel : ""
            Tk.Flex.shrink: 1
            elide: Text.ElideRight
        }
        Tk.Spacer { }
        Tk.Segmented {
            objectName: "reportMode"
            visible: !bar.view.labels
            small: true
            model: [{ text: qsTr("Preview"), iconName: "eye" }, { text: qsTr("Design"), iconName: "scroll-text" }]
            currentIndex: bar.view.designing ? 1 : 0
            onActivated: function(i) { bar.view.designing = i === 1 }
        }
        Tk.Button {
            visible: !bar.view.labels
            text: qsTr("Save design"); iconName: "save"
            enabled: bar.view.designDirty
            onClicked: bar.view.save()
        }
        Tk.IconButton { iconName: "refresh-ccw"; tooltip: qsTr("Refresh the data"); onClicked: bar.view.refreshData() }
        Tk.Button {
            objectName: "reportPrint"
            text: qsTr("Print…"); iconName: "printer"; variant: "accent"
            onClicked: bar.view.window.trigger("filePrintAction")
        }
    }
}
