// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// View ▸ New Window (M82, D-091): another window on the same workbook, as
// Excel's "Book1:2" — any of its sheets, with its own selection, scroll,
// zoom and Split, showing the document window's edits as they are made.
// It views: editing stays in the document window.
//
// AGENT-NOTE: AGENTS.md rule 1 (ADR-0110) gives a document ONE document
// window; this is a view of it — owned by the document window (made with
// it as the parent object, destroyed with it), no menus, no document
// commands — as ChartWindows.qml's chart windows are. It is sized once,
// before it is shown, and never resized after (ADR-0117); where windows go
// is the desktop's (QindaQt's containers arrange them), so there is no
// app-side Arrange All.
// AGENT-GUARD: a QtObject, not an Item — the document window seats ONE
// content child (Tk.AppWindow).
QtObject {
    id: root

    required property CalcHost host
    property Window parentWindow: null
    // Windows made so far: the next one's number (the document window is 1).
    property int made: 0

    function open() {
        root.made += 1
        const names = root.host.viewableSheets()
        const active = root.host.sheetNames[root.host.activeSheet] || ""
        const viewWindow = viewComponent.createObject(root, {
            number: root.made + 1,
            sheet: names.indexOf(active) >= 0 ? active : (names[0] || ""),
            width: root.parentWindow !== null ? Math.round(root.parentWindow.width * 0.8) : 960,
            height: root.parentWindow !== null ? Math.round(root.parentWindow.height * 0.8) : 640
        })
        viewWindow.show()
        viewWindow.requestActivate()
        return viewWindow
    }

    property Connections requests: Connections {
        target: root.host
        function onViewWindowRequested() { root.open() }
    }

    property Component viewComponent: Component {
        Window {
            id: viewWindow
            property int number: 2
            property string sheet: ""
            property var sheets: root.host.viewableSheets()
            objectName: "calcViewWindow"
            title: qsTr("%1:%2").arg(root.host.title).arg(viewWindow.number)
            color: Tk.Theme.color.surface
            onClosing: Qt.callLater(viewWindow.destroy)

            Connections {
                target: root.host
                function onSheetsChanged() { viewWindow.sheets = root.host.viewableSheets() }
            }
            Shortcut {
                sequences: [StandardKey.Close]
                onActivated: viewWindow.close()
            }

            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Column
                gap: 0
                Tk.Box {
                    objectName: "viewWindowBar"
                    color: Tk.Theme.color.panel
                    padding: Tk.Theme.space.xs
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.sm
                        wrap: Tk.Flex.Wrap
                        Tk.ComboBox {
                            objectName: "viewWindowSheet"
                            small: true
                            model: viewWindow.sheets
                            currentIndex: Math.max(0, viewWindow.sheets.indexOf(viewWindow.sheet))
                            onActivated: function(index) { viewWindow.sheet = viewWindow.sheets[index] }
                        }
                        Tk.Caption {
                            objectName: "viewWindowNote"
                            muted: true
                            text: qsTr("A view of the workbook: edit it in its window.")
                        }
                    }
                }
                CalcGrid {
                    objectName: "viewWindowGrid"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    Tk.Flex.minHeight: 0
                    host: root.host
                    viewOnly: true
                    viewSheet: viewWindow.sheet
                }
            }
        }
    }
}
