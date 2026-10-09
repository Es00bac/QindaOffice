// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Mail.App

// The Mail workspace: folders · message list · reading pane, in a Tk.Splitter
// so the user sets the proportions (D-042). View ▸ Reading Pane hides the
// third pane; the list then takes the space. Below the panes' combined
// minimum width they stack (the owner's narrow-window rule from the unmerged
// HTML-reader branch), so the reading pane — the message, its pictures bar
// and its attachments (M64) — stays inside a 520 px window.
Item {
    id: workspace

    property MailService service: null
    property MailHost host: null
    readonly property bool narrow: width < Tk.Theme.size.panelMinWidth * 3.6
    readonly property bool readingPaneVisible: host !== null && host.readingPaneVisible

    function focusSearch() { messageList.focusSearch() }
    function openMoveMenu() { messageList.openMoveMenu() }

    Tk.Splitter {
        objectName: "mailSplitter"
        anchors.fill: parent
        orientation: workspace.narrow ? Qt.Vertical : Qt.Horizontal
        tooltip: qsTr("Resize the mail panes")
        FolderPane {
            objectName: "folderPane"
            T.SplitView.preferredWidth: Tk.Theme.size.panelMinWidth * 1.25
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth * 0.8
            T.SplitView.preferredHeight: Tk.Theme.size.fieldWidth
            T.SplitView.minimumHeight: Tk.Theme.size.fieldWidth
            service: workspace.service
            host: workspace.host
            onSignInRequested: function(accountId) {
                if (workspace.host !== null) workspace.host.accountDialogRequested(accountId)
            }
        }
        MessageList {
            id: messageList
            objectName: "messageList"
            T.SplitView.preferredWidth: workspace.readingPaneVisible ? Tk.Theme.size.fieldWidth * 3.4 : -1
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth * 1.3
            T.SplitView.preferredHeight: Tk.Theme.size.fieldWidth * 1.5
            T.SplitView.minimumHeight: Tk.Theme.size.fieldWidth
            T.SplitView.fillHeight: !workspace.readingPaneVisible
            T.SplitView.fillWidth: !workspace.readingPaneVisible
            service: workspace.service
            host: workspace.host
        }
        ReadingPane {
            objectName: "readingPane"
            visible: workspace.readingPaneVisible
            T.SplitView.fillWidth: true
            T.SplitView.fillHeight: true
            T.SplitView.minimumHeight: Tk.Theme.size.fieldWidth
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth * 1.5
            service: workspace.service
            host: workspace.host
        }
    }
}
