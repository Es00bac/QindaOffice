// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Base.App

// A label report's sheets (M80, D-089 §7), one under the other, each
// painted by the painter that prints it (LabelPageItem, label_sheet.h) —
// what shows is what comes out of the printer or the PDF. Sheets fit the
// pane's width (never above 100 %), then View ▸ Zoom applies.
Tk.Scroll {
    id: sheets

    required property var view
    readonly property var sheet: view.base !== null ? view.base.labels : null
    readonly property real pageWidth: sheet !== null && sheet.pageSize.width > 0 ? sheet.pageSize.width / 72 * 96 : 816

    objectName: "labelPages"
    accessibleName: qsTr("Label sheets")

    Tk.Flex {
        width: sheets.width
        direction: Tk.Flex.Column
        align: Tk.Flex.Center
        gap: Tk.Theme.space.lg
        padding: Tk.Theme.space.lg
        Tk.Label {
            visible: sheets.sheet !== null && sheets.sheet.pageCount === 0
            text: qsTr("No records to put on labels.")
            muted: true
        }
        Repeater {
            model: sheets.sheet !== null ? sheets.sheet.pageCount : 0
            delegate: LabelPageItem {
                required property int index
                objectName: "labelPage_" + index
                sheet: sheets.sheet
                page: index
                zoom: (sheets.view.window !== null ? sheets.view.window.baseHost.zoom : 1)
                      * Math.min(1, Math.max(0.3, (sheets.width - Tk.Theme.space.lg * 2 - Tk.Theme.size.scrollbar) / sheets.pageWidth))
                width: implicitWidth
                height: implicitHeight
            }
        }
    }
}
