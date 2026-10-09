// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Page Setup ▸ Header/Footer (M78, D-087): Excel's ready-made headers and
// footers, and Custom Header / Custom Footer as three sections each, typed
// in Excel's &[Page] spelling with buttons that insert the fields; odd,
// even and first pages when Different odd and even pages / Different first
// page are on; Scale with document and Align with page margins; a preview
// of page 1. Edits the dialog's draft; built afresh on every open.
Loader {
    id: tab

    required property var dialog

    active: tab.dialog.visible
    sourceComponent: Component {
        Tk.Flex {
            id: body
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm

            // Which page's header and footer the fields edit.
            property int page: 0
            readonly property string headerKey: ["oddHeader", "evenHeader", "firstHeader"][body.page]
            readonly property string footerKey: ["oddFooter", "evenFooter", "firstFooter"][body.page]
            property var lastField: null
            readonly property var presets: tab.dialog.draft.presets || []

            function partsOf(key) { return tab.dialog.draft[key] || ({ left: "", center: "", right: "" }) }
            // The ready-made entry these parts are (0: none), or the last
            // entry, "(custom)", as Excel lists a header of one's own.
            function presetIndex(parts) {
                if (!parts.left && !parts.center && !parts.right) return 0
                for (let i = 0; i < body.presets.length; ++i) {
                    const p = body.presets[i]
                    if (p.left === (parts.left || "") && p.center === (parts.center || "") && p.right === (parts.right || ""))
                        return i + 1
                }
                return body.presets.length + 1
            }
            function choose(key, index) {
                if (index > body.presets.length) return // "(custom)": the sections below edit it
                const p = index === 0 ? { left: "", center: "", right: "" } : body.presets[index - 1]
                tab.dialog.set(key, { left: p.left, center: p.center, right: p.right })
            }
            readonly property var presetLabels: [qsTr("(none)")].concat(body.presets.map(function(p) { return p.label }))
                                                                .concat([qsTr("(custom)")])
            function insertField(code) {
                const field = body.lastField
                if (field === null) return
                field.insert(field.cursorPosition, code)
                field.edited()
            }

            Tk.PropertyRow {
                label: qsTr("Header")
                Tk.ComboBox {
                    objectName: "headerPreset"
                    width: Tk.Theme.size.fieldWidth * 3
                    model: body.presetLabels
                    currentIndex: body.presetIndex(body.partsOf(body.headerKey))
                    onActivated: function(index) { body.choose(body.headerKey, index) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Footer")
                Tk.ComboBox {
                    objectName: "footerPreset"
                    width: Tk.Theme.size.fieldWidth * 3
                    model: body.presetLabels
                    currentIndex: body.presetIndex(body.partsOf(body.footerKey))
                    onActivated: function(index) { body.choose(body.footerKey, index) }
                }
            }
            Tk.Segmented {
                objectName: "headerFooterPage"
                visible: tab.dialog.draft.differentOddEven === true || tab.dialog.draft.differentFirst === true
                model: [qsTr("Odd pages"), qsTr("Even pages"), qsTr("First page")]
                currentIndex: body.page
                onActivated: function(index) { body.page = index }
            }
            Repeater {
                model: [{ key: "header", title: qsTr("Custom header") }, { key: "footer", title: qsTr("Custom footer") }]
                Tk.Flex {
                    id: section
                    required property var modelData
                    readonly property string partKey: section.modelData.key === "header" ? body.headerKey : body.footerKey
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.SectionHeader { title: section.modelData.title }
                    Tk.Grid {
                        columns: "1fr 1fr 1fr"
                        columnGap: Tk.Theme.space.sm
                        Repeater {
                            model: [{ side: "left", hint: qsTr("Left section") }, { side: "center", hint: qsTr("Center section") },
                                    { side: "right", hint: qsTr("Right section") }]
                            Tk.TextField {
                                id: part
                                required property var modelData
                                signal edited()
                                objectName: section.modelData.key + "_" + part.modelData.side
                                placeholderText: part.modelData.hint
                                text: body.partsOf(section.partKey)[part.modelData.side] || ""
                                onActiveFocusChanged: if (activeFocus) body.lastField = part
                                onTextEdited: part.edited()
                                onEdited: tab.dialog.setIn(section.partKey, part.modelData.side, part.text)
                            }
                        }
                    }
                    Tk.Caption {
                        objectName: section.modelData.key + "Preview"
                        muted: true
                        text: qsTr("Preview: %1").arg(tab.dialog.host.headerFooterPreview(body.partsOf(section.partKey).left || "",
                                                                                              body.partsOf(section.partKey).center || "",
                                                                                              body.partsOf(section.partKey).right || ""))
                    }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                wrap: Tk.Flex.Wrap
                Repeater {
                    model: [{ code: "&[Page]", text: qsTr("Page"), icon: "hash" }, { code: "&[Pages]", text: qsTr("Pages"), icon: "files" },
                            { code: "&[Date]", text: qsTr("Date"), icon: "calendar" }, { code: "&[Time]", text: qsTr("Time"), icon: "clock" },
                            { code: "&[Path]&[File]", text: qsTr("Path"), icon: "folder" }, { code: "&[File]", text: qsTr("File"), icon: "file" },
                            { code: "&[Tab]", text: qsTr("Sheet"), icon: "sheet" }]
                    Tk.Button {
                        required property var modelData
                        objectName: "insertField_" + modelData.text
                        small: true
                        iconName: modelData.icon
                        text: modelData.text
                        tooltip: qsTr("Insert %1 where the cursor is").arg(modelData.code)
                        onClicked: body.insertField(modelData.code)
                    }
                }
            }
            Tk.CheckBox {
                objectName: "differentOddEven"
                text: qsTr("Different odd and even pages")
                checked: tab.dialog.draft.differentOddEven === true
                onToggled: tab.dialog.set("differentOddEven", checked)
            }
            Tk.CheckBox {
                objectName: "differentFirst"
                text: qsTr("Different first page")
                checked: tab.dialog.draft.differentFirst === true
                onToggled: tab.dialog.set("differentFirst", checked)
            }
            Tk.CheckBox {
                objectName: "scaleWithDoc"
                text: qsTr("Scale with document")
                checked: tab.dialog.draft.scaleWithDoc === true
                onToggled: tab.dialog.set("scaleWithDoc", checked)
            }
            Tk.CheckBox {
                objectName: "alignWithMargins"
                text: qsTr("Align with page margins")
                checked: tab.dialog.draft.alignWithMargins === true
                onToggled: tab.dialog.set("alignWithMargins", checked)
            }
        }
    }
}
