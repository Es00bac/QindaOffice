// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Note

// Live notes session setup (D-059): the pre-qualifiers the note-taker gets
// before a session — what it is, the kind of session (a scenario with its
// own emphasis), how the notes are organised and how they look (M57,
// D-063: layout, the user's own sections, style), what to focus on and
// what to leave out, which optional sections to keep, and custom
// instructions. Remembered between sessions.
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
        const layoutIds = dialog.session.layouts.map(function(e) { return e.id })
        layout.currentIndex = Math.max(0, layoutIds.indexOf(s.layout))
        sections.text = s.sections
        const styleIds = dialog.session.styles.map(function(e) { return e.id })
        look.currentIndex = Math.max(0, styleIds.indexOf(s.style))
        timeline.checked = s.timeline
        focus.text = s.focus
        skip.text = s.skip
        custom.text = s.custom
        timestamps.checked = s.timestamps
        quotes.checked = s.quotes
        actionItems.checked = s.actionItems
        openQuestions.checked = s.openQuestions
        definitions.checked = s.definitions
        askAgain.checked = dialog.session.askBeforeStart
        jargon.checked = s.explainJargon
        recording.checked = dialog.session.keepRecording
        screen.checked = dialog.session.captureScreen
        series.checked = dialog.session.continueSeries
        split.checked = dialog.session.splitLongSessions
        speakers.checked = dialog.session.speakerLabels
    }

    function useSuggestedStructure() {
        const id = dialog.session.scenarios[scenario.currentIndex].id
        const s = dialog.session.suggestedSetup(id)
        layout.currentIndex = Math.max(0, dialog.session.layouts.map(e => e.id).indexOf(s.layout))
        look.currentIndex = Math.max(0, dialog.session.styles.map(e => e.id).indexOf(s.style))
        sections.text = s.sections
        timeline.checked = s.timeline
        quotes.checked = s.quotes
        actionItems.checked = s.actionItems
        openQuestions.checked = s.openQuestions
        definitions.checked = s.definitions
        if (id === "appointment") { recording.checked = false; screen.checked = false }
    }

    onAboutToShow: load()
    onAccepted: {
        dialog.session.topic = topic.text
        dialog.session.setup = {
            scenario: dialog.session.scenarios[scenario.currentIndex].id,
            focus: focus.text, skip: skip.text, custom: custom.text,
            timestamps: timestamps.checked, quotes: quotes.checked, actionItems: actionItems.checked,
            openQuestions: openQuestions.checked, definitions: definitions.checked,
            explainJargon: jargon.checked,
            layout: dialog.session.layouts[layout.currentIndex].id, sections: sections.text,
            style: dialog.session.styles[look.currentIndex].id, timeline: timeline.checked
        }
        dialog.session.keepRecording = recording.checked
        dialog.session.captureScreen = screen.checked
        dialog.session.continueSeries = series.checked
        dialog.session.splitLongSessions = split.checked
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
            label: qsTr("Organize")
            hint: layout.currentIndex >= 0 ? dialog.session.layouts[layout.currentIndex].hint : ""
            Tk.ComboBox {
                id: layout
                objectName: "liveSetupLayout"
                model: dialog.session.layouts.map(function(e) { return e.name })
            }
        }
        Tk.Button {
            objectName: "liveSetupSuggested"
            small: true
            iconName: "list-checks"
            text: qsTr("Use suggested structure for this kind")
            tooltip: qsTr("Fill the sections and presentation defaults; keep your topic, focus and custom instructions.")
            onClicked: dialog.useSuggestedStructure()
        }
        Tk.PropertyRow {
            readonly property bool own: layout.currentIndex >= 0
                                        && dialog.session.layouts[layout.currentIndex].id === "custom"
            label: own ? qsTr("Your sections") : qsTr("Also keep")
            hint: own ? qsTr("One per line, in order, with what goes there.")
                      : qsTr("Optional: sections you always want, one per line.")
            Tk.TextArea {
                id: sections
                objectName: "liveSetupSections"
                rows: 3
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("e.g. Characters — who they are\nFight scenes — who won, how\nRecipes — anything cooked")
            }
        }
        Tk.PropertyRow {
            label: qsTr("Look")
            hint: look.currentIndex >= 0 ? dialog.session.styles[look.currentIndex].hint : ""
            Tk.Segmented {
                id: look
                objectName: "liveSetupStyle"
                small: true
                model: dialog.session.styles.map(function(e) { return e.name })
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
                Tk.CheckBox { id: timeline; objectName: "liveSetupTimeline"; text: qsTr("A timeline underneath (the chronological record)") }
                Tk.CheckBox { id: quotes; objectName: "liveSetupQuotes"; text: qsTr("Key quotes, word for word") }
                Tk.CheckBox { id: actionItems; objectName: "liveSetupActionItems"; text: qsTr("Action items and deadlines") }
                Tk.CheckBox { id: openQuestions; objectName: "liveSetupOpenQuestions"; text: qsTr("Open questions") }
                Tk.CheckBox { id: definitions; objectName: "liveSetupDefinitions"; text: qsTr("A definitions glossary") }
                Tk.CheckBox { id: jargon; objectName: "liveSetupJargon"; text: qsTr("Explain jargon (short AI notes, marked as such)") }
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
        // M56: what the note-taker reads first, and what the session keeps.
        Tk.PropertyRow {
            label: qsTr("Prep material")
            alignTop: true
            hint: qsTr("Slides, a syllabus, an agenda (.pdf .txt .md .html): the notes follow its structure and spellings.")
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Repeater {
                    model: dialog.session.prepFiles
                    Tk.Flex {
                        id: prepRow
                        required property var modelData
                        required property int index
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.xs
                        align: Tk.Flex.Center
                        Tk.Label { Tk.Flex.grow: 1; elide: Text.ElideMiddle; text: String(prepRow.modelData).split("/").pop() }
                        Tk.IconButton { iconName: "x"; small: true; tooltip: qsTr("Remove"); onClicked: dialog.session.removePrepFile(prepRow.index) }
                    }
                }
                Tk.Button { objectName: "liveSetupAddPrep"; small: true; iconName: "plus"; text: qsTr("Add a file…"); onClicked: prepDialog.open() }
                Tk.CheckBox {
                    id: series
                    objectName: "liveSetupSeries"
                    text: qsTr("Continue from the earlier pages in this section")
                    tooltip: qsTr("A course or a meeting series: the note-taker reads the previous sessions' notes and refers back instead of repeating.")
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Session")
            alignTop: true
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.CheckBox { id: recording; objectName: "liveSetupRecording"; text: qsTr("Keep the recording (replay any moment from the notes)") }
                Tk.CheckBox {
                    id: screen
                    objectName: "liveSetupScreen"
                    text: qsTr("Capture the screen for the model (desktop audio)")
                    tooltip: qsTr("Every 20 s, when the screen changed, a screenshot goes with the next update — slides and code land exactly. Needs a model that reads images (Claude, GPT, Gemini over HTTP).")
                }
                Tk.CheckBox { id: split; objectName: "liveSetupSplit"; text: qsTr("Split a long session into pages by topic when it stops") }
            }
        }
        Dialogs.FileDialog {
            id: prepDialog
            title: qsTr("Prep material")
            nameFilters: [qsTr("Documents (*.pdf *.txt *.md *.html *.htm)"), qsTr("All files (*)")]
            onAccepted: dialog.session.addPrepFile(selectedFile)
        }
        Tk.CheckBox {
            id: askAgain
            objectName: "liveSetupAskAgain"
            text: qsTr("Show this before every session")
        }
    }
}
