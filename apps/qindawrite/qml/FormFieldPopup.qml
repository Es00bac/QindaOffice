// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The choices of a Word form field (WriteControls, write_controls.h): a
// drop-down list's or combo box's items, or a calendar for a date field,
// opened where the field was clicked. A child of the page item, so the
// click's item coordinates place it.
Item {
    id: root

    required property var controls
    // Month grids and ISO dates for the calendar.
    readonly property Tk.LocaleService localeService: Tk.LocaleService { }

    width: 1
    height: 1

    Tk.Menu {
        id: listMenu
        objectName: "formListMenu"
        title: root.controls.title
        onClosed: if (root.controls.kind === "list") root.controls.dismiss()
        Instantiator {
            model: root.controls.items
            delegate: Tk.MenuItem {
                required property int index
                required property string modelData
                objectName: "formListItem_" + index
                text: modelData
                onTriggered: root.controls.choose(index)
            }
            onObjectAdded: function(index, object) { listMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { listMenu.removeItem(object) }
        }
    }

    Tk.Popover {
        id: datePopover
        objectName: "formDatePopover"
        anchorItem: root
        onClosed: if (root.controls.kind === "date") root.controls.dismiss()
        Tk.CalendarPicker {
            id: picker
            objectName: "formDatePicker"
            localeService: root.localeService
            tooltip: root.controls.title
            selectedDate: root.controls.date
            onDateSelected: function(isoDate) {
                root.controls.chooseDate(isoDate)
                datePopover.close()
            }
        }
    }

    Connections {
        target: root.controls
        function onPopupRequested(x, y) {
            root.x = x
            root.y = y
            if (root.controls.kind === "list") {
                listMenu.popup(root, 0, 0)
            } else if (root.controls.kind === "date") {
                picker.showDate(root.controls.date)
                datePopover.open()
            }
        }
    }
}
