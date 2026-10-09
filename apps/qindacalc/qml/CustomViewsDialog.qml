// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// View ▸ Custom Views (M82, D-091), Excel's dialog: the workbook's views;
// Show puts the chosen one back (and closes, as Excel's does), Add… names
// the workbook as it is now (CustomViewAddDialog.qml), Delete removes one.
// Each change is one undo step (CalcHost, calc_host_views.cpp).
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var views: []
    property int selected: -1
    property string note: ""
    readonly property var current: dialog.selected >= 0 && dialog.selected < dialog.views.length
                                    ? dialog.views[dialog.selected] : null

    signal addRequested()

    objectName: "customViewsDialog"
    title: qsTr("Custom Views")
    primaryText: qsTr("Close")
    secondaryText: ""

    function load() {
        dialog.views = dialog.host.customViewList()
        dialog.selected = dialog.views.length > 0 ? Math.min(Math.max(0, dialog.selected), dialog.views.length - 1) : -1
        dialog.note = ""
    }
    function show() {
        const problem = dialog.host.showCustomView(dialog.current.guid)
        if (problem.length > 0) {
            dialog.note = problem
            dialog.load()
            return
        }
        dialog.close()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.SectionHeader { title: qsTr("Views"); count: String(dialog.views.length) }
        Tk.Scroll {
            objectName: "customViewList"
            implicitHeight: Tk.Theme.size.row * 6
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: dialog.views
                    Tk.ListRow {
                        required property var modelData
                        required property int index
                        objectName: "customView_" + index
                        text: modelData.name
                        selected: dialog.selected === index
                        onClicked: dialog.selected = index
                        onDoubleClicked: { dialog.selected = index; dialog.show() }
                    }
                }
                Tk.Caption {
                    visible: dialog.views.length === 0
                    muted: true
                    text: qsTr("No views yet. Choose Add to name the workbook as it looks now.")
                }
            }
        }
        Tk.Notice { objectName: "customViewNote"; visible: dialog.note.length > 0; variant: "danger"; text: dialog.note }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            wrap: Tk.Flex.Wrap
            Tk.Button {
                objectName: "customViewShow"; small: true; iconName: "eye"; text: qsTr("Show"); available: dialog.current !== null
                onClicked: dialog.show()
            }
            Tk.Button { objectName: "customViewAdd"; small: true; iconName: "plus"; text: qsTr("Add…"); onClicked: dialog.addRequested() }
            Tk.Button {
                objectName: "customViewDelete"; small: true; iconName: "trash-2"; text: qsTr("Delete"); available: dialog.current !== null
                onClicked: { dialog.host.deleteCustomView(dialog.current.guid); dialog.load() }
            }
        }
    }
}
