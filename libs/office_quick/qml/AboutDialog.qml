// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

Tk.Dialog {
    id: dialog

    property DocumentHost host: null

    title: qsTr("About %1").arg(host !== null ? host.appDisplayName : "")
    primaryText: qsTr("OK")
    secondaryText: ""
    dialogWidth: 400

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Heading { level: 2; text: host !== null ? host.appDisplayName : "" }
        Tk.Label {
            text: qsTr("Part of QindaQt Office %1. One document per window on the QindaQt desktop.")
                  .arg(host !== null ? host.appVersion : "")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
        Tk.Caption { text: qsTr("GPL-3.0-or-later · built on QindaTK and Qt %1").arg(Qt.application.version.length > 0 ? "" : "") }
    }
}
