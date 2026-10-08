// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// File ▸ Company Logo… (M63, D-071): the picture printed at the top of
// invoices, estimates, sales receipts, credit memos and statements. The
// service keeps it in the company file (CompanySettings::logo) and refuses
// a file the forms could not print; the preview is the stored bytes.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property string logo: ""        // data: URL of the stored logo, "" for none
    property var answer: null       // the last set/remove answer (T.Dialog owns `result`)

    objectName: "logoDialog"
    title: qsTr("Company logo")
    subtitle: qsTr("Printed at the top of invoices, estimates, receipts, credit memos and statements")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: logo.length > 0 ? qsTr("Replace…") : qsTr("Choose a picture…")
    secondaryText: qsTr("Close")
    tertiaryText: logo.length > 0 ? qsTr("Remove logo") : ""

    function openDialog() { answer = null; logo = books.companyLogo(); open() }
    // The file step; the tests call it directly (no native file dialog).
    function useFile(url) { answer = books.setCompanyLogo(url); logo = books.companyLogo(); open() }
    function removeLogo() { answer = books.removeCompanyLogo(); logo = books.companyLogo() }
    onAccepted: fileDialog.open()
    onTertiary: removeLogo()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice {
            objectName: "logoError"
            visible: dialog.answer !== null && !dialog.answer.ok
            variant: "danger"
            title: qsTr("Not changed")
            text: dialog.answer !== null ? String(dialog.answer.error || "") : ""
        }
        Tk.Box {
            objectName: "logoPreview"
            implicitHeight: Tk.Theme.size.panelMinHeight * 1.2
            color: Tk.Theme.color.panelAlt
            radius: Tk.Theme.radius.md
            padding: Tk.Theme.space.md
            Image {
                objectName: "logoImage"
                anchors.fill: parent
                visible: dialog.logo.length > 0
                source: dialog.logo
                fillMode: Image.PreserveAspectFit
                smooth: true
                asynchronous: false
            }
            Tk.EmptyState {
                anchors.centerIn: parent
                visible: dialog.logo.length === 0
                iconName: "image"
                title: qsTr("No logo")
                text: qsTr("Choose a PNG or JPEG picture of your logo. It is stored in the company file.")
            }
        }
    }
    Dialogs.FileDialog {
        id: fileDialog
        title: qsTr("Choose a logo")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Pictures (*.png *.jpg *.jpeg)"), qsTr("All files (*)")]
        onAccepted: dialog.useFile(selectedFile)
        onRejected: dialog.open()
    }
}
