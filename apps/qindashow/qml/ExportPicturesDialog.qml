// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Export ▸ Slides as Pictures (M91, D-099), as PowerPoint's Save As
// picture: PNG or JPEG, every slide (into a folder named after the file:
// Slide1.png, Slide2.png …) or just the current one, at a width in pixels
// (the height follows the slide size). Export asks for the file, then
// writes them (ShowExport::exportPictures), drawn by the slide renderer.
Tk.Dialog {
    id: dialog
    required property ShowHost showHost
    readonly property bool jpeg: format.currentIndex === 1
    readonly property var widths: [1280, 1920, 3840]
    signal fileWanted()

    function write(file) {
        dialog.showHost.exporter.exportPictures(dialog.jpeg ? "jpeg" : "png", every.checked, dialog.widths[size.currentIndex], file)
    }

    objectName: "exportPicturesDialog"
    title: qsTr("Export Slides as Pictures")
    primaryText: qsTr("Export…")
    primaryEnabled: dialog.showHost.slideCount > 0
    dialogWidth: Tk.Theme.size.dialogWidth
    onAccepted: dialog.fileWanted()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Format")
            Tk.Segmented {
                id: format
                objectName: "picturesFormat"
                small: true
                model: [qsTr("PNG"), qsTr("JPEG")]
            }
        }
        Tk.PropertyRow {
            label: qsTr("Width")
            Tk.ComboBox {
                id: size
                objectName: "picturesWidth"
                width: Tk.Theme.size.dialogWidth * 0.36
                model: [qsTr("1280 pixels"), qsTr("1920 pixels"), qsTr("3840 pixels")]
                currentIndex: 1
            }
        }
        Tk.RadioButton {
            id: every
            objectName: "picturesAll"
            text: qsTr("All slides (a folder of pictures)")
            checked: true
        }
        Tk.RadioButton {
            objectName: "picturesCurrent"
            text: qsTr("Just this one (slide %1)").arg(dialog.showHost.currentSlide + 1)
        }
    }
}
