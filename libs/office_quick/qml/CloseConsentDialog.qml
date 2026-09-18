// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Save / Cancel / Discard for one document (ADR-0110): accepted = save,
// tertiary = discard, rejected (Cancel, Escape) = keep the window open.
Tk.MessageDialog {
    id: dialog

    property string documentTitle: ""

    title: qsTr("Unsaved changes")
    variant: "question"
    text: qsTr("Save changes to %1 before closing?").arg(documentTitle)
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")
    tertiaryText: qsTr("Discard")
}
