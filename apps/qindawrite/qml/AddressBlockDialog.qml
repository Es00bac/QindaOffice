// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Address Block (M77, D-086): Word's Insert Address Block — the
// recipient's name format, the company, the postal address with Word's
// country rule (never, always, or only when not the home country) and
// formatting by the destination country; the preview shows the record
// being previewed. Inserts Word's ADDRESSBLOCK field.
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property var formats: []
    property string preview: ""
    objectName: "addressBlockDialog"
    title: qsTr("Insert Address Block")
    dialogWidth: 520
    primaryText: qsTr("Insert")

    function options() {
        return { nameFormat: formats.length > 0 ? formats[nameFormat.currentIndex].format : "",
                 company: company.checked, postal: postal.checked,
                 country: country.currentIndex, homeCountry: homeCountry.text,
                 formatByCountry: byCountry.checked }
    }
    function refresh() { preview = dialog.mailMerge.addressBlockPreview(options()) }

    Connections {
        target: dialog.mailMerge
        function onAddressBlockRequested() {
            const info = dialog.mailMerge.addressBlockInfo()
            dialog.formats = info.formats
            let at = 0
            for (let i = 0; i < info.formats.length; ++i) if (info.formats[i].format === info.nameFormat) at = i
            nameFormat.currentIndex = at
            company.checked = info.company
            postal.checked = info.postal
            country.currentIndex = info.country
            homeCountry.text = info.homeCountry
            byCountry.checked = info.formatByCountry
            dialog.refresh()
            dialog.open()
        }
    }
    onAccepted: dialog.mailMerge.insertAddressBlock(options())

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Recipient's name")
            Tk.ComboBox {
                id: nameFormat
                objectName: "addressNameFormat"
                model: dialog.formats.map(function(f) { return f.sample })
                onActivated: dialog.refresh()
            }
        }
        Tk.CheckBox { id: company; objectName: "addressCompany"; text: qsTr("Insert company name"); onClicked: dialog.refresh() }
        Tk.CheckBox { id: postal; objectName: "addressPostal"; text: qsTr("Insert postal address"); onClicked: dialog.refresh() }
        Tk.PropertyRow {
            label: qsTr("Country or region")
            enabled: postal.checked
            Tk.ComboBox {
                id: country
                objectName: "addressCountry"
                model: [qsTr("Never include"), qsTr("Always include"), qsTr("Only include if different than:")]
                onActivated: dialog.refresh()
            }
        }
        Tk.PropertyRow {
            label: qsTr("Home country")
            enabled: postal.checked && country.currentIndex === 2
            Tk.TextField { id: homeCountry; objectName: "addressHomeCountry"; onEditingFinished: dialog.refresh() }
        }
        Tk.CheckBox { id: byCountry; objectName: "addressByCountry"; text: qsTr("Format address according to the destination country or region") }
        Tk.Overline { text: qsTr("Preview") }
        Tk.Box {
            objectName: "addressPreviewBox"
            borderWidth: 1
            borderColor: Tk.Theme.color.border
            padding: Tk.Theme.space.sm
            Tk.Label {
                objectName: "addressPreview"
                text: dialog.preview.length > 0 ? dialog.preview : qsTr("(no name or address for this recipient)")
                elide: Text.ElideNone
                wrapMode: Text.WordWrap
            }
        }
        Tk.Button { objectName: "addressMatchFields"; Tk.Flex.alignSelf: Tk.Flex.End; text: qsTr("Match Fields…"); iconName: "link-2"; onClicked: dialog.mailMerge.matchFieldsRequested() }
    }
}
