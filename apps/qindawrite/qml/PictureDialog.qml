// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Format ▸ Picture (or a double-click on a picture): its size, in inches
// or centimetres (the host speaks 96-dpi document px), with the aspect
// ratio locked by default, a reset to the picture's own size, and the
// alternative text screen readers announce (Word's alt text). The bytes
// of the picture never change; only how big it is drawn.
Tk.Dialog {
    id: dialog
    signal pictureApplied(var picture)
    property real naturalWidth: 0
    property real naturalHeight: 0
    property real ratio: 1
    property bool syncing: false
    readonly property real pxPerUnit: units.currentIndex === 0 ? 96 : 96 / 2.54
    readonly property string unitSuffix: units.currentIndex === 0 ? qsTr(" in") : qsTr(" cm")
    objectName: "pictureDialog"
    title: qsTr("Picture")
    dialogWidth: 420
    primaryText: qsTr("Apply")

    function load(info) {
        syncing = true
        naturalWidth = info.naturalWidth || 0
        naturalHeight = info.naturalHeight || 0
        ratio = info.height > 0 ? info.width / info.height : 1
        width_.value = info.width / pxPerUnit
        height_.value = info.height / pxPerUnit
        altText.text = info.altText || ""
        lock.checked = true
        syncing = false
    }
    onAccepted: dialog.pictureApplied({ width: width_.value * pxPerUnit, height: height_.value * pxPerUnit,
                                        altText: altText.text })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Units")
            Tk.Segmented {
                id: units
                objectName: "pictureUnits"
                model: [qsTr("Inches"), qsTr("Centimetres")]
                property real previous: 96
                onCurrentIndexChanged: {
                    const now = currentIndex === 0 ? 96 : 96 / 2.54
                    dialog.syncing = true
                    width_.value = width_.value * previous / now
                    height_.value = height_.value * previous / now
                    dialog.syncing = false
                    previous = now
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Width / height")
            Tk.Flex {
                gap: Tk.Theme.space.sm
                Tk.NumberField {
                    id: width_
                    objectName: "pictureWidth"
                    from: 0.05; to: 60; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix
                    onValueChanged: if (!dialog.syncing && lock.checked) {
                        dialog.syncing = true
                        height_.value = value / dialog.ratio
                        dialog.syncing = false
                    }
                }
                Tk.NumberField {
                    id: height_
                    objectName: "pictureHeight"
                    from: 0.05; to: 60; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix
                    onValueChanged: if (!dialog.syncing && lock.checked) {
                        dialog.syncing = true
                        width_.value = value * dialog.ratio
                        dialog.syncing = false
                    }
                }
            }
        }
        Tk.Flex {
            gap: Tk.Theme.space.sm
            Tk.CheckBox {
                id: lock
                objectName: "pictureLockAspect"
                text: qsTr("Lock aspect ratio")
                onCheckedChanged: if (checked && height_.value > 0) dialog.ratio = width_.value / height_.value
            }
            Tk.Button {
                objectName: "pictureResetSize"
                text: qsTr("Reset size")
                enabled: dialog.naturalWidth > 0 && dialog.naturalHeight > 0
                onClicked: {
                    dialog.syncing = true
                    dialog.ratio = dialog.naturalWidth / dialog.naturalHeight
                    width_.value = dialog.naturalWidth / dialog.pxPerUnit
                    height_.value = dialog.naturalHeight / dialog.pxPerUnit
                    dialog.syncing = false
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Alt text")
            hint: qsTr("Describes the picture for people who use a screen reader.")
            Tk.TextField { id: altText; objectName: "pictureAltText"; width: 260; placeholderText: qsTr("Description") }
        }
    }
}
