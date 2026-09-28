// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note

// Live notes settings (D-057): which speech engine hears the audio, which
// model writes the notes, and the optional System One gate (TypeSafe Jev
// hosted, or Laya self-hosted) that decides when new speech is worth an
// update. Keys typed here go to the desktop keyring; each row says where
// the key currently comes from (keyring, $ENV, Gabbee's keyring item,
// OpenCode). Changes apply to the next session.
Tk.Dialog {
    id: dialog

    required property LiveNotesSession session
    signal providersRequested()

    readonly property var origins: dialog.session.keyOrigins
    function keyHint(name, missing) {
        const origin = dialog.origins[name]
        return origin !== undefined && origin.length > 0 ? qsTr("Key found (%1).").arg(origin) : missing
    }

    objectName: "liveSettingsDialog"
    title: qsTr("Live notes settings")
    dialogWidth: 620
    primaryText: qsTr("Done")
    secondaryText: ""
    onOpened: dialog.session.checkKeys()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        enabled: dialog.session.state === "idle"

        Tk.Overline { title: qsTr("Speech to text") }
        Tk.PropertyRow {
            label: qsTr("Engine")
            Tk.Segmented {
                objectName: "liveEngine"
                small: true
                model: [qsTr("ElevenLabs realtime"), qsTr("Local Whisper"), qsTr("Transcription API")]
                currentIndex: Math.max(0, ["elevenlabs", "whisper", "http"].indexOf(dialog.session.engine))
                onActivated: function(index) { dialog.session.engine = ["elevenlabs", "whisper", "http"][index]; dialog.session.checkKeys() }
            }
        }
        Tk.PropertyRow {
            label: qsTr("ElevenLabs key")
            visible: dialog.session.engine === "elevenlabs"
            hint: dialog.keyHint("speech", qsTr("No key yet. Also read from $ELEVENLABS_API_KEY and from Gabbee's keyring entry."))
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.TextField { id: elevenKey; objectName: "liveElevenLabsKey"; Tk.Flex.grow: 1; echoMode: TextInput.Password; placeholderText: qsTr("Paste to store in the keyring") }
                Tk.Button { small: true; text: qsTr("Store"); available: elevenKey.text.trim().length > 0; onClicked: { dialog.session.storeKey("elevenlabs", elevenKey.text); elevenKey.text = "" } }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Key terms")
            hint: qsTr("Comma-separated names and jargon (a lecturer's name, course terms): recognition is biased towards them and the notes model uses them as the correct spellings.")
            Tk.TextField { objectName: "liveKeyterms"; text: dialog.session.keyterms; onEditingFinished: dialog.session.keyterms = text }
        }
        Tk.PropertyRow {
            label: qsTr("Whisper model")
            visible: dialog.session.engine === "whisper"
            hint: qsTr("A faster-whisper model name from /usr/share/faster-whisper (Portage: app-accessibility/faster-whisper-models) or a directory. Nothing leaves this machine.")
            Tk.TextField { objectName: "liveWhisperModel"; mono: true; text: dialog.session.whisperModel; onEditingFinished: dialog.session.whisperModel = text }
        }
        Tk.PropertyRow {
            label: qsTr("Endpoint")
            visible: dialog.session.engine === "http"
            hint: qsTr("Any OpenAI-compatible /audio/transcriptions: Groq, OpenAI, or a self-hosted Whisper server.")
            Tk.TextField { objectName: "liveHttpBase"; mono: true; text: dialog.session.httpBaseUrl; onEditingFinished: dialog.session.httpBaseUrl = text }
        }
        Tk.PropertyRow {
            label: qsTr("Model")
            visible: dialog.session.engine === "http"
            Tk.TextField { objectName: "liveHttpModel"; mono: true; text: dialog.session.httpModel; onEditingFinished: dialog.session.httpModel = text }
        }
        Tk.PropertyRow {
            label: qsTr("API key")
            visible: dialog.session.engine === "http"
            hint: dialog.keyHint("speech", qsTr("No key yet (a local server may not need one)."))
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.TextField { id: httpKey; Tk.Flex.grow: 1; echoMode: TextInput.Password; placeholderText: qsTr("Paste to store in the keyring") }
                Tk.Button { small: true; text: qsTr("Store"); available: httpKey.text.trim().length > 0; onClicked: { dialog.session.storeKey("transcription", httpKey.text); httpKey.text = "" } }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Language")
            hint: qsTr("An ISO code such as en or de; empty detects it.")
            Tk.TextField { objectName: "liveLanguage"; text: dialog.session.language; onEditingFinished: dialog.session.language = text }
        }

        Tk.Overline { title: qsTr("Silence") }
        Tk.PropertyRow {
            label: qsTr("Pause")
            Tk.Switch {
                objectName: "liveAutoPause"
                text: qsTr("Stop sending audio after 20 s of quiet (resumes by itself)")
                checked: dialog.session.autoPause
                onToggled: dialog.session.autoPause = checked
            }
        }
        Tk.PropertyRow {
            label: qsTr("Stop")
            hint: qsTr("End the session (with its final revision) after this long without sound.")
            Tk.ComboBox {
                objectName: "liveAutoStop"
                readonly property var minutes: [0, 5, 10, 15, 30, 60]
                model: [qsTr("Never"), qsTr("5 minutes"), qsTr("10 minutes"), qsTr("15 minutes"), qsTr("30 minutes"), qsTr("1 hour")]
                currentIndex: Math.max(0, minutes.indexOf(dialog.session.autoStopMinutes))
                onActivated: function(index) { dialog.session.autoStopMinutes = minutes[index] }
            }
        }

        Tk.Overline { title: qsTr("Notes") }
        Tk.PropertyRow {
            label: qsTr("Model")
            hint: dialog.keyHint("model", qsTr("No key found for this model yet — set one in AI providers."))
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.ComboBox {
                    objectName: "liveNotesModel"
                    Tk.Flex.grow: 1
                    model: dialog.session.models.map(function(m) { return m.name })
                    placeholderText: qsTr("No models configured")
                    currentIndex: {
                        const models = dialog.session.models
                        for (let i = 0; i < models.length; ++i) if (models[i].id === dialog.session.modelId) return i
                        return -1
                    }
                    onActivated: function(index) { dialog.session.modelId = dialog.session.models[index].id; dialog.session.checkKeys() }
                }
                Tk.Button { small: true; text: qsTr("AI providers…"); onClicked: dialog.providersRequested() }
            }
        }

        Tk.Overline { title: qsTr("Gate (optional)") }
        Tk.PropertyRow {
            label: qsTr("Decisions")
            hint: qsTr("A fast System One model judges each stretch of speech — filler, new topic, key point — so the notes model only runs when it matters, and important points get highlighted.")
            Tk.Segmented {
                objectName: "liveGateKind"
                small: true
                model: [qsTr("Off"), qsTr("TypeSafe Jev"), qsTr("Laya (self-hosted)")]
                currentIndex: Math.max(0, ["off", "jev", "laya"].indexOf(dialog.session.gate))
                onActivated: function(index) { dialog.session.gate = ["off", "jev", "laya"][index] }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Server")
            visible: dialog.session.gate !== "off"
            Tk.TextField { objectName: "liveGateBase"; mono: true; text: dialog.session.gateBaseUrl; onEditingFinished: dialog.session.gateBaseUrl = text }
        }
        Tk.PropertyRow {
            label: qsTr("Model")
            visible: dialog.session.gate !== "off"
            Tk.TextField { objectName: "liveGateModel"; mono: true; text: dialog.session.gateModel; onEditingFinished: dialog.session.gateModel = text }
        }
        Tk.PropertyRow {
            label: qsTr("Key")
            visible: dialog.session.gate !== "off"
            hint: dialog.keyHint("gate", dialog.session.gate === "jev" ? qsTr("No key yet. Also read from $JEV_API_KEY.")
                                                                      : qsTr("Only if laya-serve runs with LAYA_API_KEY."))
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.TextField { id: gateKey; Tk.Flex.grow: 1; echoMode: TextInput.Password; placeholderText: qsTr("Paste to store in the keyring") }
                Tk.Button { small: true; text: qsTr("Store"); available: gateKey.text.trim().length > 0; onClicked: { dialog.session.storeKey("gate", gateKey.text); gateKey.text = "" } }
            }
        }
    }
}
