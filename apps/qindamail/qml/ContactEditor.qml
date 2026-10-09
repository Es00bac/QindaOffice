// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Mail.App

// The contact card of the People workspace (M64, D-072), always editable
// like Outlook's: picture, name parts, company, e-mail addresses, phones,
// postal addresses, web pages, birthday, categories and notes. Save writes
// the address book (MailService.contacts, office_mail ContactStore).
//
// AGENT-CONTRACT (office_mail contact.h toVariantMap): the map keys are the
// Contact field names; emails/phones/urls are [{type, value, preferred}],
// addresses [{type, street, locality, region, postalCode, country, …}];
// a new picture is passed as `photoFile` (URL), removal as `photoRemove`.
Tk.Box {
    id: editor

    property MailService service: null
    property MailHost host: null
    property var contact: ({})
    property bool dirty: false
    property string error: ""
    property url photoFile: ""
    property bool photoRemove: false
    readonly property bool isNew: String(contact.id || "").length === 0
    readonly property var emailTypes: [{ value: "home", text: qsTr("Personal") }, { value: "work", text: qsTr("Work") },
                                       { value: "other", text: qsTr("Other") }]
    readonly property var phoneTypes: [{ value: "cell", text: qsTr("Mobile") }, { value: "work", text: qsTr("Business") },
                                       { value: "home", text: qsTr("Home") }, { value: "main", text: qsTr("Main") },
                                       { value: "fax", text: qsTr("Fax") }, { value: "pager", text: qsTr("Pager") },
                                       { value: "other", text: qsTr("Other") }]
    readonly property var addressTypes: [{ value: "work", text: qsTr("Business") }, { value: "home", text: qsTr("Home") },
                                         { value: "other", text: qsTr("Other") }]
    readonly property var urlTypes: [{ value: "", text: qsTr("Web page") }, { value: "work", text: qsTr("Business") },
                                     { value: "home", text: qsTr("Personal") }]
    property var addresses: []
    signal saved(string contactId)
    signal deleteRequested(string contactId)

    color: Tk.Theme.color.surface
    padding: 0

    function field(name) { return String(editor.contact[name] || "") }
    function edit(map) {
        editor.contact = map || ({})
        const fields = { displayName: displayName, givenName: givenName, middleName: middleName, familyName: familyName,
                         prefix: prefix, suffix: suffix, nickname: nickname, organization: organization,
                         department: department, title: jobTitle, birthday: birthday, anniversary: anniversary }
        for (const key in fields) fields[key].text = editor.field(key)
        // "Display as" edits the raw FN; `displayName` is the effective name.
        displayName.text = editor.field("formattedName")
        notes.text = editor.field("notes")
        categories.text = (editor.contact.categories || []).join(", ")
        emails.values = (editor.contact.emails || []).map(function(e) { return Object.assign({}, e) })
        phones.values = (editor.contact.phones || []).map(function(e) { return Object.assign({}, e) })
        urls.values = (editor.contact.urls || []).map(function(e) { return Object.assign({}, e) })
        editor.addresses = (editor.contact.addresses || []).map(function(e) { return Object.assign({}, e) })
        editor.photoFile = ""
        editor.photoRemove = false
        editor.dirty = false
        editor.error = ""
    }
    function focusFirst() { givenName.forceActiveFocus() }
    function collect() {
        const map = Object.assign({}, editor.contact, {
            displayName: displayName.text.trim(), givenName: givenName.text.trim(), middleName: middleName.text.trim(),
            familyName: familyName.text.trim(), prefix: prefix.text.trim(), suffix: suffix.text.trim(),
            nickname: nickname.text.trim(), organization: organization.text.trim(), department: department.text.trim(),
            title: jobTitle.text.trim(), birthday: birthday.text.trim(), anniversary: anniversary.text.trim(),
            notes: notes.text, categories: categories.text.split(/[,;]/).map(function(c) { return c.trim() }).filter(Boolean),
            emails: emails.collected(), phones: phones.collected(), urls: urls.collected(),
            addresses: editor.addresses.filter(function(a) {
                return [a.street, a.locality, a.region, a.postalCode, a.country].some(function(v) { return String(v || "").trim().length > 0 }) })
        })
        delete map.photoUrl
        if (String(editor.photoFile).length > 0) map.photoFile = String(editor.photoFile)
        if (editor.photoRemove) map.photoRemove = true
        return map
    }
    function save() {
        const store = editor.service.contacts
        const result = editor.isNew ? store.add(editor.collect()) : store.update(editor.collect())
        if (!result.ok) {
            editor.error = String(result.error || qsTr("The contact could not be saved"))
            return false
        }
        editor.edit(store.contactById(result.id))
        editor.saved(result.id)
        return true
    }
    function setAddress(index, key, value) { editor.addresses[index][key] = value; editor.dirty = true }
    function primaryLabel() {
        const list = emails.collected()
        if (list.length === 0) return ""
        const name = displayName.text.trim()
        return name.length > 0 ? "\"" + name.replace(/"/g, "") + "\" <" + list[0].value + ">" : list[0].value
    }

    Tk.Scroll {
        id: scroll
        objectName: "contactEditorScroll"
        anchors.fill: parent
        Tk.Flex {
            id: form
            width: scroll.viewportWidth
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            padding: Tk.Theme.space.xl
            readonly property real inner: width - Tk.Theme.space.xl * 2
            Tk.Flex {
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                align: Tk.Flex.Center
                gap: Tk.Theme.space.md
                width: form.inner
                Tk.Thumbnail {
                    objectName: "contactPhoto"
                    Tk.Flex.shrink: 0
                    implicitWidth: Tk.Theme.size.thumbnail
                    implicitHeight: Tk.Theme.size.thumbnail
                    source: String(editor.photoFile).length > 0 ? editor.photoFile : editor.photoRemove ? "" : editor.field("photoUrl")
                    placeholderIcon: "user-round"
                    tooltip: qsTr("Contact picture")
                }
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: Tk.Theme.size.fieldWidth
                    Tk.Heading {
                        objectName: "contactTitle"
                        level: 2
                        text: displayName.text.length > 0 ? displayName.text
                              : [givenName.text, familyName.text].filter(Boolean).join(" ") || (editor.isNew ? qsTr("New contact") : qsTr("(no name)"))
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Tk.Caption { text: [jobTitle.text, organization.text].filter(Boolean).join(" · ") }
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        wrap: Tk.Flex.Wrap
                        gap: Tk.Theme.space.xs
                        width: parent.width
                        Tk.Button { objectName: "contactSave"; small: true; variant: "accent"; iconName: "save"; text: qsTr("Save"); tooltip: qsTr("Save this contact"); available: editor.dirty || editor.isNew; onClicked: editor.save() }
                        Tk.Button { objectName: "contactRevert"; small: true; variant: "ghost"; text: qsTr("Revert"); tooltip: qsTr("Undo the changes since the last save"); available: editor.dirty; onClicked: editor.edit(editor.contact) }
                        Tk.Button {
                            objectName: "contactWrite"
                            small: true
                            variant: "ghost"
                            iconName: "mail-plus"
                            text: qsTr("New message")
                            tooltip: qsTr("Write to this contact")
                            available: editor.primaryLabel().length > 0 && editor.host !== null
                            onClicked: editor.host.compose(Object.assign(editor.service.newDraft(), { to: editor.primaryLabel() }))
                        }
                        Tk.Button { objectName: "contactPickPhoto"; small: true; variant: "ghost"; iconName: "image-plus"; text: qsTr("Picture…"); tooltip: qsTr("Choose a picture"); onClicked: photoDialog.open() }
                        Tk.Button { objectName: "contactRemovePhoto"; small: true; variant: "ghost"; iconName: "image-off"; text: qsTr("No picture"); tooltip: qsTr("Remove the picture"); visible: editor.field("photoUrl").length > 0 || String(editor.photoFile).length > 0; onClicked: { editor.photoFile = ""; editor.photoRemove = true; editor.dirty = true } }
                        Tk.Button { objectName: "contactDelete"; small: true; variant: "ghost"; iconName: "trash-2"; text: qsTr("Delete"); tooltip: qsTr("Delete this contact"); visible: !editor.isNew; onClicked: editor.deleteRequested(String(editor.contact.id)) }
                    }
                }
            }
            Tk.Notice { objectName: "contactError"; visible: editor.error.length > 0; variant: "danger"; title: qsTr("Not saved"); text: editor.error }
            Tk.Overline { title: qsTr("Name") }
            Tk.Flex {
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.sm
                width: form.inner
                Tk.TextField { id: prefix; objectName: "contactPrefix"; width: Tk.Theme.size.fieldWidth * 0.6; placeholderText: qsTr("Title"); tooltip: qsTr("Title (Mr., Dr., …)"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: givenName; objectName: "contactGivenName"; width: Tk.Theme.size.fieldWidth; placeholderText: qsTr("First name"); tooltip: qsTr("First name"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: middleName; objectName: "contactMiddleName"; width: Tk.Theme.size.fieldWidth; placeholderText: qsTr("Middle name"); tooltip: qsTr("Middle name"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: familyName; objectName: "contactFamilyName"; width: Tk.Theme.size.fieldWidth; placeholderText: qsTr("Last name"); tooltip: qsTr("Last name"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: suffix; objectName: "contactSuffix"; width: Tk.Theme.size.fieldWidth * 0.6; placeholderText: qsTr("Suffix"); tooltip: qsTr("Suffix (Jr., III, …)"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: displayName; objectName: "contactDisplayName"; width: Tk.Theme.size.fieldWidth * 2; placeholderText: qsTr("Display as (optional)"); tooltip: qsTr("The name mail shows"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: nickname; objectName: "contactNickname"; width: Tk.Theme.size.fieldWidth; placeholderText: qsTr("Nickname"); tooltip: qsTr("Nickname"); onTextEdited: editor.dirty = true }
            }
            Tk.Overline { title: qsTr("Work") }
            Tk.Flex {
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.sm
                width: form.inner
                Tk.TextField { id: organization; objectName: "contactCompany"; width: Tk.Theme.size.fieldWidth * 1.5; placeholderText: qsTr("Company"); tooltip: qsTr("Company"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: department; objectName: "contactDepartment"; width: Tk.Theme.size.fieldWidth; placeholderText: qsTr("Department"); tooltip: qsTr("Department"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: jobTitle; objectName: "contactJobTitle"; width: Tk.Theme.size.fieldWidth * 1.5; placeholderText: qsTr("Job title"); tooltip: qsTr("Job title"); onTextEdited: editor.dirty = true }
            }
            ContactValueList { id: emails; fieldName: "email"; width: form.inner; title: qsTr("E-mail"); types: editor.emailTypes; placeholderText: qsTr("name@example.com"); addText: qsTr("Add e-mail address"); onModified: editor.dirty = true }
            ContactValueList { id: phones; fieldName: "phone"; width: form.inner; title: qsTr("Phone"); types: editor.phoneTypes; placeholderText: qsTr("Phone number"); addText: qsTr("Add phone number"); onModified: editor.dirty = true }
            Tk.Label { text: qsTr("Address"); muted: true }
            Repeater {
                model: editor.addresses
                delegate: Tk.Flex {
                    id: addressRow
                    required property var modelData
                    required property int index
                    objectName: "contact_address_" + index
                    direction: Tk.Flex.Row
                    wrap: Tk.Flex.Wrap
                    gap: Tk.Theme.space.xs
                    width: form.inner
                    Tk.ComboBox {
                        small: true
                        implicitWidth: Tk.Theme.size.fieldWidth
                        model: editor.addressTypes
                        textRole: "text"
                        currentIndex: Math.max(0, editor.addressTypes.map(function(t) { return t.value }).indexOf(String(addressRow.modelData.type || "")))
                        tooltip: qsTr("Kind")
                        onActivated: function(index) { editor.setAddress(addressRow.index, "type", editor.addressTypes[index].value) }
                    }
                    Repeater {
                        model: [{ key: "street", label: qsTr("Street"), w: 2 }, { key: "locality", label: qsTr("City"), w: 1 },
                                { key: "region", label: qsTr("State/Province"), w: 1 }, { key: "postalCode", label: qsTr("ZIP/Postal code"), w: 0.8 },
                                { key: "country", label: qsTr("Country/Region"), w: 1 }]
                        delegate: Tk.TextField {
                            required property var modelData
                            objectName: "contact_address_" + addressRow.index + "_" + modelData.key
                            width: Tk.Theme.size.fieldWidth * modelData.w
                            text: String(addressRow.modelData[modelData.key] || "")
                            placeholderText: modelData.label
                            tooltip: modelData.label
                            onTextEdited: editor.setAddress(addressRow.index, modelData.key, text)
                        }
                    }
                    Tk.IconButton {
                        small: true
                        iconName: "x"
                        tooltip: qsTr("Remove this address")
                        onClicked: { const next = editor.addresses.slice(); next.splice(addressRow.index, 1); editor.addresses = next; editor.dirty = true }
                    }
                }
            }
            Tk.Button {
                objectName: "contact_address_add"
                Tk.Flex.alignSelf: Tk.Flex.Start
                small: true
                variant: "ghost"
                iconName: "plus"
                text: qsTr("Add address")
                tooltip: qsTr("Add a postal address")
                onClicked: { editor.addresses = editor.addresses.concat([{ type: "work" }]); editor.dirty = true }
            }
            ContactValueList { id: urls; fieldName: "url"; width: form.inner; title: qsTr("Web"); types: editor.urlTypes; placeholderText: "https://"; addText: qsTr("Add web page"); onModified: editor.dirty = true }
            Tk.Overline { title: qsTr("More") }
            Tk.Flex {
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.sm
                width: form.inner
                Tk.TextField { id: birthday; objectName: "contactBirthday"; width: Tk.Theme.size.fieldWidth; placeholderText: qsTr("Birthday (YYYY-MM-DD)"); tooltip: qsTr("Birthday, YYYY-MM-DD or --MM-DD without a year"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: anniversary; objectName: "contactAnniversary"; width: Tk.Theme.size.fieldWidth; placeholderText: qsTr("Anniversary (YYYY-MM-DD)"); tooltip: qsTr("Anniversary"); onTextEdited: editor.dirty = true }
                Tk.TextField { id: categories; objectName: "contactCategories"; width: Tk.Theme.size.fieldWidth * 2; placeholderText: qsTr("Categories, separated by commas"); tooltip: qsTr("Categories"); onTextEdited: editor.dirty = true }
            }
            Tk.TextArea { id: notes; objectName: "contactNotes"; width: form.inner; rows: 4; placeholderText: qsTr("Notes"); tooltip: qsTr("Notes"); onTextChanged: if (activeFocus) editor.dirty = true }
        }
    }

    Dialogs.FileDialog {
        id: photoDialog
        title: qsTr("Choose a Picture")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Pictures (*.jpg *.jpeg *.png *.gif *.bmp *.webp)")]
        onAccepted: { editor.photoFile = selectedFile; editor.photoRemove = false; editor.dirty = true }
    }
}
