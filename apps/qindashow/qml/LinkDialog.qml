// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Insert ▸ Link (M91, D-099), PowerPoint's Insert Hyperlink: link to an
// existing file or web page (an address), a Place in This Document (the
// first, last, next or previous slide, a slide by its title, a custom show)
// or an e-mail address, for the text selected in the editor — or, without
// one, for the selected shape's click. Remove Link takes it off. ONE undo
// step (ShowActionSettings::applyLink).
Tk.Dialog {
    id: dialog
    required property ShowHost showHost
    readonly property ShowActionSettings settings: showHost.actionSettings
    property var link: ({})
    property string kind: "web"
    property int placeIndex: 0
    // Places: the four jumps, the slides by title, the custom shows.
    readonly property var places: {
        const out = [{ place: "first", label: qsTr("First Slide") }, { place: "last", label: qsTr("Last Slide") },
                     { place: "next", label: qsTr("Next Slide") }, { place: "previous", label: qsTr("Previous Slide") }]
        const slides = dialog.settings.slideChoices
        for (let i = 0; i < slides.length; ++i) out.push({ place: "slide", slide: slides[i].index, label: slides[i].title })
        const shows = dialog.settings.customShowChoices
        for (let j = 0; j < shows.length; ++j) out.push({ place: "custom", custom: j, label: qsTr("Custom show: %1").arg(shows[j]) })
        return out
    }

    function placeFor(map) {
        for (let i = 0; i < places.length; ++i) {
            const p = places[i]
            if (p.place === map.place && (p.place !== "slide" || p.slide === map.slide) && (p.place !== "custom" || p.custom === map.custom)) return i
        }
        return 0
    }
    function linkResult() {
        const out = { kind: dialog.kind, text: textField.text }
        if (dialog.kind === "web") out.address = addressField.text
        else if (dialog.kind === "mail") out.address = mailField.text + (subjectField.text.length > 0 ? "?subject=" + encodeURIComponent(subjectField.text) : "")
        else {
            const p = dialog.places[dialog.placeIndex]
            out.place = p.place
            out.slide = p.slide !== undefined ? p.slide : -1
            out.custom = p.custom !== undefined ? p.custom : -1
            out.returnAfter = returnBox.checked
        }
        return out
    }

    objectName: "linkDialog"
    title: qsTr("Insert Link")
    subtitle: dialog.link.target === "text" ? qsTr("For the selected text") : qsTr("For the selected shape")
    primaryText: qsTr("OK")
    tertiaryText: dialog.link.linked === true ? qsTr("Remove Link") : ""
    dialogWidth: Tk.Theme.size.dialogWidth
    onAboutToShow: {
        dialog.link = dialog.settings.loadLink()
        dialog.kind = dialog.link.kind || "web"
        textField.text = dialog.link.text || ""
        addressField.text = dialog.kind === "web" ? (dialog.link.address || "") : ""
        mailField.text = dialog.kind === "mail" ? String(dialog.link.address || "").split("?")[0] : ""
        subjectField.text = dialog.kind === "mail" ? decodeURIComponent(String(dialog.link.address || "").split("?subject=")[1] || "") : ""
        dialog.placeIndex = dialog.kind === "place" ? dialog.placeFor(dialog.link) : 0
        returnBox.checked = dialog.link.returnAfter === true
    }
    onAccepted: dialog.settings.applyLink(dialog.linkResult())
    onTertiary: { dialog.settings.applyLink({ kind: "" }); dialog.close() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Link to")
            Tk.Segmented {
                objectName: "linkKind"
                small: true
                model: [qsTr("Web Page or File"), qsTr("Place in This Document"), qsTr("E-mail Address")]
                currentIndex: ["web", "place", "mail"].indexOf(dialog.kind)
                onActivated: function(index) { dialog.kind = ["web", "place", "mail"][index] }
            }
        }
        Tk.PropertyRow {
            visible: dialog.link.target === "text"
            label: qsTr("Text to display")
            Tk.TextField { id: textField; objectName: "linkText"; width: Tk.Theme.size.dialogWidth * 0.54 }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "web"
            label: qsTr("Address")
            Tk.TextField { id: addressField; objectName: "linkAddress"; width: Tk.Theme.size.dialogWidth * 0.54; placeholderText: qsTr("https://") }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "mail"
            label: qsTr("E-mail address")
            Tk.TextField { id: mailField; objectName: "linkMail"; width: Tk.Theme.size.dialogWidth * 0.54 }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "mail"
            label: qsTr("Subject")
            Tk.TextField { id: subjectField; objectName: "linkSubject"; width: Tk.Theme.size.dialogWidth * 0.54 }
        }
        Tk.Overline { visible: dialog.kind === "place"; text: qsTr("Select a place in this document") }
        Tk.Scroll {
            visible: dialog.kind === "place"
            implicitWidth: Tk.Theme.size.dialogWidth * 0.86
            implicitHeight: Tk.Theme.size.control * 8
            Tk.Flex {
                direction: Tk.Flex.Column
                width: Tk.Theme.size.dialogWidth * 0.84
                Repeater {
                    model: dialog.places
                    Tk.ListRow {
                        required property var modelData
                        required property int index
                        objectName: "linkPlace_" + index
                        width: Tk.Theme.size.dialogWidth * 0.84
                        small: true
                        indent: index < 4 ? 0 : Tk.Theme.space.md
                        iconName: modelData.place === "custom" ? "list-ordered" : modelData.place === "slide" ? "presentation" : "arrow-right"
                        text: modelData.label
                        selected: dialog.placeIndex === index
                        onClicked: dialog.placeIndex = index
                    }
                }
            }
        }
        Tk.CheckBox {
            id: returnBox
            objectName: "linkReturn"
            visible: dialog.kind === "place" && dialog.places[dialog.placeIndex] !== undefined && dialog.places[dialog.placeIndex].place === "custom"
            text: qsTr("Show and return")
        }
    }
}
