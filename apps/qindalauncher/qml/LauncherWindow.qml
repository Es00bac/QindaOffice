// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Launcher

// The suite's front door (plan §5.5): a card per app over the recent
// documents of all four. No menus and no document — it starts the apps
// detached and never edits their state.
Tk.AppWindow {
    id: window
    required property LauncherController launcher

    title: qsTr("QindaQt Office")
    width: 820
    height: 640

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg

        Tk.Heading { objectName: "launcherHeading"; text: qsTr("QindaQt Office") }
        Tk.Label {
            objectName: "launcherSubtitle"
            text: qsTr("Pick something to work in, or carry on where you left off.")
            muted: true
        }

        Tk.Grid {
            objectName: "launcherAppGrid"
            columns: "repeat(auto-fill, minmax(220, 1fr))"
            gap: Tk.Theme.space.sm
            Repeater {
                model: window.launcher.apps
                delegate: Tk.Card {
                    id: card
                    required property var modelData
                    required property int index
                    objectName: "appCard_" + modelData.id
                    interactive: modelData.available
                    padding: Tk.Theme.space.md
                    // AGENT-NOTE: Tk.Box ignores a non-interactive box for
                    // accessibility; a card for an app that is not installed
                    // must still be read out ("… is not installed").
                    Accessible.role: modelData.available ? Accessible.Button : Accessible.StaticText
                    Accessible.ignored: false
                    Accessible.name: modelData.name + " — " + modelData.role
                    Accessible.description: modelData.available
                        ? modelData.blurb : qsTr("%1 is not installed").arg(modelData.name)
                    onClicked: if (modelData.available) window.launcher.launchApp(card.index)
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.md
                        align: Tk.Flex.Center
                        // The mascot, or an initial-letter tile when the
                        // icons are not installed (a build-tree run).
                        Item {
                            implicitWidth: 56
                            implicitHeight: 56
                            Tk.Flex.shrink: 0
                            Image {
                                anchors.fill: parent
                                objectName: "appIcon_" + card.modelData.id
                                source: card.modelData.iconSource
                                visible: card.modelData.iconSource.length > 0
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                asynchronous: true
                            }
                            Tk.Box {
                                anchors.fill: parent
                                visible: card.modelData.iconSource.length === 0
                                color: Tk.Theme.color.accentSubtle
                                radius: Tk.Theme.radius.md
                                // Keep the letter centred: a filling Box
                                // would stretch it over the tile, flush left.
                                fill: false
                                Tk.Heading {
                                    anchors.centerIn: parent
                                    text: card.modelData.name.substring(5, 6)
                                    accent: true
                                }
                            }
                        }
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: 2
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            Tk.Flex.minWidth: 0
                            Tk.Label { text: card.modelData.name }
                            Tk.Overline { text: card.modelData.role }
                            Tk.Caption {
                                text: card.modelData.available ? card.modelData.blurb
                                                               : qsTr("Not installed")
                                wrapMode: Text.Wrap
                                Tk.Flex.minWidth: 0
                            }
                        }
                    }
                }
            }
        }

        Tk.SectionHeader { objectName: "launcherRecentHeading"; title: qsTr("Recent documents") }
        Tk.Scroll {
            objectName: "launcherRecentList"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 120
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Tk.EmptyState {
                    objectName: "launcherRecentEmpty"
                    visible: window.launcher.recents.length === 0
                    title: qsTr("Nothing yet")
                    text: qsTr("Open or create a document and it will show up here.")
                }
                Repeater {
                    model: window.launcher.recents
                    // One click opens: this is a launcher, and a list whose
                    // rows need a double-click reads as a list to browse
                    // rather than a list to use.
                    delegate: Tk.ListRow {
                        required property var modelData
                        required property int index
                        objectName: "recentRow_" + index
                        text: modelData.fileName
                        secondaryText: modelData.folder
                        Accessible.name: modelData.fileName + " — " + modelData.folder
                        onClicked: window.launcher.openRecent(index)
                        // sourceSize (not implicitWidth, which an Image
                        // derives and keeps read-only) is what sizes it.
                        leading: Image {
                            source: modelData.iconSource
                            visible: modelData.iconSource.length > 0
                            sourceSize.width: Tk.Theme.size.icon
                            sourceSize.height: Tk.Theme.size.icon
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                    }
                }
            }
        }

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.Button {
                objectName: "launcherOpenButton"
                text: qsTr("Open a Document…")
                iconName: "folder-open"
                onClicked: openDialog.open()
            }
            Tk.Spacer {}
            Tk.Caption { objectName: "launcherStatus"; text: window.launcher.status }
        }
    }

    Dialogs.FileDialog {
        id: openDialog
        objectName: "launcherOpenDialog"
        title: qsTr("Open")
        nameFilters: window.launcher.openNameFilters
        onAccepted: window.launcher.openPathChosen(selectedFile)
    }

    // The recents are four other processes' state: re-read them when the
    // front door comes back to the front.
    onActiveChanged: if (active) window.launcher.refresh()
}
