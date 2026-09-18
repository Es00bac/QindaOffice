// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// The transient message on the left (Opened…, Saved…, errors) and the
// permanent info field on the right (page n of m, word count, the
// selection's sum); both come from the host.
Tk.StatusBar {
    id: bar

    property DocumentHost host: null

    Tk.StatusField {
        objectName: "statusMessage"
        text: bar.host !== null ? bar.host.statusMessage : ""
        visible: text.length > 0
    }
    Tk.Spacer { }
    Tk.StatusField {
        objectName: "statusInfo"
        text: bar.host !== null ? bar.host.statusInfo : ""
        visible: text.length > 0
    }
}
