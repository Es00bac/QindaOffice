// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK.QindaQt

// Loaded by OfficeWindow.qml through a Loader so a missing QindaQt.Tokens
// module (off the desktop) fails this file alone, not the window.
QindaQtTheme {
    // One line in the log says which palette the chrome is on (rule 9:
    // degrade visibly), the counterpart of the preset message.
    onAppliedChanged: if (applied) console.info("QindaOffice: QindaQt tokens applied")
}
