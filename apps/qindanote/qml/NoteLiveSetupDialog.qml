// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note

// Live notes session setup (D-059): the pre-qualifiers the note-taker gets
// before a session — what it is, the kind of session (a scenario with its
// own emphasis), what to focus on and what to leave out, which optional
// sections to keep, and custom instructions. Remembered between sessions.
// Opened by Start (unless "Show this before every session" is off) and by
// the panel's setup button, where it saves without starting (a running
// session takes the changes from its next update).
Tk.Dialog {
    id: dialog

    required property LiveNotesSession session
    // true: the primary button starts the session.
    property bool starting: true

    objectName: "liveSetupDialog"
    title: dialog.starting ? qsTr("New live notes session") : qsTr("Live notes session setup")
    dialogWidth: 640
    primaryText: dialog.starting ? qsTr("Start") : qsTr("Save")

    function load() {
        const s = dialog.session.setup
        topic.text = dialog.session.topic
        const ids = dialog.session.scenarios.map(function(e) { return e.id })
        scenario.currentIndex = Math.max(0, ids.indexOf(s.scenario))
        focus.text = s.focus
        skip.text = s.skip
        custom.text = s.custom
        timestamps.checked = s.timestamps
        quotes.checked = s.quotes
        actionItems.checked = s.actionItems
        openQuestions.checked = s.openQuestions
        definitions.checked = s.definitions
        askAgain.checked = dialog.session.askBeforeStart
        speakers.checked = dialog.session.speakerLabels
    }

    onAboutToShow: load()
    onAccepted: {
        dialog.session.topic = topic.text
        dialog.session.setup = {
            scenario: dialog.session.scenarios[scenario.currentIndex].id,
            focus: focus.text, skip: skip.text, custom: custom.text,
            timestamps: timestamps.checked, quotes: quotes.checked, actionItems: actionItems.checked,
            openQuestions: openQuestions.checked, definitions: definitions.checked
        }
        dialog.session.askBeforeStart = askAgain.checked
        dialog.session.speakerLabels = speakers.checked
        if (dialog.starting) dialog.session.start()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.PropertyRow {
            label: qsTr("What is it?")
            Tk.TextField {
                id: topic
                objectName: "liveSetupTopic"
                placeholderText: qsTr("e.g. “Biology 101 — lecture 4”, “Weekly sync with design”")
            }
        }
        Tk.PropertyRow {
            label: qsTr("Kind")
            hint: scenario.currentIndex >= 0 ? dialog.session.scenarios[scenario.currentIndex].hint : ""
            Tk.ComboBox {
                id: scenario
                objectName: "liveSetupScenario"
                model: dialog.session.scenarios.map(function(e) { return e.name })
            }
        }
        Tk.PropertyRow {
            label: qsTr("Focus on")
            hint: qsTr("What you care about: it gets the most space and detail.")
            Tk.TextArea {
                id: focus
                objectName: "liveSetupFocus"
                rows: 2
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("e.g. anything that will be on the exam; pricing and the launch date")
            }
        }
        Tk.PropertyRow {
            label: qsTr("Leave out")
            hint: qsTr("What you don't care about: never noted.")
            Tk.TextArea {
                id: skip
                objectName: "liveSetupSkip"
                rows: 2
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("e.g. small talk, the sponsor segment, scheduling logistics")
            }
        }
        Tk.PropertyRow {
            label: qsTr("Include")
            alignTop: true
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.CheckBox { id: timestamps; objectName: "liveSetupTimestamps"; text: qsTr("Timestamps on sections") }
                Tk.CheckBox { id: quotes; objectName: "liveSetupQuotes"; text: qsTr("Key quotes, word for word") }
                Tk.CheckBox { id: actionItems; objectName: "liveSetupActionItems"; text: qsTr("Action items and deadlines") }
                Tk.CheckBox { id: openQuestions; objectName: "liveSetupOpenQuestions"; text: qsTr("Open questions") }
                Tk.CheckBox { id: definitions; objectName: "liveSetupDefinitions"; text: qsTr("A definitions glossary") }
                Tk.CheckBox {
                    id: speakers
                    objectName: "liveSetupSpeakers"
                    enabled: dialog.session.engine === "elevenlabs"
                    text: qsTr("Who said what (speaker labels)")
                    tooltip: dialog.session.engine === "elevenlabs"
                             ? qsTr("ElevenLabs tells the speakers apart; the text then arrives about every 45 seconds instead of live.")
                             : qsTr("Needs the ElevenLabs speech engine (Settings).")
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Instructions")
            hint: qsTr("Anything else, in your own words. It wins over the defaults.")
            Tk.TextArea {
                id: custom
                objectName: "liveSetupCustom"
                rows: 3
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("e.g. write the notes in Spanish; use my course's terms; tag anything about Project Atlas")
            }
        }
        Tk.CheckBox {
            id: askAgain
            objectName: "liveSetupAskAgain"
            text: qsTr("Show this before every session")
        }
    }
}
