// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Ai

// AI providers (D-057): the rows of the window's model catalog, added from
// the models.dev presets (OpenCode's provider list) or imported from the
// user's OpenCode setup, each with its endpoint, model and key. Keys typed
// here go to the desktop keyring and are never shown again; a key can also
// come from a reference ({env:NAME} / {file:PATH}), the provider's usual
// environment variable, or OpenCode's own auth.json (ApiKeyResolver).
Tk.Dialog {
    id: dialog

    required property ProviderSettings settings

    property string currentId: ""
    readonly property var current: {
        const rows = dialog.settings.providers
        for (let i = 0; i < rows.length; ++i) if (rows[i].id === dialog.currentId) return rows[i]
        return null
    }
    property string keyOrigin: ""
    property string message: ""
    property var fetchedModels: []

    objectName: "providerSettingsDialog"
    title: qsTr("AI providers")
    dialogWidth: 760
    primaryText: qsTr("Done")
    secondaryText: ""

    function select(id) {
        dialog.currentId = id
        dialog.keyOrigin = ""
        dialog.message = ""
        dialog.fetchedModels = []
        if (id.length > 0) dialog.settings.checkKey(id)
    }
    function commit(field, value) {
        if (dialog.current === null || dialog.current[field] === value) return
        const fields = {}
        fields[field] = value
        dialog.settings.update(dialog.currentId, fields)
    }

    onOpened: if (dialog.currentId.length === 0 && dialog.settings.providers.length > 0) select(dialog.settings.providers[0].id)

    Connections {
        target: dialog.settings
        function onKeyStatus(id, origin) { if (id === dialog.currentId) dialog.keyOrigin = origin }
        function onTestResult(id, ok, text, models) {
            if (id !== dialog.currentId) return
            dialog.message = (ok ? "✓ " : "✗ ") + text
            dialog.fetchedModels = models
        }
        function onSaveFailed(text) { dialog.message = "✗ " + text }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex.minHeight: 360

            // ---- configured rows ----
            Tk.Scroll {
                objectName: "providerList"
                Tk.Flex.basis: 230
                Tk.Flex.minHeight: 360
                Tk.Flex {
                    direction: Tk.Flex.Column
                    width: parent.width
                    Repeater {
                        model: dialog.settings.providers
                        Tk.ListRow {
                            required property var modelData
                            objectName: "providerRow_" + modelData.id
                            Tk.Flex.alignSelf: Tk.Flex.Stretch
                            text: modelData.name
                            secondaryText: modelData.model.length > 0 ? modelData.model : modelData.kind
                            iconName: modelData.offMachine ? "cloud" : "monitor"
                            selected: modelData.id === dialog.currentId
                            onClicked: dialog.select(modelData.id)
                        }
                    }
                }
            }

            // ---- the selected row ----
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Flex.grow: 1
                visible: dialog.current !== null

                Tk.PropertyRow {
                    label: qsTr("Name")
                    Tk.TextField {
                        objectName: "providerName"
                        text: dialog.current ? dialog.current.name : ""
                        onEditingFinished: dialog.commit("name", text)
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Protocol")
                    visible: dialog.current !== null && dialog.current.usesKey
                    Tk.Segmented {
                        objectName: "providerKind"
                        small: true
                        model: [qsTr("OpenAI-compatible"), qsTr("Anthropic")]
                        currentIndex: dialog.current && dialog.current.kind === "anthropic" ? 1 : 0
                        onActivated: function(index) { dialog.commit("kind", index === 1 ? "anthropic" : "openai") }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Base URL")
                    visible: dialog.current !== null && dialog.current.usesKey
                    Tk.TextField {
                        objectName: "providerBaseUrl"
                        text: dialog.current ? dialog.current.baseUrl : ""
                        placeholderText: "https://…/v1"
                        onEditingFinished: dialog.commit("baseUrl", text)
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Model")
                    hint: dialog.fetchedModels.length > 0 ? qsTr("%n model(s) found — pick one below.", "", dialog.fetchedModels.length) : ""
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.TextField {
                            id: modelField
                            objectName: "providerModel"
                            Tk.Flex.alignSelf: Tk.Flex.Stretch
                            text: dialog.current ? dialog.current.model : ""
                            onEditingFinished: dialog.commit("model", text)
                        }
                        Tk.ComboBox {
                            objectName: "providerFetchedModels"
                            visible: dialog.fetchedModels.length > 0
                            small: true
                            Tk.Flex.alignSelf: Tk.Flex.Stretch
                            model: dialog.fetchedModels
                            placeholderText: qsTr("Models on this endpoint")
                            currentIndex: dialog.fetchedModels.indexOf(modelField.text)
                            onActivated: function(index) { dialog.commit("model", dialog.fetchedModels[index]) }
                        }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("API key")
                    visible: dialog.current !== null && dialog.current.usesKey
                    hint: dialog.keyOrigin.length > 0 ? qsTr("A key is available (from the %1).").arg(dialog.keyOrigin)
                        : dialog.current && dialog.current.env.length > 0 ? qsTr("No key yet. Also read from $%1.").arg(dialog.current.env)
                        : qsTr("No key yet (a local server may not need one).")
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.xs
                        Tk.TextField {
                            id: keyField
                            objectName: "providerKey"
                            Tk.Flex.grow: 1
                            echoMode: TextInput.Password
                            placeholderText: qsTr("Paste a key to store it in the keyring")
                        }
                        Tk.Button {
                            objectName: "providerSaveKey"
                            small: true
                            text: qsTr("Store")
                            available: keyField.text.trim().length > 0
                            onClicked: { dialog.settings.setApiKey(dialog.currentId, keyField.text); keyField.text = "" }
                        }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Key reference")
                    visible: dialog.current !== null && dialog.current.usesKey
                    hint: qsTr("Optional: {env:NAME} or {file:~/path/to/key} is read at request time.")
                    Tk.TextField {
                        objectName: "providerKeyRef"
                        mono: true
                        text: dialog.current ? dialog.current.apiKeyRef : ""
                        placeholderText: "{file:~/.config/…/api-key}"
                        onEditingFinished: dialog.commit("apiKeyRef", text)
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Extra fields")
                    visible: dialog.current !== null && dialog.current.kind === "openai"
                    hint: qsTr("JSON merged into each request, e.g. {\"chat_template_kwargs\": {\"enable_thinking\": false}} for a faster local reasoning model.")
                    Tk.TextField {
                        objectName: "providerBody"
                        mono: true
                        text: dialog.current ? dialog.current.body : ""
                        onEditingFinished: dialog.commit("body", text)
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.xs
                    Tk.Button { objectName: "providerTest"; small: true; text: qsTr("Test connection"); onClicked: { dialog.message = qsTr("Testing…"); dialog.settings.test(dialog.currentId) } }
                    Tk.Spacer { }
                    Tk.Button {
                        objectName: "providerRemove"
                        small: true
                        variant: "danger"
                        text: qsTr("Remove")
                        onClicked: { dialog.settings.remove(dialog.currentId); dialog.select(dialog.settings.providers.length > 0 ? dialog.settings.providers[0].id : "") }
                    }
                }
                Tk.Label {
                    objectName: "providerMessage"
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    visible: dialog.message.length > 0
                    text: dialog.message
                    wrapMode: Text.Wrap
                }
            }
        }

        Tk.Divider { }

        // ---- add ----
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.ComboBox {
                id: presetPicker
                objectName: "providerPreset"
                small: true
                implicitWidth: 230
                model: dialog.settings.presets.map(function(p) { return p.name })
                currentIndex: 0
            }
            Tk.ComboBox {
                id: presetModel
                objectName: "providerPresetModel"
                small: true
                Tk.Flex.grow: 1
                readonly property var models: presetPicker.currentIndex >= 0 ? dialog.settings.presets[presetPicker.currentIndex].models : []
                model: models.map(function(m) { return m.name + (m.context > 0 ? " · " + Math.round(m.context / 1000) + "k" : "") })
                placeholderText: qsTr("Model (set after adding)")
                currentIndex: models.length > 0 ? 0 : -1
            }
            Tk.Button {
                objectName: "providerAdd"
                small: true
                variant: "accent"
                text: qsTr("Add")
                onClicked: {
                    const preset = dialog.settings.presets[presetPicker.currentIndex]
                    const model = presetModel.currentIndex >= 0 ? presetModel.models[presetModel.currentIndex].id : ""
                    const id = dialog.settings.addPreset(preset.id, model)
                    if (id.length > 0) dialog.select(id)
                    if (preset.hint.length > 0) dialog.message = preset.hint
                }
            }
            Tk.Button {
                objectName: "providerImportOpenCode"
                small: true
                text: qsTr("Import from OpenCode")
                tooltip: qsTr("Adds the providers configured in opencode.json and connected in OpenCode")
                onClicked: {
                    const added = dialog.settings.importOpenCode()
                    dialog.message = added > 0 ? qsTr("Imported %n provider(s) from OpenCode.", "", added)
                                               : qsTr("Nothing new to import from OpenCode.")
                    if (added > 0) dialog.select(dialog.settings.providers[dialog.settings.providers.length - 1].id)
                }
            }
        }
    }
}
