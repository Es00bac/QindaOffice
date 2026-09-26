// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// Recovered documents (D-055): shown by runOfficeApp before the first
// document window when the recovery journal holds entries. One row per
// document (title, where it lives and when it changed, a line of its
// text), every row checked. "Recover Selected" opens the checked ones and
// discards the rest; "Discard All" — and closing this window, which is a
// dismissal too — discards every one. A discarded document never comes
// back (the journal moves it aside; RecoveryJournal::discard).
//
// AGENT-CONTRACT: `entries` are OfficeApplication::pendingRecovery() rows
// {key, title, detail, preview}; the answer goes to
// `office.resolveRecovery(recoverKeys, discardKeys)` exactly once, and
// BEFORE this window closes — the document windows it creates must exist
// by then, or closing the last window would quit the application.
Tk.AppWindow {
    id: window

    property var entries: []
    property QtObject office: null
    property string appName: Qt.application.displayName
    property bool done: false
    // Keys the user unticked (rows are ticked by default).
    property var unticked: ({})

    objectName: "recoveryDialog"
    title: qsTr("Recovered Documents — %1").arg(appName)
    width: 560
    height: 440
    toolBarsVisible: false
    statusBarVisible: false

    // Same theme source as the document windows (OfficeWindow.qml): the
    // desktop's tokens when published, else the Preferences preset.
    readonly property Loader themeBridge: Loader {
        objectName: "themeBridge"
        source: "ThemeBridge.qml"
    }
    readonly property bool tokensApplied: themeBridge.status === Loader.Ready
                                          && themeBridge.item !== null && themeBridge.item.applied
    onTokensAppliedChanged: if (!tokensApplied) Tk.Theme.applyPreset(Preferences.themePreset)
    Component.onCompleted: {
        Tk.Density.modeName = Preferences.density
        if (!tokensApplied) Tk.Theme.applyPreset(Preferences.themePreset)
    }

    function selectedKeys() {
        return window.entries.filter(function(e) { return !window.unticked[e.key] })
                             .map(function(e) { return e.key })
    }
    function finish(recoverKeys) {
        if (window.done) return
        window.done = true
        const all = window.entries.map(function(e) { return e.key })
        const discard = all.filter(function(k) { return recoverKeys.indexOf(k) < 0 })
        if (window.office !== null) window.office.resolveRecovery(recoverKeys, discard)
        window.close()
    }
    // The title-bar close is a dismissal: it discards, like "Discard All".
    onClosing: function(close) { window.finish([]) }

    Tk.Flex {
        objectName: "recoveryContent"
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg

        Tk.Heading {
            objectName: "recoveryHeading"
            text: window.entries.length === 1
                  ? qsTr("%1 kept 1 document with unsaved changes").arg(window.appName)
                  : qsTr("%1 kept %2 documents with unsaved changes").arg(window.appName).arg(window.entries.length)
        }
        Tk.Label {
            objectName: "recoveryExplanation"
            text: qsTr("Tick the ones to open again. The rest are discarded and will not be offered again.")
            wrapMode: Text.WordWrap
            muted: true
            Tk.Flex.alignSelf: Tk.Flex.Stretch
        }
        Tk.Scroll {
            objectName: "recoveryScroll"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
            Tk.Flex.alignSelf: Tk.Flex.Stretch

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Repeater {
                    model: window.entries
                    delegate: Tk.Flex {
                        id: row
                        required property var modelData
                        objectName: "recoveryRow_" + modelData.key
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.sm
                        align: Tk.Flex.Start
                        Tk.CheckBox {
                            objectName: "recoveryCheck_" + row.modelData.key
                            checked: !window.unticked[row.modelData.key]
                            tooltip: qsTr("Open %1 again").arg(row.modelData.title)
                            onToggled: {
                                const next = Object.assign({}, window.unticked)
                                if (checked) delete next[row.modelData.key]
                                else next[row.modelData.key] = true
                                window.unticked = next
                            }
                        }
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: 0
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            Tk.Label { text: row.modelData.title; elide: Text.ElideRight; Tk.Flex.alignSelf: Tk.Flex.Stretch }
                            Tk.Caption { text: row.modelData.detail; elide: Text.ElideMiddle; Tk.Flex.alignSelf: Tk.Flex.Stretch }
                            Tk.Caption {
                                visible: row.modelData.preview.length > 0
                                text: "“" + row.modelData.preview + "”"
                                elide: Text.ElideRight
                                Tk.Flex.alignSelf: Tk.Flex.Stretch
                            }
                        }
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            justify: Tk.Flex.End
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            Tk.Button {
                objectName: "recoveryDiscardAll"
                text: qsTr("Discard All")
                variant: "danger"
                tooltip: qsTr("Discard every document listed here")
                onClicked: window.finish([])
            }
            Tk.Button {
                objectName: "recoveryRecover"
                text: window.selectedKeys().length === window.entries.length
                      ? qsTr("Recover All") : qsTr("Recover Selected")
                variant: "accent"
                enabled: window.selectedKeys().length > 0
                tooltip: qsTr("Open the ticked documents; discard the rest")
                onClicked: window.finish(window.selectedKeys())
            }
        }
    }
}
