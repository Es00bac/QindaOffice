// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Show

// Insert ▸ Action (M91, D-099), PowerPoint's Action Settings for the
// selected shape or picture: a Mouse Click page and a Mouse Over page, each
// choosing what happens — nothing; a hyperlink to the next, previous,
// first, last or last viewed slide, the end of the show, a slide, a custom
// show (with "show and return"), a web address or another file; run a
// program (written for PowerPoint — QindaShow never runs it); the macro a
// deck already has (kept) — the sound to play, and Highlight click. OK
// applies both pages as ONE undo step (ShowActionSettings::apply).
//
// AGENT-NOTE: the choices are state (`pages`); the radio buttons and check
// boxes only show it (checkable: false keeps their bindings), and the
// fields and lists are filled by refresh() when a page shows.
Tk.Dialog {
    id: dialog
    required property ShowHost showHost
    readonly property ShowActionSettings settings: showHost.actionSettings
    // The two pages as ShowActionSettings::actionMap() spells them.
    property var pages: [({}), ({})]
    property int page: 0
    readonly property var current: pages[page] || ({})
    readonly property var hyperlinks: ["next", "previous", "first", "last", "viewed", "end", "custom", "slide", "url", "file"]
    readonly property string mode: current.kind === undefined || current.kind === "none" || current.kind === "other" ? "none"
                                   : current.kind === "program" || current.kind === "macro" ? current.kind : "hyperlink"
    property string subjectName: ""
    // AGENT-NOTE: properties, not children — the dialog's content takes Items only.
    readonly property Dialogs.FileDialog fileDialog: Dialogs.FileDialog {
        title: qsTr("Hyperlink to Other File")
        fileMode: Dialogs.FileDialog.OpenFile
        onAccepted: {
            dialog.set("file", decodeURIComponent(String(selectedFile).replace(/^file:\/\//, "")))
            dialog.refresh()
        }
    }
    readonly property Dialogs.FileDialog soundDialog: Dialogs.FileDialog {
        title: qsTr("Add Audio")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Sounds (*.wav)")]
        onAccepted: {
            const choice = dialog.settings.addSoundFile(selectedFile)
            if (choice.length > 0) dialog.set("sound", choice)
            dialog.refresh()
        }
    }

    function set(key, value) {
        const next = dialog.pages.slice()
        const page = Object.assign({}, next[dialog.page])
        page[key] = value
        next[dialog.page] = page
        dialog.pages = next
    }
    function refresh() {
        const c = dialog.current
        hyperlinkCombo.currentIndex = Math.max(0, dialog.hyperlinks.indexOf(c.kind))
        slideCombo.currentIndex = Math.max(0, c.slide !== undefined ? c.slide : 0)
        customCombo.currentIndex = Math.max(0, c.custom !== undefined ? c.custom : 0)
        urlField.text = c.url || ""
        fileField.text = c.file || ""
        programField.text = c.program || ""
        soundCombo.currentIndex = Math.max(0, dialog.settings.soundChoices.indexOf(c.sound || "") - 1)
    }

    objectName: "actionSettingsDialog"
    title: qsTr("Action Settings")
    subtitle: qsTr("For %1").arg(dialog.subjectName)
    primaryText: qsTr("OK")
    dialogWidth: Tk.Theme.size.dialogWidth
    onAboutToShow: {
        const loaded = dialog.settings.load()
        dialog.subjectName = loaded.name || ""
        dialog.pages = [Object.assign({}, loaded.click || {}), Object.assign({}, loaded.hover || {})]
        dialog.page = 0
        dialog.refresh()
    }
    onPageChanged: dialog.refresh()
    onAccepted: dialog.settings.apply(dialog.pages[0], dialog.pages[1])

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Segmented {
            objectName: "actionPage"
            small: true
            model: [qsTr("Mouse Click"), qsTr("Mouse Over")]
            currentIndex: dialog.page
            onActivated: function(index) { dialog.page = index }
        }
        Tk.Overline { text: dialog.page === 0 ? qsTr("Action on click") : qsTr("Action on mouse over") }
        Tk.RadioButton {
            objectName: "actionNone"
            checkable: false
            text: qsTr("None")
            checked: dialog.mode === "none"
            onClicked: dialog.set("kind", "none")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.RadioButton {
                objectName: "actionHyperlink"
                checkable: false
                text: qsTr("Hyperlink to:")
                checked: dialog.mode === "hyperlink"
                onClicked: if (dialog.mode !== "hyperlink") dialog.set("kind", dialog.hyperlinks[hyperlinkCombo.currentIndex])
            }
            Tk.ComboBox {
                id: hyperlinkCombo
                objectName: "actionHyperlinkCombo"
                enabled: dialog.mode === "hyperlink"
                width: Tk.Theme.size.dialogWidth * 0.39
                model: [qsTr("Next Slide"), qsTr("Previous Slide"), qsTr("First Slide"), qsTr("Last Slide"),
                        qsTr("Last Slide Viewed"), qsTr("End Show"), qsTr("Custom Show…"), qsTr("Slide…"),
                        qsTr("URL…"), qsTr("Other File…")]
                onActivated: function(index) { dialog.set("kind", dialog.hyperlinks[index]) }
            }
        }
        Tk.PropertyRow {
            visible: dialog.mode === "hyperlink" && dialog.current.kind === "slide"
            label: qsTr("Slide")
            Tk.ComboBox {
                id: slideCombo
                objectName: "actionSlideCombo"
                width: Tk.Theme.size.dialogWidth * 0.46
                model: dialog.settings.slideChoices.map(function(s) { return s.title })
                onActivated: function(index) { dialog.set("slide", index) }
            }
        }
        Tk.PropertyRow {
            visible: dialog.mode === "hyperlink" && dialog.current.kind === "custom"
            label: qsTr("Custom show")
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.ComboBox {
                    id: customCombo
                    objectName: "actionCustomCombo"
                    width: Tk.Theme.size.dialogWidth * 0.46
                    model: dialog.settings.customShowChoices
                    placeholderText: qsTr("The presentation has no custom shows")
                    onActivated: function(index) { dialog.set("custom", index) }
                }
                Tk.CheckBox {
                    objectName: "actionReturn"
                    checkable: false
                    text: qsTr("Show and return")
                    checked: dialog.current.returnAfter === true
                    onClicked: dialog.set("returnAfter", !checked)
                }
            }
        }
        Tk.PropertyRow {
            visible: dialog.mode === "hyperlink" && dialog.current.kind === "url"
            label: qsTr("Address")
            Tk.TextField {
                id: urlField
                objectName: "actionUrlField"
                width: Tk.Theme.size.dialogWidth * 0.46
                placeholderText: qsTr("https://")
                onTextEdited: dialog.set("url", text)
            }
        }
        Tk.PropertyRow {
            visible: dialog.mode === "hyperlink" && dialog.current.kind === "file"
            label: qsTr("File")
            Tk.Flex {
                direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.xs
                Tk.TextField {
                    id: fileField
                    objectName: "actionFileField"
                    width: Tk.Theme.size.dialogWidth * 0.36
                    onTextEdited: dialog.set("file", text)
                }
                Tk.Button {
                    objectName: "actionFileBrowse"
                    text: qsTr("Browse…")
                    small: true
                    onClicked: dialog.fileDialog.open()
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.RadioButton {
                objectName: "actionProgram"
                checkable: false
                text: qsTr("Run program:")
                checked: dialog.mode === "program"
                onClicked: dialog.set("kind", "program")
            }
            Tk.TextField {
                id: programField
                objectName: "actionProgramField"
                enabled: dialog.mode === "program"
                width: Tk.Theme.size.dialogWidth * 0.39
                onTextEdited: dialog.set("program", text)
            }
        }
        Tk.Notice {
            objectName: "actionProgramNotice"
            visible: dialog.mode === "program"
            variant: "warning"
            text: qsTr("Kept for PowerPoint, which asks before it runs a program. QindaShow never runs programs.")
        }
        Tk.RadioButton {
            objectName: "actionMacro"
            checkable: false
            visible: dialog.current.kind === "macro"
            text: qsTr("Run macro: %1 (kept; QindaShow does not run macros)").arg(dialog.current.macro || "")
            checked: dialog.mode === "macro"
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.CheckBox {
                objectName: "actionSound"
                checkable: false
                text: qsTr("Play sound:")
                checked: (dialog.current.sound || "").length > 0
                onClicked: dialog.set("sound", checked ? "" : dialog.settings.soundChoices[soundCombo.currentIndex + 1])
            }
            Tk.ComboBox {
                id: soundCombo
                objectName: "actionSoundCombo"
                enabled: (dialog.current.sound || "").length > 0
                width: Tk.Theme.size.dialogWidth * 0.36
                model: dialog.settings.soundLabels.slice(1)
                onActivated: function(index) { dialog.set("sound", dialog.settings.soundChoices[index + 1]) }
            }
            Tk.Button {
                objectName: "actionSoundFile"
                text: qsTr("Other Sound…")
                small: true
                onClicked: dialog.soundDialog.open()
            }
        }
        Tk.CheckBox {
            objectName: "actionHighlight"
            checkable: false
            text: qsTr("Highlight click")
            checked: dialog.current.highlight === true
            onClicked: dialog.set("highlight", !checked)
        }
    }
}
