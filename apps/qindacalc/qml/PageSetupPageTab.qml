// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Page Setup ▸ Page (M78, D-087): orientation, scaling — Adjust to N %
// or Fit to W pages wide by H tall (0 = automatic, Excel's blank) —, the
// paper size and the first page number ("" = Auto). Edits the dialog's
// draft; built afresh each time the dialog opens, so its bindings follow
// the draft it was opened with.
Loader {
    id: tab

    required property var dialog

    active: tab.dialog.visible
    sourceComponent: Component {
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.SectionHeader { title: qsTr("Orientation") }
            Tk.Segmented {
                objectName: "pageOrientation"
                model: [{ text: qsTr("Portrait"), iconName: "file" }, { text: qsTr("Landscape"), iconName: "file-text" }]
                currentIndex: tab.dialog.draft.orientation === 1 ? 1 : 0
                onActivated: function(index) { tab.dialog.set("orientation", index) }
            }
            Tk.SectionHeader { title: qsTr("Scaling") }
            Tk.Segmented {
                objectName: "pageScaling"
                model: [qsTr("Adjust to"), qsTr("Fit to")]
                currentIndex: tab.dialog.draft.fitToPage === true ? 1 : 0
                onActivated: function(index) { tab.dialog.set("fitToPage", index === 1) }
            }
            Tk.PropertyRow {
                visible: tab.dialog.draft.fitToPage !== true
                label: qsTr("Adjust to")
                Tk.NumberField {
                    objectName: "pageScale"
                    from: 10
                    to: 400
                    stepSize: 5
                    suffix: qsTr("% normal size")
                    value: tab.dialog.draft.scale || 100
                    onValueModified: function(v) { tab.dialog.set("scale", Math.round(v)) }
                }
            }
            Tk.PropertyRow {
                visible: tab.dialog.draft.fitToPage === true
                label: qsTr("Fit to")
                hint: qsTr("0 leaves that direction automatic")
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.sm
                    align: Tk.Flex.Center
                    Tk.NumberField {
                        objectName: "pageFitWidth"
                        from: 0
                        to: 32767
                        suffix: qsTr(" wide")
                        value: tab.dialog.draft.fitToWidth || 0
                        onValueModified: function(v) { tab.dialog.set("fitToWidth", Math.round(v)) }
                    }
                    Tk.Label { text: qsTr("by") }
                    Tk.NumberField {
                        objectName: "pageFitHeight"
                        from: 0
                        to: 32767
                        suffix: qsTr(" tall")
                        value: tab.dialog.draft.fitToHeight || 0
                        onValueModified: function(v) { tab.dialog.set("fitToHeight", Math.round(v)) }
                    }
                }
            }
            Tk.Divider { }
            Tk.PropertyRow {
                label: qsTr("Paper size")
                Tk.ComboBox {
                    objectName: "pagePaperSize"
                    width: Tk.Theme.size.fieldWidth * 2
                    model: (tab.dialog.draft.papers || []).map(function(p) { return p.name })
                    currentIndex: Math.max(0, (tab.dialog.draft.papers || []).findIndex(function(p) { return p.code === tab.dialog.draft.paperSize }))
                    onActivated: function(index) { tab.dialog.set("paperSize", tab.dialog.draft.papers[index].code) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("First page number")
                Tk.TextField {
                    objectName: "pageFirstNumber"
                    placeholderText: qsTr("Auto")
                    text: tab.dialog.draft.firstPageNumber || ""
                    onTextEdited: tab.dialog.set("firstPageNumber", text)
                }
            }
        }
    }
}
