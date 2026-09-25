// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Mail.App

// The Mail workspace: folders · message list · reading pane, in a Tk.Splitter
// so the user sets the proportions (D-042). View ▸ Reading Pane hides the
// third pane; the list then takes the width.
Item {
    id: workspace

    property MailService service: null
    property MailHost host: null
    readonly property bool readingPaneVisible: host !== null && host.readingPaneVisible

    function focusSearch() { messageList.focusSearch() }
    function openMoveMenu() { messageList.openMoveMenu() }

    Tk.Splitter {
        objectName: "mailSplitter"
        anchors.fill: parent
        orientation: Qt.Horizontal
        tooltip: qsTr("Resize the mail panes")
        FolderPane {
            objectName: "folderPane"
            T.SplitView.preferredWidth: Tk.Theme.size.panelMinWidth * 1.25
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth * 0.8
            service: workspace.service
            host: workspace.host
        }
        MessageList {
            id: messageList
            objectName: "messageList"
            T.SplitView.preferredWidth: workspace.readingPaneVisible ? Tk.Theme.size.fieldWidth * 3.4 : -1
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth * 1.3
            T.SplitView.fillWidth: !workspace.readingPaneVisible
            service: workspace.service
            host: workspace.host
        }
        ReadingPane {
            objectName: "readingPane"
            visible: workspace.readingPaneVisible
            T.SplitView.fillWidth: true
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth * 1.5
            service: workspace.service
            host: workspace.host
        }
    }
}
