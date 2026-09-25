// SPDX-License-Identifier: GPL-3.0-or-later
// Placeholder main window (M44 C++ lane). The app lead's MailWindow.qml
// replaces this file at merge; it only has to compile and instantiate so
// the C++ tests can create the real module.
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Mail
import QindaOffice.Mail.App

OfficeWindow {
    width: 1280
    height: 800
    Item {}
}
