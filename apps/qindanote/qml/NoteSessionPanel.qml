// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtMultimedia
import QindaTK as Tk
import QindaOffice.Note

// Session review (M56, D-061) for the open page: the session's recording
// (Ctrl+click a [12:40] in the notes, or a section here, plays from
// there), the sections with their times, the spots the note-taker could
// not make out (listen, then correct), and what to make of the notes —
// a study page, action items for QindaMail, pages per topic, export.
Tk.Flex {
    id: panel

    required property NoteHost note
    readonly property var session: panel.note.pageSession
    property int pendingSeek: -1

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm

    function timeText(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        const mm = Math.floor(s / 60) % 60, hh = Math.floor(s / 3600)
        const pad = function(n) { return (n < 10 ? "0" : "") + n }
        return (hh > 0 ? hh + ":" + pad(mm) : pad(mm)) + ":" + pad(s % 60)
    }
    // AGENT-NOTE: the player is created on the first Play, not with the
    // panel — a MediaPlayer opens the audio device (PipeWire) as soon as it
    // exists, which a panel that is merely open should not do (and the
    // headless tests must not).
    property QtObject player: null
    readonly property bool playing: panel.player !== null && panel.player.playbackState === MediaPlayer.PlayingState
    function ensurePlayer() {
        if (panel.player === null) panel.player = playerComponent.createObject(panel)
        return panel.player
    }
    function playAt(ms) {
        if (panel.session.audioUrl === undefined || panel.session.audioUrl.length === 0) return
        const p = panel.ensurePlayer()
        if (p.mediaStatus === MediaPlayer.LoadedMedia || p.mediaStatus === MediaPlayer.BufferedMedia
                || p.mediaStatus === MediaPlayer.EndOfMedia) {
            p.position = ms
            p.play()
        } else {
            panel.pendingSeek = ms
            p.play()
        }
    }
    function togglePlay() {
        const p = panel.ensurePlayer()
        if (p.playbackState === MediaPlayer.PlayingState) p.pause(); else p.play()
    }

    Component {
        id: playerComponent
        MediaPlayer {
            source: panel.session.audioUrl !== undefined ? panel.session.audioUrl : ""
            audioOutput: AudioOutput { }
            onMediaStatusChanged: {
                if (panel.pendingSeek >= 0 && (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)) {
                    position = panel.pendingSeek
                    panel.pendingSeek = -1
                    play()
                }
            }
        }
    }
    Connections {
        target: panel.note
        function onPlayRecordingRequested(ms) { panel.playAt(ms) }
    }

    Tk.EmptyState {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: !panel.session.hasNotes
        iconName: "clipboard-list"
        title: qsTr("Session review")
        text: qsTr("Open a page with notes — a live-notes page shows its recording, its sections and anything the note-taker could not make out.")
    }

    // ---- recording ----
    Tk.Overline { title: qsTr("Recording"); visible: panel.session.hasNotes }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        align: Tk.Flex.Center
        visible: panel.session.hasNotes && panel.session.audioUrl.length > 0
        Tk.IconButton {
            objectName: "sessionPlay"
            iconName: panel.playing ? "pause" : "play"
            tooltip: qsTr("Play or pause the recording")
            onClicked: panel.togglePlay()
        }
        Tk.Slider {
            objectName: "sessionPosition"
            Tk.Flex.grow: 1
            small: true
            from: 0
            to: panel.player !== null ? Math.max(1, panel.player.duration) : 1
            value: panel.player !== null ? panel.player.position : 0
            onMoved: panel.ensurePlayer().position = value
        }
        Tk.Label {
            objectName: "sessionTime"
            muted: true
            text: panel.player !== null ? panel.timeText(panel.player.position) + " / " + panel.timeText(panel.player.duration)
                                        : qsTr("Not playing")
        }
    }
    Tk.Label {
        visible: panel.session.hasNotes && panel.session.audioUrl.length === 0
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        muted: true
        wrapMode: Text.Wrap
        text: qsTr("No recording kept for this page (Live notes ▸ setup ▸ Keep the recording).")
    }

    Tk.Scroll {
        id: scroll
        objectName: "sessionScroll"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 60
        overflowX: Tk.Scroll.Hidden
        visible: panel.session.hasNotes
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            width: scroll.width - Tk.Theme.space.sm

            // ---- sections ----
            Tk.Overline { title: qsTr("Sections"); visible: panel.session.sections.length > 0 }
            Repeater {
                model: panel.session.sections
                Tk.ListRow {
                    required property var modelData
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    small: true
                    iconName: "play"
                    text: modelData.heading
                    secondaryText: panel.timeText(modelData.ms)
                    enabled: panel.session.audioUrl.length > 0
                    onClicked: panel.playAt(modelData.ms)
                }
            }

            // ---- unclear spots ----
            Tk.Overline {
                title: qsTr("Check these (%1)").arg(panel.session.unclear.length)
                visible: panel.session.unclear.length > 0
            }
            Repeater {
                model: panel.session.unclear
                Tk.Flex {
                    id: spot
                    required property var modelData
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.xs
                    align: Tk.Flex.Center
                    Tk.IconButton {
                        iconName: "play"
                        small: true
                        enabled: spot.modelData.ms >= 0 && panel.session.audioUrl.length > 0
                        tooltip: qsTr("Listen from a few seconds before")
                        onClicked: panel.playAt(Math.max(0, spot.modelData.ms - 3000))
                    }
                    Tk.TextField {
                        id: fix
                        Tk.Flex.grow: 1
                        small: true
                        placeholderText: spot.modelData.guess.length > 0 ? spot.modelData.guess : qsTr("What was said")
                        text: spot.modelData.guess.replace(/\?$/, "")
                    }
                    Tk.Button {
                        small: true
                        text: qsTr("Fix")
                        available: fix.text.trim().length > 0
                        onClicked: panel.note.resolveUnclear(spot.modelData.text, fix.text)
                    }
                }
            }
        }
    }

    // ---- make more of it ----
    Tk.Overline { title: qsTr("Make more of it"); visible: panel.session.hasNotes }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        visible: panel.session.hasNotes
        Tk.Button { objectName: "sessionStudy"; small: true; iconName: "book-open"; text: qsTr("Study page"); available: !panel.note.live.working; onClicked: panel.note.actions.trigger("noteStudyPageAction") }
        Tk.Button { objectName: "sessionTasks"; small: true; iconName: "list-todo"; text: qsTr("Tasks → QindaMail"); available: !panel.note.live.working; onClicked: panel.note.actions.trigger("noteActionItemsToMailAction") }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        visible: panel.session.hasNotes
        Tk.Button { objectName: "sessionSplit"; small: true; iconName: "split"; text: qsTr("Pages by topic"); onClicked: panel.note.actions.trigger("noteSplitByTopicAction") }
    }
    Tk.Overline { title: qsTr("Export"); visible: panel.session.hasNotes }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        visible: panel.session.hasNotes
        Tk.Button { objectName: "sessionExportMd"; small: true; iconName: "file-text"; text: qsTr("Markdown…"); onClicked: panel.note.actions.trigger("noteExportMarkdownAction") }
        Tk.Button { objectName: "sessionOpenWrite"; small: true; iconName: "square-pen"; text: qsTr("QindaWrite"); onClicked: panel.note.actions.trigger("noteOpenInWriteAction") }
        Tk.Button { objectName: "sessionAnki"; small: true; iconName: "layers"; text: qsTr("Anki…"); available: panel.session.hasCards; onClicked: panel.note.actions.trigger("noteExportAnkiAction") }
    }
}
