// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Playback ▸ Trim, Fade and Bookmarks… (M91, D-099): PowerPoint's Trim
// Video / Trim Audio (start and end times), Fade Duration (in and out) and
// bookmarks (a name and a time each) for the selected video or sound, in
// seconds. The show plays from the start time to the end time, fades as
// set, and Alt+End / Alt+Home jump between bookmarks. One undo step
// (ShowMedia::applyMediaEdits); written as p14:trim, p14:fade, p14:bmkLst.
Tk.Dialog {
    id: dialog
    required property ShowHost showHost
    property var edits: ({})
    property var bookmarks: []
    // 0 when the file does not say how long it plays: the end is then
    // given as the time cut off the end.
    readonly property real mediaDuration: (dialog.edits.durationMs || 0) / 1000

    function seconds(ms) { return Math.round((ms || 0) / 10) / 100 }
    function setBookmark(index, key, value) {
        const next = dialog.bookmarks.slice()
        const item = Object.assign({}, next[index])
        item[key] = value
        next[index] = item
        dialog.bookmarks = next
    }

    objectName: "mediaEditDialog"
    title: qsTr("Trim, Fade and Bookmarks")
    subtitle: dialog.edits.name || ""
    primaryText: qsTr("OK")
    dialogWidth: Tk.Theme.size.dialogWidth
    onAboutToShow: {
        dialog.edits = dialog.showHost.media.mediaEdits()
        dialog.bookmarks = (dialog.edits.bookmarks || []).slice()
        startField.value = dialog.seconds(dialog.edits.trimStartMs)
        endField.value = dialog.mediaDuration > 0 ? dialog.seconds(dialog.edits.durationMs - dialog.edits.trimEndMs)
                                             : dialog.seconds(dialog.edits.trimEndMs)
        fadeInField.value = dialog.seconds(dialog.edits.fadeInMs)
        fadeOutField.value = dialog.seconds(dialog.edits.fadeOutMs)
    }
    onAccepted: {
        const endMs = Math.round(endField.value * 1000)
        dialog.showHost.media.applyMediaEdits({
            trimStartMs: Math.round(startField.value * 1000),
            trimEndMs: dialog.mediaDuration > 0 ? Math.max(0, (dialog.edits.durationMs || 0) - endMs) : endMs,
            fadeInMs: Math.round(fadeInField.value * 1000),
            fadeOutMs: Math.round(fadeOutField.value * 1000),
            bookmarks: dialog.bookmarks
        })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Overline { text: qsTr("Trim") }
        Tk.PropertyRow {
            label: qsTr("Start time")
            Tk.NumberField {
                id: startField
                objectName: "mediaTrimStart"
                from: 0
                to: dialog.mediaDuration > 0 ? dialog.mediaDuration : 86400
                decimals: 2
                stepSize: 0.1
                scrub: false
                suffix: qsTr(" s")
                onValueModified: function(next) { value = next }
            }
        }
        Tk.PropertyRow {
            label: dialog.mediaDuration > 0 ? qsTr("End time") : qsTr("Cut from the end")
            hint: dialog.mediaDuration > 0 ? qsTr("The file plays %1 s").arg(dialog.mediaDuration.toFixed(2)) : ""
            Tk.NumberField {
                id: endField
                objectName: "mediaTrimEnd"
                from: 0
                to: dialog.mediaDuration > 0 ? dialog.mediaDuration : 86400
                decimals: 2
                stepSize: 0.1
                scrub: false
                suffix: qsTr(" s")
                onValueModified: function(next) { value = next }
            }
        }
        Tk.Overline { text: qsTr("Fade duration") }
        Tk.PropertyRow {
            label: qsTr("Fade in")
            Tk.NumberField { id: fadeInField; objectName: "mediaFadeIn"; from: 0; to: 60; decimals: 2; stepSize: 0.25; scrub: false; suffix: qsTr(" s"); onValueModified: function(next) { value = next } }
        }
        Tk.PropertyRow {
            label: qsTr("Fade out")
            Tk.NumberField { id: fadeOutField; objectName: "mediaFadeOut"; from: 0; to: 60; decimals: 2; stepSize: 0.25; scrub: false; suffix: qsTr(" s"); onValueModified: function(next) { value = next } }
        }
        Tk.Overline { text: qsTr("Bookmarks") }
        Repeater {
            model: dialog.bookmarks
            Tk.Flex {
                required property var modelData
                required property int index
                direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.xs
                align: Tk.Flex.Center
                Tk.TextField {
                    objectName: "mediaBookmarkName_" + index
                    width: Tk.Theme.size.dialogWidth * 0.36
                    text: modelData.name
                    onTextEdited: dialog.setBookmark(index, "name", text)
                }
                Tk.NumberField {
                    objectName: "mediaBookmarkTime_" + index
                    implicitWidth: Tk.Theme.size.control * 3
                    from: 0
                    to: dialog.mediaDuration > 0 ? dialog.mediaDuration : 86400
                    decimals: 2
                    stepSize: 0.1
                    scrub: false
                    suffix: qsTr(" s")
                    value: dialog.seconds(modelData.timeMs)
                    onValueModified: dialog.setBookmark(index, "timeMs", Math.round(value * 1000))
                }
                Tk.IconButton {
                    objectName: "mediaBookmarkRemove_" + index
                    iconName: "trash"
                    small: true
                    tooltip: qsTr("Remove Bookmark")
                    onClicked: {
                        const next = dialog.bookmarks.slice()
                        next.splice(index, 1)
                        dialog.bookmarks = next
                    }
                }
            }
        }
        Tk.Button {
            objectName: "mediaBookmarkAdd"
            text: qsTr("Add Bookmark")
            iconName: "bookmark"
            small: true
            onClicked: dialog.bookmarks = dialog.bookmarks.concat([{ name: qsTr("Bookmark %1").arg(dialog.bookmarks.length + 1),
                                                                     timeMs: Math.round(startField.value * 1000) }])
        }
    }
}
