// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Note

// The live-notes panel (D-057, docs/live-notes.md): what to listen to,
// Start / Pause / Stop, a level meter so the user can see the source is
// alive, what was just heard, and where each stage runs. The notes
// themselves are on the page (one markdown block that keeps improving).
//
// Privacy contract: nothing is recorded or sent before Start, and the
// three stages that can leave the machine say so here before they do.
Tk.Flex {
    id: panel

    required property LiveNotesSession session
    signal settingsRequested()
    // D-059: the session setup — `starting` when Start asked for it.
    signal setupRequested(bool starting)
    signal providersRequested()

    readonly property bool idle: panel.session.state === "idle"
    readonly property var modelEntry: {
        const models = panel.session.models
        for (let i = 0; i < models.length; ++i) if (models[i].id === panel.session.modelId) return models[i]
        return null
    }

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm

    Component.onCompleted: panel.session.checkKeys()

    // ---- controls ----
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        align: Tk.Flex.Center
        Tk.Button {
            objectName: "liveStart"
            Tk.Flex.grow: 1
            variant: "accent"
            iconName: panel.session.state === "listening" ? "pause" : "mic"
            busy: panel.session.state === "starting" || panel.session.state === "finishing"
            available: panel.session.state !== "starting" && panel.session.state !== "finishing"
            text: panel.session.state === "listening" ? qsTr("Pause")
                : panel.session.state === "paused" ? qsTr("Resume")
                : panel.session.state === "finishing" ? qsTr("Finishing…")
                : panel.session.state === "starting" ? qsTr("Starting…") : qsTr("Start live notes")
            onClicked: {
                if (panel.session.state === "listening") panel.session.pause()
                else if (panel.session.state === "paused") panel.session.resume()
                else if (panel.session.askBeforeStart) panel.setupRequested(true)
                else panel.session.start()
            }
        }
        Tk.IconButton {
            objectName: "liveStop"
            iconName: "square"
            tooltip: qsTr("Stop and finish the notes")
            enabled: panel.session.state === "listening" || panel.session.state === "paused" || panel.session.state === "starting"
            onClicked: panel.session.stop()
        }
        Tk.IconButton {
            objectName: "liveSetup"
            iconName: "list-checks"
            tooltip: qsTr("Session setup: kind, focus, what to leave out, sections, instructions")
            onClicked: panel.setupRequested(false)
        }
        Tk.IconButton {
            objectName: "liveSettings"
            iconName: "settings"
            tooltip: qsTr("Live notes settings")
            onClicked: panel.settingsRequested()
        }
    }

    // M56: during a session — flag the moment, catch up.
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        visible: !panel.idle
        Tk.Button {
            objectName: "liveFlag"
            Tk.Flex.grow: 1
            iconName: "star"
            text: panel.session.flagCount > 0 ? qsTr("This matters (%1)").arg(panel.session.flagCount) : qsTr("This matters")
            tooltip: qsTr("Flag this moment (F9): it will be highlighted in the notes.")
            available: panel.session.state === "listening"
            onClicked: panel.session.flag()
        }
        Tk.Button {
            objectName: "liveCatchUp"
            iconName: "history"
            text: qsTr("Catch me up")
            busy: panel.session.catchingUp
            tooltip: qsTr("What was said in the last five minutes")
            onClicked: panel.session.catchUp(5)
        }
    }
    Tk.Notice {
        objectName: "liveCatchUpText"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: panel.session.catchUpText.length > 0
        title: qsTr("The last five minutes")
        text: panel.session.catchUpText
        dismissible: true
    }

    Tk.TextField {
        objectName: "liveTopic"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        enabled: panel.idle
        placeholderText: qsTr("What is it? e.g. “Biology 101 — lecture 4” (optional)")
        text: panel.session.topic
        onEditingFinished: panel.session.topic = text
    }

    // Detail (D-057): how much the notes keep. Live — moving it during a
    // session rewrites the notes at the new level.
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Center
        readonly property var names: [qsTr("Key points"), qsTr("Concise"), qsTr("Balanced"), qsTr("Detailed"), qsTr("Comprehensive")]
        Tk.Label { text: qsTr("Detail"); muted: true }
        Tk.Slider {
            objectName: "liveDetail"
            Tk.Flex.grow: 1
            small: true
            from: 1
            to: 5
            stepSize: 1
            snapMode: T.Slider.SnapAlways
            value: panel.session.detail
            tooltip: qsTr("How much the notes keep. Changing it during a session rewrites the notes.")
            onMoved: panel.session.detail = Math.round(value)
        }
        Tk.Label {
            objectName: "liveDetailName"
            Tk.Flex.basis: 92
            text: parent.names[panel.session.detail - 1]
        }
    }

    Tk.Segmented {
        objectName: "liveSource"
        stretch: true
        small: true
        enabled: panel.idle
        model: [qsTr("Microphone"), qsTr("Desktop audio"), qsTr("Both")]
        currentIndex: Math.max(0, ["microphone", "desktop", "both"].indexOf(panel.session.source))
        onActivated: function(index) { panel.session.source = ["microphone", "desktop", "both"][index] }
    }

    Tk.ProgressBar {
        objectName: "liveLevel"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        thin: true
        from: 0
        to: 1
        value: panel.session.level
        visible: !panel.idle
        tooltip: qsTr("Input level")
    }

    Tk.Label {
        objectName: "liveStatus"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: panel.session.statusText.length > 0
        text: panel.session.statusText + (panel.session.updating ? " ●" : "")
        muted: true
        wrapMode: Text.Wrap
    }
    Tk.Notice {
        objectName: "liveError"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: panel.session.errorText.length > 0
        variant: "warning"
        text: panel.session.errorText
    }

    // ---- what was heard ----
    Tk.Overline { title: qsTr("Heard") }
    Tk.Scroll {
        id: heard
        objectName: "liveTranscript"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 80
        overflowX: Tk.Scroll.Hidden
        Tk.Label {
            width: heard.width - Tk.Theme.space.sm
            text: {
                const lines = panel.session.recentTranscript
                const partial = panel.session.partialText
                return lines + (partial.length > 0 ? (lines.length > 0 ? "\n" : "") + "… " + partial : "")
            }
            wrapMode: Text.Wrap
            selectable: true
            onTextChanged: Qt.callLater(heard.scrollToBottom)
        }
        Tk.EmptyState {
            anchors.centerIn: parent
            width: heard.width - Tk.Theme.space.md
            visible: panel.session.recentTranscript.length === 0 && panel.session.partialText.length === 0
            iconName: "audio-lines"
            title: qsTr("Live notes")
            text: qsTr("Start, then speak, play a lecture or join a call. The notes are written on the page and revised as more is said.")
        }
    }

    // ---- speakers (M56): name them once, the notes use the names ----
    Tk.Overline { title: qsTr("Speakers"); visible: panel.session.speakers.length > 0 }
    Repeater {
        model: panel.session.speakers
        Tk.Flex {
            id: speakerRow
            required property var modelData
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.Label { text: speakerRow.modelData.label; muted: true; Tk.Flex.basis: 80 }
            Tk.TextField {
                Tk.Flex.grow: 1
                small: true
                placeholderText: qsTr("Name")
                text: speakerRow.modelData.name
                onEditingFinished: panel.session.setSpeakerName(speakerRow.modelData.label, text)
            }
        }
    }

    // ---- where each stage runs ----
    Tk.Overline { title: qsTr("Pipeline") }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        align: Tk.Flex.Center
        Tk.Label { text: qsTr("Speech"); muted: true; Tk.Flex.basis: 56 }
        Tk.Label {
            objectName: "liveSpeechEngine"
            Tk.Flex.grow: 1
            elide: Text.ElideRight
            text: panel.session.engine === "whisper" ? qsTr("Local Whisper (%1)").arg(panel.session.whisperModel)
                : panel.session.engine === "http" ? qsTr("%1").arg(panel.session.httpModel)
                : panel.session.speakerLabels ? qsTr("ElevenLabs, with speakers")
                : qsTr("ElevenLabs realtime")
        }
        Tk.Badge { visible: panel.session.audioOffMachine; text: qsTr("Leaves this machine"); variant: "warning" }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        align: Tk.Flex.Center
        Tk.Label { text: qsTr("Notes"); muted: true; Tk.Flex.basis: 56 }
        Tk.Label {
            objectName: "liveModel"
            Tk.Flex.grow: 1
            elide: Text.ElideRight
            text: panel.modelEntry !== null ? panel.modelEntry.name : qsTr("No model — add a provider")
        }
        Tk.Badge { visible: panel.session.modelOffMachine; text: qsTr("Leaves this machine"); variant: "warning" }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        align: Tk.Flex.Center
        visible: panel.session.gate !== "off"
        Tk.Label { text: qsTr("Gate"); muted: true; Tk.Flex.basis: 56 }
        Tk.Label {
            objectName: "liveGate"
            Tk.Flex.grow: 1
            elide: Text.ElideRight
            text: panel.session.gate === "jev" ? qsTr("TypeSafe Jev") : qsTr("Laya (%1)").arg(panel.session.gateBaseUrl)
        }
        Tk.Badge { visible: panel.session.gateOffMachine; text: qsTr("Leaves this machine"); variant: "warning" }
    }
    // Usage (M56): what this session has used so far.
    Tk.Label {
        objectName: "liveUsage"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: !panel.idle || panel.session.usage.requests > 0
        muted: true
        wrapMode: Text.Wrap
        text: {
            const u = panel.session.usage
            let parts = [qsTr("%1 min of audio sent").arg(u.audioMinutes.toFixed(1)),
                         qsTr("%1 model calls").arg(u.requests),
                         qsTr("%1k tokens in / %2k out").arg((u.inputTokens / 1000).toFixed(1)).arg((u.outputTokens / 1000).toFixed(1))]
            if (u.gateCalls > 0) parts.push(qsTr("%1 gate calls").arg(u.gateCalls))
            if (u.screens > 0) parts.push(qsTr("%1 screens").arg(u.screens))
            if (u.terms > 0) parts.push(qsTr("%1 terms learned").arg(u.terms))
            return qsTr("Used: ") + parts.join(" · ")
        }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        Tk.Button { objectName: "liveProviders"; small: true; text: qsTr("AI providers…"); onClicked: panel.providersRequested() }
        Tk.Spacer { }
        Tk.Button { objectName: "liveCopyTranscript"; small: true; text: qsTr("Copy transcript"); available: panel.session.segmentCount > 0; onClicked: panel.session.copyTranscript() }
    }
}
